#pragma once

#include "diagnostics/diagnostics.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace claw::frontend {

struct TypeNode;
struct Expr;
struct FnDecl;
struct BindingStmt;
struct ScanStmt;

enum class TypeCategory {
    Unknown,
    Plain,
    Owned,
    View,
};

enum class SymbolKind {
    Variable,
    Function,
    Shape,
    Choice,
    Module,
};

struct ImportedBinding {
    std::string name;
    SymbolKind kind = SymbolKind::Module;
};

struct ResolvedType {
    std::string name;
    std::string viewKind;
    std::vector<ResolvedType> params;
    TypeCategory category = TypeCategory::Unknown;
    bool isGeneric = false;

    bool isUnknown() const;
    bool isPlain() const;
    bool isOwned() const;
    bool isView() const;
    std::string describe() const;
};

struct ShapeInfo {
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, ResolvedType> fields;
    std::vector<std::string> fieldOrder;
};

struct ChoiceVariantInfo {
    std::vector<ResolvedType> payloadTypes;
};

struct ChoiceInfo {
    std::vector<std::string> typeParams;
    std::unordered_map<std::string, ChoiceVariantInfo> variants;
    std::vector<std::string> variantOrder;
};

struct FunctionSignature {
    std::vector<ResolvedType> paramTypes;
    ResolvedType returnType;
    bool isExternal = false;
};

struct AnalysisResult {
    std::unordered_map<const Expr*, ResolvedType> exprTypes;
    std::unordered_map<const BindingStmt*, ResolvedType> bindingTypes;
    std::unordered_map<const ScanStmt*, ResolvedType> scanItemTypes;
    std::unordered_map<const FnDecl*, FunctionSignature> functionSignatures;
    std::unordered_map<std::string, FunctionSignature> functionsByName;
    std::unordered_map<std::string, ShapeInfo> shapesByName;
    std::unordered_map<std::string, ChoiceInfo> choicesByName;
};

class TypeCatalog {
public:
    TypeCatalog();

    void registerShapeName(const std::string& name, std::optional<size_t> arity = std::nullopt);
    void registerChoiceName(const std::string& name, std::optional<size_t> arity = std::nullopt);
    bool hasNamedType(const std::string& name) const;
    ResolvedType resolveType(
        const TypeNode* node,
        const std::unordered_set<std::string>& localTypeParams,
        std::vector<Diagnostic>* diagnostics) const;

private:
    std::unordered_set<std::string> builtinPlainTypes;
    std::unordered_set<std::string> builtinOwnedTypes;
    std::unordered_set<std::string> shapeNames;
    std::unordered_set<std::string> choiceNames;
    std::unordered_map<std::string, size_t> knownTypeArities;

    void registerKnownTypeArity(const std::string& name, size_t arity);
    std::optional<size_t> lookupKnownTypeArity(const std::string& name) const;
};

ResolvedType makeUnknownType(const std::string& name = "");
ResolvedType adaptMemberType(const ResolvedType& baseType, const ResolvedType& fieldType);
std::unordered_map<std::string, ResolvedType> buildTypeBindings(
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes);
ResolvedType substituteType(
    const ResolvedType& type,
    const std::unordered_map<std::string, ResolvedType>& bindings);
bool sameType(const ResolvedType& left, const ResolvedType& right);
bool canAssignType(const ResolvedType& from, const ResolvedType& to);

} // namespace claw::frontend