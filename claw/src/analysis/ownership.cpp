#include "analysis/ownership.h"

#include "analysis/sema.h"
#include "ast/ast.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace claw::frontend {

void OwnershipChecker::reportError(const std::string& msg) {
    diagnostics.push_back(Diagnostic{"ownership", msg, {}});
}

void OwnershipChecker::reportError(const SourceSpan& span, const std::string& msg) {
    diagnostics.push_back(Diagnostic{"ownership", msg, span});
}

void OwnershipChecker::reportError(const AstNode* node, const std::string& msg) {
    reportError(node ? node->span : SourceSpan{}, msg);
}

void OwnershipChecker::check(RealmDecl* realm, const SemanticAnalyzer& semanticAnalyzer) {
    semantic = &semanticAnalyzer;
    diagnostics.clear();
    varStates.clear();
    scopeStack.clear();

    for (auto& decl : realm->declarations) {
        checkDecl(decl.get());
    }

    if (!diagnostics.empty()) {
        throw DiagnosticError(
            "Ownership checking failed with " + std::to_string(diagnostics.size()) + " errors.",
            diagnostics);
    }
}

void OwnershipChecker::checkDecl(Decl* decl) {
    if (auto* fn = dynamic_cast<FnDecl*>(decl)) {
        checkFnDecl(fn);
    }
}

void OwnershipChecker::checkFnDecl(FnDecl* fn) {
    varStates.clear();
    scopeStack.clear();
    enterScope();

    const FunctionSignature* signature = semantic->lookupFunctionSignature(fn);
    if (signature) {
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ResolvedType type = i < signature->paramTypes.size() ? signature->paramTypes[i] : makeUnknownType();
            defineTrackedVar(fn->params[i].name, TrackedVar{type, false, 0, false, std::nullopt});
        }
    }

    if (fn->body) {
        for (auto& stmt : fn->body->statements) {
            checkStmt(stmt.get());
        }
    }

    exitScope();
}

void OwnershipChecker::checkStmt(Stmt* stmt) {
    if (auto* bind = dynamic_cast<BindingStmt*>(stmt)) {
        const auto bindingTypeIt = semantic->result().bindingTypes.find(bind);
        const ResolvedType bindingType = bindingTypeIt != semantic->result().bindingTypes.end()
            ? bindingTypeIt->second
            : makeUnknownType();

        if (bind->value) {
            const bool consumeInitializer = !bindingType.isView() && typeOfExpr(bind->value.get()).isOwned();
            checkExpr(bind->value.get(), consumeInitializer);
        }

        TrackedVar state{bindingType, false, 0, false, std::nullopt};
        if (bindingType.isView() && bind->value) {
            if (const auto token = resolveBorrowToken(bind->value.get(), bindingType)) {
                if (acquireBorrowToken(*token, bind->value.get())) {
                    state.lexicalBorrow = token;
                }
            }
        }

        defineTrackedVar(bind->name, state);
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<IdentExpr*>(assign->target.get())) {
            const auto it = varStates.find(ident->name);
            if (it != varStates.end() && assign->value) {
                const bool consumeValue = !it->second.type.isView() && typeOfExpr(assign->value.get()).isOwned();
                checkExpr(assign->value.get(), consumeValue);
            } else if (assign->value) {
                checkExpr(assign->value.get(), typeOfExpr(assign->value.get()).isOwned());
            }

            if (it != varStates.end()) {
                auto& state = it->second;
                if (state.type.isView()) {
                    releaseLexicalBorrow(state);
                    if (assign->value) {
                        if (const auto token = resolveBorrowToken(assign->value.get(), state.type)) {
                            if (acquireBorrowToken(*token, assign->value.get())) {
                                state.lexicalBorrow = token;
                            }
                        }
                    }
                } else {
                    if (isTrackedOwned(state.type) && (state.sharedBorrows > 0 || state.mutableBorrow)) {
                        reportError(assign, "Cannot assign to value while it is borrowed -> " + ident->name);
                    }
                    state.sharedBorrows = 0;
                    state.mutableBorrow = false;
                }
                state.moved = false;
            }
            return;
        }

        if (assign->value) {
            checkExpr(assign->value.get(), typeOfExpr(assign->value.get()).isOwned());
        }

        if (auto* member = dynamic_cast<MemberExpr*>(assign->target.get())) {
            if (const auto rootName = resolveBorrowRootName(member->object.get())) {
                const auto it = varStates.find(*rootName);
                const ResolvedType objectType = typeOfExpr(member->object.get());
                const bool viaEditView = objectType.isView() && objectType.viewKind == "edit";
                if (it != varStates.end() && isTrackedOwned(it->second.type) && !viaEditView &&
                    (it->second.sharedBorrows > 0 || it->second.mutableBorrow)) {
                    reportError(assign, "Cannot mutate value while it is borrowed -> " + *rootName);
                }
            }
        }
        return;
    }

    if (auto* give = dynamic_cast<GiveStmt*>(stmt)) {
        if (give->value) {
            checkExpr(give->value.get(), typeOfExpr(give->value.get()).isOwned());
        }
        return;
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            checkExpr(exprStmt->expr.get(), false);
        }
        return;
    }

    if (auto* when = dynamic_cast<WhenStmt*>(stmt)) {
        checkExpr(when->condition.get(), false);

        const auto baseline = varStates;
        enterScope();
        for (auto& stmtInBlock : when->thenBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        const auto thenState = retainExistingStates(baseline, varStates);

        if (when->elseBlock) {
            varStates = baseline;
            enterScope();
            for (auto& stmtInBlock : when->elseBlock->statements) {
                checkStmt(stmtInBlock.get());
            }
            exitScope();
            const auto elseState = retainExistingStates(baseline, varStates);

            auto merged = baseline;
            for (auto& [name, state] : merged) {
                state = thenState.at(name);
                mergeTrackedState(state, elseState.at(name));
            }
            varStates = std::move(merged);
        } else {
            auto merged = baseline;
            for (auto& [name, state] : merged) {
                mergeTrackedState(state, thenState.at(name));
            }
            varStates = std::move(merged);
        }
        return;
    }

    if (auto* loop = dynamic_cast<LoopStmt*>(stmt)) {
        if (loop->condition) {
            checkExpr(loop->condition.get(), false);
        }

        const auto baseline = varStates;
        enterScope();
        for (auto& stmtInBlock : loop->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        const auto bodyState = retainExistingStates(baseline, varStates);

        auto merged = baseline;
        for (auto& [name, state] : merged) {
            mergeTrackedState(state, bodyState.at(name));
        }
        varStates = std::move(merged);
        return;
    }

    if (auto* scan = dynamic_cast<ScanStmt*>(stmt)) {
        checkExpr(scan->iterable.get(), false);

        const auto baseline = varStates;
        const auto itemTypeIt = semantic->result().scanItemTypes.find(scan);
        const ResolvedType itemType = itemTypeIt != semantic->result().scanItemTypes.end()
            ? itemTypeIt->second
            : makeUnknownType();

        enterScope();
        defineTrackedVar(scan->itemName, TrackedVar{itemType, false, 0, false, std::nullopt});
        for (auto& stmtInBlock : scan->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        const auto bodyState = retainExistingStates(baseline, varStates);

        auto merged = baseline;
        for (auto& [name, state] : merged) {
            mergeTrackedState(state, bodyState.at(name));
        }
        varStates = std::move(merged);
        return;
    }

    if (auto* pick = dynamic_cast<PickStmt*>(stmt)) {
        checkExpr(pick->value.get(), false);

        const auto baseline = varStates;
        const ResolvedType pickedType = typeOfExpr(pick->value.get());
        const ChoiceInfo* choiceInfo = pickedType.isUnknown() ? nullptr : semantic->lookupChoice(pickedType.name);
        const auto choiceBindings = choiceInfo
            ? buildTypeBindings(choiceInfo->typeParams, pickedType.params)
            : std::unordered_map<std::string, ResolvedType>{};

        std::optional<std::unordered_map<std::string, TrackedVar>> mergedState;
        for (const auto& branch : pick->branches) {
            varStates = baseline;
            enterScope();

            if (choiceInfo) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt != choiceInfo->variants.end()) {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        defineTrackedVar(branch.bindings[i], TrackedVar{payloadType, false, 0, false, std::nullopt});
                    }
                }
            }

            for (auto& stmtInBlock : branch.body->statements) {
                checkStmt(stmtInBlock.get());
            }
            exitScope();

            const auto branchState = retainExistingStates(baseline, varStates);
            if (!mergedState.has_value()) {
                mergedState = branchState;
            } else {
                for (auto& [name, state] : *mergedState) {
                    mergeTrackedState(state, branchState.at(name));
                }
            }
        }

        varStates = mergedState.has_value() ? std::move(*mergedState) : baseline;
        return;
    }

    if (auto* lift = dynamic_cast<LiftStmt*>(stmt)) {
        checkExpr(lift->expr.get(), false);

        const ResolvedType outcomeType = typeOfExpr(lift->expr.get());
        ResolvedType okType = makeUnknownType();
        ResolvedType failType = makeUnknownType();
        if (outcomeType.name == "Outcome" && outcomeType.params.size() == 2) {
            okType = outcomeType.params[0];
            failType = outcomeType.params[1];
        }

        const auto baseline = varStates;
        enterScope();
        defineTrackedVar(lift->failName, TrackedVar{failType, false, 0, false, std::nullopt});
        for (auto& stmtInBlock : lift->failBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();

        varStates = baseline;
        defineTrackedVar(lift->valueName, TrackedVar{okType, false, 0, false, std::nullopt});
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        enterScope();
        for (auto& stmtInBlock : raw->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        return;
    }
}

void OwnershipChecker::checkExpr(Expr* expr, bool isConsume) {
    if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const auto it = varStates.find(ident->name);
        if (it == varStates.end() || !isTrackedOwned(it->second.type)) {
            return;
        }

        auto& state = it->second;
        if (state.moved) {
            reportError(ident, "Use of moved value -> " + ident->name);
        }

        if (isConsume) {
            if (state.sharedBorrows > 0 || state.mutableBorrow) {
                reportError(ident, "Cannot move value while it is borrowed -> " + ident->name);
            }
            state.moved = true;
        }
        return;
    }

    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        checkExpr(binary->left.get(), false);
        checkExpr(binary->right.get(), false);
        return;
    }

    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        checkCallExpr(call);
        return;
    }

    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        const ResolvedType memberType = typeOfExpr(expr);
        checkExpr(member->object.get(), isConsume && memberType.isOwned());
        return;
    }
}

void OwnershipChecker::checkCallExpr(CallExpr* call) {
    checkExpr(call->callee.get(), false);

    const FunctionSignature* signature = resolveCallSignature(call->callee.get());
    if (!signature) {
        for (auto& arg : call->args) {
            checkExpr(arg.get(), false);
        }
        return;
    }

    std::vector<std::pair<Expr*, ResolvedType>> borrowedArgs;
    for (size_t i = 0; i < call->args.size(); ++i) {
        const ResolvedType paramType = i < signature->paramTypes.size() ? signature->paramTypes[i] : makeUnknownType();
        Expr* arg = call->args[i].get();

        if (paramType.isView()) {
            acquireView(arg, paramType);
            borrowedArgs.push_back({arg, paramType});
            checkExpr(arg, false);
        } else {
            checkExpr(arg, paramType.isOwned());
        }
    }

    for (auto it = borrowedArgs.rbegin(); it != borrowedArgs.rend(); ++it) {
        releaseView(it->first, it->second);
    }
}

void OwnershipChecker::enterScope() {
    scopeStack.push_back(ScopeFrame{});
}

void OwnershipChecker::exitScope() {
    if (scopeStack.empty()) {
        return;
    }

    auto frame = std::move(scopeStack.back());
    scopeStack.pop_back();

    for (auto it = frame.bindings.rbegin(); it != frame.bindings.rend(); ++it) {
        auto current = varStates.find(it->name);
        if (current != varStates.end()) {
            releaseLexicalBorrow(current->second);
        }

        if (it->previousState.has_value()) {
            varStates[it->name] = *it->previousState;
        } else {
            varStates.erase(it->name);
        }
    }
}

void OwnershipChecker::defineTrackedVar(const std::string& name, const TrackedVar& state) {
    if (!scopeStack.empty()) {
        ScopeBinding binding;
        binding.name = name;
        const auto existing = varStates.find(name);
        if (existing != varStates.end()) {
            binding.previousState = existing->second;
        }
        scopeStack.back().bindings.push_back(std::move(binding));
    }
    varStates[name] = state;
}

void OwnershipChecker::acquireView(Expr* expr, const ResolvedType& viewType) {
    if (const auto token = resolveBorrowToken(expr, viewType)) {
        acquireBorrowToken(*token, expr);
    }
}

void OwnershipChecker::releaseView(Expr* expr, const ResolvedType& viewType) {
    if (const auto token = resolveBorrowToken(expr, viewType)) {
        releaseBorrowToken(*token);
    }
}

const FunctionSignature* OwnershipChecker::resolveCallSignature(const Expr* callee) const {
    return semantic->lookupCallableSignature(callee);
}

std::optional<std::string> OwnershipChecker::resolveBorrowRootName(Expr* expr) const {
    if (!expr) {
        return std::nullopt;
    }

    if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const auto it = varStates.find(ident->name);
        if (it == varStates.end()) {
            return std::nullopt;
        }
        if (isTrackedOwned(it->second.type)) {
            return ident->name;
        }
        if (it->second.lexicalBorrow.has_value()) {
            return it->second.lexicalBorrow->rootName;
        }
        return std::nullopt;
    }

    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return resolveBorrowRootName(member->object.get());
    }

    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        const FunctionSignature* signature = resolveCallSignature(call->callee.get());
        if (!signature || !signature->returnType.isView() || !signature->viewReturnSourceParam.has_value()) {
            return std::nullopt;
        }

        const size_t sourceIndex = *signature->viewReturnSourceParam;
        if (sourceIndex >= call->args.size()) {
            return std::nullopt;
        }
        return resolveBorrowRootName(call->args[sourceIndex].get());
    }

    return std::nullopt;
}

std::optional<BorrowToken> OwnershipChecker::resolveBorrowToken(Expr* expr, const ResolvedType& viewType) const {
    if (!viewType.isView()) {
        return std::nullopt;
    }

    const auto rootName = resolveBorrowRootName(expr);
    if (!rootName.has_value()) {
        return std::nullopt;
    }

    return BorrowToken{*rootName, viewType.viewKind};
}

bool OwnershipChecker::acquireBorrowToken(const BorrowToken& token, const AstNode* node) {
    const auto it = varStates.find(token.rootName);
    if (it == varStates.end() || !isTrackedOwned(it->second.type)) {
        return false;
    }

    auto& state = it->second;
    if (state.moved) {
        reportError(node, "Cannot create view from moved value -> " + token.rootName);
        return false;
    }

    if (token.viewKind == "look") {
        if (state.mutableBorrow) {
            reportError(node, "Cannot create look view while edit view is active -> " + token.rootName);
            return false;
        }
        ++state.sharedBorrows;
        return true;
    }

    if (token.viewKind == "edit") {
        if (state.mutableBorrow || state.sharedBorrows > 0) {
            reportError(node, "Cannot create edit view while another view is active -> " + token.rootName);
            return false;
        }
        state.mutableBorrow = true;
        return true;
    }

    return false;
}

void OwnershipChecker::releaseBorrowToken(const BorrowToken& token) {
    const auto it = varStates.find(token.rootName);
    if (it == varStates.end() || !isTrackedOwned(it->second.type)) {
        return;
    }

    auto& state = it->second;
    if (token.viewKind == "look") {
        if (state.sharedBorrows > 0) {
            --state.sharedBorrows;
        }
        return;
    }

    if (token.viewKind == "edit") {
        state.mutableBorrow = false;
    }
}

void OwnershipChecker::releaseLexicalBorrow(TrackedVar& state) {
    if (state.lexicalBorrow.has_value()) {
        releaseBorrowToken(*state.lexicalBorrow);
        state.lexicalBorrow.reset();
    }
}

ResolvedType OwnershipChecker::typeOfExpr(const Expr* expr) const {
    const ResolvedType* type = semantic->lookupExprType(expr);
    return type ? *type : makeUnknownType();
}

bool OwnershipChecker::isTrackedOwned(const ResolvedType& type) const {
    return type.isOwned();
}

void OwnershipChecker::mergeTrackedState(TrackedVar& target, const TrackedVar& source) const {
    target.moved = target.moved || source.moved;
    target.sharedBorrows = std::max(target.sharedBorrows, source.sharedBorrows);
    target.mutableBorrow = target.mutableBorrow || source.mutableBorrow;
    if (target.lexicalBorrow != source.lexicalBorrow) {
        target.lexicalBorrow.reset();
    }
}

std::unordered_map<std::string, TrackedVar> OwnershipChecker::retainExistingStates(
    const std::unordered_map<std::string, TrackedVar>& baseline,
    const std::unordered_map<std::string, TrackedVar>& current) const {
    auto retained = baseline;
    for (auto& [name, state] : retained) {
        const auto it = current.find(name);
        if (it != current.end()) {
            state = it->second;
        }
    }
    return retained;
}

} // namespace claw::frontend
