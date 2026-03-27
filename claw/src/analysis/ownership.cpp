#include "analysis/ownership.h"

#include "analysis/sema.h"
#include "ast/ast.h"

#include <algorithm>

namespace claw::frontend {

namespace {

bool isDefinitelyInitialized(StorageState state) {
    return state == StorageState::Initialized;
}

StorageState mergeStorageState(StorageState left, StorageState right) {
    return left == right ? left : StorageState::MaybeUninitialized;
}

std::string unavailableValueMessage(StorageState state, const std::string& name) {
    if (state == StorageState::Moved) {
        return "Use of moved value -> " + name;
    }
    return "Use of uninitialized value -> " + name;
}

void releaseBorrowTokenInStateMap(
    std::unordered_map<std::string, TrackedVar>& stateMap,
    const BorrowToken& token) {
    const auto it = stateMap.find(token.rootName);
    if (it == stateMap.end() || !it->second.type.isOwned()) {
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

} // namespace

const OwnershipResult& OwnershipChecker::result() const {
    return ownershipResult;
}

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
    ownershipResult = OwnershipResult{};

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
    enterScope(fn ? fn->body.get() : nullptr, ScopeKind::FunctionBoundary);

    const FunctionSignature* signature = semantic->lookupFunctionSignature(fn);
    if (signature) {
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ResolvedType type = i < signature->paramTypes.size() ? signature->paramTypes[i] : makeUnknownType();
            defineTrackedVar(
                fn->params[i].name,
                TrackedVar{type, false, StorageState::Initialized, 0, false, std::nullopt});
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

        TrackedVar state;
        state.type = bindingType;
        state.isMutableStorage = bind->isMutable;
        state.storageState = bind->value ? StorageState::Initialized : StorageState::Uninitialized;
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
            const ResolvedType assignedValueType = assign->value ? typeOfExpr(assign->value.get()) : makeUnknownType();
            const auto it = varStates.find(ident->name);
            if (it != varStates.end() && assign->value) {
                if (it->second.type.isUnknown() && !assignedValueType.isUnknown() && !assignedValueType.isOpaqueExternal()) {
                    it->second.type = assignedValueType;
                }
                const bool consumeValue = !it->second.type.isView() && assignedValueType.isOwned();
                checkExpr(assign->value.get(), consumeValue);
            } else if (assign->value) {
                checkExpr(assign->value.get(), assignedValueType.isOwned());
            }

            if (it != varStates.end()) {
                auto& state = it->second;
                if (state.type.isView()) {
                    releaseLexicalBorrow(state);
                    state.lexicalBorrow.reset();
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
                    if (shouldScheduleDrop(state)) {
                        recordDropBeforeStmt(assign, ident->name, state.type);
                    }
                    state.sharedBorrows = 0;
                    state.mutableBorrow = false;
                }
                state.storageState = StorageState::Initialized;
            }
            return;
        }

        if (assign->value) {
            checkExpr(assign->value.get(), typeOfExpr(assign->value.get()).isOwned());
        }

        if (auto* member = dynamic_cast<MemberExpr*>(assign->target.get())) {
            checkExpr(member->object.get(), false);

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
        for (const auto& drop : collectUnwindDrops(std::nullopt)) {
            recordDropBeforeStmt(give, drop.name, drop.type);
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
        enterScope(when->thenBlock.get(), ScopeKind::Normal);
        for (auto& stmtInBlock : when->thenBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        const auto thenState = retainExistingStates(baseline, varStates);

        if (when->elseBlock) {
            varStates = baseline;
            enterScope(when->elseBlock.get(), ScopeKind::Normal);
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
        enterScope(loop->body.get(), ScopeKind::LoopBoundary);
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

        enterScope(scan->body.get(), ScopeKind::LoopBoundary);
        defineTrackedVar(
            scan->itemName,
            TrackedVar{itemType, false, StorageState::Initialized, 0, false, std::nullopt});
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
            enterScope(branch.body.get(), ScopeKind::Normal);

            if (choiceInfo) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt != choiceInfo->variants.end()) {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        defineTrackedVar(
                            branch.bindings[i],
                            TrackedVar{payloadType, false, StorageState::Initialized, 0, false, std::nullopt});
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
        enterScope(lift->failBlock.get(), ScopeKind::Normal);
        defineTrackedVar(
            lift->failName,
            TrackedVar{failType, false, StorageState::Initialized, 0, false, std::nullopt});
        for (auto& stmtInBlock : lift->failBlock->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();

        varStates = baseline;
        defineTrackedVar(
            lift->valueName,
            TrackedVar{okType, false, StorageState::Initialized, 0, false, std::nullopt});
        return;
    }

    if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        checkExpr(tryStmt->expr.get(), false);

        const ResolvedType outcomeType = typeOfExpr(tryStmt->expr.get());
        ResolvedType okType = makeUnknownType();
        ResolvedType failType = makeUnknownType();
        if (outcomeType.name == "Outcome" && outcomeType.params.size() == 2) {
            okType = outcomeType.params[0];
            failType = outcomeType.params[1];
        }

        if (semantic != nullptr) {
            const auto typeIt = semantic->result().tryBindingTypes.find(tryStmt);
            if (typeIt != semantic->result().tryBindingTypes.end()) {
                okType = typeIt->second;
            }
        }

        if (tryStmt->failBlock) {
            const auto baseline = varStates;
            enterScope(tryStmt->failBlock.get(), ScopeKind::Normal);
            defineTrackedVar(
                tryStmt->failName,
                TrackedVar{failType, false, StorageState::Initialized, 0, false, std::nullopt});
            for (auto& stmtInBlock : tryStmt->failBlock->statements) {
                checkStmt(stmtInBlock.get());
            }
            exitScope();
            varStates = baseline;
        }

        defineTrackedVar(
            tryStmt->name,
            TrackedVar{okType, false, StorageState::Initialized, 0, false, std::nullopt});
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        enterScope(raw->body.get(), ScopeKind::Normal);
        for (auto& stmtInBlock : raw->body->statements) {
            checkStmt(stmtInBlock.get());
        }
        exitScope();
        return;
    }

    if (auto* stop = dynamic_cast<StopStmt*>(stmt)) {
        for (const auto& drop : collectUnwindDrops(ScopeKind::LoopBoundary)) {
            recordDropBeforeStmt(stop, drop.name, drop.type);
        }
        return;
    }

    if (auto* skip = dynamic_cast<SkipStmt*>(stmt)) {
        for (const auto& drop : collectUnwindDrops(ScopeKind::LoopBoundary)) {
            recordDropBeforeStmt(skip, drop.name, drop.type);
        }
        return;
    }
}

void OwnershipChecker::checkExpr(Expr* expr, bool isConsume) {
    if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const auto it = varStates.find(ident->name);
        if (it == varStates.end()) {
            return;
        }

        auto& state = it->second;
        if (!isDefinitelyInitialized(state.storageState)) {
            reportError(ident, unavailableValueMessage(state.storageState, ident->name));
            return;
        }

        if (isConsume && isTrackedOwned(state.type)) {
            if (state.sharedBorrows > 0 || state.mutableBorrow) {
                reportError(ident, "Cannot move value while it is borrowed -> " + ident->name);
            }
            state.storageState = StorageState::Moved;
            state.sharedBorrows = 0;
            state.mutableBorrow = false;
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
    const auto methodSignature = resolveMethodSignature(call->callee.get());
    if (!signature && !methodSignature.has_value()) {
        for (auto& arg : call->args) {
            checkExpr(arg.get(), false);
        }
        return;
    }

    std::vector<std::pair<Expr*, ResolvedType>> borrowedArgs;
    if (methodSignature.has_value()) {
        if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            if (methodSignature->receiverType.isView()) {
                acquireView(member->object.get(), methodSignature->receiverType);
                borrowedArgs.push_back({member->object.get(), methodSignature->receiverType});
            }
        }
        signature = &methodSignature->function;
    }

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

void OwnershipChecker::enterScope(const BlockStmt* block, ScopeKind kind) {
    ScopeFrame frame;
    frame.block = block;
    frame.kind = kind;
    scopeStack.push_back(std::move(frame));
}

void OwnershipChecker::exitScope() {
    if (scopeStack.empty()) {
        return;
    }

    auto frame = std::move(scopeStack.back());
    scopeStack.pop_back();

    std::vector<DropAction> drops;
    for (auto it = frame.bindings.rbegin(); it != frame.bindings.rend(); ++it) {
        auto current = varStates.find(it->name);
        if (current != varStates.end()) {
            if (shouldScheduleDrop(current->second)) {
                drops.push_back(DropAction{it->name, current->second.type});
            }
            releaseLexicalBorrow(current->second);
        }

        if (it->previousState.has_value()) {
            varStates[it->name] = *it->previousState;
        } else {
            varStates.erase(it->name);
        }
    }

    if (frame.block && !drops.empty()) {
        ownershipResult.dropsAtBlockEnd[frame.block] = std::move(drops);
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

void OwnershipChecker::recordDropBeforeStmt(const Stmt* stmt, const std::string& name, const ResolvedType& type) {
    if (!stmt) {
        return;
    }
    ownershipResult.dropsBeforeStmt[stmt].push_back(DropAction{name, type});
}

std::vector<DropAction> OwnershipChecker::collectUnwindDrops(std::optional<ScopeKind> stopAfterKind) const {
    std::vector<DropAction> drops;
    auto stateMap = varStates;

    for (auto frameIt = scopeStack.rbegin(); frameIt != scopeStack.rend(); ++frameIt) {
        for (auto bindingIt = frameIt->bindings.rbegin(); bindingIt != frameIt->bindings.rend(); ++bindingIt) {
            auto current = stateMap.find(bindingIt->name);
            if (current != stateMap.end()) {
                if (shouldScheduleDrop(current->second)) {
                    drops.push_back(DropAction{bindingIt->name, current->second.type});
                }
                if (current->second.lexicalBorrow.has_value()) {
                    releaseBorrowTokenInStateMap(stateMap, *current->second.lexicalBorrow);
                }
            }

            if (bindingIt->previousState.has_value()) {
                stateMap[bindingIt->name] = *bindingIt->previousState;
            } else {
                stateMap.erase(bindingIt->name);
            }
        }

        if (stopAfterKind.has_value() && frameIt->kind == *stopAfterKind) {
            break;
        }
    }

    return drops;
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

std::optional<MethodSignature> OwnershipChecker::resolveMethodSignature(const Expr* callee) const {
    return semantic->lookupMethodSignature(callee);
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
        const auto methodSignature = resolveMethodSignature(call->callee.get());
        if (methodSignature.has_value()) {
            if (methodSignature->viewReturnFromReceiver) {
                if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                    return resolveBorrowRootName(member->object.get());
                }
                return std::nullopt;
            }
            if (methodSignature->viewReturnSourceArg.has_value()) {
                const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                if (sourceIndex < call->args.size()) {
                    return resolveBorrowRootName(call->args[sourceIndex].get());
                }
                return std::nullopt;
            }
        }
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
    if (!isDefinitelyInitialized(state.storageState)) {
        return false;
    }

    if (token.viewKind == "look") {
        if (state.mutableBorrow) {
            reportError(node, "Cannot create ref view while ref mut view is active -> " + token.rootName);
            return false;
        }
        ++state.sharedBorrows;
        return true;
    }

    if (token.viewKind == "edit") {
        if (state.mutableBorrow || state.sharedBorrows > 0) {
            reportError(node, "Cannot create ref mut view while another view is active -> " + token.rootName);
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

bool OwnershipChecker::shouldScheduleDrop(const TrackedVar& state) const {
    return isTrackedOwned(state.type) && state.storageState == StorageState::Initialized;
}

void OwnershipChecker::mergeTrackedState(TrackedVar& target, const TrackedVar& source) const {
    target.storageState = mergeStorageState(target.storageState, source.storageState);
    target.sharedBorrows = std::max(target.sharedBorrows, source.sharedBorrows);
    target.mutableBorrow = target.mutableBorrow || source.mutableBorrow;
    if (target.storageState != StorageState::Initialized || target.lexicalBorrow != source.lexicalBorrow) {
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
