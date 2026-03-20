#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace claw::frontend {

class SemanticAnalyzer;
struct OwnershipResult;
struct RealmDecl;
struct Decl;
struct Stmt;
struct Expr;
struct FnDecl;
struct ShapeDecl;
struct ChoiceDecl;
struct BlockStmt;

struct OirValue {
    std::string text;
    std::string type;
    bool isUnit = false;
};

struct OirParam {
    std::string name;
    std::string type;
};

struct OirShapeField {
    std::string name;
    std::string type;
};

struct OirChoiceCase {
    std::string name;
    std::vector<std::string> payloadTypes;
};

struct OirPickCase {
    std::string tag;
    std::vector<std::string> bindings;
    std::string targetLabel;
};

struct OirHoldInst {
    bool isMutable = false;
    std::string name;
    std::string type;
    std::optional<OirValue> init;
};

struct OirStoreInst {
    std::string target;
    OirValue value;
};

struct OirStoreFieldInst {
    OirValue object;
    std::string field;
    OirValue value;
};

struct OirReturnInst {
    OirValue value;
};

struct OirDiscardInst {
    OirValue value;
};

struct OirDropInst {
    std::string name;
    std::string type;
};

struct OirBranchInst {
    OirValue condition;
    std::string trueLabel;
    std::string falseLabel;
};

struct OirGotoInst {
    std::string targetLabel;
};

struct OirScanInst {
    std::string itemName;
    std::string itemType;
    OirValue iterable;
    std::string bodyLabel;
    std::string exitLabel;
};

struct OirPickInst {
    OirValue value;
    std::vector<OirPickCase> cases;
};

struct OirLiftInst {
    OirValue value;
    std::string okName;
    std::string failLabel;
};

struct OirStopInst {
    std::string targetLabel;
};

struct OirSkipInst {
    std::string targetLabel;
};

struct OirCallInst {
    std::optional<std::string> result;
    std::string callee;
    std::vector<OirValue> args;
    std::string type;
};

struct OirFieldInst {
    std::string result;
    OirValue object;
    std::string field;
    std::string type;
};

struct OirBinaryInst {
    std::string result;
    std::string op;
    OirValue left;
    OirValue right;
    std::string type;
};

using OirInst = std::variant<
    OirHoldInst,
    OirStoreInst,
    OirStoreFieldInst,
    OirReturnInst,
    OirDiscardInst,
    OirDropInst,
    OirBranchInst,
    OirGotoInst,
    OirScanInst,
    OirPickInst,
    OirLiftInst,
    OirStopInst,
    OirSkipInst,
    OirCallInst,
    OirFieldInst,
    OirBinaryInst>;

struct OirBlock {
    std::string label;
    std::vector<OirInst> insts;
};

struct OirFunction {
    std::string name;
    std::vector<OirParam> params;
    std::string returnType;
    std::vector<OirBlock> blocks;
};

struct OirShape {
    std::string name;
    std::vector<OirShapeField> fields;
};

struct OirChoice {
    std::string name;
    std::vector<OirChoiceCase> cases;
};

using OirDecl = std::variant<OirFunction, OirShape, OirChoice>;

struct OirRealm {
    std::string name;
    std::vector<OirDecl> decls;
};

struct OirProgram {
    std::string entryRealm;
    std::vector<OirRealm> realms;
};

struct OirUnitView {
    const RealmDecl* realm = nullptr;
    const SemanticAnalyzer* sema = nullptr;
    const OwnershipResult* ownership = nullptr;
};

class OirEmitter {
public:
    explicit OirEmitter(const SemanticAnalyzer& sema, const OwnershipResult* ownership = nullptr);

    OirRealm lowerRealm(const RealmDecl* realm) const;
    std::string emit(const RealmDecl* realm) const;
    bool blockDefinitelyTerminates(const BlockStmt* block) const;
    bool stmtDefinitelyTerminates(const Stmt* stmt) const;

private:
    const SemanticAnalyzer& sema;
    const OwnershipResult* ownership = nullptr;

    std::optional<OirDecl> lowerDecl(const Decl* decl) const;
    OirFunction lowerFn(const FnDecl* fn) const;
    OirShape lowerShape(const ShapeDecl* shape) const;
    OirChoice lowerChoice(const ChoiceDecl* choice) const;
};

OirProgram buildOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units);
std::string formatOirRealm(const OirRealm& realm);
std::string formatOirProgram(const OirProgram& program);
std::string emitOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units);

} // namespace claw::frontend
