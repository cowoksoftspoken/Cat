#include "analysis/types.h"

#include "ast/ast.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace claw::frontend {

namespace {

bool contains(const std::unordered_set<std::string>& set, const std::string& value) {
    return set.find(value) != set.end();
}

size_t pointerSizeBytes(const TargetSpec& target) {
    const unsigned widthBits = target.pointerWidthBits == 0 ? 64u : std::min(target.pointerWidthBits, 128u);
    return std::max<size_t>(1, widthBits / 8);
}

size_t roundUp(size_t value, size_t align) {
    if (align <= 1) {
        return value;
    }
    const size_t remainder = value % align;
    return remainder == 0 ? value : value + (align - remainder);
}

bool hasGenericPlaceholder(const ResolvedType& type) {
    if (type.isGeneric) {
        return true;
    }
    for (const auto& param : type.params) {
        if (hasGenericPlaceholder(param)) {
            return true;
        }
    }
    return false;
}

std::string canonicalTypeKey(const ResolvedType& type) {
    std::ostringstream out;
    if (!type.viewKind.empty()) {
        out << type.viewKind << ':';
    }
    out << type.name;
    if (!type.params.empty()) {
        out << '<';
        for (size_t i = 0; i < type.params.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            out << canonicalTypeKey(type.params[i]);
        }
        out << '>';
    }
    return out.str();
}

std::optional<TypeLayoutInfo> scalarLayout(const std::string& name, size_t size, size_t align) {
    TypeLayoutInfo layout;
    layout.typeName = name;
    layout.repr = "claw-core";
    layout.abi = "claw";
    layout.kind = TypeLayoutKind::Scalar;
    layout.passKind = size == 0 ? AbiPassKind::Void : AbiPassKind::Scalar;
    layout.ffiStable = true;
    layout.sizeBytes = size;
    layout.alignBytes = std::max<size_t>(1, align);
    return layout;
}

std::optional<TypeLayoutInfo> pointerLayout(
    const std::string& name,
    const TargetSpec& target,
    bool ffiStable,
    TypeLayoutKind kind = TypeLayoutKind::Pointer,
    AbiPassKind passKind = AbiPassKind::Direct,
    std::string repr = "claw-core") {
    TypeLayoutInfo layout;
    layout.typeName = name;
    layout.repr = std::move(repr);
    layout.abi = "claw";
    layout.kind = kind;
    layout.passKind = passKind;
    layout.ffiStable = ffiStable;
    layout.sizeBytes = pointerSizeBytes(target);
    layout.alignBytes = pointerSizeBytes(target);
    return layout;
}

struct AggregateLayoutBuilder {
    TypeLayoutInfo layout;

    void pushField(const std::string& name, const ResolvedType& type, const TypeLayoutInfo& fieldLayout) {
        const size_t offset = roundUp(layout.sizeBytes, fieldLayout.alignBytes);
        layout.fields.push_back(LayoutFieldInfo{name, type, offset, fieldLayout.sizeBytes, fieldLayout.alignBytes});
        layout.sizeBytes = offset + fieldLayout.sizeBytes;
        layout.alignBytes = std::max(layout.alignBytes, fieldLayout.alignBytes);
    }

    void finalize() {
        layout.sizeBytes = roundUp(layout.sizeBytes, layout.alignBytes);
    }
};

class LayoutComputer {
public:
    LayoutComputer(const AnalysisResult& analysis, const TargetSpec& target)
        : analysis(analysis), target(target) {}

    std::optional<TypeLayoutInfo> compute(const ResolvedType& type) {
        if (type.isUnknown() || type.isOpaqueExternal()) {
            return std::nullopt;
        }

        if (type.isView()) {
            TypeLayoutInfo layout;
            layout.typeName = type.describe();
            layout.repr = "claw-view";
            layout.abi = "claw";
            layout.kind = TypeLayoutKind::View;
            layout.passKind = AbiPassKind::Borrow;
            layout.ffiStable = false;
            layout.sizeBytes = pointerSizeBytes(target);
            layout.alignBytes = pointerSizeBytes(target);
            return layout;
        }

        if (auto layout = computeBuiltin(type)) {
            return layout;
        }

        if (analysis.shapesByName.find(type.name) != analysis.shapesByName.end()) {
            return computeShape(type);
        }
        if (analysis.choicesByName.find(type.name) != analysis.choicesByName.end()) {
            return computeChoice(type);
        }

        return std::nullopt;
    }

private:
    const AnalysisResult& analysis;
    const TargetSpec& target;
    std::vector<std::string> stack;

    AbiPassKind classifyAggregatePass(const TypeLayoutInfo& layout) const {
        if (layout.isTemplate) {
            return AbiPassKind::Indirect;
        }
        if (layout.kind == TypeLayoutKind::View) {
            return AbiPassKind::Borrow;
        }
        if (layout.kind == TypeLayoutKind::Scalar) {
            return layout.sizeBytes == 0 ? AbiPassKind::Void : AbiPassKind::Scalar;
        }
        if (layout.kind == TypeLayoutKind::Pointer || layout.kind == TypeLayoutKind::OpaqueHandle) {
            return AbiPassKind::Direct;
        }
        const size_t directThreshold = pointerSizeBytes(target) * 2;
        return layout.sizeBytes <= directThreshold && layout.alignBytes <= pointerSizeBytes(target)
            ? AbiPassKind::Direct
            : AbiPassKind::Indirect;
    }

    std::optional<TypeLayoutInfo> computeBuiltin(const ResolvedType& type) {
        if (type.name == "Unit") return scalarLayout(type.describe(), 0, 1);
        if (type.name == "Bool") return scalarLayout(type.describe(), 1, 1);
        if (type.name == "Byte") return scalarLayout(type.describe(), 1, 1);
        if (type.name == "Int8" || type.name == "UInt8" || type.name == "Bits8") return scalarLayout(type.describe(), 1, 1);
        if (type.name == "Int16" || type.name == "UInt16" || type.name == "Bits16") return scalarLayout(type.describe(), 2, 2);
        if (type.name == "Rune" || type.name == "Int32" || type.name == "UInt32" || type.name == "Bits32" || type.name == "Float32") return scalarLayout(type.describe(), 4, 4);
        if (type.name == "Int64" || type.name == "UInt64" || type.name == "Bits64" || type.name == "Float64" || type.name == "USize" || type.name == "ISize") {
            return scalarLayout(type.describe(), 8, 8);
        }
        if (type.name == "Int128" || type.name == "UInt128" || type.name == "Bits128") return scalarLayout(type.describe(), 16, 16);

        if (type.name == "Addr" || type.name == "RawPtr" || type.name == "RawMut") {
            return pointerLayout(type.describe(), target, true, TypeLayoutKind::Pointer, AbiPassKind::Direct);
        }
        if (type.name == "Fn") {
            return pointerLayout(type.describe(), target, true, TypeLayoutKind::Pointer, AbiPassKind::Direct, "claw-function");
        }
        if (type.name == "CStr") {
            return pointerLayout(type.describe(), target, true, TypeLayoutKind::Pointer, AbiPassKind::Direct, "claw-cstr");
        }
        if (type.name == "OwnedCStr" || type.name == "Arena" || type.name == "Pool" || type.name == "Anchor" ||
            type.name == "Table" || type.name == "Set" || type.name == "Heap" || type.name == "Ring") {
            return pointerLayout(type.describe(), target, false, TypeLayoutKind::OpaqueHandle, AbiPassKind::Direct, "claw-handle");
        }
        if (type.name == "Text" || type.name == "Span") {
            TypeLayoutInfo layout;
            layout.typeName = type.describe();
            layout.repr = "claw-slice";
            layout.abi = "claw";
            layout.kind = TypeLayoutKind::Slice;
            layout.ffiStable = false;
            layout.alignBytes = pointerSizeBytes(target);
            layout.sizeBytes = pointerSizeBytes(target) * 2;
            layout.fields.push_back(LayoutFieldInfo{"data", makeUnknownType("RawPtr"), 0, pointerSizeBytes(target), pointerSizeBytes(target)});
            layout.fields.push_back(LayoutFieldInfo{"len", makeUnknownType("USize"), pointerSizeBytes(target), pointerSizeBytes(target), pointerSizeBytes(target)});
            layout.passKind = classifyAggregatePass(layout);
            return layout;
        }
        if (type.name == "Bytes" || type.name == "Vec") {
            TypeLayoutInfo layout;
            layout.typeName = type.describe();
            layout.repr = "claw-buffer";
            layout.abi = "claw";
            layout.kind = TypeLayoutKind::Buffer;
            layout.ffiStable = false;
            layout.alignBytes = pointerSizeBytes(target);
            layout.sizeBytes = pointerSizeBytes(target) * 3;
            layout.fields.push_back(LayoutFieldInfo{"data", makeUnknownType("RawPtr"), 0, pointerSizeBytes(target), pointerSizeBytes(target)});
            layout.fields.push_back(LayoutFieldInfo{"len", makeUnknownType("USize"), pointerSizeBytes(target), pointerSizeBytes(target), pointerSizeBytes(target)});
            layout.fields.push_back(LayoutFieldInfo{"cap", makeUnknownType("USize"), pointerSizeBytes(target) * 2, pointerSizeBytes(target), pointerSizeBytes(target)});
            layout.passKind = classifyAggregatePass(layout);
            return layout;
        }

        return std::nullopt;
    }

    std::optional<TypeLayoutInfo> computeShape(const ResolvedType& type) {
        const auto it = analysis.shapesByName.find(type.name);
        if (it == analysis.shapesByName.end()) {
            return std::nullopt;
        }
        const auto& shape = it->second;

        TypeLayoutInfo layout;
        layout.typeName = type.describe();
        layout.repr = "claw-internal";
        layout.abi = "claw";
        layout.kind = TypeLayoutKind::Aggregate;
        layout.ffiStable = false;
        layout.alignBytes = 1;

        if (!shape.typeParams.empty() && shape.typeParams.size() != type.params.size()) {
            layout.kind = TypeLayoutKind::Template;
            layout.isTemplate = true;
            layout.passKind = AbiPassKind::Indirect;
            return layout;
        }

        const std::string key = canonicalTypeKey(type);
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
            return std::nullopt;
        }

        const auto bindings = buildTypeBindings(shape.typeParams, type.params);
        stack.push_back(key);

        AggregateLayoutBuilder builder;
        builder.layout = layout;
        for (const auto& fieldName : shape.fieldOrder) {
            const auto fieldIt = shape.fields.find(fieldName);
            if (fieldIt == shape.fields.end()) {
                continue;
            }
            const ResolvedType fieldType = substituteType(fieldIt->second, bindings);
            if (hasGenericPlaceholder(fieldType)) {
                builder.layout.kind = TypeLayoutKind::Template;
                builder.layout.isTemplate = true;
                builder.layout.passKind = AbiPassKind::Indirect;
                builder.layout.fields.push_back(LayoutFieldInfo{fieldName, fieldType, 0, 0, 1});
                continue;
            }
            const auto fieldLayout = compute(fieldType);
            if (!fieldLayout.has_value()) {
                stack.pop_back();
                return std::nullopt;
            }
            builder.pushField(fieldName, fieldType, *fieldLayout);
        }
        stack.pop_back();

        if (!builder.layout.isTemplate) {
            builder.finalize();
            builder.layout.passKind = classifyAggregatePass(builder.layout);
        }
        return builder.layout;
    }

    std::optional<TypeLayoutInfo> computeChoice(const ResolvedType& type) {
        const auto it = analysis.choicesByName.find(type.name);
        if (it == analysis.choicesByName.end()) {
            return std::nullopt;
        }
        const auto& choice = it->second;

        TypeLayoutInfo layout;
        layout.typeName = type.describe();
        layout.repr = "claw-internal";
        layout.abi = "claw";
        layout.kind = TypeLayoutKind::Tagged;
        layout.ffiStable = false;
        layout.tagSizeBytes = 4;
        layout.alignBytes = 4;

        if (!choice.typeParams.empty() && choice.typeParams.size() != type.params.size()) {
            layout.kind = TypeLayoutKind::Template;
            layout.isTemplate = true;
            layout.passKind = AbiPassKind::Indirect;
            return layout;
        }

        const std::string key = canonicalTypeKey(type);
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) {
            return std::nullopt;
        }

        const auto bindings = buildTypeBindings(choice.typeParams, type.params);
        stack.push_back(key);

        size_t maxPayloadSize = 0;
        size_t maxPayloadAlign = 1;
        for (const auto& variantName : choice.variantOrder) {
            const auto variantIt = choice.variants.find(variantName);
            if (variantIt == choice.variants.end()) {
                continue;
            }

            LayoutVariantInfo variant;
            variant.name = variantName;
            size_t payloadSize = 0;
            size_t payloadAlign = 1;
            for (size_t i = 0; i < variantIt->second.payloadTypes.size(); ++i) {
                const ResolvedType payloadType = substituteType(variantIt->second.payloadTypes[i], bindings);
                if (hasGenericPlaceholder(payloadType)) {
                    layout.kind = TypeLayoutKind::Template;
                    layout.isTemplate = true;
                    variant.payloadFields.push_back(LayoutFieldInfo{"payload" + std::to_string(i), payloadType, 0, 0, 1});
                    continue;
                }
                const auto payloadLayout = compute(payloadType);
                if (!payloadLayout.has_value()) {
                    stack.pop_back();
                    return std::nullopt;
                }
                const size_t offset = roundUp(payloadSize, payloadLayout->alignBytes);
                variant.payloadFields.push_back(LayoutFieldInfo{
                    "payload" + std::to_string(i),
                    payloadType,
                    offset,
                    payloadLayout->sizeBytes,
                    payloadLayout->alignBytes});
                payloadSize = offset + payloadLayout->sizeBytes;
                payloadAlign = std::max(payloadAlign, payloadLayout->alignBytes);
            }
            if (!layout.isTemplate) {
                payloadSize = roundUp(payloadSize, payloadAlign);
                variant.payloadSizeBytes = payloadSize;
                variant.payloadAlignBytes = payloadAlign;
                maxPayloadSize = std::max(maxPayloadSize, payloadSize);
                maxPayloadAlign = std::max(maxPayloadAlign, payloadAlign);
            }
            layout.variants.push_back(std::move(variant));
        }
        stack.pop_back();

        if (layout.isTemplate) {
            layout.passKind = AbiPassKind::Indirect;
            return layout;
        }

        layout.alignBytes = std::max(layout.alignBytes, maxPayloadAlign);
        layout.payloadOffsetBytes = roundUp(layout.tagSizeBytes, maxPayloadAlign);
        layout.payloadSizeBytes = maxPayloadSize;
        layout.sizeBytes = roundUp(layout.payloadOffsetBytes + layout.payloadSizeBytes, layout.alignBytes);
        for (auto& variant : layout.variants) {
            variant.payloadOffsetBytes = layout.payloadOffsetBytes;
        }
        layout.passKind = classifyAggregatePass(layout);
        return layout;
    }
};

} // namespace

bool ResolvedType::isUnknown() const {
    return category == TypeCategory::Unknown;
}

bool ResolvedType::isOpaqueExternal() const {
    return category == TypeCategory::OpaqueExternal;
}

bool ResolvedType::isPlain() const {
    return category == TypeCategory::Plain;
}

bool ResolvedType::isOwned() const {
    return category == TypeCategory::Owned;
}

bool ResolvedType::isView() const {
    return category == TypeCategory::View;
}

std::string ResolvedType::describe() const {
    std::ostringstream out;
    if (!viewKind.empty()) {
        out << viewKind << ' ';
    }
    if (isOpaqueExternal()) {
        out << "opaque external result";
        if (!name.empty()) {
            out << " from " << name;
        }
    } else if (name.empty()) {
        out << "<unknown>";
    } else if (name == "IntLiteral") {
        out << "integer literal";
    } else if (name == "FloatLiteral") {
        out << "float literal";
    } else {
        out << name;
    }
    if (!params.empty()) {
        out << " of ";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << params[i].describe();
        }
    }
    return out.str();
}

ResolvedType makeUnknownType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::Unknown;
    return type;
}

ResolvedType makeOpaqueExternalType(const std::string& name) {
    ResolvedType type;
    type.name = name;
    type.category = TypeCategory::OpaqueExternal;
    return type;
}

TargetSpec defaultTargetSpec() {
    return TargetSpec{};
}

bool sameType(const ResolvedType& left, const ResolvedType& right) {
    if (left.name != right.name || left.viewKind != right.viewKind || left.params.size() != right.params.size()) {
        return false;
    }

    for (size_t i = 0; i < left.params.size(); ++i) {
        if (!sameType(left.params[i], right.params[i])) {
            return false;
        }
    }

    return true;
}

bool isNumericTypeName(const std::string& name) {
    static const std::unordered_set<std::string> numericTypes = {
        "Byte", "Rune", "Int8", "Int16", "Int32", "Int64", "Int128",
        "UInt8", "UInt16", "UInt32", "UInt64", "UInt128",
        "Bits8", "Bits16", "Bits32", "Bits64", "Bits128",
        "Float32", "Float64", "USize", "ISize"};
    return contains(numericTypes, name);
}

bool isIntegerLikeTypeName(const std::string& name) {
    static const std::unordered_set<std::string> integerLikeTypes = {
        "Byte", "Rune", "Int8", "Int16", "Int32", "Int64", "Int128",
        "UInt8", "UInt16", "UInt32", "UInt64", "UInt128",
        "Bits8", "Bits16", "Bits32", "Bits64", "Bits128",
        "USize", "ISize"};
    return contains(integerLikeTypes, name);
}

bool isIntegerLiteralType(const ResolvedType& type) {
    return type.category == TypeCategory::Plain && type.viewKind.empty() && type.name == "IntLiteral";
}

bool canAssignType(const ResolvedType& from, const ResolvedType& to) {
    if (from.isOpaqueExternal() || to.isOpaqueExternal()) {
        return false;
    }

    if (from.isUnknown() || to.isUnknown()) {
        return true;
    }

    if (sameType(from, to)) {
        return true;
    }

    if (isIntegerLiteralType(from) && to.isPlain() && isIntegerLikeTypeName(to.name)) {
        return true;
    }

    if (from.viewKind == "edit" && to.viewKind == "look") {
        ResolvedType weakened = from;
        weakened.viewKind = "look";
        return sameType(weakened, to);
    }

    return false;
}

ResolvedType adaptMemberType(const ResolvedType& baseType, const ResolvedType& fieldType) {
    if (!baseType.isView() || fieldType.isPlain() || fieldType.isView()) {
        return fieldType;
    }

    ResolvedType adapted = fieldType;
    adapted.viewKind = baseType.viewKind;
    adapted.category = TypeCategory::View;
    return adapted;
}

std::unordered_map<std::string, ResolvedType> buildTypeBindings(
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes) {
    std::unordered_map<std::string, ResolvedType> bindings;
    const size_t count = std::min(paramNames.size(), argTypes.size());
    for (size_t i = 0; i < count; ++i) {
        bindings[paramNames[i]] = argTypes[i];
    }
    return bindings;
}

ResolvedType substituteType(
    const ResolvedType& type,
    const std::unordered_map<std::string, ResolvedType>& bindings) {
    if (type.isGeneric) {
        const auto it = bindings.find(type.name);
        if (it != bindings.end()) {
            ResolvedType substituted = it->second;
            if (!type.viewKind.empty()) {
                substituted.viewKind = type.viewKind;
                substituted.category = TypeCategory::View;
            }
            return substituted;
        }
    }

    ResolvedType substituted = type;
    substituted.params.clear();
    for (const auto& param : type.params) {
        substituted.params.push_back(substituteType(param, bindings));
    }
    return substituted;
}

TypeCatalog::TypeCatalog()
    : builtinPlainTypes{
          "Bool", "Byte", "Rune", "Int8", "Int16", "Int32", "Int64", "Int128",
          "UInt8", "UInt16", "UInt32", "UInt64", "UInt128",
          "Bits8", "Bits16", "Bits32", "Bits64", "Bits128",
          "Float32", "Float64", "USize", "ISize", "Unit"},
      builtinOwnedTypes{
          "Text", "Bytes", "Span", "CStr", "OwnedCStr", "Vec", "Table",
          "Set", "Heap", "Ring", "Arena", "Pool", "Anchor", "Addr",
          "RawPtr", "RawMut", "Fn"} {
    for (const auto& name : builtinPlainTypes) {
        registerKnownTypeArity(name, 0);
    }
    for (const auto& name : builtinOwnedTypes) {
        registerKnownTypeArity(name, 0);
    }

    registerKnownTypeArity("Span", 1);
    registerKnownTypeArity("Vec", 1);
    registerKnownTypeArity("Set", 1);
    registerKnownTypeArity("Heap", 1);
    registerKnownTypeArity("Ring", 1);
    registerKnownTypeArity("Pool", 1);
    registerKnownTypeArity("Table", 2);
}

void TypeCatalog::registerKnownTypeArity(const std::string& name, size_t arity) {
    knownTypeArities[name] = arity;
}

std::optional<size_t> TypeCatalog::lookupKnownTypeArity(const std::string& name) const {
    const auto it = knownTypeArities.find(name);
    return it != knownTypeArities.end() ? std::optional<size_t>(it->second) : std::nullopt;
}

void TypeCatalog::registerShapeName(const std::string& name, std::optional<size_t> arity) {
    shapeNames.insert(name);
    if (arity.has_value()) {
        registerKnownTypeArity(name, *arity);
    }
}

void TypeCatalog::registerChoiceName(const std::string& name, std::optional<size_t> arity) {
    choiceNames.insert(name);
    if (arity.has_value()) {
        registerKnownTypeArity(name, *arity);
    }
}

bool TypeCatalog::hasNamedType(const std::string& name) const {
    return contains(builtinPlainTypes, name) || contains(builtinOwnedTypes, name) ||
           contains(shapeNames, name) || contains(choiceNames, name);
}

ResolvedType TypeCatalog::resolveType(
    const TypeNode* node,
    const std::unordered_set<std::string>& localTypeParams,
    std::vector<Diagnostic>* diagnostics) const {
    if (!node) {
        return makeUnknownType();
    }

    ResolvedType type;
    type.name = node->name;
    type.viewKind = node->viewKind;

    std::optional<size_t> expectedArity;

    if (contains(localTypeParams, node->name)) {
        type.category = node->viewKind.empty() ? TypeCategory::Owned : TypeCategory::View;
        type.isGeneric = true;
        expectedArity = 0;
    } else if (contains(builtinPlainTypes, node->name)) {
        type.category = node->viewKind.empty() ? TypeCategory::Plain : TypeCategory::View;
        expectedArity = lookupKnownTypeArity(node->name);
    } else if (contains(builtinOwnedTypes, node->name) || contains(shapeNames, node->name) || contains(choiceNames, node->name)) {
        type.category = node->viewKind.empty() ? TypeCategory::Owned : TypeCategory::View;
        expectedArity = lookupKnownTypeArity(node->name);
    } else {
        type = makeUnknownType(node->name);
        if (diagnostics) {
            diagnostics->push_back(Diagnostic{"semantic", "Unknown type: " + node->name, node->span});
        }
    }

    if (expectedArity.has_value() && node->params.size() != *expectedArity && diagnostics) {
        diagnostics->push_back(Diagnostic{
            "semantic",
            "Type '" + node->name + "' expects " + std::to_string(*expectedArity) +
                " type argument(s), got " + std::to_string(node->params.size()) + ".",
            node->span});
    }

    for (const auto& param : node->params) {
        type.params.push_back(resolveType(param.get(), localTypeParams, diagnostics));
    }

    return type;
}

std::optional<TypeLayoutInfo> computeTypeLayout(
    const ResolvedType& type,
    const AnalysisResult& analysis,
    const TargetSpec& target) {
    LayoutComputer computer(analysis, target);
    return computer.compute(type);
}

std::string describeTypeLayoutKind(TypeLayoutKind kind) {
    switch (kind) {
    case TypeLayoutKind::Unknown: return "unknown";
    case TypeLayoutKind::Scalar: return "scalar";
    case TypeLayoutKind::Pointer: return "pointer";
    case TypeLayoutKind::Slice: return "slice";
    case TypeLayoutKind::Buffer: return "buffer";
    case TypeLayoutKind::Aggregate: return "aggregate";
    case TypeLayoutKind::Tagged: return "tagged";
    case TypeLayoutKind::OpaqueHandle: return "opaque-handle";
    case TypeLayoutKind::Template: return "template";
    case TypeLayoutKind::View: return "view";
    }
    return "unknown";
}

std::string describeAbiPassKind(AbiPassKind kind) {
    switch (kind) {
    case AbiPassKind::Unknown: return "unknown";
    case AbiPassKind::Void: return "void";
    case AbiPassKind::Scalar: return "scalar";
    case AbiPassKind::Direct: return "direct";
    case AbiPassKind::Indirect: return "indirect";
    case AbiPassKind::Borrow: return "borrow";
    }
    return "unknown";
}

std::string describeLinkageKind(LinkageKind kind) {
    switch (kind) {
    case LinkageKind::Internal: return "internal";
    case LinkageKind::Shared: return "shared";
    case LinkageKind::Runtime: return "runtime";
    case LinkageKind::External: return "external";
    }
    return "internal";
}

} // namespace claw::frontend
