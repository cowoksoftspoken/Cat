#pragma once

#include "analysis/types.h"

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

struct TrackedVar {
    ResolvedType type;
    bool moved = false;
    int sharedBorrows = 0;
    bool mutableBorrow = false;
};

class OwnershipChecker {
public:
    void check(RealmDecl* realm, const SemanticAnalyzer& semantic);

private:
    const SemanticAnalyzer* semantic = nullptr;
    std::unordered_map<std::string, TrackedVar> varStates;
    std::vector<Diagnostic> diagnostics;

    void reportError(const std::string& msg);
    void reportError(const SourceSpan& span, const std::string& msg);
    void reportError(const AstNode* node, const std::string& msg);
    void checkDecl(Decl* decl);
    void checkFnDecl(FnDecl* fn);
    void checkStmt(Stmt* stmt);
    void checkExpr(Expr* expr, bool isConsume);
    void checkCallExpr(CallExpr* call);

    void acquireView(Expr* expr, const ResolvedType& viewType);
    void releaseView(Expr* expr, const ResolvedType& viewType);
    TrackedVar* trackedValueForExpr(Expr* expr);
    ResolvedType typeOfExpr(const Expr* expr) const;
    bool isTrackedOwned(const ResolvedType& type) const;
    std::unordered_map<std::string, TrackedVar> retainExistingStates(
        const std::unordered_map<std::string, TrackedVar>& baseline,
        const std::unordered_map<std::string, TrackedVar>& current) const;
};

} // namespace claw::frontend