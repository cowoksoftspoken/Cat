#pragma once

#include <string>
#include <string_view>
#include <vector>

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

struct OirUnitView {
    const RealmDecl* realm = nullptr;
    const SemanticAnalyzer* sema = nullptr;
};

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

std::string emitOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units);

} // namespace claw::frontend