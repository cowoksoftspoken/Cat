#pragma once

#include "ir/oir.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace claw::frontend {

enum class LirSafetyTag {
    None,
    ProvenSafe,
    BoundsCheckRequired,
    UnsafeBoundary,
};

enum class LirCallKind {
    Direct,
    ModuleDirect,
    Runtime,
    Builtin,
    External,
    Foreign,
};

struct LirValue {
    std::string text;
    std::string type;
    bool isUnit = false;
};

struct LirParam {
    std::string name;
    std::string type;
    AbiPassKind passKind = AbiPassKind::Unknown;
};

struct LirPhiInput {
    std::string label;
    LirValue value;
};

struct LirSwitchCase {
    std::string tag;
    std::vector<std::string> bindings;
    std::string targetLabel;
};

struct LirShapeField {
    std::string name;
    std::string type;
};

struct LirChoiceCase {
    std::string name;
    std::vector<std::string> payloadTypes;
};

struct LirObjectInst {
    bool isMutable = false;
    std::string name;
    std::string type;
    std::string storage;
};

struct LirPhiInst {
    std::string result;
    std::string type;
    std::vector<LirPhiInput> inputs;
};

struct LirBoundsCheckInst {
    std::string kind;
    std::string subject;
    std::vector<LirValue> args;
    std::string failLabel;
};

struct LirStoreInst {
    std::string target;
    LirValue value;
};

struct LirStoreFieldInst {
    LirValue object;
    std::string field;
    LirValue value;
    LirSafetyTag safety = LirSafetyTag::ProvenSafe;
};

struct LirReturnInst {
    LirValue value;
};

struct LirDiscardInst {
    LirValue value;
};

struct LirDropInst {
    std::string name;
    std::string type;
};

struct LirBranchInst {
    LirValue condition;
    std::string trueLabel;
    std::string falseLabel;
};

struct LirGotoInst {
    std::string targetLabel;
};

struct LirIterNextInst {
    std::string itemName;
    std::string itemType;
    LirValue iterable;
    std::string bodyLabel;
    std::string exitLabel;
};

struct LirSwitchInst {
    LirValue value;
    std::vector<LirSwitchCase> cases;
    std::string defectLabel;
};

struct LirLiftInst {
    LirValue value;
    std::string okName;
    std::string failName;
    std::string successLabel;
    std::string failLabel;
};

struct LirBreakInst {
    std::string targetLabel;
};

struct LirContinueInst {
    std::string targetLabel;
};

struct LirExternalCallInfo {
    std::string dependencyRoot;
    std::string abi = "unknown";
    std::string linkageName;
    bool rawOnly = true;
    bool opaqueResult = false;
};

struct LirCallInst {
    std::optional<std::string> result;
    std::string callee;
    std::vector<LirValue> args;
    std::string type;
    LirCallKind kind = LirCallKind::Direct;
    LirSafetyTag safety = LirSafetyTag::None;
    std::string hook;
    std::optional<LirExternalCallInfo> externalInfo;
};

struct LirFieldInst {
    std::string result;
    LirValue object;
    std::string field;
    std::string type;
    LirSafetyTag safety = LirSafetyTag::ProvenSafe;
};

struct LirBinaryInst {
    std::string result;
    std::string op;
    LirValue left;
    LirValue right;
    std::string type;
};

struct LirDefectInst {
    std::string kind;
    std::string detail;
};

using LirInst = std::variant<
    LirObjectInst,
    LirPhiInst,
    LirBoundsCheckInst,
    LirStoreInst,
    LirStoreFieldInst,
    LirReturnInst,
    LirDiscardInst,
    LirDropInst,
    LirBranchInst,
    LirGotoInst,
    LirIterNextInst,
    LirSwitchInst,
    LirLiftInst,
    LirBreakInst,
    LirContinueInst,
    LirCallInst,
    LirFieldInst,
    LirBinaryInst,
    LirDefectInst>;

struct LirBlock {
    std::string label;
    std::optional<std::string> rawRegion;
    std::vector<std::string> predecessors;
    std::vector<LirInst> insts;
};

struct LirFunction {
    std::string name;
    std::vector<LirParam> params;
    std::string returnType;
    AbiPassKind returnPassKind = AbiPassKind::Unknown;
    SymbolLinkInfo linkage;
    std::vector<LirBlock> blocks;
};

struct LirShape {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<LirShapeField> fields;
    SymbolLinkInfo linkage;
    std::optional<TypeLayoutInfo> layout;
};

struct LirChoice {
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<LirChoiceCase> cases;
    SymbolLinkInfo linkage;
    std::optional<TypeLayoutInfo> layout;
};

using LirDecl = std::variant<LirFunction, LirShape, LirChoice>;

struct LirRealm {
    std::string name;
    std::vector<LirDecl> decls;
};

struct LirProgram {
    std::string entryRealm;
    std::string entrySymbol;
    std::vector<LirRealm> realms;
};

LirProgram buildLirProgram(const OirProgram& program);
LirProgram buildLirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units);
std::string formatLirRealm(const LirRealm& realm);
std::string formatLirProgram(const LirProgram& program);
std::string emitLirProgram(const OirProgram& program);
std::string emitLirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units);

} // namespace claw::frontend
