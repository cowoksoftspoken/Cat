#pragma once

#include "analysis/types.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace claw::frontend {

class SemanticAnalyzer;
struct AstNode;
struct RealmDecl;
struct Decl;
struct FnDecl;
struct Stmt;
struct Expr;
struct CallExpr;

struct BorrowToken {
    std::string rootName;
    std::string viewKind;

    bool operator==(const BorrowToken& other) const = default;
};

struct TrackedVar {
    ResolvedType type;
    bool moved = false;
    int sharedBorrows = 0;
    bool mutableBorrow = false;
    std::optional<BorrowToken> lexicalBorrow;
};

struct ScopeBinding {
    std::string name;
    std::optional<TrackedVar> previousState;
};

struct ScopeFrame {
    std::vector<ScopeBinding> bindings;
};

class OwnershipChecker {
public:
    void check(RealmDecl* realm, const SemanticAnalyzer& semantic);

private:
    const SemanticAnalyzer* semantic = nullptr;
    std::unordered_map<std::string, TrackedVar> varStates;
    std::vector<ScopeFrame> scopeStack;
    std::vector<Diagnostic> diagnostics;

    void reportError(const std::string& msg);
    void reportError(const SourceSpan& span, const std::string& msg);
    void reportError(const AstNode* node, const std::string& msg);
    void checkDecl(Decl* decl);
    void checkFnDecl(FnDecl* fn);
    void checkStmt(Stmt* stmt);
    void checkExpr(Expr* expr, bool isConsume);
    void checkCallExpr(CallExpr* call);

    void enterScope();
    void exitScope();
    void defineTrackedVar(const std::string& name, const TrackedVar& state);
    void acquireView(Expr* expr, const ResolvedType& viewType);
    void releaseView(Expr* expr, const ResolvedType& viewType);
    const FunctionSignature* resolveCallSignature(const Expr* callee) const;
    std::optional<std::string> resolveBorrowRootName(Expr* expr) const;
    std::optional<BorrowToken> resolveBorrowToken(Expr* expr, const ResolvedType& viewType) const;
    bool acquireBorrowToken(const BorrowToken& token, const AstNode* node);
    void releaseBorrowToken(const BorrowToken& token);
    void releaseLexicalBorrow(TrackedVar& state);
    ResolvedType typeOfExpr(const Expr* expr) const;
    bool isTrackedOwned(const ResolvedType& type) const;
    void mergeTrackedState(TrackedVar& target, const TrackedVar& source) const;
    std::unordered_map<std::string, TrackedVar> retainExistingStates(
        const std::unordered_map<std::string, TrackedVar>& baseline,
        const std::unordered_map<std::string, TrackedVar>& current) const;
};

} // namespace claw::frontend
