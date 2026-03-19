#include "analysis/ownership.h"

#include "analysis/sema.h"
#include "ast/ast.h"

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

    const FunctionSignature* signature = semantic->lookupFunctionSignature(fn);
    if (signature) {
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ResolvedType type = i < signature->paramTypes.size() ? signature->paramTypes[i] : makeUnknownType();
            varStates[fn->params[i].name] = TrackedVar{type, false, 0, false};
        }
    }

    if (!fn->body) {
        return;
    }

    for (auto& stmt : fn->body->statements) {
        checkStmt(stmt.get());
    }
}

void OwnershipChecker::checkStmt(Stmt* stmt) {
    if (auto* bind = dynamic_cast<BindingStmt*>(stmt)) {
        if (bind->value) {
            checkExpr(bind->value.get(), typeOfExpr(bind->value.get()).isOwned());
        }

        const auto bindingTypeIt = semantic->result().bindingTypes.find(bind);
        const ResolvedType bindingType = bindingTypeIt != semantic->result().bindingTypes.end()
            ? bindingTypeIt->second
            : makeUnknownType();
        varStates[bind->name] = TrackedVar{bindingType, false, 0, false};
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (assign->value) {
            checkExpr(assign->value.get(), typeOfExpr(assign->value.get()).isOwned());
        }

        if (auto* ident = dynamic_cast<IdentExpr*>(assign->target.get())) {
            const auto it = varStates.find(ident->name);
            if (it != varStates.end()) {
                it->second.moved = false;
                it->second.sharedBorrows = 0;
                it->second.mutableBorrow = false;
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
        for (auto& stmtInBlock : when->thenBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        const auto thenState = retainExistingStates(baseline, varStates);

        if (when->elseBlock) {
            varStates = baseline;
            for (auto& stmtInBlock : when->elseBlock->statements) {
                checkStmt(stmtInBlock.get());
            }
            const auto elseState = retainExistingStates(baseline, varStates);

            auto merged = baseline;
            for (auto& [name, state] : merged) {
                const auto& thenVar = thenState.at(name);
                const auto& elseVar = elseState.at(name);
                state.moved = thenVar.moved || elseVar.moved;
                state.sharedBorrows = 0;
                state.mutableBorrow = false;
            }
            varStates = std::move(merged);
        } else {
            auto merged = baseline;
            for (auto& [name, state] : merged) {
                const auto& thenVar = thenState.at(name);
                state.moved = state.moved || thenVar.moved;
                state.sharedBorrows = 0;
                state.mutableBorrow = false;
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
        for (auto& stmtInBlock : loop->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        const auto bodyState = retainExistingStates(baseline, varStates);
        auto merged = baseline;
        for (auto& [name, state] : merged) {
            const auto& bodyVar = bodyState.at(name);
            state.moved = state.moved || bodyVar.moved;
            state.sharedBorrows = 0;
            state.mutableBorrow = false;
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
        varStates[scan->itemName] = TrackedVar{itemType, false, 0, false};

        for (auto& stmtInBlock : scan->body->statements) {
            checkStmt(stmtInBlock.get());
        }

        varStates = retainExistingStates(baseline, varStates);
        return;
    }

    if (auto* pick = dynamic_cast<PickStmt*>(stmt)) {
        checkExpr(pick->value.get(), false);

        const auto baseline = varStates;
        auto merged = baseline;
        const ResolvedType pickedType = typeOfExpr(pick->value.get());
        const ChoiceInfo* choiceInfo = pickedType.isUnknown() ? nullptr : semantic->lookupChoice(pickedType.name);
        const auto choiceBindings = choiceInfo
            ? buildTypeBindings(choiceInfo->typeParams, pickedType.params)
            : std::unordered_map<std::string, ResolvedType>{};

        for (const auto& branch : pick->branches) {
            varStates = baseline;
            if (choiceInfo) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt != choiceInfo->variants.end()) {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        varStates[branch.bindings[i]] = TrackedVar{payloadType, false, 0, false};
                    }
                }
            }

            for (auto& stmtInBlock : branch.body->statements) {
                checkStmt(stmtInBlock.get());
            }

            const auto branchState = retainExistingStates(baseline, varStates);
            for (auto& [name, state] : merged) {
                const auto& branchVar = branchState.at(name);
                state.moved = state.moved || branchVar.moved;
                state.sharedBorrows = 0;
                state.mutableBorrow = false;
            }
        }

        varStates = std::move(merged);
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
        varStates[lift->failName] = TrackedVar{failType, false, 0, false};
        for (auto& stmtInBlock : lift->failBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        varStates = baseline;
        varStates[lift->valueName] = TrackedVar{okType, false, 0, false};
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        const auto baseline = varStates;
        for (auto& stmtInBlock : raw->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        varStates = retainExistingStates(baseline, varStates);
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

    const FunctionSignature* signature = nullptr;
    if (auto* ident = dynamic_cast<IdentExpr*>(call->callee.get())) {
        signature = semantic->lookupFunctionSignature(ident->name);
    }

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

void OwnershipChecker::acquireView(Expr* expr, const ResolvedType& viewType) {
    TrackedVar* tracked = trackedValueForExpr(expr);
    if (!tracked || !isTrackedOwned(tracked->type)) {
        return;
    }

    if (tracked->moved) {
        reportError(expr, "Cannot create view from moved value.");
        return;
    }

    if (viewType.viewKind == "look") {
        if (tracked->mutableBorrow) {
            reportError(expr, "Cannot create look view while edit view is active.");
            return;
        }
        ++tracked->sharedBorrows;
        return;
    }

    if (viewType.viewKind == "edit") {
        if (tracked->mutableBorrow || tracked->sharedBorrows > 0) {
            reportError(expr, "Cannot create edit view while another view is active.");
            return;
        }
        tracked->mutableBorrow = true;
    }
}

void OwnershipChecker::releaseView(Expr* expr, const ResolvedType& viewType) {
    TrackedVar* tracked = trackedValueForExpr(expr);
    if (!tracked || !isTrackedOwned(tracked->type)) {
        return;
    }

    if (viewType.viewKind == "look") {
        if (tracked->sharedBorrows > 0) {
            --tracked->sharedBorrows;
        }
        return;
    }

    if (viewType.viewKind == "edit") {
        tracked->mutableBorrow = false;
    }
}

TrackedVar* OwnershipChecker::trackedValueForExpr(Expr* expr) {
    if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const auto it = varStates.find(ident->name);
        return it != varStates.end() ? &it->second : nullptr;
    }

    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return trackedValueForExpr(member->object.get());
    }

    return nullptr;
}

ResolvedType OwnershipChecker::typeOfExpr(const Expr* expr) const {
    const ResolvedType* type = semantic->lookupExprType(expr);
    return type ? *type : makeUnknownType();
}

bool OwnershipChecker::isTrackedOwned(const ResolvedType& type) const {
    return type.isOwned();
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