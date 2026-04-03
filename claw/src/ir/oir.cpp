#include "ir/oir.h"

#include "analysis/ownership.h"
#include "analysis/sema.h"
#include "ast/ast.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace claw::frontend {

namespace {

template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

std::string indentText(int indent) {
    return std::string(static_cast<size_t>(indent) * 2, ' ');
}

std::string quoted(const std::string& text) {
    return '"' + text + '"';
}

std::string binaryOpName(const std::string& op) {
    if (op == "+") return "add";
    if (op == "-") return "sub";
    if (op == "*") return "mul";
    if (op == "/") return "div";
    if (op == "==") return "eq";
    if (op == "!=") return "neq";
    if (op == "<") return "lt";
    if (op == "<=") return "lte";
    if (op == ">") return "gt";
    if (op == ">=") return "gte";
    return "op";
}

std::vector<OirRealm> canonicalizeRealmOrder(std::vector<OirRealm> realms, std::string_view entryRealm) {
    std::stable_sort(realms.begin(), realms.end(), [&](const OirRealm& left, const OirRealm& right) {
        const bool leftIsEntry = left.name == entryRealm;
        const bool rightIsEntry = right.name == entryRealm;
        if (leftIsEntry != rightIsEntry) {
            return leftIsEntry;
        }
        return left.name < right.name;
    });
    return realms;
}

std::string exprType(const SemanticAnalyzer& sema, const Expr* expr) {
    const auto* type = sema.lookupExprType(expr);
    return type ? type->describe() : std::string("<unknown>");
}

const std::vector<DropAction>* dropsBeforeStmt(const OwnershipResult* ownership, const Stmt* stmt) {
    if (!ownership || !stmt) {
        return nullptr;
    }
    const auto it = ownership->dropsBeforeStmt.find(stmt);
    return it != ownership->dropsBeforeStmt.end() ? &it->second : nullptr;
}

const std::vector<DropAction>* dropsAtBlockEnd(const OwnershipResult* ownership, const BlockStmt* block) {
    if (!ownership || !block) {
        return nullptr;
    }
    const auto it = ownership->dropsAtBlockEnd.find(block);
    return it != ownership->dropsAtBlockEnd.end() ? &it->second : nullptr;
}

std::string calleeText(const Expr* expr) {
    if (!expr) {
        return "<callee>";
    }
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return ident->name;
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        return calleeText(member->object.get()) + "." + member->member;
    }
    return "<callee>";
}

std::string tailSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    if (pos == std::string_view::npos) {
        return std::string(text);
    }
    return std::string(text.substr(pos + 1));
}

std::string receiverSegment(std::string_view text) {
    const size_t pos = text.rfind('.');
    if (pos == std::string_view::npos) {
        return {};
    }
    return std::string(text.substr(0, pos));
}

std::optional<OirExternalCallInfo> externalCallInfo(
    const SemanticAnalyzer& sema,
    const CallExpr* call,
    std::string_view callee,
    const std::string& type) {
    if (!call) {
        return std::nullopt;
    }

    if (const auto* signature = sema.lookupCallableSignature(call->callee.get())) {
        if (signature->externalInfo.has_value()) {
            const auto& info = *signature->externalInfo;
            return OirExternalCallInfo{
                info.dependencyRoot,
                info.abi.empty() ? std::string("claw") : info.abi,
                info.linkageName.empty() ? tailSegment(callee) : info.linkageName,
                info.rawOnly,
                false};
        }
    }

    if (type.rfind("opaque external result", 0) != 0) {
        return std::nullopt;
    }

    std::string dependencyRoot = receiverSegment(callee);
    std::string linkageName = tailSegment(callee);
    if (dependencyRoot.empty()) {
        dependencyRoot = linkageName;
    }
    return OirExternalCallInfo{dependencyRoot, "opaque", linkageName, true, true};
}

std::string externalCallSuffix(const std::optional<OirExternalCallInfo>& info) {
    if (!info.has_value()) {
        return {};
    }

    std::ostringstream out;
    out << " [extern abi=" << (info->abi.empty() ? std::string("unknown") : info->abi)
        << " dep=" << (info->dependencyRoot.empty() ? std::string("<external>") : info->dependencyRoot)
        << " symbol=" << (info->linkageName.empty() ? std::string("<callee>") : info->linkageName)
        << (info->rawOnly ? " raw" : " safe");
    if (info->opaqueResult) {
        out << " opaque-result";
    }
    out << ']';
    return out.str();
}


SymbolLinkInfo localLinkInfo(std::string_view realmName, std::string_view name, bool isShared) {
    SymbolLinkInfo info;
    info.symbol = std::string(realmName.empty() ? std::string_view("<unknown>") : realmName) + "::" + std::string(name);
    info.abi = "claw";
    info.linkage = isShared ? LinkageKind::Shared : LinkageKind::Internal;
    info.ffiStable = false;
    return info;
}

std::string formatSymbolLinkInfo(const SymbolLinkInfo& info) {
    std::ostringstream out;
    out << " [abi=" << (info.abi.empty() ? std::string("unknown") : info.abi)
        << " link=" << describeLinkageKind(info.linkage)
        << " symbol=" << (info.symbol.empty() ? std::string("<unknown>") : info.symbol)
        << " ffi=" << (info.ffiStable ? "true" : "false") << ']';
    return out.str();
}

std::string formatLayoutInfo(const TypeLayoutInfo& layout) {
    std::ostringstream out;
    out << "layout repr=" << layout.repr
        << " kind=" << describeTypeLayoutKind(layout.kind)
        << " size=" << layout.sizeBytes
        << " align=" << layout.alignBytes
        << " pass=" << describeAbiPassKind(layout.passKind)
        << " ffi=" << (layout.ffiStable ? "true" : "false");
    if (layout.kind == TypeLayoutKind::Tagged && !layout.isTemplate) {
        out << " tag=" << layout.tagSizeBytes
            << " payload_offset=" << layout.payloadOffsetBytes
            << " payload_size=" << layout.payloadSizeBytes;
    }
    if (layout.isTemplate) {
        out << " unresolved=true";
    }
    return out.str();
}

std::string formatParam(const OirParam& param) {
    return param.name + ": " + param.type + " [" + describeAbiPassKind(param.passKind) + "]";
}

std::string formatTypeParams(const std::vector<std::string>& params) {
    if (params.empty()) {
        return {};
    }

    std::ostringstream out;
    out << " of ";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    return out.str();
}

const LayoutFieldInfo* findLayoutField(const TypeLayoutInfo& layout, std::string_view fieldName) {
    for (const auto& field : layout.fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

const LayoutVariantInfo* findLayoutVariant(const TypeLayoutInfo& layout, std::string_view variantName) {
    for (const auto& variant : layout.variants) {
        if (variant.name == variantName) {
            return &variant;
        }
    }
    return nullptr;
}

struct LoopTargets {
    std::string continueLabel;
    std::string breakLabel;
};

struct FunctionLoweringContext {
    const SemanticAnalyzer& sema;
    const OwnershipResult* ownership = nullptr;
    OirFunction function;
    int nextTemp = 0;
    int nextBlock = 0;
    int nextRawRegion = 0;
    std::vector<LoopTargets> loopStack;
    std::vector<std::string> rawRegionStack;

    std::string tempName() {
        return "%t" + std::to_string(nextTemp++);
    }

    std::string blockName(const std::string& prefix) {
        return prefix + "_" + std::to_string(nextBlock++);
    }

    std::string rawRegionName() {
        return "raw_region_" + std::to_string(nextRawRegion++);
    }

    size_t addBlock(const std::string& label) {
        function.blocks.push_back(OirBlock{label, currentRawRegion(), {}});
        return function.blocks.size() - 1;
    }

    const LoopTargets* currentLoop() const {
        return loopStack.empty() ? nullptr : &loopStack.back();
    }

    std::optional<std::string> currentRawRegion() const {
        return rawRegionStack.empty() ? std::nullopt : std::optional<std::string>(rawRegionStack.back());
    }
};

void appendInst(FunctionLoweringContext& context, size_t blockIndex, OirInst inst) {
    context.function.blocks[blockIndex].insts.push_back(std::move(inst));
}

void appendDropInsts(
    FunctionLoweringContext& context,
    size_t blockIndex,
    const std::vector<DropAction>* drops) {
    if (!drops) {
        return;
    }
    for (const auto& drop : *drops) {
        appendInst(context, blockIndex, OirDropInst{drop.name, drop.type.describe()});
    }
}

OirValue lowerExpr(
    const SemanticAnalyzer& sema,
    const Expr* expr,
    FunctionLoweringContext& context,
    size_t blockIndex);

std::optional<size_t> lowerStmt(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const Stmt* stmt,
    FunctionLoweringContext& context,
    size_t currentBlockIndex,
    const OirEmitter& emitter);

size_t lowerBlock(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const BlockStmt* block,
    const std::string& label,
    FunctionLoweringContext& context,
    const std::string& fallthroughLabel,
    const OirEmitter& emitter) {
    const size_t entryBlockIndex = context.addBlock(label);
    std::optional<size_t> currentBlockIndex = entryBlockIndex;

    if (!block) {
        if (!fallthroughLabel.empty()) {
            appendInst(context, entryBlockIndex, OirGotoInst{fallthroughLabel});
        }
        return entryBlockIndex;
    }

    for (const auto& stmt : block->statements) {
        if (!currentBlockIndex.has_value()) {
            return entryBlockIndex;
        }
        currentBlockIndex = lowerStmt(
            sema,
            ownership,
            stmt.get(),
            context,
            *currentBlockIndex,
            emitter);
    }

    if (currentBlockIndex.has_value()) {
        appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, block));
        if (!fallthroughLabel.empty()) {
            appendInst(context, *currentBlockIndex, OirGotoInst{fallthroughLabel});
        }
    }

    return entryBlockIndex;
}

OirValue lowerExpr(
    const SemanticAnalyzer& sema,
    const Expr* expr,
    FunctionLoweringContext& context,
    size_t blockIndex) {
    if (!expr) {
        return OirValue{"unit", "Unit", true};
    }

    if (auto* value = dynamic_cast<const IntExpr*>(expr)) {
        return OirValue{value->value, exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const FloatExpr*>(expr)) {
        return OirValue{value->value, exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const StringExpr*>(expr)) {
        return OirValue{quoted(value->value), exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const BoolExpr*>(expr)) {
        return OirValue{value->value ? "true" : "false", exprType(sema, expr), false};
    }
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        if (const auto ctorIt = sema.result().choiceConstructors.find(ident);
            ctorIt != sema.result().choiceConstructors.end()) {
            const std::string result = context.tempName();
            appendInst(context, blockIndex, OirChoiceMakeInst{
                result,
                ctorIt->second.resultType.describe(),
                ctorIt->second.variantName,
                {}});
            return OirValue{result, exprType(sema, expr), false};
        }
        return OirValue{ident->name, exprType(sema, expr), false};
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        const OirValue object = lowerExpr(sema, member->object.get(), context, blockIndex);
        const std::string result = context.tempName();
        const std::string type = exprType(sema, expr);
        appendInst(context, blockIndex, OirFieldInst{result, object, member->member, type});
        return OirValue{result, type, false};
    }
    if (auto* borrow = dynamic_cast<const BorrowExpr*>(expr)) {
        OirValue lowered = lowerExpr(sema, borrow->target.get(), context, blockIndex);
        lowered.type = exprType(sema, expr);
        return lowered;
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        const OirValue left = lowerExpr(sema, binary->left.get(), context, blockIndex);
        const OirValue right = lowerExpr(sema, binary->right.get(), context, blockIndex);
        const std::string result = context.tempName();
        const std::string type = exprType(sema, expr);
        appendInst(context, blockIndex, OirBinaryInst{result, binaryOpName(binary->op), left, right, type});
        return OirValue{result, type, false};
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        if (const auto ctorIt = sema.result().choiceConstructors.find(call);
            ctorIt != sema.result().choiceConstructors.end()) {
            std::vector<OirValue> payloads;
            payloads.reserve(call->args.size());
            for (const auto& arg : call->args) {
                payloads.push_back(lowerExpr(sema, arg.get(), context, blockIndex));
            }

            const std::string result = context.tempName();
            appendInst(context, blockIndex, OirChoiceMakeInst{
                result,
                ctorIt->second.resultType.describe(),
                ctorIt->second.variantName,
                std::move(payloads)});
            return OirValue{result, exprType(sema, expr), false};
        }

        std::vector<OirValue> args;
        args.reserve(call->args.size());
        for (const auto& arg : call->args) {
            args.push_back(lowerExpr(sema, arg.get(), context, blockIndex));
        }

        const std::string type = exprType(sema, expr);
        const std::string callee = calleeText(call->callee.get());
        const auto callExternalInfo = externalCallInfo(sema, call, callee, type);
        if (type == "Unit") {
            appendInst(context, blockIndex, OirCallInst{std::nullopt, callee, std::move(args), type, callExternalInfo});
            return OirValue{"unit", "Unit", true};
        }

        const std::string result = context.tempName();
        appendInst(context, blockIndex, OirCallInst{result, callee, std::move(args), type, callExternalInfo});
        return OirValue{result, type, false};
    }

    return OirValue{"<expr>", exprType(sema, expr), false};
}

std::optional<size_t> lowerStmt(
    const SemanticAnalyzer& sema,
    const OwnershipResult* ownership,
    const Stmt* stmt,
    FunctionLoweringContext& context,
    size_t currentBlockIndex,
    const OirEmitter& emitter) {
    if (auto* bind = dynamic_cast<const BindingStmt*>(stmt)) {
        const auto bindingIt = sema.result().bindingTypes.find(bind);
        const std::string type = bindingIt != sema.result().bindingTypes.end()
            ? bindingIt->second.describe()
            : std::string("<unknown>");
        std::optional<OirValue> init;
        if (bind->value) {
            init = lowerExpr(sema, bind->value.get(), context, currentBlockIndex);
        }
        appendInst(context, currentBlockIndex, OirHoldInst{bind->isMutable, bind->name, type, init});
        return currentBlockIndex;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, assign->value.get(), context, currentBlockIndex);
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        if (auto* ident = dynamic_cast<const IdentExpr*>(assign->target.get())) {
            appendInst(context, currentBlockIndex, OirStoreInst{ident->name, value});
            return currentBlockIndex;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            const OirValue object = lowerExpr(sema, member->object.get(), context, currentBlockIndex);
            appendInst(context, currentBlockIndex, OirStoreFieldInst{object, member->member, value});
            return currentBlockIndex;
        }
        appendInst(context, currentBlockIndex, OirStoreInst{"<unsupported>", value});
        return currentBlockIndex;
    }

    if (auto* give = dynamic_cast<const GiveStmt*>(stmt)) {
        const OirValue value = give->value
            ? lowerExpr(sema, give->value.get(), context, currentBlockIndex)
            : OirValue{"unit", "Unit", true};
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        appendInst(context, currentBlockIndex, OirReturnInst{value});
        return std::nullopt;
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, exprStmt->expr.get(), context, currentBlockIndex);
        if (!value.isUnit) {
            appendInst(context, currentBlockIndex, OirDiscardInst{value});
        }
        return currentBlockIndex;
    }

    if (auto* when = dynamic_cast<const WhenStmt*>(stmt)) {
        const OirValue condition = lowerExpr(sema, when->condition.get(), context, currentBlockIndex);
        const std::string thenLabel = context.blockName("when_then");
        if (when->elseBlock) {
            const std::string elseLabel = context.blockName("when_else");
            const bool thenTerminates = emitter.blockDefinitelyTerminates(when->thenBlock.get());
            const bool elseTerminates = emitter.blockDefinitelyTerminates(when->elseBlock.get());
            const bool needsJoin = !thenTerminates || !elseTerminates;
            const std::string joinLabel = needsJoin ? context.blockName("when_join") : std::string{};
            appendInst(context, currentBlockIndex, OirBranchInst{condition, thenLabel, elseLabel});
            lowerBlock(sema, ownership, when->thenBlock.get(), thenLabel, context, needsJoin ? joinLabel : std::string{}, emitter);
            lowerBlock(sema, ownership, when->elseBlock.get(), elseLabel, context, needsJoin ? joinLabel : std::string{}, emitter);
            if (!needsJoin) {
                return std::nullopt;
            }
            return context.addBlock(joinLabel);
        }

        const std::string joinLabel = context.blockName("when_join");
        appendInst(context, currentBlockIndex, OirBranchInst{condition, thenLabel, joinLabel});
        lowerBlock(sema, ownership, when->thenBlock.get(), thenLabel, context, joinLabel, emitter);
        return context.addBlock(joinLabel);
    }

    if (auto* loop = dynamic_cast<const LoopStmt*>(stmt)) {
        const std::string headerLabel = context.blockName("loop_header");
        const std::string bodyLabel = context.blockName("loop_body");
        const std::string exitLabel = context.blockName("loop_exit");
        appendInst(context, currentBlockIndex, OirGotoInst{headerLabel});
        const size_t headerIndex = context.addBlock(headerLabel);
        if (loop->condition) {
            const OirValue condition = lowerExpr(sema, loop->condition.get(), context, headerIndex);
            appendInst(context, headerIndex, OirBranchInst{condition, bodyLabel, exitLabel});
        } else {
            appendInst(context, headerIndex, OirGotoInst{bodyLabel});
        }
        context.loopStack.push_back(LoopTargets{headerLabel, exitLabel});
        lowerBlock(sema, ownership, loop->body.get(), bodyLabel, context, headerLabel, emitter);
        context.loopStack.pop_back();
        return context.addBlock(exitLabel);
    }

    if (auto* scan = dynamic_cast<const ScanStmt*>(stmt)) {
        const OirValue iterable = lowerExpr(sema, scan->iterable.get(), context, currentBlockIndex);
        const auto itemTypeIt = sema.result().scanItemTypes.find(scan);
        const std::string itemType = itemTypeIt != sema.result().scanItemTypes.end()
            ? itemTypeIt->second.describe()
            : std::string("<unknown>");
        const std::string headerLabel = context.blockName("scan_header");
        const std::string bodyLabel = context.blockName("scan_body");
        const std::string exitLabel = context.blockName("scan_exit");
        appendInst(context, currentBlockIndex, OirGotoInst{headerLabel});
        const size_t headerIndex = context.addBlock(headerLabel);
        appendInst(context, headerIndex, OirScanInst{scan->itemName, itemType, iterable, bodyLabel, exitLabel});
        context.loopStack.push_back(LoopTargets{headerLabel, exitLabel});
        lowerBlock(sema, ownership, scan->body.get(), bodyLabel, context, headerLabel, emitter);
        context.loopStack.pop_back();
        return context.addBlock(exitLabel);
    }

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        const std::string scopeLabel = context.blockName("scope_" + scopeStmt->name);
        const std::string contLabel = context.blockName("scope_cont");
        appendInst(context, currentBlockIndex, OirGotoInst{scopeLabel});
        lowerBlock(sema, ownership, scopeStmt->body.get(), scopeLabel, context, contLabel, emitter);
        return context.addBlock(contLabel);
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, pick->value.get(), context, currentBlockIndex);
        const bool needsJoin = [&]() {
            for (const auto& branch : pick->branches) {
                if (!emitter.blockDefinitelyTerminates(branch.body.get())) {
                    return true;
                }
            }
            return false;
        }();
        const std::string joinLabel = needsJoin ? context.blockName("pick_join") : std::string{};
        std::vector<OirPickCase> cases;
        cases.reserve(pick->branches.size());
        for (const auto& branch : pick->branches) {
            cases.push_back(OirPickCase{branch.tag, branch.bindings, context.blockName("pick_" + branch.tag)});
        }
        appendInst(context, currentBlockIndex, OirPickInst{value, cases});
        for (size_t i = 0; i < pick->branches.size(); ++i) {
            lowerBlock(
                sema,
                ownership,
                pick->branches[i].body.get(),
                cases[i].targetLabel,
                context,
                needsJoin ? joinLabel : std::string{},
                emitter);
        }
        if (!needsJoin) {
            return std::nullopt;
        }
        return context.addBlock(joinLabel);
    }

    if (auto* lift = dynamic_cast<const LiftStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, lift->expr.get(), context, currentBlockIndex);
        const std::string failLabel = context.blockName("lift_fail");
        appendInst(context, currentBlockIndex, OirLiftInst{value, lift->valueName, lift->failName, failLabel});
        lowerBlock(sema, ownership, lift->failBlock.get(), failLabel, context, {}, emitter);
        return currentBlockIndex;
    }

    if (auto* tryStmt = dynamic_cast<const TryStmt*>(stmt)) {
        const OirValue value = lowerExpr(sema, tryStmt->expr.get(), context, currentBlockIndex);
        if (tryStmt->autoPropagate) {
            appendInst(context, currentBlockIndex, OirLiftInst{value, tryStmt->name, {}, {}, true});
            return currentBlockIndex;
        }

        const std::string failLabel = context.blockName("try_fail");
        appendInst(context, currentBlockIndex, OirLiftInst{value, tryStmt->name, tryStmt->failName, failLabel, false});
        lowerBlock(sema, ownership, tryStmt->failBlock.get(), failLabel, context, {}, emitter);
        return currentBlockIndex;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        const std::string rawLabel = context.blockName("raw");
        const std::string contLabel = context.blockName("raw_cont");
        const std::string rawRegion = context.rawRegionName();
        appendInst(context, currentBlockIndex, OirGotoInst{rawLabel});
        context.rawRegionStack.push_back(rawRegion);
        lowerBlock(sema, ownership, raw->body.get(), rawLabel, context, contLabel, emitter);
        context.rawRegionStack.pop_back();
        return context.addBlock(contLabel);
    }

    if (dynamic_cast<const StopStmt*>(stmt)) {
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        const auto* loop = context.currentLoop();
        appendInst(context, currentBlockIndex, OirStopInst{loop ? loop->breakLabel : std::string{}});
        return std::nullopt;
    }

    if (dynamic_cast<const SkipStmt*>(stmt)) {
        appendDropInsts(context, currentBlockIndex, dropsBeforeStmt(ownership, stmt));
        const auto* loop = context.currentLoop();
        appendInst(context, currentBlockIndex, OirSkipInst{loop ? loop->continueLabel : std::string{}});
        return std::nullopt;
    }

    return currentBlockIndex;
}

std::string formatValue(const OirValue& value) {
    return value.text;
}

std::string formatInst(const OirInst& inst, int indent) {
    const std::string pad = indentText(indent);
    return std::visit(Overloaded{
        [&](const OirHoldInst& value) {
            std::ostringstream out;
            out << pad << (value.isMutable ? "var " : "val ") << value.name << ": " << value.type;
            if (value.init.has_value()) {
                out << " = " << formatValue(*value.init);
            }
            out << "\n";
            return out.str();
        },
        [&](const OirStoreInst& value) {
            std::ostringstream out;
            out << pad << "store " << value.target << " <- " << formatValue(value.value)
                << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirStoreFieldInst& value) {
            std::ostringstream out;
            out << pad << "store_field " << formatValue(value.object) << "." << value.field << " <- "
                << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirReturnInst& value) {
            std::ostringstream out;
            out << pad << "return " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirDiscardInst& value) {
            std::ostringstream out;
            out << pad << "discard " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const OirDropInst& value) {
            std::ostringstream out;
            out << pad << "drop " << value.name << " : " << value.type << "\n";
            return out.str();
        },
        [&](const OirBranchInst& value) {
            std::ostringstream out;
            out << pad << "branch " << formatValue(value.condition) << " -> " << value.trueLabel << ", "
                << value.falseLabel << "\n";
            return out.str();
        },
        [&](const OirGotoInst& value) {
            return pad + "goto " + value.targetLabel + "\n";
        },
        [&](const OirScanInst& value) {
            std::ostringstream out;
            out << pad << "scan " << value.itemName << ": " << value.itemType << " over "
                << formatValue(value.iterable) << " -> " << value.bodyLabel << ", " << value.exitLabel << "\n";
            return out.str();
        },
        [&](const OirPickInst& value) {
            std::ostringstream out;
            out << pad << "pick " << formatValue(value.value) << " : " << value.value.type << "\n";
            for (const auto& item : value.cases) {
                out << pad << "case " << item.tag;
                if (!item.bindings.empty()) {
                    out << "(";
                    for (size_t i = 0; i < item.bindings.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.bindings[i];
                    }
                    out << ")";
                }
                out << " -> " << item.targetLabel << "\n";
            }
            return out.str();
        },
        [&](const OirLiftInst& value) {
            std::ostringstream out;
            out << pad << "try " << formatValue(value.value) << " -> " << value.okName;
            if (value.autoPropagate) {
                out << " propagate\n";
            } else {
                out << ", else " << value.failName << " -> " << value.failLabel << "\n";
            }
            return out.str();
        },
        [&](const OirChoiceMakeInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.variantName << "(";
            for (size_t i = 0; i < value.payloads.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.payloads[i]);
            }
            out << ") : " << value.type << "\n";
            return out.str();
        },
        [&](const OirStopInst& value) {
            if (value.targetLabel.empty()) {
                return pad + "stop\n";
            }
            return pad + "stop -> " + value.targetLabel + "\n";
        },
        [&](const OirSkipInst& value) {
            if (value.targetLabel.empty()) {
                return pad + "skip\n";
            }
            return pad + "skip -> " + value.targetLabel + "\n";
        },
        [&](const OirCallInst& value) {
            std::ostringstream out;
            if (value.result.has_value()) {
                out << pad << *value.result << " = ";
            } else {
                out << pad;
            }
            out << "call " << value.callee << "(";
            for (size_t i = 0; i < value.args.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.args[i]);
            }
            out << ") : " << value.type << externalCallSuffix(value.externalInfo) << "\n";
            return out.str();
        },
        [&](const OirFieldInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = field " << formatValue(value.object) << "." << value.field
                << " : " << value.type << "\n";
            return out.str();
        },
        [&](const OirBinaryInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.op << " " << formatValue(value.left) << ", "
                << formatValue(value.right) << " : " << value.type << "\n";
            return out.str();
        }
    }, inst);
}

std::string formatDecl(const OirDecl& decl) {
    return std::visit(Overloaded{
        [&](const OirFunction& fn) {
            std::ostringstream out;
            out << "oir.fn " << fn.name << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatParam(fn.params[i]);
            }
            out << ") -> " << fn.returnType << " [" << describeAbiPassKind(fn.returnPassKind) << "]"
                << formatSymbolLinkInfo(fn.linkage) << "\n";
            for (const auto& block : fn.blocks) {
                out << "  block " << block.label;
                if (block.rawRegion.has_value()) {
                    out << " [raw: " << *block.rawRegion << "]";
                }
                out << ":\n";
                for (const auto& inst : block.insts) {
                    out << formatInst(inst, 2);
                }
            }
            return out.str();
        },
        [&](const OirShape& shape) {
            std::ostringstream out;
            out << "oir.shape " << shape.name << formatTypeParams(shape.typeParams)
                << formatSymbolLinkInfo(shape.linkage) << "\n";
            if (shape.layout.has_value()) {
                out << "  " << formatLayoutInfo(*shape.layout) << "\n";
            }
            for (const auto& field : shape.fields) {
                out << "  field " << field.name << ": " << field.type;
                if (shape.layout.has_value() && !shape.layout->isTemplate) {
                    if (const auto* layoutField = findLayoutField(*shape.layout, field.name)) {
                        out << " @offset=" << layoutField->offsetBytes
                            << " size=" << layoutField->sizeBytes
                            << " align=" << layoutField->alignBytes;
                    }
                }
                out << "\n";
            }
            return out.str();
        },
        [&](const OirChoice& choice) {
            std::ostringstream out;
            out << "oir.choice " << choice.name << formatTypeParams(choice.typeParams)
                << formatSymbolLinkInfo(choice.linkage) << "\n";
            if (choice.layout.has_value()) {
                out << "  " << formatLayoutInfo(*choice.layout) << "\n";
            }
            for (const auto& item : choice.cases) {
                out << "  case " << item.name;
                if (!item.payloadTypes.empty()) {
                    out << "(";
                    for (size_t i = 0; i < item.payloadTypes.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.payloadTypes[i];
                    }
                    out << ")";
                }
                if (choice.layout.has_value() && !choice.layout->isTemplate) {
                    if (const auto* variant = findLayoutVariant(*choice.layout, item.name)) {
                        out << " @payload_offset=" << variant->payloadOffsetBytes
                            << " size=" << variant->payloadSizeBytes
                            << " align=" << variant->payloadAlignBytes;
                    }
                }
                out << "\n";
            }
            return out.str();
        }
    }, decl);
}

bool blockDefinitelyTerminatesImpl(const OirEmitter& emitter, const BlockStmt* block) {
    if (!block) {
        return false;
    }

    for (const auto& stmt : block->statements) {
        if (emitter.stmtDefinitelyTerminates(stmt.get())) {
            return true;
        }
    }
    return false;
}

bool stmtDefinitelyTerminatesImpl(const OirEmitter& emitter, const Stmt* stmt) {
    if (dynamic_cast<const GiveStmt*>(stmt) || dynamic_cast<const StopStmt*>(stmt) || dynamic_cast<const SkipStmt*>(stmt)) {
        return true;
    }

    if (auto* when = dynamic_cast<const WhenStmt*>(stmt)) {
        return when->elseBlock && emitter.blockDefinitelyTerminates(when->thenBlock.get()) &&
               emitter.blockDefinitelyTerminates(when->elseBlock.get());
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        if (pick->branches.empty()) {
            return false;
        }
        for (const auto& branch : pick->branches) {
            if (!emitter.blockDefinitelyTerminates(branch.body.get())) {
                return false;
            }
        }
        return true;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        return emitter.blockDefinitelyTerminates(raw->body.get());
    }

    if (auto* scopeStmt = dynamic_cast<const ScopeStmt*>(stmt)) {
        return emitter.blockDefinitelyTerminates(scopeStmt->body.get());
    }

    return false;
}

} // namespace

OirEmitter::OirEmitter(const SemanticAnalyzer& sema, const OwnershipResult* ownership)
    : sema(sema), ownership(ownership) {}

OirRealm OirEmitter::lowerRealm(const RealmDecl* realm) const {
    OirRealm lowered;
    lowered.name = realm ? realm->name : std::string("<unknown>");
    if (!realm) {
        return lowered;
    }

    for (const auto& decl : realm->declarations) {
        if (auto loweredDecl = lowerDecl(decl.get(), lowered.name)) {
            lowered.decls.push_back(std::move(*loweredDecl));
        }
    }
    return lowered;
}

std::string OirEmitter::emit(const RealmDecl* realm) const {
    return formatOirRealm(lowerRealm(realm));
}

std::optional<OirDecl> OirEmitter::lowerDecl(const Decl* decl, std::string_view realmName) const {
    if (auto* fn = dynamic_cast<const FnDecl*>(decl)) {
        return OirDecl{lowerFn(fn, realmName)};
    }
    if (auto* shape = dynamic_cast<const ShapeDecl*>(decl)) {
        return OirDecl{lowerShape(shape, realmName)};
    }
    if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl)) {
        return OirDecl{lowerChoice(choice, realmName)};
    }
    return std::nullopt;
}

OirFunction OirEmitter::lowerFn(const FnDecl* fn, std::string_view realmName) const {
    FunctionLoweringContext context{sema, ownership};
    context.function.name = fn ? fn->name : std::string("<unknown>");
    context.function.linkage = localLinkInfo(realmName, context.function.name, fn && fn->isShared);

    const auto* signature = sema.lookupFunctionSignature(fn);
    if (fn) {
        for (size_t i = 0; i < fn->params.size(); ++i) {
            const ResolvedType paramType = signature && i < signature->paramTypes.size()
                ? signature->paramTypes[i]
                : makeUnknownType();
            const auto layout = computeTypeLayout(paramType, sema.result(), sema.targetSpec());
            context.function.params.push_back(OirParam{
                fn->params[i].name,
                paramType.describe(),
                layout.has_value() ? layout->passKind : AbiPassKind::Unknown});
        }
    }
    const ResolvedType returnType = signature ? signature->returnType : makeUnknownType();
    context.function.returnType = returnType.describe();
    const auto returnLayout = computeTypeLayout(returnType, sema.result(), sema.targetSpec());
    context.function.returnPassKind = returnLayout.has_value() ? returnLayout->passKind : AbiPassKind::Unknown;

    const size_t entryBlockIndex = context.addBlock("entry");
    std::optional<size_t> currentBlockIndex = entryBlockIndex;

    if (fn && fn->body) {
        const ExprStmt* implicitTailExpr = nullptr;
        if (!fn->body->statements.empty()) {
            implicitTailExpr = dynamic_cast<const ExprStmt*>(fn->body->statements.back().get());
        }

        for (const auto& stmt : fn->body->statements) {
            if (!currentBlockIndex.has_value()) {
                break;
            }

            if (implicitTailExpr && stmt.get() == implicitTailExpr) {
                const OirValue value = implicitTailExpr->expr
                    ? lowerExpr(sema, implicitTailExpr->expr.get(), context, *currentBlockIndex)
                    : OirValue{"unit", "Unit", true};
                appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, fn->body.get()));
                appendInst(context, *currentBlockIndex, OirReturnInst{value});
                currentBlockIndex = std::nullopt;
                break;
            }

            currentBlockIndex = lowerStmt(sema, ownership, stmt.get(), context, *currentBlockIndex, *this);
        }

        if (currentBlockIndex.has_value()) {
            appendDropInsts(context, *currentBlockIndex, dropsAtBlockEnd(ownership, fn->body.get()));
        }
    }

    return std::move(context.function);
}

OirShape OirEmitter::lowerShape(const ShapeDecl* shape, std::string_view realmName) const {
    OirShape lowered;
    lowered.name = shape ? shape->name : std::string("<unknown>");
    lowered.linkage = localLinkInfo(realmName, lowered.name, shape && shape->isShared);
    if (!shape) {
        return lowered;
    }

    const auto* info = sema.lookupShape(shape->name);
    if (!info) {
        return lowered;
    }

    lowered.typeParams = info->typeParams;
    ResolvedType namedType = makeUnknownType();
    namedType.name = shape->name;
    namedType.category = TypeCategory::Owned;
    lowered.layout = computeTypeLayout(namedType, sema.result(), sema.targetSpec());

    for (const auto& fieldName : info->fieldOrder) {
        const auto it = info->fields.find(fieldName);
        if (it == info->fields.end()) {
            continue;
        }
        lowered.fields.push_back(OirShapeField{fieldName, it->second.describe()});
    }
    return lowered;
}

OirChoice OirEmitter::lowerChoice(const ChoiceDecl* choice, std::string_view realmName) const {
    OirChoice lowered;
    lowered.name = choice ? choice->name : std::string("<unknown>");
    lowered.linkage = localLinkInfo(realmName, lowered.name, choice && choice->isShared);
    if (!choice) {
        return lowered;
    }

    const auto* info = sema.lookupChoice(choice->name);
    if (!info) {
        return lowered;
    }

    lowered.typeParams = info->typeParams;
    ResolvedType namedType = makeUnknownType();
    namedType.name = choice->name;
    namedType.category = TypeCategory::Owned;
    lowered.layout = computeTypeLayout(namedType, sema.result(), sema.targetSpec());

    for (const auto& variantName : info->variantOrder) {
        OirChoiceCase item;
        item.name = variantName;
        const auto it = info->variants.find(variantName);
        if (it != info->variants.end()) {
            for (const auto& payloadType : it->second.payloadTypes) {
                item.payloadTypes.push_back(payloadType.describe());
            }
        }
        lowered.cases.push_back(std::move(item));
    }
    return lowered;
}

bool OirEmitter::blockDefinitelyTerminates(const BlockStmt* block) const {
    return blockDefinitelyTerminatesImpl(*this, block);
}

bool OirEmitter::stmtDefinitelyTerminates(const Stmt* stmt) const {
    return stmtDefinitelyTerminatesImpl(*this, stmt);
}

OirProgram buildOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    OirProgram program;
    program.entryRealm = entryRealm.empty() ? std::string("<unknown>") : std::string(entryRealm);
    program.entrySymbol = program.entryRealm + "::main";

    for (const auto& unit : units) {
        if (!unit.realm || !unit.sema) {
            program.realms.push_back(OirRealm{"<unknown>", {}});
            continue;
        }
        OirEmitter emitter(*unit.sema, unit.ownership);
        program.realms.push_back(emitter.lowerRealm(unit.realm));
    }

    program.realms = canonicalizeRealmOrder(std::move(program.realms), program.entryRealm);
    return program;
}

std::string formatOirRealm(const OirRealm& realm) {
    std::ostringstream out;
    out << "oir.module " << (realm.name.empty() ? std::string("<unknown>") : realm.name) << "\n";
    for (const auto& decl : realm.decls) {
        out << formatDecl(decl);
    }
    return out.str();
}

std::string formatOirProgram(const OirProgram& program) {
    std::ostringstream out;
    out << "oir.program entry " << (program.entryRealm.empty() ? std::string("<unknown>") : program.entryRealm)
        << " symbol " << (program.entrySymbol.empty() ? std::string("<unknown>::main") : program.entrySymbol) << "\n";
    for (size_t i = 0; i < program.realms.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << formatOirRealm(program.realms[i]);
    }
    return out.str();
}

std::string emitOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return formatOirProgram(buildOirProgram(entryRealm, units));
}

} // namespace claw::frontend
