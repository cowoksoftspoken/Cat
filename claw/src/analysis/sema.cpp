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
        {"Int8", 8}, {"Int16", 16}, {"Int32", 32}, {"Int64", 64}, {"Int128", 128},
        {"UInt8", 8}, {"UInt16", 16}, {"UInt32", 32}, {"UInt64", 64}, {"UInt128", 128}};
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

    if (targetName == "Char") {
        return value <= UInt128{0x10FFFF};
    }
    if (targetName == "USize") {
        return value <= maxUnsignedBits(pointerWidthBits);
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

bool isAnchorStaticConstructorCall(const Expr* callee) {
    const auto* member = dynamic_cast<const MemberExpr*>(callee);
    if (!member) {
        return false;
    }
    const auto* objectIdent = dynamic_cast<const IdentExpr*>(member->object.get());
    return objectIdent && objectIdent->name == "Anchor" && member->member == "new";
}

bool isAnchorPayloadTypeAllowed(const ResolvedType& type) {
    if (type.isUnknown() || type.isOpaqueExternal() || type.isView()) {
        return false;
    }

    const std::string base = canonicalTypeName(type.name);
    return base != "Span" && base != "CStr" && !isRawAddressTypeName(base);
}

std::string anchorPayloadTypeError(const ResolvedType& type) {
    return "Anchor.new(...) requires an owned payload with stable ownership. " +
        type.describe() + " cannot be anchored directly.";
}

ResolvedType makeOpaqueExternalCallType(const Expr* callee) {
    return makeOpaqueExternalType(describeCalleeExpr(callee));
}

std::string opaqueExternalValueUseMessage(const ResolvedType& type, const std::string& context) {
    const std::string subject = type.name.empty()
        ? std::string("Opaque external call result")
        : "Opaque external call result from '" + type.name + "'";
    return subject + " may only be used as a standalone statement, not in " + context + ".";
}

std::string opaqueExternalRawRequirementMessage(const Expr* callee) {
    return "Opaque external call '" + describeCalleeExpr(callee) +
        "' requires an explicit raw block or a shared typed import contract.";
}

std::string typedExternalRawRequirementMessage(const Expr* callee) {
    return "External call '" + describeCalleeExpr(callee) +
        "' is declared raw-only and requires an explicit raw block.";
}

std::string lowercaseAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char c : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered;
}

bool sameIdentifierIgnoringCase(std::string_view left, std::string_view right) {
    return lowercaseAscii(left) == lowercaseAscii(right);
}

std::optional<std::string> findChoiceVariantName(const ChoiceInfo& choice, std::string_view requested) {
    for (const auto& variantName : choice.variantOrder) {
        if (sameIdentifierIgnoringCase(variantName, requested)) {
            return variantName;
        }
    }
    return std::nullopt;
}

std::optional<ChoiceVariantInfo> resolveChoiceVariantInfo(
    const ChoiceInfo& choice,
    const ResolvedType& concreteType,
    std::string_view variantName) {
    const auto matchedName = findChoiceVariantName(choice, variantName);
    if (!matchedName.has_value()) {
        return std::nullopt;
    }
    const auto bindings = buildTypeBindings(choice.typeParams, concreteType.params);
    const auto variantIt = choice.variants.find(*matchedName);
    if (variantIt == choice.variants.end()) {
        return std::nullopt;
    }

    ChoiceVariantInfo resolved;
    for (const auto& payloadType : variantIt->second.payloadTypes) {
        resolved.payloadTypes.push_back(substituteType(payloadType, bindings));
    }
    return resolved;
}

bool isOutcomeLikeChoice(const ChoiceInfo& choice) {
    const auto okName = findChoiceVariantName(choice, "Ok");
    const auto failName = findChoiceVariantName(choice, "Fail");
    if (!okName.has_value() || !failName.has_value() || choice.variantOrder.size() != 2) {
        return false;
    }
    const auto okIt = choice.variants.find(*okName);
    const auto failIt = choice.variants.find(*failName);
    if (okIt == choice.variants.end() || failIt == choice.variants.end()) {
        return false;
    }
    return okIt->second.payloadTypes.size() == 1 && failIt->second.payloadTypes.size() == 1;
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
    adapted.viewScope = base.viewScope;
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
                makePlainType("UInt8")});
        };
        auto addByteEdgeMethods = [&](const std::string& receiverName) {
            entries.push_back(BuiltinMethodSpec{receiverName, "first_byte", "look", {}, makePlainType("UInt8")});
            entries.push_back(BuiltinMethodSpec{receiverName, "last_byte", "look", {}, makePlainType("UInt8")});
            entries.push_back(BuiltinMethodSpec{receiverName, "find_byte", "look", {makePlainType("UInt8")}, makePlainType("Int64")});
            entries.push_back(BuiltinMethodSpec{receiverName, "count_byte", "look", {makePlainType("UInt8")}, makePlainType("USize")});
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

        addSizedLookMethods("Str");
        addSizedLookMethods("Span");
        addSizedLookMethods("Vec");
        addSizedLookMethods("Map");
        addSizedLookMethods("Set");
        addSizedLookMethods("Queue");

        addByteIndexMethod("Str");
        addByteEdgeMethods("Str");
        addTextSearchMethods("Str");

        entries.push_back(BuiltinMethodSpec{"Str", "contains", "look", {asViewType(makeOwnedType("Str"), "look")}, makePlainType("Bool")});
        entries.push_back(BuiltinMethodSpec{"Str", "contains_byte", "look", {makePlainType("UInt8")}, makePlainType("Bool")});

        addSliceViewMethod("Str");
        addSliceViewMethod("Span");

        addCapacityLookMethod("Vec");
        addCapacityLookMethod("Map");
        addCapacityLookMethod("Set");
        addCapacityLookMethod("Queue");

        addClearEditMethod("Vec");
        addClearEditMethod("Map");
        addClearEditMethod("Set");
        addClearEditMethod("Queue");

        addReserveEditMethod("Vec");
        addReserveEditMethod("Map");
        addReserveEditMethod("Set");
        addReserveEditMethod("Queue");

        addTruncateEditMethod("Vec");
        addTruncateEditMethod("Queue");

        addShrinkToFitEditMethod("Vec");
        addShrinkToFitEditMethod("Map");
        addShrinkToFitEditMethod("Set");
        addShrinkToFitEditMethod("Queue");
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
    if (it != analysisResult.shapesByName.end()) {
        return &it->second;
    }

    const std::string canonical = canonicalTypeName(name);
    if (canonical != name) {
        const auto canonicalIt = analysisResult.shapesByName.find(canonical);
        if (canonicalIt != analysisResult.shapesByName.end()) {
            return &canonicalIt->second;
        }
    }

    return nullptr;
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
    if (it != analysisResult.choicesByName.end()) {
        return &it->second;
    }

    const std::string canonical = canonicalTypeName(name);
    if (canonical != name) {
        const auto canonicalIt = analysisResult.choicesByName.find(canonical);
        if (canonicalIt != analysisResult.choicesByName.end()) {
            return &canonicalIt->second;
        }
    }

    return nullptr;
}

const TargetSpec& SemanticAnalyzer::targetSpec() const {
    return target;
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

bool SemanticAnalyzer::typeContainsBorrowedStorage(const ResolvedType& type) const {
    if (type.isView()) {
        return true;
    }

    const std::string base = canonicalTypeName(type.name);
    if (base == "Span") {
        return true;
    }

    for (const auto& param : type.params) {
        if (typeContainsBorrowedStorage(param)) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::typeUsesOnlyNamedScope(const ResolvedType& type, std::string_view scopeName) const {
    if (type.isView()) {
        return !type.viewScope.empty() && type.viewScope == scopeName;
    }
    if (!type.scopeName.empty()) {
        return type.scopeName == scopeName;
    }

    const std::string base = canonicalTypeName(type.name);
    if (base == "Span") {
        return false;
    }

    for (const auto& param : type.params) {
        if (typeContainsBorrowedStorage(param) && !typeUsesOnlyNamedScope(param, scopeName)) {
            return false;
        }
    }
    return true;
}

bool SemanticAnalyzer::typeContainsScopeName(const ResolvedType& type) const {
    if (!type.scopeName.empty()) {
        return true;
    }
    for (const auto& param : type.params) {
        if (typeContainsScopeName(param)) {
            return true;
        }
    }
    return false;
}

bool SemanticAnalyzer::typeIsMustUse(const ResolvedType& type) const {
    const std::string base = canonicalTypeName(type.name);
    return base == "Result" || base == "Maybe";
}

bool SemanticAnalyzer::isImplicitTailExpr(const ExprStmt* stmt) const {
    return stmt && stmt == implicitTailExpr;
}

bool SemanticAnalyzer::isNamedScopeActive(std::string_view scopeName) const {
    return std::find(activeNamedScopes.begin(), activeNamedScopes.end(), scopeName) != activeNamedScopes.end();
}

bool SemanticAnalyzer::symbolCanStoreNamedScope(const Symbol& symbol, std::string_view scopeName) const {
    return std::find(symbol.declaredNamedScopes.begin(), symbol.declaredNamedScopes.end(), scopeName) != symbol.declaredNamedScopes.end();
}

ResolvedType SemanticAnalyzer::bindFormalScopeName(
    const ResolvedType& type,
    std::string_view formalScope,
    std::string_view actualScope) const {
    ResolvedType bound = type;
    if (!formalScope.empty() && bound.scopeName == formalScope) {
        bound.scopeName = std::string(actualScope);
    } else if (actualScope.empty() && bound.scopeName == formalScope) {
        bound.scopeName.clear();
    }
    if (!formalScope.empty() && bound.viewScope == formalScope) {
        bound.viewScope = std::string(actualScope);
    } else if (actualScope.empty() && bound.viewScope == formalScope) {
        bound.viewScope.clear();
    }
    bound.params.clear();
    for (const auto& param : type.params) {
        bound.params.push_back(bindFormalScopeName(param, formalScope, actualScope));
    }
    return bound;
}

void SemanticAnalyzer::enterSemanticScope() {
    scopes.enterScope();
    ++lexicalScopeDepth;
}

void SemanticAnalyzer::exitSemanticScope() {
    if (lexicalScopeDepth > 0) {
        --lexicalScopeDepth;
    }
    scopes.exitScope();
}

bool SemanticAnalyzer::enterNamedBorrowScope(const std::string& name, const SourceSpan& span) {
    if (lookupNamedBorrowScope(name)) {
        reportError(span, "Borrow scope '" + name + "' is already active. Choose a different scope name.");
        return false;
    }

    namedBorrowScopes.push_back(NamedBorrowScope{name, span, lexicalScopeDepth});
    return true;
}

void SemanticAnalyzer::exitNamedBorrowScope() {
    if (!namedBorrowScopes.empty()) {
        namedBorrowScopes.pop_back();
    }
}

const NamedBorrowScope* SemanticAnalyzer::lookupNamedBorrowScope(const std::string& name) const {
    for (auto it = namedBorrowScopes.rbegin(); it != namedBorrowScopes.rend(); ++it) {
        if (it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

bool SemanticAnalyzer::validateScopedViewType(const ResolvedType& type, const AstNode* node, std::string_view context) {
    if (!type.isView() || type.viewScope.empty()) {
        return true;
    }

    if (type.viewKind != "look") {
        reportError(node, "Scoped borrows currently use `ref[s]`, not `" + type.describe() + "`.");
        return false;
    }

    if (!lookupNamedBorrowScope(type.viewScope)) {
        reportError(
            node,
            "Scoped borrow `" + type.describe() + "` is only valid inside `scope " + type.viewScope +
                " { ... }` for " + std::string(context) + ".");
        return false;
    }

    return true;
}

bool SemanticAnalyzer::validateAnchorPayloadType(const ResolvedType& type, const AstNode* node, std::string_view context) {
    auto validateNested = [&](const auto& self, const ResolvedType& current) -> bool {
        if (current.isUnknown()) {
            return true;
        }
        if (current.isOpaqueExternal()) {
            reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                " because Anchor[T] requires a concrete owned payload, not an opaque external value.");
            return false;
        }
        if (current.isView()) {
            reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                " because Anchor[T] requires an owned payload. Store an owned value instead of `" + current.describe() + "`.");
            return false;
        }
        if (current.name == "Span") {
            reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                " because Anchor[T] cannot hold Span[T]. Keep the Span borrow local or store the owned collection instead.");
            return false;
        }
        if (isRawAddressType(current)) {
            reportError(node, "`" + type.describe() + "` cannot be used in " + std::string(context) +
                " because Anchor[T] cannot hold raw address types.");
            return false;
        }
        for (const auto& param : current.params) {
            if (!self(self, param)) {
                return false;
            }
        }
        return true;
    };

    if (type.name != "Anchor") {
        return true;
    }
    if (type.params.size() != 1) {
        reportError(node, "Anchor[T] requires exactly one payload type.");
        return false;
    }
    return validateNested(validateNested, type.params.front());
}
const Symbol* SemanticAnalyzer::resolveBorrowSourceSymbol(const Expr* expr) const {
    if (!expr) {
        return nullptr;
    }

    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        const auto sym = lookupSymbol(ident->name);
        return sym && sym->kind == SymbolKind::Variable ? sym.get() : nullptr;
    }

    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return resolveBorrowSourceSymbol(member->object.get());
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return resolveBorrowSourceSymbol(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return resolveBorrowSourceSymbol(borrow->target.get());
    }

    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        const FunctionSignature* signature = lookupCallableSignature(call->callee.get());
        auto methodSignature = lookupMethodSignature(call->callee.get());

        if (methodSignature.has_value()) {
            if (methodSignature->viewReturnFromReceiver) {
                if (auto* member = dynamic_cast<const MemberExpr*>(call->callee.get())) {
                    return resolveBorrowSourceSymbol(member->object.get());
                }
                return nullptr;
            }
            if (methodSignature->viewReturnSourceArg.has_value()) {
                const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                if (sourceIndex < call->args.size()) {
                    return resolveBorrowSourceSymbol(call->args[sourceIndex].get());
                }
                return nullptr;
            }
        }

        if (signature && signature->returnType.isView() && signature->viewReturnSourceParam.has_value()) {
            const size_t sourceIndex = *signature->viewReturnSourceParam;
            if (sourceIndex < call->args.size()) {
                return resolveBorrowSourceSymbol(call->args[sourceIndex].get());
            }
        }
    }

    return nullptr;
}

bool SemanticAnalyzer::validateScopedBorrowSource(
    const Expr* expr,
    const ResolvedType& targetType,
    const AstNode* node,
    std::string_view context) {
    if (!targetType.isView() || targetType.viewScope.empty()) {
        return true;
    }

    const auto* namedScope = lookupNamedBorrowScope(targetType.viewScope);
    if (!namedScope) {
        reportError(
            node,
            "Scoped borrow `" + targetType.describe() + "` is only valid inside `scope " + targetType.viewScope +
                " { ... }` for " + std::string(context) + ".");
        return false;
    }

    const auto* sourceSymbol = resolveBorrowSourceSymbol(expr);
    if (!sourceSymbol) {
        reportError(
            node,
            "Scoped borrow `" + targetType.describe() +
                "` must come from a named value that lives for the whole scope. Borrow a stable binding instead.");
        return false;
    }

    if (sourceSymbol->lexicalDepth > namedScope->lexicalDepth) {
        reportError(
            node,
            "Source value for `" + targetType.describe() + "` does not live long enough for scope `" +
                targetType.viewScope + "`. Move the source binding outside the inner block or keep the ref in a shorter scope.");
        return false;
    }

    if (sourceSymbol->type.isView() && !sourceSymbol->type.viewScope.empty() &&
        sourceSymbol->type.viewScope != targetType.viewScope) {
        reportError(
            node,
            "Cannot rebind `" + sourceSymbol->type.describe() + "` into `" + targetType.describe() +
                "`. Scoped refs must stay in the same named scope.");
        return false;
    }

    return true;
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
    implicitTailExpr = nullptr;
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    loopDepth = 0;
    rawDepth = 0;
    activeNamedScopes.clear();
    lexicalScopeDepth = 0;
    namedBorrowScopes.clear();

    enterSemanticScope();
    registerPrelude();
    registerImports(realm);
    declareTopLevel(realm);
    resolveTopLevelTypes(realm);
    validateNamedLayouts(realm);

    for (auto& decl : realm->declarations) {
        analyzeDecl(decl.get());
    }

    exitSemanticScope();

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
        if (!imp.items.empty()) {
            for (const auto& item : imp.items) {
                registerImportedName(item.alias.empty() ? item.name : item.alias);
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


void SemanticAnalyzer::validateNamedLayouts(const RealmDecl* realm) {
    for (const auto& decl : realm->declarations) {
        if (auto* shape = dynamic_cast<ShapeDecl*>(decl.get())) {
            bool hadSpecificError = false;
            std::vector<std::string> stack{shape->name};
            const auto* info = lookupShape(shape->name);
            if (info) {
                for (const auto& field : shape->fields) {
                    const auto fieldIt = info->fields.find(field.name);
                    if (fieldIt == info->fields.end()) {
                        continue;
                    }
                    hadSpecificError = validateOwnedLayoutDependency(shape->name, fieldIt->second, field.span, stack) || hadSpecificError;
                }
            }

            ResolvedType type = makeOwnedType(shape->name);
            const auto layout = computeTypeLayout(type, analysisResult, target);
            if (!layout.has_value() && !hadSpecificError) {
                reportError(shape, "Type layout could not be resolved for '" + shape->name + "'. Use indirection or remove recursive owned fields.");
            }
        } else if (auto* choice = dynamic_cast<ChoiceDecl*>(decl.get())) {
            bool hadSpecificError = false;
            std::vector<std::string> stack{choice->name};
            const auto* info = lookupChoice(choice->name);
            if (info) {
                for (const auto& variant : choice->variants) {
                    const auto variantIt = info->variants.find(variant.tag);
                    if (variantIt == info->variants.end()) {
                        continue;
                    }
                    for (size_t i = 0; i < variant.payloads.size() && i < variantIt->second.payloadTypes.size(); ++i) {
                        hadSpecificError = validateOwnedLayoutDependency(choice->name, variantIt->second.payloadTypes[i], variant.payloads[i].span, stack) || hadSpecificError;
                    }
                }
            }

            ResolvedType type = makeOwnedType(choice->name);
            const auto layout = computeTypeLayout(type, analysisResult, target);
            if (!layout.has_value() && !hadSpecificError) {
                reportError(choice, "Type layout could not be resolved for '" + choice->name + "'. Use indirection or remove recursive owned payloads.");
            }
        }
    }
}

bool SemanticAnalyzer::validateOwnedLayoutDependency(
    const std::string& rootName,
    const ResolvedType& type,
    const SourceSpan& span,
    std::vector<std::string>& stack) {
    if (type.isUnknown() || type.isOpaqueExternal() || type.isPlain() || type.isView() || type.isGeneric) {
        return false;
    }
    if (type.name == "Str" || type.name == "Span" || type.name == "CStr" ||
        type.name == "OwnedCStr" || type.name == "Vec" || type.name == "Map" || type.name == "Set" ||
        type.name == "Queue" || type.name == "Arena" || type.name == "Anchor" ||
        type.name == "Anchor" || type.name == "Addr" || type.name == "RawPtr" || type.name == "RawMut" ||
        type.name == "Fn") {
        return false;
    }

    const auto stackIt = std::find(stack.begin(), stack.end(), type.name);
    if (stackIt != stack.end()) {
        std::string cycle;
        for (auto it = stackIt; it != stack.end(); ++it) {
            if (!cycle.empty()) {
                cycle += " -> ";
            }
            cycle += *it;
        }
        cycle += " -> " + type.name;
        reportError(span, "Owned type layout cycle requires indirection: " + cycle + ".");
        return true;
    }

    const auto* shape = lookupShape(type.name);
    const auto* choice = lookupChoice(type.name);
    if (!shape && !choice) {
        return false;
    }

    stack.push_back(type.name);
    bool hadError = false;

    if (shape) {
        for (const auto& fieldName : shape->fieldOrder) {
            const auto fieldIt = shape->fields.find(fieldName);
            if (fieldIt == shape->fields.end()) {
                continue;
            }
            const ResolvedType fieldType = instantiateGenericType(
                fieldIt->second,
                shape->typeParams,
                type.params,
                "shape '" + type.name + "'");
            hadError = validateOwnedLayoutDependency(rootName, fieldType, span, stack) || hadError;
        }
    }

    if (choice) {
        for (const auto& variantName : choice->variantOrder) {
            const auto variantIt = choice->variants.find(variantName);
            if (variantIt == choice->variants.end()) {
                continue;
            }
            for (const auto& payloadType : variantIt->second.payloadTypes) {
                const ResolvedType instantiatedPayload = instantiateGenericType(
                    payloadType,
                    choice->typeParams,
                    type.params,
                    "choice '" + type.name + "'");
                hadError = validateOwnedLayoutDependency(rootName, instantiatedPayload, span, stack) || hadError;
            }
        }
    }

    stack.pop_back();
    return hadError;
}

ResolvedType SemanticAnalyzer::resolveTypeNode(
    const TypeNode* node,
    const std::unordered_set<std::string>& localTypeParams) {
    ResolvedType type = typeCatalog.resolveType(node, localTypeParams, &diagnostics);
    if (node) {
        validateAnchorPayloadType(type, node, "type positions");
    }
    return type;
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
    if (receiverType.isUnknown() || receiverType.isOpaqueExternal()) {
        return std::nullopt;
    }

    if (receiverType.name == "Anchor" && methodName == "get" && receiverType.params.size() == 1) {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        signature.function.returnType = asViewType(receiverType.params.front(), "look");
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
    }

    if (methodName == "span" && (receiverType.name == "Vec" || receiverType.name == "Array") && receiverType.params.size() == 1) {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        ResolvedType spanType = makeOwnedType("Span");
        spanType.params.push_back(receiverType.params.front());
        signature.function.returnType = std::move(spanType);
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
    }

    if (methodName == "get" && receiverType.name == "Anchor" && receiverType.params.size() == 1) {
        MethodSignature signature;
        signature.name = methodName;
        signature.receiverType = asViewType(receiverType, "look");
        signature.function.returnType = asViewType(receiverType.params.front(), "look");
        signature.function.isExternal = true;
        signature.viewReturnFromReceiver = true;
        signature.isBuiltin = true;
        return signature;
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
    enterSemanticScope();

    const FnDecl* previousFunction = currentFunction;
    const FunctionSignature* previousSignature = currentSignature;
    const ExprStmt* previousImplicitTailExpr = implicitTailExpr;
    const auto previousViewReturnSourceParam = currentViewReturnSourceParam;
    const bool previousViewReturnSeen = currentViewReturnSeen;
    const int previousRawDepth = rawDepth;
    currentFunction = fn;
    currentSignature = lookupFunctionSignature(fn);
    implicitTailExpr = nullptr;
    currentViewReturnSourceParam.reset();
    currentViewReturnSeen = false;
    rawDepth = 0;

    if (currentSignature) {
        if (currentSignature->returnType.viewKind == "edit") {
            reportError(fn, "Safe functions cannot return ref mut views. Return an owned value or a ref view derived from a parameter.");
        }
        if (!currentSignature->returnType.viewScope.empty()) {
            reportError(fn, "Function signatures cannot use scoped refs like `" + currentSignature->returnType.describe() + "`. Keep scoped refs inside `scope ... {}` blocks.");
        }
        for (size_t i = 0; i < fn->params.size() && i < currentSignature->paramTypes.size(); ++i) {
            if (isRawAddressType(currentSignature->paramTypes[i])) {
                reportError(fn->params[i].span, "Raw address types are not allowed in safe function parameters.");
            }
            if (!currentSignature->paramTypes[i].viewScope.empty()) {
                reportError(
                    fn->params[i].span,
                    "Function parameters cannot use scoped refs like `" + currentSignature->paramTypes[i].describe() +
                        "`. Keep scoped refs inside `scope ... {}` blocks.");
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
            (type.isView() || type.name == "Span") ? std::optional<size_t>(i) : std::nullopt);
    }

    bool hasImplicitTailReturn = false;
    if (fn->body) {
        if (!fn->body->statements.empty()) {
            implicitTailExpr = dynamic_cast<ExprStmt*>(fn->body->statements.back().get());
        }
        analyzeBlock(fn->body.get());

        if (!fn->body->statements.empty()) {
            if (auto* tailExprStmt = dynamic_cast<ExprStmt*>(fn->body->statements.back().get())) {
                hasImplicitTailReturn = true;
                const ResolvedType valueType = tailExprStmt->expr
                    ? (lookupExprType(tailExprStmt->expr.get()) ? *lookupExprType(tailExprStmt->expr.get()) : makeUnknownType())
                    : makePlainType("Unit");

                if (valueType.isOpaqueExternal()) {
                    reportError(tailExprStmt->expr.get(), opaqueExternalValueUseMessage(valueType, "implicit return value"));
                } else if (!valueType.viewScope.empty()) {
                    reportError(
                        tailExprStmt,
                        "Scoped ref `" + valueType.describe() + "` cannot leave `scope " + valueType.viewScope +
                            "`. Return an owned value instead or keep the borrow inside that scope.");
                } else if (currentSignature && currentSignature->returnType.isView()) {
                    if (!canBorrowAsView(valueType, currentSignature->returnType)) {
                        reportError(
                            tailExprStmt,
                            "Implicit return type mismatch in function '" + currentFunction->name + "': expected " +
                                currentSignature->returnType.describe() + ", got " + valueType.describe());
                    }
                    const auto sourceParamIndex = tailExprStmt->expr ? resolveViewSourceParam(tailExprStmt->expr.get()) : std::nullopt;
                    if (!sourceParamIndex.has_value()) {
                        reportError(
                            tailExprStmt,
                            "Returned ref value must come from one of the function's ref parameters. Return an owned value instead, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
                    } else if (!currentViewReturnSeen) {
                        currentViewReturnSourceParam = sourceParamIndex;
                        currentViewReturnSeen = true;
                    } else if (currentViewReturnSourceParam != sourceParamIndex) {
                        reportError(tailExprStmt, "All returned ref values in a function must come from the same parameter.");
                    }
                } else if (currentSignature && currentFunction && !canAssignType(valueType, currentSignature->returnType)) {
                    reportError(
                        tailExprStmt,
                        "Implicit return type mismatch in function '" + currentFunction->name + "': expected " +
                            currentSignature->returnType.describe() + ", got " + valueType.describe());
                }

                if (rawDepth == 0 && isRawAddressType(valueType)) {
                    reportError(tailExprStmt, "Raw address values may only appear inside raw blocks.");
                }
                if (!valueType.scopeName.empty()) {
                    reportError(
                        tailExprStmt,
                        "Implicit return cannot let value `" + valueType.describe() +
                            "` escape scope `" + valueType.scopeName +
                            "`. Return an owned value, use Anchor[T], or keep the borrow inside the scope.");
                }
            }
        }
    }

    const bool unitReturn = currentSignature &&
        currentSignature->returnType.viewKind.empty() &&
        currentSignature->returnType.name == "Unit";

    if (currentSignature && currentSignature->returnType.isView()) {
        auto signatureIt = analysisResult.functionSignatures.find(fn);
        if (signatureIt != analysisResult.functionSignatures.end()) {
            signatureIt->second.viewReturnSourceParam = currentViewReturnSourceParam;
            analysisResult.functionsByName[fn->name] = signatureIt->second;
        }
        if (!currentViewReturnSeen) {
            reportError(fn, "View-returning functions must return a view derived from one of their view parameters.");
        }
    } else if (currentSignature && !unitReturn && !hasImplicitTailReturn && (!fn->body || !blockDefinitelyTerminates(fn->body.get()))) {
        reportError(
            fn,
            "Function '" + fn->name + "' must end with a value of type " +
                currentSignature->returnType.describe() + " or use explicit return.");
    }

    currentFunction = previousFunction;
    currentSignature = previousSignature;
    implicitTailExpr = previousImplicitTailExpr;
    currentViewReturnSourceParam = previousViewReturnSourceParam;
    currentViewReturnSeen = previousViewReturnSeen;
    rawDepth = previousRawDepth;
    exitSemanticScope();
}

void SemanticAnalyzer::analyzeShapeDecl(ShapeDecl* shape) {
    ShapeInfo info;
    info.isViewShape = shape->isViewShape;
    info.scopeParamName = shape->scopeParamName;
    info.typeParams = shape->typeParams;

    std::unordered_set<std::string> localTypeParams(shape->typeParams.begin(), shape->typeParams.end());
    std::unordered_set<std::string> seenFields;

    for (const auto& field : shape->fields) {
        if (!seenFields.insert(field.name).second) {
            reportError(field.span, "Duplicate field in shape '" + shape->name + "': " + field.name);
            continue;
        }

        ResolvedType type = resolveTypeNode(field.type.get(), localTypeParams);
        const bool shapeFieldUsesDeclaredScope =
            shape->isViewShape && !shape->scopeParamName.empty() && type.isView() &&
            type.viewScope == shape->scopeParamName;
        if (!shapeFieldUsesDeclaredScope && !validateScopedViewType(type, field.type.get(), "shape fields")) {
            info.fields[field.name] = type;
            info.fieldOrder.push_back(field.name);
            continue;
        }
        if (shape->isViewShape) {
            if (shape->scopeParamName.empty()) {
                reportError(shape, "view shape '" + shape->name + "' requires a named scope parameter like [s].");
            } else if (typeContainsBorrowedStorage(type) && !typeUsesOnlyNamedScope(type, shape->scopeParamName)) {
                reportError(
                    field.span,
                    "view shape '" + shape->name + "' field '" + field.name +
                        "' must use the declared scope '" + shape->scopeParamName +
                        "' for all borrowed storage.");
            }
        } else if (typeContainsBorrowedStorage(type)) {
            reportError(
                field.span,
                "shape '" + shape->name + "' cannot store borrowed field '" + field.name + "' of type " +
                    type.describe() + ". Normal shapes may outlive borrowed data. Use an owned field, Anchor[T], or a scoped borrowed aggregate design.");
        } else if (type.isView() && type.viewScope.empty()) {
            reportError(
                field.span,
                "Shape '" + shape->name + "' cannot store " + type.describe() +
                    " directly in field '" + field.name +
                    "'. Store an owned value, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
        }
        info.fields[field.name] = type;
        info.fieldOrder.push_back(field.name);
    }

    analysisResult.shapesByName[shape->name] = info;
    const std::string canonicalName = canonicalTypeName(shape->name);
    if (canonicalName != shape->name) {
        analysisResult.shapesByName[canonicalName] = info;
    }
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
            ResolvedType payloadType = resolveTypeNode(payload.type.get(), localTypeParams);
            if (!validateScopedViewType(payloadType, payload.type.get(), "choice payloads")) {
                variantInfo.payloadTypes.push_back(std::move(payloadType));
                continue;
            }
            if (payloadType.isView() && payloadType.viewScope.empty()) {
                reportError(
                    payload.span,
                    "Choice '" + choice->name + "' cannot store `" + payloadType.describe() +
                        "` directly in variant '" + variant.tag +
                        "'. Store an owned value, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
            }
            variantInfo.payloadTypes.push_back(std::move(payloadType));
        }

        info.variants[variant.tag] = std::move(variantInfo);
        info.variantOrder.push_back(variant.tag);
    }

    analysisResult.choicesByName[choice->name] = info;
    const std::string canonicalName = canonicalTypeName(choice->name);
    if (canonicalName != choice->name) {
        analysisResult.choicesByName[canonicalName] = info;
    }
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
        const bool opaqueInitializer = bind->value && valueType.isOpaqueExternal();
        ResolvedType finalType = bind->type ? declaredType : normalizeInferredLiteralType(valueType);

        if (!bind->type && !bind->value && !bind->isMutable) {
            reportError(bind, "Binding requires a type or an initializer: " + bind->name);
            finalType = makeUnknownType();
        }

        if (!bind->isMutable && !bind->value) {
            reportError(bind, "Immutable binding requires an initializer: " + bind->name);
        }

        if (opaqueInitializer) {
            reportError(
                bind->value.get(),
                opaqueExternalValueUseMessage(valueType, "binding initializer for '" + bind->name + "'"));
            if (!bind->type) {
                finalType = makeUnknownType();
            }
        }

        const bool bindingTypeMatches = finalType.isView()
            ? canPassArgumentType(bind->value.get(), valueType, finalType)
            : canAssignType(valueType, finalType);
        if (bind->type && bind->value && !opaqueInitializer && !bindingTypeMatches) {
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
        validateScopedViewType(finalType, bind, "bindings");
        if (bind->value && finalType.isView() && !finalType.viewScope.empty()) {
            validateScopedBorrowSource(bind->value.get(), finalType, bind, "binding initializer");
        }
        if (bind->value && valueType.isView() && !valueType.viewScope.empty() &&
            (!finalType.isView() || finalType.viewScope.empty())) {
            reportError(
                bind,
                "Scoped ref `" + valueType.describe() + "` cannot escape into binding '" + bind->name +
                    "'. Keep it inside `scope " + valueType.viewScope + "` or store an owned value instead.");
        }

        if (!finalType.scopeName.empty() && !isNamedScopeActive(finalType.scopeName)) {
            reportError(bind, "Type " + finalType.describe() + " is bound to scope `" + finalType.scopeName + "`, but that scope is not active here.");
        }
        if (!valueType.scopeName.empty() && !isNamedScopeActive(valueType.scopeName)) {
            reportError(bind, "Initializer value for '" + bind->name + "' carries scoped borrow `" + valueType.scopeName + "` outside its active scope.");
        }

        const std::optional<size_t> viewSourceParamIndex = ((finalType.isView() || finalType.name == "Span") && bind->value)
            ? resolveViewSourceParam(bind->value.get())
            : std::nullopt;

        analysisResult.bindingTypes[bind] = finalType;
        defineVariable(
            bind->name,
            finalType,
            bind->isMutable,
            "Duplicate variable declaration: " + bind->name,
            bind->span,
            viewSourceParamIndex,
            bind);
        return;
    }

    if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        const ResolvedType declaredType = tryStmt->type ? resolveTypeNode(tryStmt->type.get(), {}) : makeUnknownType();
        const ResolvedType outcomeType = analyzeExpr(tryStmt->expr.get());
        ResolvedType okType = makeUnknownType();
        ResolvedType failType = makeUnknownType();

        if (tryStmt->isMutable) {
            reportError(tryStmt, "`try` bindings currently require `val`. Mutable `var = try ...` is not supported yet.");
        }

        if (outcomeType.isOpaqueExternal()) {
            reportError(tryStmt->expr.get(), opaqueExternalValueUseMessage(outcomeType, "try operand"));
        } else if (!outcomeType.isUnknown()) {
            if (outcomeType.name != "Result" || outcomeType.params.size() != 2) {
                reportError(tryStmt->expr.get(), "`try` requires a Result value, got " + outcomeType.describe());
            } else {
                const auto* outcomeInfo = lookupChoice(outcomeType.name);
                if (!outcomeInfo || !isOutcomeLikeChoice(*outcomeInfo)) {
                    reportError(tryStmt->expr.get(), "`try` requires Result to define Ok(T) and Fail(E) single-payload variants.");
                }
                okType = outcomeType.params[0];
                failType = outcomeType.params[1];
            }
        }

        ResolvedType finalType = tryStmt->type ? declaredType : normalizeInferredLiteralType(okType);
        if (tryStmt->type && !okType.isUnknown()) {
            const bool matches = finalType.isView()
                ? canBorrowAsView(okType, finalType)
                : canAssignType(okType, finalType);
            if (!matches) {
                reportError(
                    tryStmt,
                    "`try` binding type mismatch for '" + tryStmt->name + "': expected " +
                        finalType.describe() + ", got " + okType.describe());
            }
        }

        if (tryStmt->autoPropagate) {
            if (!currentSignature || currentSignature->returnType.name != "Result" || currentSignature->returnType.params.size() != 2) {
                reportError(tryStmt, "`try` shorthand requires the current function to return Result[T, E].");
            } else if (!failType.isUnknown()) {
                const ResolvedType& currentFailType = currentSignature->returnType.params[1];
                if (!sameType(failType, currentFailType)) {
                    reportError(
                        tryStmt,
                        "`try` shorthand error type mismatch: expected " +
                            currentFailType.describe() + ", got " + failType.describe());
                }
            }
        } else {
            if (!tryStmt->failBlock || tryStmt->failName.empty()) {
                reportError(tryStmt, "`try ... else` requires a failure binding and block.");
            } else {
                if (!blockDefinitelyTerminates(tryStmt->failBlock.get())) {
                    reportError(tryStmt, "`try ... else` failure block must terminate the current control path.");
                }

                enterSemanticScope();
                defineVariable(
                    tryStmt->failName,
                    failType,
                    false,
                    "Duplicate try failure binding: " + tryStmt->failName,
                    tryStmt->span);
                analyzeBlock(tryStmt->failBlock.get());
                exitSemanticScope();
            }
        }

        if (rawDepth == 0 && isRawAddressType(finalType)) {
            reportError(tryStmt, "Raw address values may only appear inside raw blocks.");
        }
        if (!finalType.scopeName.empty() && !isNamedScopeActive(finalType.scopeName)) {
            reportError(tryStmt, "Type " + finalType.describe() + " is bound to scope `" + finalType.scopeName + "`, but that scope is not active here.");
        }
        validateScopedViewType(finalType, tryStmt, "`try` bindings");
        if (finalType.isView() && !finalType.viewScope.empty()) {
            validateScopedBorrowSource(tryStmt->expr.get(), finalType, tryStmt, "`try` binding");
        }

        analysisResult.tryBindingTypes[tryStmt] = finalType;
        defineVariable(
            tryStmt->name,
            finalType,
            false,
            "Duplicate variable declaration: " + tryStmt->name,
            tryStmt->span);
        return;
    }

    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        const ResolvedType targetType = assign->target ? analyzeExpr(assign->target.get()) : makeUnknownType();
        const ResolvedType valueType = assign->value ? analyzeExpr(assign->value.get(), &targetType) : makeUnknownType();

        if (rawDepth == 0 && (isRawAddressType(valueType) || isRawAddressType(targetType))) {
            reportError(assign, "Raw address values may only appear inside raw blocks.");
        }

        const bool opaqueAssignedValue = assign->value && valueType.isOpaqueExternal();

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

                if (opaqueAssignedValue) {
                    reportError(
                        assign->value.get(),
                        opaqueExternalValueUseMessage(valueType, "assignment to '" + ident->name + "'"));
                } else {
                    if (!valueType.scopeName.empty() && !symbolCanStoreNamedScope(*sym, valueType.scopeName)) {
                        reportError(
                            assign,
                            "Cannot store value bound to scope `" + valueType.scopeName +
                                "` in binding '" + ident->name +
                                "' declared outside that scope. Move the binding inside the scope or return an owned value.");
                    }

                    if (sym->type.isUnknown()) {
                        const ResolvedType inferredType = normalizeInferredLiteralType(valueType);
                        if (!inferredType.isUnknown()) {
                            sym->type = inferredType;
                            if (sym->bindingDecl) {
                                analysisResult.bindingTypes[sym->bindingDecl] = inferredType;
                            }
                            if (isNumericLiteralType(valueType)) {
                                analysisResult.exprTypes[assign->value.get()] = inferredType;
                            }
                            if (inferredType.isView()) {
                                sym->viewSourceParamIndex = resolveViewSourceParam(assign->value.get());
                            } else {
                                sym->viewSourceParamIndex.reset();
                            }
                        }
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
                    if (sym->type.isView() && !sym->type.viewScope.empty()) {
                        validateScopedBorrowSource(assign->value.get(), sym->type, assign, "assignment");
                    }
                    if (valueType.isView() && !valueType.viewScope.empty() &&
                        (!sym->type.isView() || sym->type.viewScope.empty())) {
                        reportError(
                            assign,
                            "Scoped ref `" + valueType.describe() + "` cannot escape into `" + ident->name +
                                "`. Keep it inside `scope " + valueType.viewScope + "` or store an owned value instead.");
                    }

                    if (sym->type.isView()) {
                        sym->viewSourceParamIndex = resolveViewSourceParam(assign->value.get());
                    }
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
                reportError(assign, "Member assignment requires mutable storage or a ref mut view.");
            }

            if (opaqueAssignedValue) {
                reportError(assign->value.get(), opaqueExternalValueUseMessage(valueType, "member assignment"));
            } else if (!canAssignType(valueType, targetType)) {
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
        if (valueType.isOpaqueExternal()) {
            reportError(give->value.get(), opaqueExternalValueUseMessage(valueType, "return value"));
        } else if (!valueType.viewScope.empty()) {
            reportError(
                give,
                "Scoped ref `" + valueType.describe() + "` cannot leave `scope " + valueType.viewScope +
                    "`. Return an owned value instead or keep the borrow inside that scope.");
        } else if (currentSignature && currentSignature->returnType.isView()) {
            if (!canBorrowAsView(valueType, currentSignature->returnType)) {
                reportError(
                    give,
                    "Return type mismatch in function '" + currentFunction->name + "': expected " +
                        currentSignature->returnType.describe() + ", got " + valueType.describe());
            }
            const auto sourceParamIndex = give->value ? resolveViewSourceParam(give->value.get()) : std::nullopt;
            if (!sourceParamIndex.has_value()) {
                reportError(give, "Returned ref value must come from one of the function's ref parameters. Return an owned value instead, use Anchor[T] for stable storage, or keep the borrow inside a scope block.");
            } else if (!currentViewReturnSeen) {
                currentViewReturnSourceParam = sourceParamIndex;
                currentViewReturnSeen = true;
            } else if (currentViewReturnSourceParam != sourceParamIndex) {
                reportError(give, "All returned ref values in a function must come from the same parameter.");
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
        if (!valueType.scopeName.empty()) {
            reportError(
                give,
                "Cannot return value `" + valueType.describe() + "` because it is bound to scope `" +
                    valueType.scopeName + "`. Return owned data or keep the borrowed aggregate inside the scope.");
        }
        return;
    }

    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            const ResolvedType exprType = analyzeExpr(exprStmt->expr.get());
            if (rawDepth == 0 && isRawAddressType(exprType)) {
                reportError(exprStmt, "Raw address values may only appear inside raw blocks.");
            }
            if (!isImplicitTailExpr(exprStmt) && typeIsMustUse(exprType)) {
                reportError(
                    exprStmt,
                    "Unused " + exprType.describe() + " value. Handle it with `try`, `pick`, or bind it to `_` explicitly if you intend to ignore it.");
            }
        }
        return;
    }

    if (auto* when = dynamic_cast<WhenStmt*>(stmt)) {
        const ResolvedType conditionType = analyzeExpr(when->condition.get());
        if (conditionType.isOpaqueExternal()) {
            reportError(when->condition.get(), opaqueExternalValueUseMessage(conditionType, "'when' condition"));
        } else if (!isConditionLike(conditionType)) {
            reportError(when->condition.get(), "'when' condition must be Bool, got " + conditionType.describe());
        }

        enterSemanticScope();
        analyzeBlock(when->thenBlock.get());
        exitSemanticScope();

        if (when->elseBlock) {
            enterSemanticScope();
            analyzeBlock(when->elseBlock.get());
            exitSemanticScope();
        }
        return;
    }

    if (auto* loop = dynamic_cast<LoopStmt*>(stmt)) {
        if (loop->condition) {
            const ResolvedType conditionType = analyzeExpr(loop->condition.get());
            if (conditionType.isOpaqueExternal()) {
                reportError(loop->condition.get(), opaqueExternalValueUseMessage(conditionType, "'loop' condition"));
            } else if (!isConditionLike(conditionType)) {
                reportError(loop->condition.get(), "'loop' condition must be Bool, got " + conditionType.describe());
            }
        }

        enterSemanticScope();
        ++loopDepth;
        analyzeBlock(loop->body.get());
        --loopDepth;
        exitSemanticScope();
        return;
    }

    if (auto* scan = dynamic_cast<ScanStmt*>(stmt)) {
        const ResolvedType iterableType = analyzeExpr(scan->iterable.get());
        if (iterableType.isOpaqueExternal()) {
            reportError(scan->iterable.get(), opaqueExternalValueUseMessage(iterableType, "scan iterable"));
        }
        ResolvedType itemType = makeUnknownType();
        if (iterableType.name == "Str") {
            itemType = makePlainType("UInt8");
        } else if (!iterableType.params.empty()) {
            itemType = iterableType.params.front();
        }
        analysisResult.scanItemTypes[scan] = itemType;

        enterSemanticScope();
        ++loopDepth;
        defineVariable(scan->itemName, itemType, false, "Duplicate scan variable: " + scan->itemName, scan->span);
        analyzeBlock(scan->body.get());
        --loopDepth;
        exitSemanticScope();
        return;
    }

    if (auto* scopeStmt = dynamic_cast<ScopeStmt*>(stmt)) {
        enterSemanticScope();
        const bool namedScopeActive = enterNamedBorrowScope(scopeStmt->name, scopeStmt->span);
        if (namedScopeActive) {
            activeNamedScopes.push_back(scopeStmt->name);
        }
        analyzeBlock(scopeStmt->body.get());
        if (namedScopeActive) {
            activeNamedScopes.pop_back();
            exitNamedBorrowScope();
        }
        exitSemanticScope();
        return;
    }

    if (auto* pick = dynamic_cast<PickStmt*>(stmt)) {
        const ResolvedType valueType = analyzeExpr(pick->value.get());
        if (valueType.isOpaqueExternal()) {
            reportError(pick->value.get(), opaqueExternalValueUseMessage(valueType, "pick value"));
        }
        const ChoiceInfo* choiceInfo = (valueType.isUnknown() || valueType.isOpaqueExternal()) ? nullptr : lookupChoice(valueType.name);
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

            enterSemanticScope();

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
            exitSemanticScope();
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

        if (outcomeType.isOpaqueExternal()) {
            reportError(lift->expr.get(), opaqueExternalValueUseMessage(outcomeType, "lift operand"));
        } else if (!outcomeType.isUnknown()) {
            if (outcomeType.name != "Result" || outcomeType.params.size() != 2) {
                reportError(lift->expr.get(), "'lift' requires a Result value, got " + outcomeType.describe());
            } else {
                const auto* outcomeInfo = lookupChoice(outcomeType.name);
                if (!outcomeInfo || !isOutcomeLikeChoice(*outcomeInfo)) {
                    reportError(lift->expr.get(), "'lift' requires Result to define Ok(T) and Fail(E) single-payload variants.");
                }
                okType = outcomeType.params[0];
                failType = outcomeType.params[1];
            }
        }

        if (!blockDefinitelyTerminates(lift->failBlock.get())) {
            reportError(lift, "'lift' failure block must terminate the current control path.");
        }

        enterSemanticScope();
        defineVariable(lift->failName, failType, false, "Duplicate lift failure binding: " + lift->failName, lift->span);
        analyzeBlock(lift->failBlock.get());
        exitSemanticScope();

        defineVariable(lift->valueName, okType, false, "Duplicate lift success binding: " + lift->valueName, lift->span);
        return;
    }

    if (auto* raw = dynamic_cast<RawStmt*>(stmt)) {
        enterSemanticScope();
        ++rawDepth;
        analyzeBlock(raw->body.get());
        --rawDepth;
        exitSemanticScope();
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
        type = makeOwnedType("Str");
    } else if (auto* shapeInit = dynamic_cast<ShapeInitExpr*>(expr)) {
        const ShapeInfo* shapeInfo = lookupShape(shapeInit->name);
        if (!shapeInfo) {
            reportError(shapeInit, "Unknown shape constructor: " + shapeInit->name);
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (!shapeInfo->typeParams.empty()) {
            reportError(shapeInit, "Shape literal construction for generic shape '" + shapeInit->name + "' is not supported yet.");
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (!shapeInfo->isViewShape && !shapeInit->scopeName.empty()) {
            reportError(shapeInit, "Shape '" + shapeInit->name + "' is not a view shape and cannot take a named scope.");
        }
        if (!shapeInit->scopeName.empty() && !isNamedScopeActive(shapeInit->scopeName)) {
            reportError(shapeInit, "Named scope `" + shapeInit->scopeName + "` is not active here.");
        }

        std::unordered_set<std::string> seenFields;
        std::vector<std::pair<const ShapeInitField*, ResolvedType>> fieldValueTypes;
        fieldValueTypes.reserve(shapeInit->fields.size());
        std::string actualScopeName = shapeInit->scopeName;

        for (const auto& field : shapeInit->fields) {
            if (!seenFields.insert(field.name).second) {
                reportError(field.span, "Duplicate field in shape literal '" + shapeInit->name + "': " + field.name);
                continue;
            }

            const auto fieldIt = shapeInfo->fields.find(field.name);
            if (fieldIt == shapeInfo->fields.end()) {
                reportError(field.span, "Unknown field '" + field.name + "' in shape literal '" + shapeInit->name + "'.");
                continue;
            }

            const ResolvedType valueType = analyzeExpr(field.value.get(), &fieldIt->second);
            fieldValueTypes.push_back({&field, valueType});

            if (shapeInfo->isViewShape && typeContainsBorrowedStorage(fieldIt->second)) {
                if (valueType.viewScope.empty()) {
                    reportError(
                        field.value.get(),
                        "Field '" + field.name + "' of view shape '" + shapeInit->name +
                            "' must be initialized from a named scoped borrow like ref[s] ...");
                } else if (actualScopeName.empty()) {
                    actualScopeName = valueType.viewScope;
                } else if (actualScopeName != valueType.viewScope) {
                    reportError(
                        field.value.get(),
                        "All borrowed fields of view shape '" + shapeInit->name +
                            "' must come from the same named scope. Saw `" + actualScopeName +
                            "` and `" + valueType.viewScope + "`.");
                }
            }
        }

        for (const auto& fieldName : shapeInfo->fieldOrder) {
            if (!seenFields.contains(fieldName)) {
                reportError(shapeInit, "Missing field '" + fieldName + "' in shape literal '" + shapeInit->name + "'.");
            }
        }

        if (shapeInfo->isViewShape && actualScopeName.empty()) {
            reportError(
                shapeInit,
                "View shape '" + shapeInit->name + "' requires borrowed fields tied to an active named scope. Use `scope s { ... }` and initialize with `ref[s] ...`.");
        }

        for (const auto& [field, valueType] : fieldValueTypes) {
            const auto fieldIt = shapeInfo->fields.find(field->name);
            if (fieldIt == shapeInfo->fields.end()) {
                continue;
            }
            ResolvedType expectedFieldType = fieldIt->second;
            if (shapeInfo->isViewShape) {
                expectedFieldType = bindFormalScopeName(expectedFieldType, shapeInfo->scopeParamName, actualScopeName);
            }
            const bool matches = expectedFieldType.isView()
                ? canPassArgumentType(field->value.get(), valueType, expectedFieldType)
                : canAssignType(valueType, expectedFieldType);
            if (!valueType.isOpaqueExternal() && !matches) {
                reportError(
                    field->value.get(),
                    "Shape literal field type mismatch for '" + field->name + "': expected " +
                        expectedFieldType.describe() + ", got " + valueType.describe());
            }
        }

        type = makeOwnedType(shapeInit->name);
        if (shapeInfo->isViewShape) {
            type.scopeName = actualScopeName;
        }
    } else if (auto* ident = dynamic_cast<IdentExpr*>(expr)) {
        if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal()) {
            if (const auto* choiceInfo = lookupChoice(expectedType->name)) {
                if (const auto variantInfo = resolveChoiceVariantInfo(*choiceInfo, *expectedType, ident->name)) {
                    if (!variantInfo->payloadTypes.empty()) {
                        reportError(
                            ident,
                            "Constructor '" + ident->name + "' requires " +
                                std::to_string(variantInfo->payloadTypes.size()) + " argument(s). Use call syntax.");
                    } else {
                        ChoiceConstructorInfo constructorInfo;
                        constructorInfo.resultType = *expectedType;
                        constructorInfo.variantName = findChoiceVariantName(*choiceInfo, ident->name).value_or(ident->name);
                        analysisResult.choiceConstructors[ident] = std::move(constructorInfo);
                        type = *expectedType;
                        analysisResult.exprTypes[expr] = type;
                        return type;
                    }
                }
            }
        }

        const auto sym = lookupSymbol(ident->name);
        if (!sym) {
            reportError(ident, "Undefined variable: " + ident->name);
        } else if (sym->kind == SymbolKind::Variable) {
            type = sym->type;
        } else {
            type = makeUnknownType(ident->name);
        }
    } else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        const ResolvedType objectType = analyzeExpr(index->object.get());
        const ResolvedType expectedIndexType = makePlainType("USize");
        const ResolvedType indexType = analyzeExpr(index->index.get(), &expectedIndexType);

        if (objectType.isOpaqueExternal()) {
            reportError(index->object.get(), opaqueExternalValueUseMessage(objectType, "index receiver"));
        }
        if (indexType.isOpaqueExternal()) {
            reportError(index->index.get(), opaqueExternalValueUseMessage(indexType, "index value"));
        }
        if (!indexType.isUnknown() && !canAssignType(indexType, expectedIndexType)) {
            reportError(index->index.get(), "Index expression must have type USize, got " + indexType.describe());
        }

        if (!objectType.isUnknown() && !objectType.isOpaqueExternal()) {
            if (objectType.name == "Str") {
                type = makePlainType("UInt8");
            } else if ((objectType.name == "Span" || objectType.name == "Vec" || objectType.name == "Array") &&
                       !objectType.params.empty()) {
                type = objectType.params.front();
            } else {
                reportError(index, "Type '" + objectType.describe() + "' does not support index access.");
            }
        }
    } else if (auto* borrow = dynamic_cast<BorrowExpr*>(expr)) {
        const ResolvedType targetType = analyzeExpr(borrow->target.get());
        const bool addressable =
            dynamic_cast<IdentExpr*>(borrow->target.get()) ||
            dynamic_cast<MemberExpr*>(borrow->target.get()) ||
            dynamic_cast<IndexExpr*>(borrow->target.get());

        if (!addressable) {
            reportError(borrow, "Borrow target must be an addressable expression.");
        }
        if (targetType.isOpaqueExternal()) {
            reportError(borrow->target.get(), opaqueExternalValueUseMessage(targetType, "borrow target"));
        }
        if (auto* index = dynamic_cast<IndexExpr*>(borrow->target.get())) {
            if (const auto* objectType = lookupExprType(index->object.get())) {
                if (objectType->name == "Vec") {
                    reportError(borrow, "Direct element borrows from Vec are forbidden. Borrow through `.span()` first.");
                }
            }
        }
        if (borrow->isMutable && !canBorrowExprAsEdit(borrow->target.get())) {
            reportError(borrow, "Mutable borrow requires mutable storage or ref mut access.");
        }
        if (!borrow->scopeName.empty() && !isNamedScopeActive(borrow->scopeName)) {
            reportError(borrow, "Named scope `" + borrow->scopeName + "` is not active here.");
        }
        if (!borrow->scopeName.empty()) {
            ResolvedType scopedType = targetType;
            scopedType.viewKind = "look";
            scopedType.viewScope = borrow->scopeName;
            scopedType.category = TypeCategory::View;
            validateScopedViewType(scopedType, borrow, "borrow expressions");
            validateScopedBorrowSource(borrow->target.get(), scopedType, borrow, "borrow expression");
        }

        type = targetType;
        type.viewKind = borrow->isMutable ? "edit" : "look";
        type.viewScope = borrow->scopeName;
        type.category = TypeCategory::View;
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

        if (leftType.isOpaqueExternal()) {
            reportError(binary->left.get(), opaqueExternalValueUseMessage(leftType, isComparison ? "comparison operand" : "arithmetic operand"));
        }
        if (rightType.isOpaqueExternal()) {
            reportError(binary->right.get(), opaqueExternalValueUseMessage(rightType, isComparison ? "comparison operand" : "arithmetic operand"));
        }

        if (leftType.isOpaqueExternal() || rightType.isOpaqueExternal()) {
            type = makeUnknownType();
        } else if (isComparison) {
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
        bool opaqueExternalCall = false;
        bool skipCalleeAnalysis = false;
        std::string opaqueExternalCallee;

        if (isAnchorStaticConstructorCall(call->callee.get())) {
            const ResolvedType* expectedPayloadType = nullptr;
            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal() &&
                expectedType->name == "Anchor" && expectedType->params.size() == 1) {
                expectedPayloadType = &expectedType->params.front();
            }

            if (call->args.size() != 1) {
                reportError(call, "Anchor.new(...) expects exactly 1 argument.");
            }

            ResolvedType payloadType = makeUnknownType();
            if (!call->args.empty()) {
                payloadType = analyzeExpr(call->args.front().get(), expectedPayloadType);
                if (payloadType.isOpaqueExternal()) {
                    reportError(call->args.front().get(), opaqueExternalValueUseMessage(payloadType, "Anchor payload"));
                }
                if (rawDepth == 0 && isRawAddressType(payloadType)) {
                    reportError(call->args.front().get(), "Raw address values may only appear inside raw blocks.");
                }
                if (!payloadType.isUnknown() && !payloadType.isOpaqueExternal() && !isAnchorPayloadTypeAllowed(payloadType)) {
                    reportError(call->args.front().get(), anchorPayloadTypeError(payloadType));
                }
                if (expectedPayloadType && !payloadType.isOpaqueExternal() &&
                    !canPassArgumentType(call->args.front().get(), payloadType, *expectedPayloadType)) {
                    reportError(
                        call->args.front().get(),
                        "Anchor payload type mismatch: expected " + expectedPayloadType->describe() +
                            ", got " + payloadType.describe());
                }
            }

            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal() &&
                expectedType->name == "Anchor" && expectedType->params.size() == 1) {
                type = *expectedType;
            } else {
                type = makeOwnedType("Anchor");
                type.params.push_back(normalizeInferredLiteralType(payloadType));
            }
            analysisResult.exprTypes[expr] = type;
            return type;
        }

        if (auto* calleeIdent = dynamic_cast<IdentExpr*>(call->callee.get())) {
            if (expectedType && !expectedType->isUnknown() && !expectedType->isOpaqueExternal()) {
                if (const auto* choiceInfo = lookupChoice(expectedType->name)) {
                    if (const auto variantInfo = resolveChoiceVariantInfo(*choiceInfo, *expectedType, calleeIdent->name)) {
                        if (call->args.size() != variantInfo->payloadTypes.size()) {
                            reportError(
                                call,
                                "Constructor '" + calleeIdent->name + "' expects " +
                                    std::to_string(variantInfo->payloadTypes.size()) + " argument(s), got " +
                                    std::to_string(call->args.size()));
                        }

                        for (size_t i = 0; i < call->args.size(); ++i) {
                            const ResolvedType* payloadType = i < variantInfo->payloadTypes.size()
                                ? &variantInfo->payloadTypes[i]
                                : nullptr;
                            const ResolvedType argType = analyzeExpr(call->args[i].get(), payloadType);
                            if (argType.isOpaqueExternal()) {
                                reportError(call->args[i].get(), opaqueExternalValueUseMessage(argType, "constructor argument"));
                                continue;
                            }
                            if (payloadType && !canPassArgumentType(call->args[i].get(), argType, *payloadType)) {
                                reportError(
                                    call->args[i].get(),
                                    "Constructor argument type mismatch: expected " + payloadType->describe() +
                                        ", got " + argType.describe());
                            }
                        }

                        ChoiceConstructorInfo constructorInfo;
                        constructorInfo.resultType = *expectedType;
                        constructorInfo.variantName = findChoiceVariantName(*choiceInfo, calleeIdent->name).value_or(calleeIdent->name);
                        constructorInfo.payloadTypes = variantInfo->payloadTypes;
                        analysisResult.choiceConstructors[call] = std::move(constructorInfo);
                        type = *expectedType;
                        analysisResult.exprTypes[expr] = type;
                        return type;
                    }
                }
            }

            signature = lookupFunctionSignature(calleeIdent->name);
            if (!signature) {
                const auto sym = lookupSymbol(calleeIdent->name);
                if (sym && sym->kind == SymbolKind::Function && sym->isExternal) {
                    externalCall = true;
                    opaqueExternalCall = true;
                    opaqueExternalCallee = calleeIdent->name;
                }
            }
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
                                skipCalleeAnalysis = true;
                            } else {
                                externalCall = true;
                                opaqueExternalCall = true;
                                opaqueExternalCallee = describeCalleeExpr(call->callee.get());
                            }
                        } else {
                            reportError(call, "Module '" + objectIdent->name + "' does not expose callable member '" + member->member + "'.");
                            skipCalleeAnalysis = true;
                        }
                    } else {
                        externalCall = true;
                        opaqueExternalCall = true;
                        opaqueExternalCallee = describeCalleeExpr(call->callee.get());
                    }
                }
            }

            if (!signature && !externalCall && !skipCalleeAnalysis) {
                if (auto* objectIdent = dynamic_cast<IdentExpr*>(member->object.get())) {
                    if (objectIdent->name == "Anchor" && member->member == "new") {
                        if (call->args.size() != 1) {
                            reportError(call, "Constructor 'Anchor.new' expects exactly 1 argument.");
                        } else {
                            const ResolvedType* payloadExpectedType =
                                (expectedType && expectedType->name == "Anchor" && expectedType->params.size() == 1)
                                    ? &expectedType->params.front()
                                    : nullptr;
                            ResolvedType payloadType = analyzeExpr(call->args[0].get(), payloadExpectedType);
                            payloadType = normalizeInferredLiteralType(payloadType);
                            if (isNumericLiteralType(payloadType)) {
                                analysisResult.exprTypes[call->args[0].get()] = payloadType;
                            }

                            ResolvedType anchorType = makeOwnedType("Anchor");
                            if (payloadExpectedType) {
                                anchorType.params.push_back(*payloadExpectedType);
                                if (!canAssignType(payloadType, *payloadExpectedType)) {
                                    reportError(
                                        call->args[0].get(),
                                        "Anchor.new payload type mismatch: expected " + payloadExpectedType->describe() +
                                            ", got " + payloadType.describe());
                                }
                            } else {
                                anchorType.params.push_back(payloadType);
                            }

                            if (!validateAnchorPayloadType(anchorType, call, "Anchor.new payloads")) {
                                type = makeUnknownType();
                            } else {
                                type = anchorType;
                            }
                            analysisResult.exprTypes[expr] = type;
                            return type;
                        }
                    }
                }

                const ResolvedType receiverType = analyzeExpr(member->object.get());
                if (receiverType.isOpaqueExternal()) {
                    reportError(member->object.get(), opaqueExternalValueUseMessage(receiverType, "method receiver"));
                    skipCalleeAnalysis = true;
                } else {
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
        }

        if (!methodSignature.has_value() && !skipCalleeAnalysis && !opaqueExternalCall) {
            analyzeExpr(call->callee.get());
        }

        if (opaqueExternalCall && rawDepth == 0) {
            reportError(call->callee.get(), opaqueExternalRawRequirementMessage(call->callee.get()));
        }
        if (signature && signature->externalInfo.has_value() && signature->externalInfo->rawOnly && rawDepth == 0) {
            reportError(call->callee.get(), typedExternalRawRequirementMessage(call->callee.get()));
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
                if (argType.isOpaqueExternal()) {
                    reportError(call->args[i].get(), opaqueExternalValueUseMessage(argType, "call argument"));
                    continue;
                }
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
            if (type.isView() && signature->viewReturnSourceParam.has_value()) {
                const size_t sourceIndex = *signature->viewReturnSourceParam;
                if (sourceIndex < call->args.size()) {
                    if (const auto* sourceType = lookupExprType(call->args[sourceIndex].get())) {
                        type.viewScope = sourceType->viewScope;
                    }
                }
            }
            if (type.isView() && methodSignature.has_value()) {
                if (methodSignature->viewReturnFromReceiver) {
                    if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                        if (const auto* receiverType = lookupExprType(member->object.get())) {
                            type.viewScope = receiverType->viewScope;
                        }
                    }
                } else if (methodSignature->viewReturnSourceArg.has_value()) {
                    const size_t sourceIndex = *methodSignature->viewReturnSourceArg;
                    if (sourceIndex < call->args.size()) {
                        if (const auto* sourceType = lookupExprType(call->args[sourceIndex].get())) {
                            type.viewScope = sourceType->viewScope;
                        }
                    }
                }
            }
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
                const ResolvedType argType = analyzeExpr(arg.get());
                if (argType.isOpaqueExternal()) {
                    reportError(arg.get(), opaqueExternalValueUseMessage(argType, "call argument"));
                }
            }

            if (opaqueExternalCall) {
                type = opaqueExternalCallee.empty()
                    ? makeOpaqueExternalCallType(call->callee.get())
                    : makeOpaqueExternalType(opaqueExternalCallee);
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
            if (objectType.isOpaqueExternal()) {
                reportError(member->object.get(), opaqueExternalValueUseMessage(objectType, "member access"));
            } else if (lookupMethodSignature(objectType, member->member).has_value()) {
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
                        ResolvedType fieldType = instantiateGenericType(
                            fieldIt->second,
                            shape->typeParams,
                            objectType.params,
                            "shape '" + objectType.name + "'");
                        if (shape->isViewShape) {
                            fieldType = bindFormalScopeName(fieldType, shape->scopeParamName, objectType.scopeName);
                        }
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

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        return resolveViewSourceParam(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return resolveViewSourceParam(borrow->target.get());
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

    if (from.isOpaqueExternal() || to.isOpaqueExternal()) {
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
    if (!to.scopeName.empty() && !from.scopeName.empty() && from.scopeName != to.scopeName) {
        return false;
    }

    for (size_t i = 0; i < from.params.size(); ++i) {
        if (!sameType(from.params[i], to.params[i])) {
            return false;
        }
    }

    if (!to.viewScope.empty()) {
        if (from.isView() && !from.viewScope.empty() && from.viewScope != to.viewScope) {
            return false;
        }
    } else if (from.isView() && !from.viewScope.empty()) {
        return to.viewKind == "look";
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
    if (from.isOpaqueExternal() || to.isOpaqueExternal()) {
        return false;
    }

    if (canAssignType(from, to)) {
        return true;
    }

    if (from.isView() && !from.viewScope.empty() && to.isView() && to.viewScope.empty() && to.viewKind == "look") {
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
            if (sym && sym->kind == SymbolKind::Variable && sym->isMutable && sym->type.isOwned()) {
                return true;
            }
        }

        return canBorrowExprAsEdit(member->object.get());
    }

    if (auto* index = dynamic_cast<const IndexExpr*>(expr)) {
        if (const auto* objectType = lookupExprType(index->object.get())) {
            if (objectType->name == "Vec") {
                return false;
            }
            if (objectType->isView()) {
                return objectType->viewKind == "edit";
            }
        }

        if (auto* objectIdent = dynamic_cast<const IdentExpr*>(index->object.get())) {
            const auto sym = lookupSymbol(objectIdent->name);
            if (sym && sym->kind == SymbolKind::Variable && sym->isMutable && sym->type.name == "Array") {
                return true;
            }
        }

        return canBorrowExprAsEdit(index->object.get());
    }

    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        return borrow->isMutable && canBorrowExprAsEdit(borrow->target.get());
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
    std::optional<size_t> viewSourceParamIndex,
    const BindingStmt* bindingDecl) {
    auto sym = std::make_shared<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::Variable;
    sym->isMutable = isMutable;
    sym->type = type;
    sym->bindingDecl = bindingDecl;
    sym->viewSourceParamIndex = viewSourceParamIndex;
    sym->declaredNamedScopes = activeNamedScopes;
    sym->lexicalDepth = lexicalScopeDepth;

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

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        return blockDefinitelyTerminates(scopeStmt->body.get());
    }

    return false;
}

} // namespace claw::frontend









