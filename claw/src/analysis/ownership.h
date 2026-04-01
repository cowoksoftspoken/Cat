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
struct BlockStmt;

struct BorrowToken {
    std::string rootName;
    std::vector<std::string> accessPath;
    std::string viewKind;

    bool operator==(const BorrowToken& other) const = default;
};

enum class StorageState {
    Initialized,
    Uninitialized,
    Moved,
    MaybeUninitialized,
};

struct DropAction {
    std::string name;
    ResolvedType type;
};

struct OwnershipResult {
    std::unordered_map<const Stmt*, std::vector<DropAction>> dropsBeforeStmt;
    std::unordered_map<const BlockStmt*, std::vector<DropAction>> dropsAtBlockEnd;
};

struct TrackedVar {
    ResolvedType type;
    bool isMutableStorage = false;
    StorageState storageState = StorageState::Initialized;
    std::vector<BorrowToken> activeBorrows;
    std::optional<BorrowToken> lexicalBorrow;
};

struct ScopeBinding {
    std::string name;
    std::optional<TrackedVar> previousState;
};

enum class ScopeKind {
    Normal,
    LoopBoundary,
    FunctionBoundary,
};

struct ScopeFrame {
    const BlockStmt* block = nullptr;
    ScopeKind kind = ScopeKind::Normal;
    std::vector<ScopeBinding> bindings;
};

class OwnershipChecker {
public:
    void check(RealmDecl* realm, const SemanticAnalyzer& semantic);
    const OwnershipResult& result() const;

private:
    const SemanticAnalyzer* semantic = nullptr;
    std::unordered_map<std::string, TrackedVar> varStates;
    std::vector<ScopeFrame> scopeStack;
    OwnershipResult ownershipResult;
    std::vector<Diagnostic> diagnostics;

    void reportError(const std::string& msg);
    void reportError(const SourceSpan& span, const std::string& msg);
    void reportError(const AstNode* node, const std::string& msg);
    void checkDecl(Decl* decl);
    void checkFnDecl(FnDecl* fn);
    void checkStmt(Stmt* stmt);
    void checkExpr(Expr* expr, bool isConsume);
    void checkCallExpr(CallExpr* call);

    void enterScope(const BlockStmt* block = nullptr, ScopeKind kind = ScopeKind::Normal);
    void exitScope();
    void defineTrackedVar(const std::string& name, const TrackedVar& state);
    void recordDropBeforeStmt(const Stmt* stmt, const std::string& name, const ResolvedType& type);
    std::vector<DropAction> collectUnwindDrops(std::optional<ScopeKind> stopAfterKind) const;
    void acquireView(Expr* expr, const ResolvedType& viewType);
    void releaseView(Expr* expr, const ResolvedType& viewType);
    const FunctionSignature* resolveCallSignature(const Expr* callee) const;
    std::optional<MethodSignature> resolveMethodSignature(const Expr* callee) const;
    std::optional<BorrowToken> resolveBorrowPath(Expr* expr) const;
    std::optional<std::string> resolveBorrowRootName(Expr* expr) const;
    std::optional<BorrowToken> resolveBorrowToken(Expr* expr, const ResolvedType& viewType) const;
    bool acquireBorrowToken(const BorrowToken& token, const AstNode* node);
    void releaseBorrowToken(const BorrowToken& token);
    void releaseLexicalBorrow(TrackedVar& state);
    ResolvedType typeOfExpr(const Expr* expr) const;
    bool isTrackedOwned(const ResolvedType& type) const;
    bool shouldScheduleDrop(const TrackedVar& state) const;
    void mergeTrackedState(TrackedVar& target, const TrackedVar& source) const;
    std::unordered_map<std::string, TrackedVar> retainExistingStates(
        const std::unordered_map<std::string, TrackedVar>& baseline,
        const std::unordered_map<std::string, TrackedVar>& current) const;
};

} // namespace claw::frontend
