#include "analysis/sema.h"

#include "ast/ast.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <limits>
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

ResolvedType makeIntegerLiteralType() {
    ResolvedType type;
    type.name = "IntLiteral";
    type.category = TypeCategory::Plain;
    return type;
}

ResolvedType makeFloatLiteralType() {
    ResolvedType type;
    type.name = "FloatLiteral";
    type.category = TypeCategory::Plain;
    return type;
}

bool isFloatLiteralType(const ResolvedType& type) {
    return type.category == TypeCategory::Plain && type.viewKind.empty() && type.name == "FloatLiteral";
}

bool isNumericLiteralType(const ResolvedType& type) {
    return isIntegerLiteralType(type) || isFloatLiteralType(type);
}

bool isConcreteNumericType(const ResolvedType& type) {
    return type.isPlain() && type.viewKind.empty() && isNumericTypeName(type.name);
}

bool isFloatTypeName(const std::string& name) {
    return name == "Float32" || name == "Float64";
}

bool literalMagnitudeHasNonZeroDigit(const std::string& magnitude) {
    for (char c : magnitude) {
        if (std::isdigit(static_cast<unsigned char>(c)) && c != '0') {
            return true;
        }
    }
    return false;
}

struct NumericLiteralParts {
    std::string magnitude;
    std::string suffix;
};

NumericLiteralParts splitNumericLiteralText(const std::string& text) {
    const size_t suffixPos = text.find('_');
    if (suffixPos == std::string::npos) {
        return {text, {}};
    }
    return {text.substr(0, suffixPos), text.substr(suffixPos + 1)};
}

std::optional<ResolvedType> resolveNumericLiteralSuffixType(const std::string& suffix) {
    if (suffix.empty() || !isNumericTypeName(suffix)) {
        return std::nullopt;
    }
    return makePlainType(suffix);
}

using UInt128 = unsigned __int128;

bool tryParseUnsignedDecimal(const std::string& text, UInt128* value) {
    if (!value || text.empty()) {
        return false;
    }

    UInt128 parsed = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        const unsigned digit = static_cast<unsigned>(c - '0');
        const UInt128 maxValue = std::numeric_limits<UInt128>::max();
        if (parsed > (maxValue - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }

    *value = parsed;
    return true;
}

UInt128 maxUnsignedBits(unsigned bits) {
    if (bits >= 128) {
        return std::numeric_limits<UInt128>::max();
    }
    return (UInt128{1} << bits) - 1;
}

UInt128 maxSignedPositiveBits(unsigned bits) {
    if (bits <= 1) {
        return 0;
    }
    return maxUnsignedBits(bits - 1);
}

std::optional<unsigned> integerLikeBitWidth(const std::string& typeName) {
    static const std::unordered_map<std::string, unsigned> widths = {
        {"Byte", 8}, {"Int8", 8}, {"Int16", 16}, {"Int32", 32}, {"Int64", 64}, {"Int128", 128},
        {"UInt8", 8}, {"UInt16", 16}, {"UInt32", 32}, {"UInt64", 64}, {"UInt128", 128},
        {"Bits8", 8}, {"Bits16", 16}, {"Bits32", 32}, {"Bits64", 64}, {"Bits128", 128}};
    const auto it = widths.find(typeName);
    return it != widths.end() ? std::optional<unsigned>(it->second) : std::nullopt;
}

bool integerLiteralFitsTarget(const std::string& magnitude, const std::string& targetName, const TargetSpec& target) {
    UInt128 value = 0;
    if (!tryParseUnsignedDecimal(magnitude, &value)) {
        return false;
    }

    const unsigned pointerWidthBits = std::min(target.pointerWidthBits == 0 ? 64u : target.pointerWidthBits, 128u);
    const unsigned ptrdiffWidthBits = std::min(target.ptrdiffWidthBits == 0 ? 64u : target.ptrdiffWidthBits, 128u);

    if (targetName == "Rune") {
        return value <= UInt128{0x10FFFF};
    }
    if (targetName == "USize") {
        return value <= maxUnsignedBits(pointerWidthBits);
    }
    if (targetName == "ISize") {
        return value <= maxSignedPositiveBits(ptrdiffWidthBits);
    }

    if (const auto bits = integerLikeBitWidth(targetName)) {
        if (targetName.rfind("Int", 0) == 0) {
            return value <= maxSignedPositiveBits(*bits);
        }
        return value <= maxUnsignedBits(*bits);
    }

    if (targetName == "Float32") {
        return value <= UInt128{1} << 24;
    }
    if (targetName == "Float64") {
        return value <= UInt128{1} << 53;
    }

    return false;
}

bool floatLiteralFitsTarget(const std::string& magnitude, const std::string& targetName) {
    if (!isFloatTypeName(targetName)) {
        return false;
    }

    try {
        const long double value = std::stold(magnitude);
        if (!std::isfinite(value)) {
            return false;
        }
        const bool nonZeroMagnitude = literalMagnitudeHasNonZeroDigit(magnitude);
        if (targetName == "Float32") {
            const float narrowed = static_cast<float>(value);
            return std::isfinite(narrowed) && !(narrowed == 0.0f && nonZeroMagnitude);
        }
        const double narrowed = static_cast<double>(value);
        return std::isfinite(narrowed) && !(narrowed == 0.0 && nonZeroMagnitude);
    } catch (const std::exception&) {
        return false;
    }
}

bool startsWithUppercase(const std::string& name) {
    return !name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0;
}

bool shouldUseExpectedIntegerType(const ResolvedType* expectedType) {
    return expectedType && expectedType->isPlain() && expectedType->viewKind.empty() &&
           isIntegerLikeTypeName(expectedType->name);
}

bool shouldUseExpectedFloatType(const ResolvedType* expectedType) {
    return expectedType && expectedType->isPlain() && expectedType->viewKind.empty() &&
           isFloatTypeName(expectedType->name);
}

ResolvedType defaultIntegerLiteralType() {
    return makePlainType("Int32");
}

ResolvedType defaultFloatLiteralType() {
    return makePlainType("Float64");
}

ResolvedType normalizeInferredLiteralType(const ResolvedType& type) {
    if (isIntegerLiteralType(type)) {
        return defaultIntegerLiteralType();
    }
    if (isFloatLiteralType(type)) {
        return defaultFloatLiteralType();
    }
    return type;
}

std::optional<ResolvedType> numericLiteralContextType(
    const ResolvedType* expectedType,
    const ResolvedType& leftType,
    const ResolvedType& rightType) {
    if (expectedType && isConcreteNumericType(*expectedType)) {
        return *expectedType;
    }
    if (isConcreteNumericType(leftType)) {
        return leftType;
    }
    if (isConcreteNumericType(rightType)) {
        return rightType;
    }
    if (isFloatLiteralType(leftType) || isFloatLiteralType(rightType)) {
        return defaultFloatLiteralType();
    }
    if (isIntegerLiteralType(leftType) && isIntegerLiteralType(rightType)) {
        return defaultIntegerLiteralType();
    }
    return std::nullopt;
}

bool isRawAddressTypeName(const std::string& name) {
    return name == "Addr" || name == "RawPtr" || name == "RawMut";
}

std::string describeCalleeExpr(const Expr* expr) {
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return ident->name;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return describeCalleeExpr(member->object.get()) + "." + member->member;
    }

    return "<callee>";
}

struct BuiltinMethodSpec {
    std::string receiverName;
    std::string methodName;
    std::string receiverViewKind;
    std::vector<ResolvedType> paramTypes;
    ResolvedType returnType;
    std::optional<size_t> viewReturnSourceArg;
    bool viewReturnFromReceiver = false;
};

ResolvedType asViewType(const ResolvedType& base, const std::string& viewKind) {
    ResolvedType adapted = base;
    adapted.viewKind = viewKind;
    adapted.category = viewKind.empty() ? base.category : TypeCategory::View;
    return adapted;
}

const std::vector<BuiltinMethodSpec>& builtinMethodSpecs() {
    static const std::vector<BuiltinMethodSpec> specs = [] {
        std::vector<BuiltinMethodSpec> entries;
        auto addSizedLookMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "len", "look", {}, makePlainType("USize")});
            entries.push_back(BuiltinMethodSpec{receiverName, "is_empty", "look", {}, makePlainType("Bool")});
        };
        auto addByteIndexMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{
                receiverName,
                "byte_at",
                "look",
                {makePlainType("USize")},
                makePlainType("Byte")});
        };
        auto addByteEdgeMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "first_byte", "look", {}, makePlainType("Byte")});
            entries.push_back(BuiltinMethodSpec{receiverName, "last_byte", "look", {}, makePlainType("Byte")});
            entries.push_back(BuiltinMethodSpec{receiverName, "find_byte", "look", {makePlainType("Byte")}, makePlainType("ISize")});
            entries.push_back(BuiltinMethodSpec{receiverName, "count_byte", "look", {makePlainType("Byte")}, makePlainType("USize")});
        };
        auto addTextSearchMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "starts_with", "look", {asViewType(makeOwnedType(receiverName), "look")}, makePlainType("Bool")});
            entries.push_back(BuiltinMethodSpec{receiverName, "ends_with", "look", {asViewType(makeOwnedType(receiverName), "look")}, makePlainType("Bool")});
        };
        auto addCapacityLookMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "capacity", "look", {}, makePlainType("USize")});
            entries.push_back(BuiltinMethodSpec{receiverName, "has_capacity", "look", {makePlainType("USize")}, makePlainType("Bool")});
        };
        auto addClearEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "clear", "edit", {}, makePlainType("Unit")});
        };
        auto addReserveEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "reserve", "edit", {makePlainType("USize")}, makePlainType("Unit")});
        };
        auto addTruncateEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "truncate", "edit", {makePlainType("USize")}, makePlainType("Unit")});
        };
        auto addShrinkToFitEditMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "shrink_to_fit", "edit", {}, makePlainType("Unit")});
        };
        auto addSliceViewMethod = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{
                receiverName,
                "slice",
                "look",
                {makePlainType("USize"), makePlainType("USize")},
                asViewType(makeOwnedType(receiverName), "look"),
                std::nullopt,
                true});
        };

        addSizedLookMethods("Text");
        addSizedLookMethods("Bytes");
        addSizedLookMethods("Span");
        addSizedLookMethods("Vec");
        addSizedLookMethods("Table");
        addSizedLookMethods("Set");
        addSizedLookMethods("Ring");

        addByteIndexMethod("Text");
        addByteIndexMethod("Bytes");
        addByteEdgeMethods("Text");
        addByteEdgeMethods("Bytes");
        addTextSearchMethods("Text");
        addTextSearchMethods("Bytes");

        entries.push_back(BuiltinMethodSpec{"Text", "contains", "look", {asViewType(makeOwnedType("Text"), "look")}, makePlainType("Bool")});
        entries.push_back(BuiltinMethodSpec{"Bytes", "contains", "look", {asViewType(makeOwnedType("Bytes"), "look")}, makePlainType("Bool")});
        entries.push_back(BuiltinMethodSpec{"Text", "contains_byte", "look", {makePlainType("Byte")}, makePlainType("Bool")});
        entries.push_back(BuiltinMethodSpec{"Bytes", "contains_byte", "look", {makePlainType("Byte")}, makePlainType("Bool")});

        addSliceViewMethod("Text");
        addSliceViewMethod("Bytes");

        addCapacityLookMethod("Bytes");
        addCapacityLookMethod("Vec");
        addCapacityLookMethod("Table");
        addCapacityLookMethod("Set");
        addCapacityLookMethod("Ring");

        addClearEditMethod("Bytes");
        addClearEditMethod("Vec");
        addClearEditMethod("Table");
        addClearEditMethod("Set");
        addClearEditMethod("Ring");

        addReserveEditMethod("Bytes");
        addReserveEditMethod("Vec");
        addReserveEditMethod("Table");
        addReserveEditMethod("Set");
        addReserveEditMethod("Ring");

        addTruncateEditMethod("Bytes");
        addTruncateEditMethod("Vec");
        addTruncateEditMethod("Ring");

        addShrinkToFitEditMethod("Bytes");
        addShrinkToFitEditMethod("Vec");
        addShrinkToFitEditMethod("Table");
        addShrinkToFitEditMethod("Set");
        addShrinkToFitEditMethod("Ring");
        return entries;
    }();
    return specs;
}

} // namespace

SemanticAnalyzer::SemanticAnalyzer(std::vector<ImportedBinding> importedBindings, TargetSpec target)
    : importedBindings(std::move(importedBindings)), target(std::move(target)) {}

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

const FunctionSignature* SemanticAnalyzer::lookupCallableSignature(const Expr* callee) const {
    if (auto* ident = dynamic_cast<const IdentExpr*>(callee)) {
        return lookupFunctionSignature(ident->name);
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(callee)) {
        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Module && sym->moduleInfo) {
                const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                if (exported != sym->moduleInfo->exportedItems.end() &&
                    exported->second.kind == SymbolKind::Function &&
                    exported->second.functionSignature.has_value()) {
                    return &*exported->second.functionSignature;
                }
            }
        }
    }

    return nullptr;
}

const ShapeInfo* SemanticAnalyzer::lookupShape(const std::string& name) const {
    const auto it = analysisResult.shapesByName.find(name);
    return it != analysisResult.shapesByName.end() ? &it->second : nullptr;
}

std::optional<MethodSignature> SemanticAnalyzer::lookupMethodSignature(const Expr* callee) const {
    auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return std::nullopt;
    }

    if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
        const auto sym = lookupSymbol(objectIdent->name);
        if (sym && sym->kind == SymbolKind::Module) {
            return std::nullopt;
        }
    }

    const auto* receiverType = lookupExprType(member->object.get());
    if (!receiverType) {
        return std::nullopt;
    }

    return lookupMethodSignature(*receiverType, member->member);
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
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    loopDepth = 0;
    rawDepth = 0;

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
    auto defineModule = [&](const std::string& name, std::shared_ptr<ModuleInfo> moduleInfo = {}) {
        auto sym = std::make_shared<Symbol>();
        sym->name = name;
        sym->kind = SymbolKind::Module;
        sym->isExternal = true;
        sym->type = makeUnknownType(name);
        sym->moduleInfo = std::move(moduleInfo);
        if (!scopes.define(name, sym)) {
            reportError("Duplicate imported name: " + name);
        }
    };

    auto defineFunction = [&](const ImportedBinding& binding) {
        if (binding.functionSignature.has_value()) {
            FunctionSignature signature = *binding.functionSignature;
            signature.isExternal = true;
            analysisResult.functionsByName[binding.name] = signature;
        }

        auto sym = std::make_shared<Symbol>();
        sym->name = binding.name;
        sym->kind = SymbolKind::Function;
        sym->isExternal = true;
        sym->type = makeUnknownType(binding.name);
        if (!scopes.define(binding.name, sym)) {
            reportError("Duplicate imported name: " + binding.name);
        }
    };

    auto defineType = [&](const ImportedBinding& binding) {
        if (binding.kind == SymbolKind::Choice) {
            typeCatalog.registerChoiceName(
                binding.name,
                binding.choiceInfo.has_value()
                    ? std::optional<size_t>(binding.choiceInfo->typeParams.size())
                    : std::nullopt);
            if (binding.choiceInfo.has_value()) {
                analysisResult.choicesByName[binding.name] = *binding.choiceInfo;
            }
        } else {
            typeCatalog.registerShapeName(
                binding.name,
                binding.shapeInfo.has_value()
                    ? std::optional<size_t>(binding.shapeInfo->typeParams.size())
                    : std::nullopt);
            if (binding.shapeInfo.has_value()) {
                analysisResult.shapesByName[binding.name] = *binding.shapeInfo;
            }
        }

        auto sym = std::make_shared<Symbol>();
        sym->name = binding.name;
        sym->kind = binding.kind;
        sym->isExternal = true;
        sym->type = makeOwnedType(binding.name);
        if (!scopes.define(binding.name, sym)) {
            reportError("Duplicate imported name: " + binding.name);
        }
    };

    if (!importedBindings.empty()) {
        for (const auto& binding : importedBindings) {
            switch (binding.kind) {
                case SymbolKind::Module:
                    defineModule(binding.name, binding.moduleInfo);
                    break;
                case SymbolKind::Function:
                    defineFunction(binding);
                    break;
                case SymbolKind::Choice:
                case SymbolKind::Shape:
                    defineType(binding);
                    break;
                default:
                    defineModule(binding.name, binding.moduleInfo);
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

std::optional<MethodSignature> SemanticAnalyzer::lookupMethodSignature(
    const ResolvedType& receiverType,
    const std::string& methodName) const {
    if (receiverType.isUnknown()) {
        return std::nullopt;
    }

    for (const auto& spec : builtinMethodSpecs()) {
        if (spec.receiverName != receiverType.name || spec.methodName != methodName) {
            continue;
        }

        MethodSignature signature;
        signature.name = spec.methodName;
        signature.receiverType = asViewType(receiverType, spec.receiverViewKind);
        signature.function.paramTypes = spec.paramTypes;
        signature.function.returnType = spec.returnType;
        signature.function.isExternal = true;
        signature.viewReturnSourceArg = spec.viewReturnSourceArg;
        signature.viewReturnFromReceiver = spec.viewReturnFromReceiver;
        signature.isBuiltin = true;
        return signature;
    }

    return std::nullopt;
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
    const auto previousViewReturnSourceParam = currentViewReturnSourceParam;
    const bool previousViewReturnSeen = currentViewReturnSeen;
    const int previousRawDepth = rawDepth;
    currentFunction = fn;
    currentSignature = lookupFunctionSignature(fn);
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    rawDepth = 0;

    if (currentSignature) {
        if (currentSignature->returnType.viewKind == "edit") {
            reportError(fn, "Safe functions cannot return edit views. Return an owned value or a look view derived from a parameter.");
        }
        for (size_t i = 0; i < fn->params.size() && i < currentSignature->paramTypes.size(); ++i) {
            if (isRawAddressType(currentSignature->paramTypes[i])) {
                reportError(fn->params[i].span, "Raw address types are not allowed in safe function parameters.");
            }
        }
        if (isRawAddressType(currentSignature->returnType)) {
            reportError(fn, "Raw address types are not allowed in safe function return types.");
        }
    }

    for (size_t i = 0; i < fn->params.size(); ++i) {
        const ResolvedType type = (currentSignature && i < currentSignature->paramTypes.size())
            ? currentSignature->paramTypes[i]
            : makeUnknownType();
        defineVariable(
            fn->params[i].name,
            type,
            false,
            "Duplicate parameter name: " + fn->params[i].name,
            fn->params[i].span,
            type.isView() ? std::optional<size_t>(i) : std::nullopt);
    }

    if (fn->body) {
        analyzeBlock(fn->body.get());
    }

    if (currentSignature && currentSignature->returnType.isView()) {
        auto signatureIt = analysisResult.functionSignatures.find(fn);
        if (signatureIt != analysisResult.functionSignatures.end()) {
            signatureIt->second.viewReturnSourceParam = currentViewReturnSourceParam;
            analysisResult.functionsByName[fn->name] = signatureIt->second;
        }
        if (!currentViewReturnSeen) {
            reportError(fn, "View-returning functions must return a view derived from one of their view parameters.");
        }
    }

    currentFunction = previousFunction;
    currentSignature = previousSignature;
    currentViewReturnSourceParam = previousViewReturnSourceParam;
    currentViewReturnSeen = previousViewReturnSeen;
    rawDepth = previousRawDepth;
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
        const ResolvedType declaredType = bind->type ? resolveTypeNode(bind->type.get(), {}) : makeUnknownType();
        const ResolvedType valueType = bind->value
            ? analyzeExpr(bind->value.get(), bind->type ? &declaredType : nullptr)
            : makeUnknownType();
        ResolvedType finalType = bind->type ? declaredType : normalizeInferredLiteralType(valueType);

        if (!bind->type && !bind->value) {
            reportError(bind, "Binding requires a type or an initializer: " + bind->name);
            finalType = makeUnknownType();
        }

        if (!bind->isMutable && !bind->value) {
            reportError(bind, "Immutable binding requires an initializer: " + bind->name);
        }

        const bool bindingTypeMatches = finalType.isView()
            ? canPassArgumentType(bind->value.get(), valueType, finalType)
            : canAssignType(valueType, finalType);
        if (bind->type && bind->value && !bindingTypeMatches) {
            reportError(
                bind,
                "Initializer type mismatch for '" + bind->name + "': expected " +
                    finalType.describe() + ", got " + valueType.describe());
        }

        if (!bind->type && bind->value && isIntegerLiteralType(valueType)) {
            analysisResult.exprTypes[bind->value.get()] = finalType;
        }

        if (rawDepth == 0 && isRawAddressType(finalType)) {
            reportError(bind, "Raw address values may only appear inside raw blocks.");
        }

        const std::optional<size_t> viewSourceParamIndex = (finalType.isView() && bind->value)
            ? resolveViewSourceParam(bind->value.get())
            : std::nullopt;

        analysisResult.bindingTypes[bind] = finalType;
        defineVariable(
            bind->name,
            finalType,
            bind->isMutable,
            "Duplicate variable declaration: " + bind->name,
            bind->span,
            viewSourceParamIndex);
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        const ResolvedType targetType = assign->target ? analyzeExpr(assign->target.get()) : makeUnknownType();
        const ResolvedType valueType = assign->value ? analyzeExpr(assign->value.get(), &targetType) : makeUnknownType();

        if (rawDepth == 0 && (isRawAddressType(valueType) || isRawAddressType(targetType))) {
            reportError(assign, "Raw address values may only appear inside raw blocks.");
        }

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

                const bool assignmentTypeMatches = sym->type.isView()
                    ? canPassArgumentType(assign->value.get(), valueType, sym->type)
                    : canAssignType(valueType, sym->type);
                if (!assignmentTypeMatches) {
                    reportError(
                        assign,
                        "Assigned value type mismatch for '" + ident->name + "': expected " +
                            sym->type.describe() + ", got " + valueType.describe());
                }

                if (sym->type.isView()) {
                    sym->viewSourceParamIndex = resolveViewSourceParam(assign->value.get());
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
        const ResolvedType valueType = give->value
            ? analyzeExpr(give->value.get(), currentSignature ? &currentSignature->returnType : nullptr)
            : makePlainType("Unit");
        if (currentSignature && currentSignature->returnType.isView()) {
            if (!canBorrowAsView(valueType, currentSignature->returnType)) {
                reportError(
                    give,
                    "Return type mismatch in function '" + currentFunction->name + "': expected " +
                        currentSignature->returnType.describe() + ", got " + valueType.describe());
            }
            const auto sourceParamIndex = give->value ? resolveViewSourceParam(give->value.get()) : std::nullopt;
            if (!sourceParamIndex.has_value()) {
                reportError(give, "Returned view must be derived from a view parameter. Views cannot escape local owners or temporaries.");
            } else if (!currentViewReturnSeen) {
                currentViewReturnSourceParam = sourceParamIndex;
                currentViewReturnSeen = true;
            } else if (currentViewReturnSourceParam != sourceParamIndex) {
                reportError(give, "All returned views in a function must be derived from the same view parameter.");
            }
        } else if (currentSignature && currentFunction && !canAssignType(valueType, currentSignature->returnType)) {
            reportError(
                give,
                "Return type mismatch in function '" + currentFunction->name + "': expected " +
                    currentSignature->returnType.describe() + ", got " + valueType.describe());
        }

        if (rawDepth == 0 && isRawAddressType(valueType)) {
            reportError(give, "Raw address values may only appear inside raw blocks.");
        }
        return;
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            const ResolvedType exprType = analyzeExpr(exprStmt->expr.get());
            if (rawDepth == 0 && isRawAddressType(exprType)) {
                reportError(exprStmt, "Raw address values may only appear inside raw blocks.");
            }
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
        ++rawDepth;
        analyzeBlock(raw->body.get());
        --rawDepth;
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

ResolvedType SemanticAnalyzer::analyzeExpr(Expr* expr, const ResolvedType* expectedType) {
    ResolvedType type = makeUnknownType();

    if (dynamic_cast<BoolExpr*>(expr)) {
        type = makePlainType("Bool");
    } else if (auto* intExpr = dynamic_cast<IntExpr*>(expr)) {
        const auto literal = splitNumericLiteralText(intExpr->value);
        if (!literal.suffix.empty()) {
            const auto suffixType = resolveNumericLiteralSuffixType(literal.suffix);
            if (!suffixType.has_value()) {
                reportError(intExpr, "Unknown numeric literal suffix: " + literal.suffix);
            } else {
                type = *suffixType;
                if (!integerLiteralFitsTarget(literal.magnitude, type.name, target)) {
                    reportError(intExpr, "Integer literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
                }
            }
        } else if (shouldUseExpectedIntegerType(expectedType) || shouldUseExpectedFloatType(expectedType)) {
            type = *expectedType;
            if (!integerLiteralFitsTarget(literal.magnitude, type.name, target)) {
                const std::string detail = shouldUseExpectedFloatType(expectedType)
                    ? "does not fit exactly in target type "
                    : "does not fit target type ";
                reportError(intExpr, "Integer literal '" + literal.magnitude + "' " + detail + type.name + ".");
            }
        } else {
            type = makeIntegerLiteralType();
        }
    } else if (auto* floatExpr = dynamic_cast<FloatExpr*>(expr)) {
        const auto literal = splitNumericLiteralText(floatExpr->value);
        if (!literal.suffix.empty()) {
            const auto suffixType = resolveNumericLiteralSuffixType(literal.suffix);
            if (!suffixType.has_value()) {
                reportError(floatExpr, "Unknown numeric literal suffix: " + literal.suffix);
            } else if (!isFloatTypeName(suffixType->name)) {
                reportError(floatExpr, "Float literal suffix must be Float32 or Float64, got " + literal.suffix + ".");
                type = *suffixType;
            } else {
                type = *suffixType;
                if (!floatLiteralFitsTarget(literal.magnitude, type.name)) {
                    reportError(floatExpr, "Float literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
                }
            }
        } else if (shouldUseExpectedFloatType(expectedType)) {
            type = *expectedType;
            if (!floatLiteralFitsTarget(literal.magnitude, type.name)) {
                reportError(floatExpr, "Float literal '" + literal.magnitude + "' does not fit target type " + type.name + ".");
            }
        } else {
            type = makeFloatLiteralType();
        }
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
        const bool isComparison = binary->op == "==" || binary->op == "!=" || binary->op == "<" || binary->op == "<=" ||
            binary->op == ">" || binary->op == ">=";
        const ResolvedType* operandExpected = (expectedType && isConcreteNumericType(*expectedType)) ? expectedType : nullptr;
        ResolvedType leftType = analyzeExpr(binary->left.get(), operandExpected);
        ResolvedType rightType = analyzeExpr(binary->right.get(), operandExpected);

        const auto preferredType = numericLiteralContextType(operandExpected, leftType, rightType);
        if (preferredType.has_value()) {
            if (isNumericLiteralType(leftType)) {
                leftType = analyzeExpr(binary->left.get(), &*preferredType);
            }
            if (isNumericLiteralType(rightType)) {
                rightType = analyzeExpr(binary->right.get(), &*preferredType);
            }
        }

        if (isComparison) {
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
        std::optional<MethodSignature> methodSignature;
        bool externalCall = false;
        bool skipCalleeAnalysis = false;

        if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
            signature = lookupFunctionSignature(calleeIdent->name);
        } else if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Module) {
                    if (sym->moduleInfo) {
                        const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                        if (exported != sym->moduleInfo->exportedItems.end()) {
                            if (exported->second.kind == SymbolKind::Function && exported->second.functionSignature.has_value()) {
                                signature = &*exported->second.functionSignature;
                            } else if (exported->second.kind != SymbolKind::Function) {
                                reportError(call, "Module member is not callable: " + objectIdent->name + "." + member->member);
                            } else {
                                externalCall = true;
                            }
                        } else {
                            externalCall = true;
                        }
                    } else {
                        externalCall = true;
                    }
                }
            }

            if (!signature && !externalCall) {
                const ResolvedType receiverType = analyzeExpr(member->object.get());
                methodSignature = lookupMethodSignature(receiverType, member->member);
                if (methodSignature.has_value()) {
                    signature = &methodSignature->function;
                    if (!canPassArgumentType(member->object.get(), receiverType, methodSignature->receiverType)) {
                        reportError(
                            member->object.get(),
                            "Method receiver type mismatch: expected " + methodSignature->receiverType.describe() +
                                ", got " + receiverType.describe());
                    }
                } else if (!receiverType.isUnknown() && !lookupShape(receiverType.name)) {
                    reportError(call, "Type '" + receiverType.name + "' does not provide method '" + member->member + "'.");
                    skipCalleeAnalysis = true;
                }
            }
        }

        if (!methodSignature.has_value() && !skipCalleeAnalysis) {
            analyzeExpr(call->callee.get());
        }

        if (signature) {
            if (call->args.size() != signature->paramTypes.size()) {
                if (!signature->isExternal || signature->paramTypes.size() != 1) {
                    const std::string calleeName = dynamic_cast<IdentExpr*>(call->callee.get())
                        ? dynamic_cast<IdentExpr*>(call->callee.get())->name
                        : describeCalleeExpr(call->callee.get());
                    reportError(
                        call,
                        "Call to '" + calleeName + "' expects " +
                            std::to_string(signature->paramTypes.size()) + " argument(s), got " +
                            std::to_string(call->args.size()));
                }
            }

            for (size_t i = 0; i < call->args.size(); ++i) {
                const ResolvedType* paramType = i < signature->paramTypes.size() ? &signature->paramTypes[i] : nullptr;
                const ResolvedType argType = analyzeExpr(call->args[i].get(), paramType);
                if (rawDepth == 0 &&
                    ((paramType && isRawAddressType(*paramType)) || isRawAddressType(argType))) {
                    reportError(
                        call->args[i].get(),
                        "Raw address values may only cross call boundaries inside raw blocks.");
                }
                if (paramType && !canPassArgumentType(call->args[i].get(), argType, *paramType)) {
                    reportError(
                        call->args[i].get(),
                        "Call argument type mismatch: expected " + paramType->describe() +
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
                if (sym->moduleInfo) {
                    const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                    if (exported != sym->moduleInfo->exportedItems.end()) {
                        if (exported->second.kind == SymbolKind::Shape || exported->second.kind == SymbolKind::Choice) {
                            type.name = exported->second.name;
                            type.category = TypeCategory::Owned;
                        } else {
                            type = makeUnknownType(member->member);
                        }
                    } else {
                        type = makeUnknownType(member->member);
                    }
                } else {
                    type = makeUnknownType(member->member);
                }
                analysisResult.exprTypes[expr] = type;
                return type;
            }
        }

        if (!objectType.isUnknown()) {
            if (lookupMethodSignature(objectType, member->member).has_value()) {
                type = makeUnknownType(member->member);
            } else {
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
    }

    analysisResult.exprTypes[expr] = type;
    return type;
}

std::shared_ptr<Symbol> SemanticAnalyzer::lookupSymbol(const std::string& name) const {
    return const_cast<ScopeTree&>(scopes).lookup(name);
}

std::optional<size_t> SemanticAnalyzer::resolveViewSourceParam(const Expr* expr) const {
    if (!expr) {
        return std::nullopt;
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        if (sym && sym->kind == SymbolKind::Variable) {
            return sym->viewSourceParamIndex;
        }
        return std::nullopt;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return resolveViewSourceParam(member->object.get());
    }

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const FunctionSignature* signature = nullptr;
        std::optional<MethodSignature> methodSignature;

        if (auto* calleeIdent = dynamic_cast<const IdentExpr*>(call->callee.get())) {
            signature = lookupFunctionSignature(calleeIdent->name);
        } else if (auto* member = dynamic_cast<const MemberExpr*>(call->callee.get())) {
            if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
                const auto sym = lookupSymbol(objectIdent->name);
                if (sym && sym->kind == SymbolKind::Module && sym->moduleInfo) {
                    const auto exported = sym->moduleInfo->exportedItems.find(member->member);
                    if (exported != sym->moduleInfo->exportedItems.end() &&
                        exported->second.kind == SymbolKind::Function &&
                        exported->second.functionSignature.has_value()) {
                        signature = &*exported->second.functionSignature;
                    }
                }
            }

            if (!signature) {
                methodSignature = lookupMethodSignature(call->callee.get());
                if (methodSignature.has_value()) {
                    if (methodSignature->viewReturnFromReceiver) {
                        return resolveViewSourceParam(member->object.get());
                    }
                    if (methodSignature->viewReturnSourceArg.has_value()) {
                        const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                        if (sourceIndex < call->args.size()) {
                            return resolveViewSourceParam(call->args[sourceIndex].get());
                        }
                    }
                }
            }
        }

        if (!signature || !signature->returnType.isView() || !signature->viewReturnSourceParam.has_value()) {
            return std::nullopt;
        }

        const size_t sourceIndex = *signature->viewReturnSourceParam;
        if (sourceIndex >= call->args.size()) {
            return std::nullopt;
        }
        return resolveViewSourceParam(call->args[sourceIndex].get());
    }

    return std::nullopt;
}

bool SemanticAnalyzer::canBorrowAsView(const ResolvedType& from, const ResolvedType& to) const {
    if (!to.isView()) {
        return false;
    }

    if (from.isUnknown() || to.isUnknown()) {
        return true;
    }

    if (!(from.isOwned() || from.isView())) {
        return false;
    }

    if (from.name != to.name || from.params.size() != to.params.size()) {
        return false;
    }

    for (size_t i = 0; i < from.params.size(); ++i) {
        if (!sameType(from.params[i], to.params[i])) {
            return false;
        }
    }

    if (from.isOwned()) {
        return true;
    }

    if (from.viewKind == to.viewKind) {
        return true;
    }

    return from.viewKind == "edit" && to.viewKind == "look";
}

bool SemanticAnalyzer::canPassArgumentType(Expr* expr, const ResolvedType& from, const ResolvedType& to) const {
    if (canAssignType(from, to)) {
        return true;
    }

    if (!to.isView() || !canBorrowAsView(from, to)) {
        return false;
    }

    if (to.viewKind != "edit") {
        return true;
    }

    if (from.isView()) {
        return from.viewKind == "edit";
    }

    return canBorrowExprAsEdit(expr);
}

bool SemanticAnalyzer::canBorrowExprAsEdit(const Expr* expr) const {
    if (!expr) {
        return false;
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        if (!sym || sym->kind != SymbolKind::Variable) {
            return false;
        }
        if (sym->type.isView()) {
            return sym->type.viewKind == "edit";
        }
        return sym->isMutable && sym->type.isOwned();
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        if (const auto* objectType = lookupExprType(member->object.get())) {
            if (objectType->isView()) {
                return objectType->viewKind == "edit";
            }
        }

        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            return sym && sym->kind == SymbolKind::Variable && sym->isMutable && sym->type.isOwned();
        }
    }

    return false;
}

bool SemanticAnalyzer::isRawAddressType(const ResolvedType& type) const {
    return isRawAddressTypeName(type.name);
}

void SemanticAnalyzer::defineVariable(
    const std::string& name,
    const ResolvedType& type,
    bool isMutable,
    const std::string& duplicateMessage,
    const SourceSpan& duplicateSpan,
    std::optional<size_t> viewSourceParamIndex) {
    auto sym = std::make_shared<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::Variable;
    sym->isMutable = isMutable;
    sym->type = type;
    sym->viewSourceParamIndex = viewSourceParamIndex;

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
