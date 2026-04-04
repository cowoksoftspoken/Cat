#pragma once

#include "diagnostics/diagnostics.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace claw::frontend {

struct TypeNode;
struct Expr;
struct CallExpr;
struct FnDecl;
struct BindingStmt;
struct TryStmt;
struct ScanStmt;
struct ModuleInfo;

enum class TypeCategory {
    Unknown,
    OpaqueExternal,
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

enum class TypeLayoutKind {
    Unknown,
    Scalar,
    Pointer,
    Slice,
    Buffer,
    Aggregate,
    Tagged,
    OpaqueHandle,
    Template,
    View,
};

enum class AbiPassKind {
    Unknown,
    Void,
    Scalar,
    Direct,
    Indirect,
    Borrow,
};

enum class LinkageKind {
    Internal,
    Shared,
    Runtime,
    External,
};

struct ResolvedType {
    std::string name;
    std::string viewKind;
    std::string viewScope;
    std::vector<ResolvedType> params;
    TypeCategory category = TypeCategory::Unknown;
    bool isGeneric = false;

    bool isUnknown() const;
    bool isOpaqueExternal() const;
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

struct LayoutFieldInfo {
    std::string name;
    ResolvedType type;
    size_t offsetBytes = 0;
    size_t sizeBytes = 0;
    size_t alignBytes = 1;
};

struct LayoutVariantInfo {
    std::string name;
    std::vector<LayoutFieldInfo> payloadFields;
    size_t payloadOffsetBytes = 0;
    size_t payloadSizeBytes = 0;
    size_t payloadAlignBytes = 1;
};

struct TypeLayoutInfo {
    std::string typeName;
    std::string repr = "claw-internal";
    std::string abi = "claw";
    TypeLayoutKind kind = TypeLayoutKind::Unknown;
    AbiPassKind passKind = AbiPassKind::Unknown;
    bool ffiStable = false;
    bool isTemplate = false;
    size_t sizeBytes = 0;
    size_t alignBytes = 1;
    size_t tagSizeBytes = 0;
    size_t payloadOffsetBytes = 0;
    size_t payloadSizeBytes = 0;
    std::vector<LayoutFieldInfo> fields;
    std::vector<LayoutVariantInfo> variants;
};

struct SymbolLinkInfo {
    std::string symbol;
    std::string abi = "claw";
    LinkageKind linkage = LinkageKind::Internal;
    bool ffiStable = false;
};

struct ExternalCallableInfo {
    std::string dependencyRoot;
    std::string abi = "unknown";
    std::string linkageName;
    bool rawOnly = true;
};

struct FunctionSignature {
    std::vector<ResolvedType> paramTypes;
    ResolvedType returnType;
    std::optional<size_t> viewReturnSourceParam;
    bool isExternal = false;
    std::optional<ExternalCallableInfo> externalInfo;
};

struct MethodSignature {
    std::string name;
    ResolvedType receiverType;
    FunctionSignature function;
    std::optional<size_t> viewReturnSourceArg;
    bool viewReturnFromReceiver = false;
    bool isBuiltin = false;
};

struct TargetSpec {
    std::string name = "native64";
    unsigned pointerWidthBits = 64;
    unsigned ptrdiffWidthBits = 64;
};

struct ImportedBinding {
    std::string name;
    SymbolKind kind = SymbolKind::Module;
    std::optional<FunctionSignature> functionSignature;
    std::optional<ShapeInfo> shapeInfo;
    std::optional<ChoiceInfo> choiceInfo;
    std::shared_ptr<ModuleInfo> moduleInfo;
};

struct ModuleInfo {
    std::string realmName;
    std::unordered_map<std::string, ImportedBinding> exportedItems;
};

struct ChoiceConstructorInfo {
    ResolvedType resultType;
    std::string variantName;
    std::vector<ResolvedType> payloadTypes;
};

struct AnalysisResult {
    std::unordered_map<const Expr*, ResolvedType> exprTypes;
    std::unordered_map<const BindingStmt*, ResolvedType> bindingTypes;
    std::unordered_map<const TryStmt*, ResolvedType> tryBindingTypes;
    std::unordered_map<const ScanStmt*, ResolvedType> scanItemTypes;
    std::unordered_map<const Expr*, ChoiceConstructorInfo> choiceConstructors;
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
std::string canonicalTypeName(std::string_view name);
ResolvedType makeOpaqueExternalType(const std::string& name = "");
ResolvedType adaptMemberType(const ResolvedType& baseType, const ResolvedType& fieldType);
std::unordered_map<std::string, ResolvedType> buildTypeBindings(
    const std::vector<std::string>& paramNames,
    const std::vector<ResolvedType>& argTypes);
ResolvedType substituteType(
    const ResolvedType& type,
    const std::unordered_map<std::string, ResolvedType>& bindings);
bool sameType(const ResolvedType& left, const ResolvedType& right);
bool isNumericTypeName(const std::string& name);
bool isIntegerLikeTypeName(const std::string& name);
bool isIntegerLiteralType(const ResolvedType& type);
bool canAssignType(const ResolvedType& from, const ResolvedType& to);
TargetSpec defaultTargetSpec();
std::optional<TypeLayoutInfo> computeTypeLayout(
    const ResolvedType& type,
    const AnalysisResult& analysis,
    const TargetSpec& target);
std::string describeTypeLayoutKind(TypeLayoutKind kind);
std::string describeAbiPassKind(AbiPassKind kind);
std::string describeLinkageKind(LinkageKind kind);

} // namespace claw::frontend
