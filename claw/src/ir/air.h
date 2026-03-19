#pragma once

#include <string>

namespace claw::frontend {

class SemanticAnalyzer;
struct RealmDecl;
struct Decl;
struct Stmt;
struct Expr;
struct FnDecl;
struct ShapeDecl;
struct ChoiceDecl;
struct BlockStmt;

class AirEmitter {
public:
    explicit AirEmitter(const SemanticAnalyzer& sema);

    std::string emit(const RealmDecl* realm) const;

private:
    const SemanticAnalyzer& sema;

    std::string emitDecl(const Decl* decl, int indent) const;
    std::string emitFn(const FnDecl* fn, int indent) const;
    std::string emitShape(const ShapeDecl* shape, int indent) const;
    std::string emitChoice(const ChoiceDecl* choice, int indent) const;
    std::string emitBlock(const BlockStmt* block, int indent) const;
    std::string emitStmt(const Stmt* stmt, int indent) const;
    std::string emitExpr(const Expr* expr) const;
    std::string emitExprValue(const Expr* expr) const;
    std::string emitTypeOf(const Expr* expr) const;
    std::string indentText(int indent) const;
};

} // namespace claw::frontend
