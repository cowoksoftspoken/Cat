#include "ir/oir.h"

#include "analysis/sema.h"
#include "ast/ast.h"

#include <sstream>
#include <utility>
#include <vector>

namespace claw::frontend {

namespace {

struct LoweredValue {
    std::string text;
    std::string type;
    bool isUnit = false;
};

struct FunctionBuilder {
    std::ostringstream out;
    int nextTemp = 0;
    int nextBlock = 0;

    std::string tempName() {
        return "%t" + std::to_string(nextTemp++);
    }

    std::string blockName(const std::string& prefix) {
        return prefix + "_" + std::to_string(nextBlock++);
    }
};

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

std::string exprType(const SemanticAnalyzer& sema, const Expr* expr) {
    const auto* type = sema.lookupExprType(expr);
    return type ? type->describe() : std::string("<unknown>");
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

LoweredValue lowerExpr(const SemanticAnalyzer& sema, const Expr* expr, FunctionBuilder& builder, int indent);
void emitBlock(
    const SemanticAnalyzer& sema,
    const BlockStmt* block,
    const std::string& label,
    FunctionBuilder& builder,
    int indent,
    const std::string& fallthroughLabel,
    const OirEmitter& emitter);

LoweredValue lowerExpr(const SemanticAnalyzer& sema, const Expr* expr, FunctionBuilder& builder, int indent) {
    if (!expr) {
        return {"unit", "Unit", true};
    }

    if (auto* value = dynamic_cast<const IntExpr*>(expr)) {
        return {value->value, exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const StringExpr*>(expr)) {
        return {quoted(value->value), exprType(sema, expr), false};
    }
    if (auto* value = dynamic_cast<const BoolExpr*>(expr)) {
        return {value->value ? "true" : "false", exprType(sema, expr), false};
    }
    if (auto* ident = dynamic_cast<const IdentExpr*>(expr)) {
        return {ident->name, exprType(sema, expr), false};
    }
    if (auto* member = dynamic_cast<const MemberExpr*>(expr)) {
        const auto object = lowerExpr(sema, member->object.get(), builder, indent);
        const std::string temp = builder.tempName();
        const std::string type = exprType(sema, expr);
        builder.out << indentText(indent) << temp << " = field " << object.text << "." << member->member
                    << " : " << type << "\n";
        return {temp, type, false};
    }
    if (auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        const auto left = lowerExpr(sema, binary->left.get(), builder, indent);
        const auto right = lowerExpr(sema, binary->right.get(), builder, indent);
        const std::string temp = builder.tempName();
        const std::string type = exprType(sema, expr);
        builder.out << indentText(indent) << temp << " = " << binaryOpName(binary->op) << " "
                    << left.text << ", " << right.text << " : " << type << "\n";
        return {temp, type, false};
    }
    if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
        std::vector<LoweredValue> args;
        args.reserve(call->args.size());
        for (const auto& arg : call->args) {
            args.push_back(lowerExpr(sema, arg.get(), builder, indent));
        }

        const std::string type = exprType(sema, expr);
        const std::string target = calleeText(call->callee.get());
        if (type == "Unit") {
            builder.out << indentText(indent) << "call " << target << "(";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    builder.out << ", ";
                }
                builder.out << args[i].text;
            }
            builder.out << ") : Unit\n";
            return {"unit", "Unit", true};
        }

        const std::string temp = builder.tempName();
        builder.out << indentText(indent) << temp << " = call " << target << "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                builder.out << ", ";
            }
            builder.out << args[i].text;
        }
        builder.out << ") : " << type << "\n";
        return {temp, type, false};
    }

    return {"<expr>", exprType(sema, expr), false};
}

void emitStmt(
    const SemanticAnalyzer& sema,
    const Stmt* stmt,
    FunctionBuilder& builder,
    int indent,
    const OirEmitter& emitter) {
    const std::string pad = indentText(indent);

    if (auto* bind = dynamic_cast<const BindingStmt*>(stmt)) {
        const auto bindingIt = sema.result().bindingTypes.find(bind);
        const std::string type = bindingIt != sema.result().bindingTypes.end()
            ? bindingIt->second.describe()
            : std::string("<unknown>");
        builder.out << pad << (bind->isMutable ? "slot " : "hold ") << bind->name << ": " << type;
        if (bind->value) {
            const auto value = lowerExpr(sema, bind->value.get(), builder, indent);
            builder.out << " = " << value.text;
        }
        builder.out << "\n";
        return;
    }

    if (auto* assign = dynamic_cast<const AssignStmt*>(stmt)) {
        const auto value = lowerExpr(sema, assign->value.get(), builder, indent);
        if (auto* ident = dynamic_cast<const IdentExpr*>(assign->target.get())) {
            builder.out << pad << "store " << ident->name << " <- " << value.text
                        << " : " << value.type << "\n";
            return;
        }
        if (auto* member = dynamic_cast<const MemberExpr*>(assign->target.get())) {
            const auto object = lowerExpr(sema, member->object.get(), builder, indent);
            builder.out << pad << "store_field " << object.text << "." << member->member << " <- "
                        << value.text << " : " << value.type << "\n";
            return;
        }
        builder.out << pad << "store <unsupported> <- " << value.text << "\n";
        return;
    }

    if (auto* give = dynamic_cast<const GiveStmt*>(stmt)) {
        if (!give->value) {
            builder.out << pad << "return unit : Unit\n";
            return;
        }
        const auto value = lowerExpr(sema, give->value.get(), builder, indent);
        builder.out << pad << "return " << value.text << " : " << value.type << "\n";
        return;
    }

    if (auto* exprStmt = dynamic_cast<const ExprStmt*>(stmt)) {
        const auto value = lowerExpr(sema, exprStmt->expr.get(), builder, indent);
        if (!value.isUnit) {
            builder.out << pad << "discard " << value.text << " : " << value.type << "\n";
        }
        return;
    }

    if (auto* when = dynamic_cast<const WhenStmt*>(stmt)) {
        const auto condition = lowerExpr(sema, when->condition.get(), builder, indent);
        const std::string thenLabel = builder.blockName("when_then");
        const std::string joinLabel = builder.blockName("when_join");
        if (when->elseBlock) {
            const std::string elseLabel = builder.blockName("when_else");
            builder.out << pad << "branch " << condition.text << " -> " << thenLabel << ", " << elseLabel << "\n";
            emitBlock(sema, when->thenBlock.get(), thenLabel, builder, indent - 1, joinLabel, emitter);
            emitBlock(sema, when->elseBlock.get(), elseLabel, builder, indent - 1, joinLabel, emitter);
            if (!emitter.blockDefinitelyTerminates(when->thenBlock.get()) ||
                !emitter.blockDefinitelyTerminates(when->elseBlock.get())) {
                builder.out << indentText(indent - 1) << "block " << joinLabel << ":\n";
            }
        } else {
            builder.out << pad << "branch " << condition.text << " -> " << thenLabel << ", " << joinLabel << "\n";
            emitBlock(sema, when->thenBlock.get(), thenLabel, builder, indent - 1, joinLabel, emitter);
            builder.out << indentText(indent - 1) << "block " << joinLabel << ":\n";
        }
        return;
    }

    if (auto* loop = dynamic_cast<const LoopStmt*>(stmt)) {
        const std::string headerLabel = builder.blockName("loop_header");
        const std::string bodyLabel = builder.blockName("loop_body");
        const std::string exitLabel = builder.blockName("loop_exit");
        builder.out << pad << "goto " << headerLabel << "\n";
        builder.out << indentText(indent - 1) << "block " << headerLabel << ":\n";
        if (loop->condition) {
            const auto condition = lowerExpr(sema, loop->condition.get(), builder, indent);
            builder.out << pad << "branch " << condition.text << " -> " << bodyLabel << ", " << exitLabel << "\n";
        } else {
            builder.out << pad << "goto " << bodyLabel << "\n";
        }
        emitBlock(sema, loop->body.get(), bodyLabel, builder, indent - 1, headerLabel, emitter);
        builder.out << indentText(indent - 1) << "block " << exitLabel << ":\n";
        return;
    }

    if (auto* scan = dynamic_cast<const ScanStmt*>(stmt)) {
        const auto iterable = lowerExpr(sema, scan->iterable.get(), builder, indent);
        const auto itemIt = sema.result().scanItemTypes.find(scan);
        const std::string itemType = itemIt != sema.result().scanItemTypes.end()
            ? itemIt->second.describe()
            : std::string("<unknown>");
        const std::string bodyLabel = builder.blockName("scan_body");
        const std::string exitLabel = builder.blockName("scan_exit");
        builder.out << pad << "scan " << scan->itemName << ": " << itemType << " over " << iterable.text
                    << " -> " << bodyLabel << ", " << exitLabel << "\n";
        emitBlock(sema, scan->body.get(), bodyLabel, builder, indent - 1, exitLabel, emitter);
        builder.out << indentText(indent - 1) << "block " << exitLabel << ":\n";
        return;
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        const auto value = lowerExpr(sema, pick->value.get(), builder, indent);
        const std::string joinLabel = builder.blockName("pick_join");
        bool needsJoin = false;
        builder.out << pad << "pick " << value.text << " : " << value.type << "\n";
        std::vector<std::pair<const PickBranch*, std::string>> branchLabels;
        for (const auto& branch : pick->branches) {
            branchLabels.push_back({&branch, builder.blockName("pick_" + branch.tag)});
            builder.out << pad << "case " << branch.tag;
            if (!branch.bindings.empty()) {
                builder.out << "(";
                for (size_t i = 0; i < branch.bindings.size(); ++i) {
                    if (i > 0) {
                        builder.out << ", ";
                    }
                    builder.out << branch.bindings[i];
                }
                builder.out << ")";
            }
            builder.out << " -> " << branchLabels.back().second << "\n";
            needsJoin = needsJoin || !emitter.blockDefinitelyTerminates(branch.body.get());
        }
        for (const auto& [branch, label] : branchLabels) {
            emitBlock(sema, branch->body.get(), label, builder, indent - 1, needsJoin ? joinLabel : std::string{}, emitter);
        }
        if (needsJoin) {
            builder.out << indentText(indent - 1) << "block " << joinLabel << ":\n";
        }
        return;
    }

    if (auto* lift = dynamic_cast<const LiftStmt*>(stmt)) {
        const auto value = lowerExpr(sema, lift->expr.get(), builder, indent);
        const std::string failLabel = builder.blockName("lift_fail");
        builder.out << pad << "lift " << value.text << " -> " << lift->valueName << ", fail " << failLabel << "\n";
        emitBlock(sema, lift->failBlock.get(), failLabel, builder, indent - 1, {}, emitter);
        return;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        const std::string rawLabel = builder.blockName("raw");
        builder.out << pad << "goto " << rawLabel << "\n";
        emitBlock(sema, raw->body.get(), rawLabel, builder, indent - 1, {}, emitter);
        return;
    }

    if (dynamic_cast<const StopStmt*>(stmt)) {
        builder.out << pad << "stop\n";
        return;
    }

    if (dynamic_cast<const SkipStmt*>(stmt)) {
        builder.out << pad << "skip\n";
        return;
    }
}

void emitBlock(
    const SemanticAnalyzer& sema,
    const BlockStmt* block,
    const std::string& label,
    FunctionBuilder& builder,
    int indent,
    const std::string& fallthroughLabel,
    const OirEmitter& emitter) {
    builder.out << indentText(indent) << "block " << label << ":\n";
    if (!block) {
        if (!fallthroughLabel.empty()) {
            builder.out << indentText(indent + 1) << "goto " << fallthroughLabel << "\n";
        }
        return;
    }

    for (const auto& stmt : block->statements) {
        emitStmt(sema, stmt.get(), builder, indent + 1, emitter);
        if (emitter.stmtDefinitelyTerminates(stmt.get())) {
            return;
        }
    }

    if (!fallthroughLabel.empty()) {
        builder.out << indentText(indent + 1) << "goto " << fallthroughLabel << "\n";
    }
}

} // namespace

OirEmitter::OirEmitter(const SemanticAnalyzer& sema) : sema(sema) {}

std::string OirEmitter::emit(const RealmDecl* realm) const {
    if (!realm) {
        return "oir.realm <unknown>\n";
    }

    std::ostringstream out;
    out << "oir.realm " << realm->name << "\n";
    for (const auto& decl : realm->declarations) {
        out << emitDecl(decl.get());
    }
    return out.str();
}

std::string OirEmitter::emitDecl(const Decl* decl) const {
    if (auto* fn = dynamic_cast<const FnDecl*>(decl)) {
        return emitFn(fn);
    }
    if (auto* shape = dynamic_cast<const ShapeDecl*>(decl)) {
        return emitShape(shape);
    }
    if (auto* choice = dynamic_cast<const ChoiceDecl*>(decl)) {
        return emitChoice(choice);
    }
    return {};
}

std::string OirEmitter::emitFn(const FnDecl* fn) const {
    std::ostringstream out;
    const auto* signature = sema.lookupFunctionSignature(fn);
    out << "oir.fn " << fn->name << "(";
    for (size_t i = 0; i < fn->params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << fn->params[i].name << ": "
            << (signature && i < signature->paramTypes.size() ? signature->paramTypes[i].describe() : "<unknown>");
    }
    out << ") -> " << (signature ? signature->returnType.describe() : "<unknown>") << "\n";

    FunctionBuilder builder;
    emitBlock(sema, fn->body.get(), "entry", builder, 1, {}, *this);
    out << builder.out.str();
    return out.str();
}

std::string OirEmitter::emitShape(const ShapeDecl* shape) const {
    std::ostringstream out;
    out << "oir.shape " << shape->name << "\n";
    const auto* info = sema.lookupShape(shape->name);
    if (!info) {
        return out.str();
    }

    for (const auto& fieldName : info->fieldOrder) {
        const auto it = info->fields.find(fieldName);
        if (it == info->fields.end()) {
            continue;
        }
        out << "  field " << fieldName << ": " << it->second.describe() << "\n";
    }
    return out.str();
}

std::string OirEmitter::emitChoice(const ChoiceDecl* choice) const {
    std::ostringstream out;
    out << "oir.choice " << choice->name << "\n";
    const auto* info = sema.lookupChoice(choice->name);
    if (!info) {
        return out.str();
    }

    for (const auto& variantName : info->variantOrder) {
        out << "  case " << variantName;
        const auto it = info->variants.find(variantName);
        if (it != info->variants.end() && !it->second.payloadTypes.empty()) {
            out << "(";
            for (size_t i = 0; i < it->second.payloadTypes.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << it->second.payloadTypes[i].describe();
            }
            out << ")";
        }
        out << "\n";
    }
    return out.str();
}

bool OirEmitter::blockDefinitelyTerminates(const BlockStmt* block) const {
    if (!block) {
        return false;
    }

    for (const auto& stmt : block->statements) {
        if (stmtDefinitelyTerminates(stmt.get())) {
            return true;
        }
    }
    return false;
}

bool OirEmitter::stmtDefinitelyTerminates(const Stmt* stmt) const {
    if (dynamic_cast<const GiveStmt*>(stmt) || dynamic_cast<const StopStmt*>(stmt) || dynamic_cast<const SkipStmt*>(stmt)) {
        return true;
    }

    if (auto* when = dynamic_cast<const WhenStmt*>(stmt)) {
        return when->elseBlock && blockDefinitelyTerminates(when->thenBlock.get()) &&
               blockDefinitelyTerminates(when->elseBlock.get());
    }

    if (auto* pick = dynamic_cast<const PickStmt*>(stmt)) {
        if (pick->branches.empty()) {
            return false;
        }
        for (const auto& branch : pick->branches) {
            if (!blockDefinitelyTerminates(branch.body.get())) {
                return false;
            }
        }
        return true;
    }

    if (auto* raw = dynamic_cast<const RawStmt*>(stmt)) {
        return blockDefinitelyTerminates(raw->body.get());
    }

    return false;
}

} // namespace claw::frontend
std::string claw::frontend::emitOirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    std::ostringstream out;
    out << "oir.program entry " << (entryRealm.empty() ? "<unknown>" : std::string(entryRealm)) << "\n";

    for (size_t i = 0; i < units.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }

        const auto& unit = units[i];
        if (!unit.realm || !unit.sema) {
            out << "oir.realm <unknown>\n";
            continue;
        }

        OirEmitter emitter(*unit.sema);
        out << emitter.emit(unit.realm);
    }

    return out.str();
}