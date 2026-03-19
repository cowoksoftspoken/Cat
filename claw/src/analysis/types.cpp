#include "analysis/types.h"

#include "ast/ast.h"

#include <sstream>

namespace claw::frontend {

namespace {

bool contains(const std::unordered_set<std::string>& set, const std::string& value) {
    return set.find(value) != set.end();
}

} // namespace

bool ResolvedType::isUnknown() const {
    return category == TypeCategory::Unknown;
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
    out << (name.empty() ? "<unknown>" : name);
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

bool canAssignType(const ResolvedType& from, const ResolvedType& to) {
    if (from.isUnknown() || to.isUnknown()) {
        return true;
    }

    if (sameType(from, to)) {
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

} // namespace claw::frontend