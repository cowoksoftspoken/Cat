#pragma once

#include "analysis/scope.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace claw::frontend {

struct AstNode;
struct Decl;
struct RealmDecl;
struct FnDecl;
struct ShapeDecl;
struct ChoiceDecl;
struct Stmt;
struct BlockStmt;
struct Expr;
struct ExprStmt;
struct TypeNode;
struct ScopeStmt;
struct ShapeInitExpr;

struct NamedBorrowScope {
    std::string name;
    SourceSpan span;
    int lexicalDepth = 0;
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(std::vector<ImportedBinding> importedBindings = {}, TargetSpec target = defaultTargetSpec());

    void analyze(RealmDecl* realm);

    const AnalysisResult& result() const;
    const FunctionSignature* lookupFunctionSignature(const FnDecl* fn) const;
    const FunctionSignature* lookupFunctionSignature(const std::string& name) const;
    const FunctionSignature* lookupCallableSignature(const Expr* callee) const;
    std::optional<MethodSignature> lookupMethodSignature(const Expr* callee) const;
    const ResolvedType* lookupExprType(const Expr* expr) const;
    const ShapeInfo* lookupShape(const std::string& name) const;
    const ChoiceInfo* lookupChoice(const std::string& name) const;
    const TargetSpec& targetSpec() const;

private:
    ScopeTree scopes;
    TypeCatalog typeCatalog;
    AnalysisResult analysisResult;
    std::vector<Diagnostic> diagnostics;
    std::vector<ImportedBinding> importedBindings;
    TargetSpec target;
    const FnDecl* currentFunction = nullptr;
    const FunctionSignature* currentSignature = nullptr;
    const ExprStmt* implicitTailExpr = nullptr;
    std::optional<size_t> currentViewReturnSourceParam;
    bool currentViewReturnSeen = false;
    int loopDepth = 0;
    int rawDepth = 0;
    std::vector<std::string> activeNamedScopes;
    int lexicalScopeDepth = 0;
    std::vector<NamedBorrowScope> namedBorrowScopes;

    void reportError(const std::string& msg);
    void reportError(const SourceSpan& span, const std::string& msg);
    void reportError(const AstNode* node, const std::string& msg);
    void enterSemanticScope();
    void exitSemanticScope();
    bool enterNamedBorrowScope(const std::string& name, const SourceSpan& span);
    void exitNamedBorrowScope();
    const NamedBorrowScope* lookupNamedBorrowScope(const std::string& name) const;
    bool validateScopedViewType(const ResolvedType& type, const AstNode* node, std::string_view context);
    bool validateAnchorPayloadType(const ResolvedType& type, const AstNode* node, std::string_view context);
    const Symbol* resolveBorrowSourceSymbol(const Expr* expr) const;
    bool validateScopedBorrowSource(const Expr* expr, const ResolvedType& targetType, const AstNode* node, std::string_view context);
    void registerPrelude();
    void registerImports(const RealmDecl* realm);
    void declareTopLevel(const RealmDecl* realm);
    void resolveTopLevelTypes(const RealmDecl* realm);
    void validateNamedLayouts(const RealmDecl* realm);
    bool validateOwnedLayoutDependency(const std::string& rootName, const ResolvedType& type, const SourceSpan& span, std::vector<std::string>& stack);

    void analyzeDecl(Decl* decl);
    void analyzeFnDecl(FnDecl* fn);
    void analyzeShapeDecl(ShapeDecl* shape);
    void analyzeChoiceDecl(ChoiceDecl* choice);

    void analyzeBlock(BlockStmt* block);
    void analyzeStmt(Stmt* stmt);
    ResolvedType analyzeExpr(Expr* expr, const ResolvedType* expectedType = nullptr);
    ResolvedType resolveTypeNode(const TypeNode* node, const std::unordered_set<std::string>& localTypeParams);
    std::unordered_map<std::string, ResolvedType> buildTypeBindingsChecked(
        const std::vector<std::string>& paramNames,
        const std::vector<ResolvedType>& argTypes,
        const std::string& context);
    ResolvedType instantiateGenericType(
        const ResolvedType& type,
        const std::vector<std::string>& paramNames,
        const std::vector<ResolvedType>& argTypes,
        const std::string& context);
    std::optional<MethodSignature> lookupMethodSignature(const ResolvedType& receiverType, const std::string& methodName) const;
    std::optional<size_t> resolveViewSourceParam(const Expr* expr) const;
    bool canBorrowAsView(const ResolvedType& from, const ResolvedType& to) const;
    bool canPassArgumentType(Expr* expr, const ResolvedType& from, const ResolvedType& to) const;
    bool canBorrowExprAsEdit(const Expr* expr) const;
    bool isRawAddressType(const ResolvedType& type) const;
    bool typeContainsBorrowedStorage(const ResolvedType& type) const;
    bool typeUsesOnlyNamedScope(const ResolvedType& type, std::string_view scopeName) const;
    bool typeContainsScopeName(const ResolvedType& type) const;
    bool typeIsMustUse(const ResolvedType& type) const;
    bool isImplicitTailExpr(const ExprStmt* stmt) const;
    bool isNamedScopeActive(std::string_view scopeName) const;
    bool symbolCanStoreNamedScope(const Symbol& symbol, std::string_view scopeName) const;
    ResolvedType bindFormalScopeName(const ResolvedType& type, std::string_view formalScope, std::string_view actualScope) const;

    std::shared_ptr<Symbol> lookupSymbol(const std::string& name) const;
    void defineVariable(
        const std::string& name,
        const ResolvedType& type,
        bool isMutable,
        const std::string& duplicateMessage,
        const SourceSpan& duplicateSpan = {},
        std::optional<size_t> viewSourceParamIndex = std::nullopt,
        const BindingStmt* bindingDecl = nullptr);
    bool isConditionLike(const ResolvedType& type) const;
    bool blockDefinitelyTerminates(const BlockStmt* block) const;
    bool stmtDefinitelyTerminates(const Stmt* stmt) const;
};

} // namespace claw::frontend

