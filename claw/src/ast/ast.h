#pragma once

#include <memory>
#include <string>
#include <vector>

#include "diagnostics/diagnostics.h"
#include "lexer/lexer.h"

namespace claw::frontend {

struct AstNode {
    SourceSpan span;
    virtual ~AstNode() = default;
};

struct TypeNode : public AstNode {
    std::string name;
    std::string viewKind;
    std::vector<std::unique_ptr<TypeNode>> params;
};

struct ImportItem {
    std::string name;
    std::string alias;
};

struct Expr : public AstNode {};

struct BoolExpr : public Expr {
    bool value = false;
};

struct IntExpr : public Expr {
    std::string value;
};

struct FloatExpr : public Expr {
    std::string value;
};

struct StringExpr : public Expr {
    std::string value;
};

struct IdentExpr : public Expr {
    std::string name;
};

struct BinaryExpr : public Expr {
    std::string op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct CallExpr : public Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
};

struct MemberExpr : public Expr {
    std::unique_ptr<Expr> object;
    std::string member;
};

struct IndexExpr : public Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
};

struct BorrowExpr : public Expr {
    bool isMutable = false;
    std::unique_ptr<Expr> target;
};

struct Stmt : public AstNode {};

struct BlockStmt : public Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct ExprStmt : public Stmt {
    std::unique_ptr<Expr> expr;
};

struct GiveStmt : public Stmt {
    std::unique_ptr<Expr> value;
};

struct AssignStmt : public Stmt {
    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct BindingStmt : public Stmt {
    bool isMutable = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> value;
};

struct TryStmt : public Stmt {
    bool isMutable = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expr> expr;
    bool autoPropagate = false;
    std::string failName;
    std::unique_ptr<BlockStmt> failBlock;
};

struct WhenStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBlock;
    std::unique_ptr<BlockStmt> elseBlock;
};

struct LoopStmt : public Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;
};

struct ScanStmt : public Stmt {
    std::string itemName;
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<BlockStmt> body;
};

struct StopStmt : public Stmt {};
struct SkipStmt : public Stmt {};

struct PickBranch {
    SourceSpan span;
    std::string tag;
    std::vector<std::string> bindings;
    std::unique_ptr<BlockStmt> body;
};

struct PickStmt : public Stmt {
    std::unique_ptr<Expr> value;
    std::vector<PickBranch> branches;
};

struct LiftStmt : public Stmt {
    std::unique_ptr<Expr> expr;
    std::string valueName;
    std::string failName;
    std::unique_ptr<BlockStmt> failBlock;
};

struct RawStmt : public Stmt {
    std::unique_ptr<BlockStmt> body;
};

struct Decl : public AstNode {
    bool isShared = false;
};

struct FnParam {
    SourceSpan span;
    std::string name;
    std::unique_ptr<TypeNode> type;
};

struct FnDecl : public Decl {
    std::string name;
    std::vector<FnParam> params;
    std::unique_ptr<TypeNode> returnType;
    std::unique_ptr<BlockStmt> body;
};

struct ShapeField {
    SourceSpan span;
    bool isShared = false;
    std::string name;
    std::unique_ptr<TypeNode> type;
};

struct ShapeDecl : public Decl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<ShapeField> fields;
};

struct ChoiceVariant {
    SourceSpan span;
    std::string tag;
    std::vector<FnParam> payloads;
};

struct ChoiceDecl : public Decl {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<ChoiceVariant> variants;
};

struct ImportDecl {
    SourceSpan span;
    std::string modulePath;
    std::vector<ImportItem> items;
    bool isSuper = false;
};

struct RealmDecl : public AstNode {
    std::string name;
    std::vector<ImportDecl> imports;
    std::vector<std::unique_ptr<Decl>> declarations;
};

} // namespace claw::frontend


