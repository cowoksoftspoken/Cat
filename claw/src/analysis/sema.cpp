#include "analysis/sema.h"

#include "ast/ast.h"

#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace claw::frontend {

namespace {

ResolvedType makePlainType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Plain;
    return type;
}

ResolvedType makeOwnedType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Owned;
    return type;
}

bool startsWithUppercase(const std::string& name) {
    return !name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

bool isNumericTypeName(const std::string& name) {
    static const std::unordered_set<std::string> numericTypes = {
        "Byte", "Rune", "Int8", "Int16", "Int32", "Int64", "Int128",
        "UInt8", "UInt16", "UInt32", "UInt64", "UInt128",
        "Bits8", "Bits16", "Bits32", "Bits64", "Bits128",
        "Float32", "Float64", "USize", "ISize"};
    return numericTypes.find(name) != numericTypes.end();
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer(std::vector<ImportedBinding> importedBindings)
    : importedBindings(std::move(importedBindings)) {}

const AnalysisResult& SemanticAnalyzer::result() const {
    return analysisResult;
}

const ResolvedType* SemanticAnalyzer::lookupExprType(const Expr* expr) const {
    const auto it = analysisResult.exprTypes.find(expr);
    return it != analysisResult.exprTypes.end() ? &it->second : nullptr;
}

const FunctionSignature* SemanticAnalyzer::lookupFunctionSignature(const FnDecl* fn) const {
    const auto it = analysisResult.functionSignatures.find(fn);
    return it != analysisResult.functionSignatures.end() ? &it->second : nullptr;
}

const FunctionSignature* SemanticAnalyzer::lookupFunctionSignature(const std::string& name) const {
    const auto it = analysisResult.functionsByName.find(name);
    return it != analysisResult.functionsByName.end() ? &it->second : nullptr;
}

const ShapeInfo* SemanticAnalyzer::lookupShape(const std::string& name) const {
    const auto it = analysisResult.shapesByName.find(name);
    return it != analysisResult.shapesByName.end() ? &it->second : nullptr;
}

const ChoiceInfo* SemanticAnalyzer::lookupChoice(const std::string& name) const {
    const auto it = analysisResult.choicesByName.find(name);
    return it != analysisResult.choicesByName.end() ? &it->second : nullptr;
}

void SemanticAnalyzer::reportError(const std::string& msg) {
    diagnostics.push_back(Diagnostic{"semantic", msg, {}});
}

void SemanticAnalyzer::reportError(const SourceSpan& span, const std::string& msg) {
    diagnostics.push_back(Diagnostic{"semantic", msg, span});
}

void SemanticAnalyzer::reportError(const AstNode* node, const std::string& msg) {
    reportError(node ? node->span : SourceSpan{}, msg);
}

void SemanticAnalyzer::registerPrelude() {
    auto registerBuiltin = [&](const std::string& name) {
        FunctionSignature signature;
        signature.paramTypes.push_back(makeUnknownType());
        signature.returnType = makePlainType("Unit");
        signature.isExternal = true;

        analysisResult.functionsByName[name] = signature;

        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Function;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate prelude name: " + name);
        }
    };

    registerBuiltin("print");
    registerBuiltin("println");
}

void SemanticAnalyzer::analyze(RealmDecl* realm) {
    analysisResult = AnalysisResult{};
    diagnostics.clear();
    currentFunction = nullptr;
    currentSignature = nullptr;
    loopDepth = 0;

    scopes.enterScope();
    registerPrelude();
    registerImports(realm);
    declareTopLevel(realm);
    resolveTopLevelTypes(realm);

    for (auto& decl : realm->declarations) {
        analyzeDecl(decl.get());
    }

    scopes.exitScope();

    if (!diagnostics.empty()) {
        throw DiagnosticError(
            "Semantic analysis failed with " + std::to_string(diagnostics.size()) + " errors.",
            diagnostics);
    }
}

void SemanticAnalyzer::registerImports(const RealmDecl* realm) {
    auto defineModule = [&](const std::string& name) {
        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Module;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate imported name: " + name);
        }
    };

    auto defineFunction = [&](const std::string& name) {
        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Function;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate imported name: " + name);
        }
    };

    auto defineType = [&](const std::string& name, SymbolKind kind) {
        if (kind == SymbolKind::Choice) {
            typeCatalog.registerChoiceName(name);
        } else {
            typeCatalog.registerShapeName(name);
        }

        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = kind;
        sym->isExternal = true;
        sym->type = makeOwnedType(name);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate imported name: " + name);
        }
    };

    if (!importedBindings.empty()) {
        for (const auto& binding : importedBindings) {
            switch (binding.kind) {
                case SymbolKind::Module:
                    defineModule(binding.name);
                    break;
                case SymbolKind::Function:
                    defineFunction(binding.name);
                    break;
                case SymbolKind::Choice:
                case SymbolKind::Shape:
                    defineType(binding.name, binding.kind);
                    break;
                default:
                    defineModule(binding.name);
                    break;
            }
        }
        return;
    }

    auto registerImportedName = [&](const std::string& name) {
        if (startsWithUppercase(name)) {
            typeCatalog.registerShapeName(name);
        } else {
            defineModule(name);
        }
    };

    for (const auto& imp : realm->imports) {
        if (!imp.specificItems.empty()) {
            for (const auto& item : imp.specificItems) {
                registerImportedName(item);
            }
            continue;
        }

        std::string importedName = imp.modulePath;
        const auto dotPos = importedName.rfind('.');
        if (dotPos != std::string::npos) {
            importedName = importedName.substr(dotPos + 1);
        }
        registerImportedName(importedName);
    }
}

void SemanticAnalyzer::declareTopLevel(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            typeCatalog.registerShapeName(shape->name, shape->typeParams.size());
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            typeCatalog.registerChoiceName(choice->name, choice->typeParams.size());
        }
    }

    for (const auto& decl : realm->declarations) {
        auto sym = std::make_shared<Symbol>();

        if (auto* fn = dynamic_cast<FnDecl*>(decl.get())) {
            sym->name = fn->name;
            sym->kind = SymbolKind::Function;
            sym->type = makeUnknownType(fn->name);
            if (!scopes.define(fn->name, sym)) {
                reportError(fn, "Duplicate declaration: " + fn->name);
            }
        } else if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            sym->name = shape->name;
            sym->kind = SymbolKind::Shape;
            sym->type = makeOwnedType(shape->name);
            if (!scopes.define(shape->name, sym)) {
                reportError(shape, "Duplicate type declaration: " + shape->name);
            }
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            sym->name = choice->name;
            sym->kind = SymbolKind::Choice;
            sym->type = makeOwnedType(choice->name);
            if (!scopes.define(choice->name, sym)) {
                reportError(choice, "Duplicate type declaration: " + choice->name);
            }
        }
    }
}

void SemanticAnalyzer::resolveTopLevelTypes(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            analyzeShapeDecl(shape);
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            analyzeChoiceDecl(choice);
        } else if (auto* fn = dynamic_cast<FnDecl*>(decl.get())) {
            FunctionSignature signature;
            std::unordered_set<std::string> typeParams;

            for (const auto& param : fn->params) {
                signature.paramTypes.push_back(resolveTypeNode(param.type.get(), typeParams));
            }

            signature.returnType = fn->returnType
                ? resolveTypeNode(fn->returnType.get(), typeParams)
                : makePlainType("Unit");

            analysisResult.functionSignatures[fn] = signature;
            analysisResult.functionsByName[fn->name] = signature;
        }
    }
}

ResolvedType SemanticAnalyzer::resolveTypeNode(
    const TypeNode* node,
    const std::unordered_set<std::string>& localTypeParams) {
    return typeCatalog.resolveType(node, localTypeParams, &diagnostics);
}

std::unordered_map<std::string, ResolvedType> SemanticAnalyzer::buildTypeBindingsChecked(
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes,
    const std::string& context) {
    if (paramNames.empty()) {
        return {};
    }

    if (paramNames.size() != argTypes.size()) {
        reportError(
            "Generic argument count mismatch for " + context + ": expected " +
            std::to_string(paramNames.size()) + ", got " + std::to_string(argTypes.size()));
    }

    return buildTypeBindings(paramNames, argTypes);
}

ResolvedType SemanticAnalyzer::instantiateGenericType(
    const ResolvedType& type,
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes,
    const std::string& context) {
    const auto bindings = buildTypeBindingsChecked(paramNames, argTypes, context);
    return substituteType(type, bindings);
}

void SemanticAnalyzer::analyzeDecl(Decl* decl) {
    if (auto* fn = dynamic_cast<FnDecl*>(decl)) {
        analyzeFnDecl(fn);
    }
}

void SemanticAnalyzer::analyzeFnDecl(FnDecl* fn) {
    scopes.enterScope();

    const FnDecl* previousFunction = currentFunction;
    const FunctionSignature* previousSignature = currentSignature;
    currentFunction = fn;
    currentSignature = lookupFunctionSignature(fn);

    for (size_t i = 0; i < fn->params.size(); ++i) {
        const ResolvedType type = (currentSignature && i < currentSignature->paramTypes.size())
            ? currentSignature->paramTypes[i]
            : makeUnknownType();
        defineVariable(
            fn->params[i].name,
            type,
            false,
            "Duplicate parameter name: " + fn->params[i].name,
            fn->params[i].span);
    }

    if (fn->body) {
        analyzeBlock(fn->body.get());
    }

    currentFunction = previousFunction;
    currentSignature = previousSignature;
    scopes.exitScope();
}

void SemanticAnalyzer::analyzeShapeDecl(ShapeDecl* shape) {
    ShapeInfo info;
    info.typeParams = shape->typeParams;

    std::unordered_set<std::string> localTypeParams(shape->typeParams.begin(), shape->typeParams.end());
    std::unordered_set<std::string> seenFields;

    for (const auto& field : shape->fields) {
        if (!seenFields.insert(field.name).second) {
            reportError(field.span, "Duplicate field in shape '" + shape->name + "': " + field.name);
            continue;
        }

        ResolvedType type = resolveTypeNode(field.type.get(), localTypeParams);
        info.fields[field.name] = type;
        info.fieldOrder.push_back(field.name);
    }

    analysisResult.shapesByName[shape->name] = std::move(info);
}

void SemanticAnalyzer::analyzeChoiceDecl(ChoiceDecl* choice) {
    ChoiceInfo info;
    info.typeParams = choice->typeParams;

    std::unordered_set<std::string> localTypeParams(choice->typeParams.begin(), choice->typeParams.end());
    std::unordered_set<std::string> seenVariants;

    for (const auto& variant : choice->variants) {
        if (!seenVariants.insert(variant.tag).second) {
            reportError(variant.span, "Duplicate variant in choice '" + choice->name + "': " + variant.tag);
            continue;
        }

        ChoiceVariantInfo variantInfo;
        for (const auto& payload : variant.payloads) {
            variantInfo.payloadTypes.push_back(resolveTypeNode(payload.type.get(), localTypeParams));
        }

        info.variants[variant.tag] = std::move(variantInfo);
        info.variantOrder.push_back(variant.tag);
    }

    analysisResult.choicesByName[choice->name] = std::move(info);
}

void SemanticAnalyzer::analyzeBlock(BlockStmt* block) {
    for (auto& stmt : block->statements) {
        analyzeStmt(stmt.get());
    }
}

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (auto* bind = dynamic_cast<BindingStmt*>(stmt)) {
        const ResolvedType valueType = bind->value ? analyzeExpr(bind->value.get()) : makeUnknownType();
        ResolvedType finalType = bind->type ? resolveTypeNode(bind->type.get(), {}) : valueType;

        if (!bind->type && !bind->value) {
            reportError(bind, "Binding requires a type or an initializer: " + bind->name);
            finalType = makeUnknownType();
        }

        if (bind->type && bind->value && !canAssignType(valueType, finalType)) {
            reportError(
                bind,
                "Initializer type mismatch for '" + bind->name + "': expected " +
                    finalType.describe() + ", got " + valueType.describe());
        }

        analysisResult.bindingTypes[bind] = finalType;
        defineVariable(
            bind->name,
            finalType,
            bind->isMutable,
            "Duplicate variable declaration: " + bind->name,
            bind->span);
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        const ResolvedType valueType = assign->value ? analyzeExpr(assign->value.get()) : makeUnknownType();
        const ResolvedType targetType = assign->target ? analyzeExpr(assign->target.get()) : makeUnknownType();

        if (auto* ident = dynamic_cast<IdentExpr*>(assign->target.get())) {
            const auto sym = lookupSymbol(ident->name);
            if (!sym) {
                reportError(ident, "Undefined variable: " + ident->name);
            } else {
                if (sym->kind != SymbolKind::Variable) {
                    reportError(assign, "Assignment target is not mutable storage: " + ident->name);
                } else if (!sym->isMutable) {
                    reportError(assign, "Cannot assign to immutable binding: " + ident->name);
                }

                if (!canAssignType(valueType, sym->type)) {
                    reportError(
                        assign,
                        "Assigned value type mismatch for '" + ident->name + "': expected " +
                            sym->type.describe() + ", got " + valueType.describe());
                }
            }
        } else if (auto* member = dynamic_cast<MemberExpr*>(assign->target.get())) {
            bool canMutate = false;
            if (const auto* objectType = lookupExprType(member->object.get())) {
                if (objectType->viewKind == "edit") {
                    canMutate = true;
                }
            }

            if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Variable && sym->isMutable) {
                    canMutate = true;
                }
            }

            if (!canMutate) {
                reportError(assign, "Member assignment requires mutable storage or an edit view.");
            }

            if (!canAssignType(valueType, targetType)) {
                reportError(
                    assign,
                    "Assigned value type mismatch for member access: expected " +
                        targetType.describe() + ", got " + valueType.describe());
            }
        } else {
            reportError(assign, "Unsupported assignment target.");
        }
        return;
    }

    if (auto* give = dynamic_cast<GiveStmt*>(stmt)) {
        const ResolvedType valueType = give->value ? analyzeExpr(give->value.get()) : makePlainType("Unit");
        if (currentSignature && currentFunction && !canAssignType(valueType, currentSignature->returnType)) {
            reportError(
                give,
                "Return type mismatch in function '" + currentFunction->name + "': expected " +
                    currentSignature->returnType.describe() + ", got " + valueType.describe());
        }
        return;
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            analyzeExpr(exprStmt->expr.get());
        }
        return;
    }

    if (auto* when = dynamic_cast<WhenStmt*>(stmt)) {
        const ResolvedType conditionType = analyzeExpr(when->condition.get());
        if (!isConditionLike(conditionType)) {
            reportError(when->condition.get(), "'when' condition must be Bool, got " + conditionType.describe());
        }

        scopes.enterScope();
        analyzeBlock(when->thenBlock.get());
        scopes.exitScope();

        if (when->elseBlock) {
            scopes.enterScope();
            analyzeBlock(when->elseBlock.get());
            scopes.exitScope();
        }
        return;
    }

    if (auto* loop = dynamic_cast<LoopStmt*>(stmt)) {
        if (loop->condition) {
            const ResolvedType conditionType = analyzeExpr(loop->condition.get());
            if (!isConditionLike(conditionType)) {
                reportError(loop->condition.get(), "'loop' condition must be Bool, got " + conditionType.describe());
            }
        }

        scopes.enterScope();
        ++loopDepth;
        analyzeBlock(loop->body.get());
        --loopDepth;
        scopes.exitScope();
        return;
    }

    if (auto* scan = dynamic_cast<ScanStmt*>(stmt)) {
        const ResolvedType iterableType = analyzeExpr(scan->iterable.get());
        const ResolvedType itemType = !iterableType.params.empty() ? iterableType.params.front() : makeUnknownType();
        analysisResult.scanItemTypes[scan] = itemType;

        scopes.enterScope();
        ++loopDepth;
        defineVariable(scan->itemName, itemType, false, "Duplicate scan variable: " + scan->itemName, scan->span);
        analyzeBlock(scan->body.get());
        --loopDepth;
        scopes.exitScope();
        return;
    }

    if (auto* pick = dynamic_cast<PickStmt*>(stmt)) {
        const ResolvedType valueType = analyzeExpr(pick->value.get());
        const ChoiceInfo* choiceInfo = valueType.isUnknown() ? nullptr : lookupChoice(valueType.name);
        std::unordered_set<std::string> seenTags;

        std::unordered_map<std::string, ResolvedType> choiceBindings;
        if (choiceInfo) {
            choiceBindings = buildTypeBindingsChecked(
                choiceInfo->typeParams,
                valueType.params,
                "choice '" + valueType.name + "'");
        }

        for (const auto& branch : pick->branches) {
            if (!seenTags.insert(branch.tag).second) {
                reportError(branch.span, "Duplicate pick branch: " + branch.tag);
            }

            scopes.enterScope();

            if (choiceInfo) {
                const auto variantIt = choiceInfo->variants.find(branch.tag);
                if (variantIt == choiceInfo->variants.end()) {
                    reportError(branch.span, "Unknown choice variant '" + branch.tag + "' for type '" + valueType.name + "'.");
                } else {
                    const auto& payloadTypes = variantIt->second.payloadTypes;
                    if (payloadTypes.size() != branch.bindings.size()) {
                        reportError(
                            branch.span,
                            "Branch '" + branch.tag + "' expects " + std::to_string(payloadTypes.size()) +
                                " binding(s), got " + std::to_string(branch.bindings.size()));
                    }
                    for (size_t i = 0; i < branch.bindings.size(); ++i) {
                        const ResolvedType payloadType = i < payloadTypes.size()
                            ? substituteType(payloadTypes[i], choiceBindings)
                            : makeUnknownType();
                        defineVariable(
                            branch.bindings[i],
                            payloadType,
                            false,
                            "Duplicate pick binding: " + branch.bindings[i],
                            branch.span);
                    }
                }
            } else {
                for (const auto& binding : branch.bindings) {
                    defineVariable(binding, makeUnknownType(), false, "Duplicate pick binding: " + binding, branch.span);
                }
            }

            analyzeBlock(branch.body.get());
            scopes.exitScope();
        }

        if (choiceInfo && seenTags.size() != choiceInfo->variantOrder.size()) {
            reportError(pick, "Non-exhaustive pick for choice type '" + valueType.name + "'.");
        }
        return;
    }

    if (auto* lift = dynamic_cast<LiftStmt*>(stmt)) {
        const ResolvedType outcomeType = analyzeExpr(lift->expr.get());
        ResolvedType okType = makeUnknownType();
        ResolvedType failType = makeUnknownType();

        if (!outcomeType.isUnknown()) {
            if (outcomeType.name != "Outcome" || outcomeType.params.size() != 2) {
                reportError(lift->expr.get(), "'lift' requires an Outcome value, got " + outcomeType.describe());
            } else {
                okType = outcomeType.params[0];
                failType = outcomeType.params[1];
            }
        }

        if (!blockDefinitelyTerminates(lift->failBlock.get())) {
            reportError(lift, "'lift' failure block must terminate the current control path.");
        }

        scopes.enterScope();
        defineVariable(lift->failName, failType, false, "Duplicate lift failure binding: " + lift->failName, lift->span);
        analyzeBlock(lift->failBlock.get());
        scopes.exitScope();

        defineVariable(lift->valueName, okType, false, "Duplicate lift success binding: " + lift->valueName, lift->span);
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        scopes.enterScope();
        analyzeBlock(raw->body.get());
        scopes.exitScope();
        return;
    }

    if (dynamic_cast<StopStmt*>(stmt)) {
        if (loopDepth == 0) {
            reportError(stmt, "'stop' is only valid inside loops.");
        }
        return;
    }

    if (dynamic_cast<SkipStmt*>(stmt)) {
        if (loopDepth == 0) {
            reportError(stmt, "'skip' is only valid inside loops.");
        }
        return;
    }
}

ResolvedType SemanticAnalyzer::analyzeExpr(Expr* expr) {
    ResolvedType type = makeUnknownType();

    if (dynamic_cast<BoolExpr*>(expr)) {
        type = makePlainType("Bool");
    } else if (dynamic_cast<IntExpr*>(expr)) {
        type = makePlainType("Int32");
    } else if (dynamic_cast<StringExpr*>(expr)) {
        type = makeOwnedType("Text");
    } else if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        if (!sym) {
            reportError(ident, "Undefined variable: " + ident->name);
        } else if (sym->kind == SymbolKind::Variable) {
            type = sym->type;
        } else {
            type = makeUnknownType(ident->name);
        }
    } else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        const ResolvedType leftType = analyzeExpr(binary->left.get());
        const ResolvedType rightType = analyzeExpr(binary->right.get());

        if (binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
            binary->op == ">" || binary->op == ">=") {
            if (!leftType.isUnknown() && !rightType.isUnknown() &&
                !canAssignType(rightType, leftType) && !canAssignType(leftType, rightType)) {
                reportError(binary, "Incompatible comparison operands: " + leftType.describe() + " and " + rightType.describe());
            }
            type = makePlainType("Bool");
        } else if (!leftType.isUnknown() && !rightType.isUnknown()) {
            if (sameType(leftType, rightType) && isNumericTypeName(leftType.name)) {
                type = leftType;
            } else {
                reportError(binary, "Incompatible arithmetic operands: " + leftType.describe() + " and " + rightType.describe());
                type = makeUnknownType();
            }
        }
    } else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        const FunctionSignature* signature = nullptr;
        bool externalCall = false;

        if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
            signature = lookupFunctionSignature(calleeIdent->name);
        } else if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Module) {
                    externalCall = true;
                }
            }
        }

        analyzeExpr(call->callee.get());

        if (signature) {
            if (call->args.size() != signature->paramTypes.size()) {
                if (!signature->isExternal || signature->paramTypes.size() != 1) {
                    reportError(
                        call,
                        "Call to '" + dynamic_cast<IdentExpr*>(call->callee.get())->name + "' expects " +
                            std::to_string(signature->paramTypes.size()) + " argument(s), got " +
                            std::to_string(call->args.size()));
                }
            }

            for (size_t i = 0; i < call->args.size(); ++i) {
                const ResolvedType argType = analyzeExpr(call->args[i].get());
                if (i < signature->paramTypes.size() && !canAssignType(argType, signature->paramTypes[i])) {
                    reportError(
                        call->args[i].get(),
                        "Call argument type mismatch: expected " + signature->paramTypes[i].describe() +
                            ", got " + argType.describe());
                }
            }

            type = signature->returnType;
        } else {
            if (!externalCall) {
                if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
                    const auto sym = lookupSymbol(calleeIdent->name);
                    if (sym && sym->kind == SymbolKind::Variable) {
                        reportError(calleeIdent, "Expression is not callable: " + calleeIdent->name);
                    }
                }
            }

            for (auto& arg : call->args) {
                analyzeExpr(arg.get());
            }
        }
    } else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        const ResolvedType objectType = analyzeExpr(member->object.get());

        if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Module) {
                type = makeUnknownType(member->member);
                analysisResult.exprTypes[expr] = type;
                return type;
            }
        }

        if (!objectType.isUnknown()) {
            const ShapeInfo* shape = lookupShape(objectType.name);
            if (!shape) {
                reportError(member, "Type '" + objectType.name + "' does not expose fields.");
            } else {
                const auto fieldIt = shape->fields.find(member->member);
                if (fieldIt == shape->fields.end()) {
                    reportError(member, "Unknown field '" + member->member + "' on type '" + objectType.name + "'.");
                } else {
                    const ResolvedType fieldType = instantiateGenericType(
                        fieldIt->second,
                        shape->typeParams,
                        objectType.params,
                        "shape '" + objectType.name + "'");
                    type = adaptMemberType(objectType, fieldType);
                }
            }
        }
    }

    analysisResult.exprTypes[expr] = type;
    return type;
}

std::shared_ptr<Symbol> SemanticAnalyzer::lookupSymbol(const std::string& name) const {
    return const_cast<ScopeTree&>(scopes).lookup(name);
}

void SemanticAnalyzer::defineVariable(
    const std::string& name,
    const ResolvedType& type,
    bool isMutable,
    const std::string& duplicateMessage,
    const SourceSpan& duplicateSpan) {
    auto sym = std::make_shared<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::Variable;
    sym->isMutable = isMutable;
    sym->type = type;

    if (!scopes.define(name, sym)) {
        reportError(duplicateSpan, duplicateMessage);
    }
}

bool SemanticAnalyzer::isConditionLike(const ResolvedType& type) const {
    return type.isUnknown() || (type.isPlain() && type.name == "Bool");
}

bool SemanticAnalyzer::blockDefinitelyTerminates(const BlockStmt* block) const {
    for (const auto& stmt : block->statements) {
        if (stmtDefinitelyTerminates(stmt.get())) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::stmtDefinitelyTerminates(const Stmt* stmt) const {
    if (dynamic_cast<const GiveStmt*>(stmt) || dynamic_cast<const StopStmt*>(stmt) || dynamic_cast<const SkipStmt*>(stmt)) {
        return true;
    }

    if (auto* when = dynamic_cast<const WhenStmt*>(stmt)) {
        return when->elseBlock && blockDefinitelyTerminates(when->thenBlock.get()) &&
               blockDefinitelyTerminates(when->elseBlock.get());
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        if (pick->branches.empty()) {
            return false;
        }

        for (const auto& branch : pick->branches) {
            if (!blockDefinitelyTerminates(branch.body.get())) {
                return false;
            }
        }
        return true;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        return blockDefinitelyTerminates(raw->body.get());
    }

    return false;
}

} // namespace claw::frontend
