#pragma once

#include "analysis/scope.h"

#include <memory>
#include <string>
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
struct TypeNode;

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(std::vector<ImportedBinding> importedBindings = {});

    void analyze(RealmDecl* realm);

    const AnalysisResult& result() const;
    const FunctionSignature* lookupFunctionSignature(const FnDecl* fn) const;
    const FunctionSignature* lookupFunctionSignature(const std::string& name) const;
    const ResolvedType* lookupExprType(const Expr* expr) const;
    const ShapeInfo* lookupShape(const std::string& name) const;
    const ChoiceInfo* lookupChoice(const std::string& name) const;

private:
    ScopeTree scopes;
    TypeCatalog typeCatalog;
    AnalysisResult analysisResult;
    std::vector<Diagnostic> diagnostics;
    std::vector<ImportedBinding> importedBindings;
    const FnDecl* currentFunction = nullptr;
    const FunctionSignature* currentSignature = nullptr;
    int loopDepth = 0;

    void reportError(const std::string& msg);
    void reportError(const SourceSpan& span, const std::string& msg);
    void reportError(const AstNode* node, const std::string& msg);
    void registerPrelude();
    void registerImports(const RealmDecl* realm);
    void declareTopLevel(const RealmDecl* realm);
    void resolveTopLevelTypes(const RealmDecl* realm);

    void analyzeDecl(Decl* decl);
    void analyzeFnDecl(FnDecl* fn);
    void analyzeShapeDecl(ShapeDecl* shape);
    void analyzeChoiceDecl(ChoiceDecl* choice);

    void analyzeBlock(BlockStmt* block);
    void analyzeStmt(Stmt* stmt);
    ResolvedType analyzeExpr(Expr* expr);
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

    std::shared_ptr<Symbol> lookupSymbol(const std::string& name) const;
    void defineVariable(
        const std::string& name,
        const ResolvedType& type,
        bool isMutable,
        const std::string& duplicateMessage,
        const SourceSpan& duplicateSpan = {});
    bool isConditionLike(const ResolvedType& type) const;
    bool blockDefinitelyTerminates(const BlockStmt* block) const;
    bool stmtDefinitelyTerminates(const Stmt* stmt) const;
};

} // namespace claw::frontend