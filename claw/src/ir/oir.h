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

class OirEmitter {
public:
    explicit OirEmitter(const SemanticAnalyzer& sema);

    std::string emit(const RealmDecl* realm) const;
    bool blockDefinitelyTerminates(const BlockStmt* block) const;
    bool stmtDefinitelyTerminates(const Stmt* stmt) const;

private:
    const SemanticAnalyzer& sema;

    std::string emitDecl(const Decl* decl) const;
    std::string emitFn(const FnDecl* fn) const;
    std::string emitShape(const ShapeDecl* shape) const;
    std::string emitChoice(const ChoiceDecl* choice) const;
};

} // namespace claw::frontend