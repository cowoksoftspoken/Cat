#include "ir/lir.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

std::string formatValue(const LirValue& value) {
    return value.text;
}

LirValue lowerValue(const OirValue& value) {
    return LirValue{value.text, value.type, value.isUnit};
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

bool isOpaqueExternalType(std::string_view type);

std::string sanitizeHookSegment(std::string_view text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            sanitized.push_back(c);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "unknown";
    }
    return sanitized;
}

std::optional<LirExternalCallInfo> lowerExternalInfo(const OirCallInst& call) {
    if (call.externalInfo.has_value()) {
        const auto& info = *call.externalInfo;
        return LirExternalCallInfo{info.dependencyRoot, info.abi, info.linkageName, info.rawOnly, info.opaqueResult};
    }

    if (!isOpaqueExternalType(call.type)) {
        return std::nullopt;
    }

    std::string dependencyRoot = receiverSegment(call.callee);
    std::string linkageName = tailSegment(call.callee);
    if (dependencyRoot.empty()) {
        dependencyRoot = linkageName;
    }
    return LirExternalCallInfo{dependencyRoot, "opaque", linkageName, true, true};
}

std::string externalInfoSuffix(const std::optional<LirExternalCallInfo>& info) {
    if (!info.has_value()) {
        return {};
    }

    std::ostringstream out;
    out << " [abi=" << (info->abi.empty() ? std::string("unknown") : info->abi)
        << " dep=" << (info->dependencyRoot.empty() ? std::string("<external>") : info->dependencyRoot)
        << " symbol=" << (info->linkageName.empty() ? std::string("<callee>") : info->linkageName)
        << (info->rawOnly ? " raw" : " safe");
    if (info->opaqueResult) {
        out << " opaque-result";
    }
    out << ']';
    return out.str();
}

std::string formatSymbolLinkInfo(const SymbolLinkInfo& info) {
    std::ostringstream out;
    out << " [abi=" << (info.abi.empty() ? std::string("unknown") : info.abi)
        << " link=" << describeLinkageKind(info.linkage)
        << " symbol=" << (info.symbol.empty() ? std::string("<unknown>") : info.symbol)
        << " ffi=" << (info.ffiStable ? "true" : "false") << ']';
    return out.str();
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

std::string formatParam(const LirParam& param) {
    return param.name + ": " + param.type + " [" + describeAbiPassKind(param.passKind) + "]";
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

bool isRuntimeCall(std::string_view callee) {
    return callee == "print" || callee == "println" || callee.starts_with("runtime.");
}

bool isKnownBuiltinMethod(std::string_view name) {
    static const std::unordered_set<std::string> methods = {
        "len",
        "is_empty",
        "new",
        "get",
        "byte_at",
        "first_byte",
        "last_byte",
        "find_byte",
        "count_byte",
        "starts_with",
        "ends_with",
        "contains",
        "contains_byte",
        "slice",
        "capacity",
        "has_capacity",
        "reserve",
        "truncate",
        "shrink_to_fit",
        "clear",
    };
    return methods.contains(std::string(name));
}

bool requiresBoundsCheck(std::string_view name) {
    static const std::unordered_set<std::string> methods = {
        "byte_at",
        "first_byte",
        "last_byte",
        "slice",
        "truncate",
    };
    return methods.contains(std::string(name));
}

bool isOpaqueExternalType(std::string_view type) {
    return type.starts_with("opaque external result");
}

LirCallKind classifyCallKind(const OirCallInst& call, const std::unordered_set<std::string>& knownFunctions) {
    if (isRuntimeCall(call.callee)) {
        return LirCallKind::Runtime;
    }

    const std::string tail = tailSegment(call.callee);
    if (call.callee.find('.') != std::string::npos && isKnownBuiltinMethod(tail)) {
        return LirCallKind::Builtin;
    }

    if (call.externalInfo.has_value() || isOpaqueExternalType(call.type)) {
        return LirCallKind::External;
    }

    if (knownFunctions.contains(call.callee)) {
        return LirCallKind::Direct;
    }

    if (call.callee.find('.') != std::string::npos && knownFunctions.contains(tail)) {
        return LirCallKind::ModuleDirect;
    }

    if (call.type == "<unknown>") {
        return LirCallKind::Foreign;
    }

    return LirCallKind::Foreign;
}

LirSafetyTag classifyCallSafety(const OirCallInst& call) {
    if (isRuntimeCall(call.callee)) {
        return LirSafetyTag::ProvenSafe;
    }

    if (call.externalInfo.has_value()) {
        return (call.externalInfo->rawOnly || call.externalInfo->opaqueResult)
            ? LirSafetyTag::UnsafeBoundary
            : LirSafetyTag::ProvenSafe;
    }

    if (isOpaqueExternalType(call.type) || call.type == "<unknown>") {
        return LirSafetyTag::UnsafeBoundary;
    }

    const std::string tail = tailSegment(call.callee);
    if (!isKnownBuiltinMethod(tail)) {
        return LirSafetyTag::None;
    }
    return requiresBoundsCheck(tail) ? LirSafetyTag::BoundsCheckRequired : LirSafetyTag::ProvenSafe;
}

std::string classifyHook(const OirCallInst& call, LirCallKind kind) {
    const std::string tail = tailSegment(call.callee);
    switch (kind) {
    case LirCallKind::Runtime:
        return std::string("runtime.") + tail;
    case LirCallKind::Builtin:
        return std::string("builtin.") + tail;
    case LirCallKind::External: {
        const auto info = lowerExternalInfo(call);
        const std::string abi = sanitizeHookSegment(info && !info->abi.empty() ? info->abi : std::string("unknown"));
        const std::string dependencyRoot = sanitizeHookSegment(
            info && !info->dependencyRoot.empty() ? info->dependencyRoot : tail);
        const std::string linkageName = sanitizeHookSegment(
            info && !info->linkageName.empty() ? info->linkageName : tail);
        return std::string("external.") + abi + "." + dependencyRoot + "." + linkageName;
    }
    case LirCallKind::Foreign:
        return std::string("foreign.") + sanitizeHookSegment(call.callee);
    case LirCallKind::Direct:
    case LirCallKind::ModuleDirect:
        return {};
    }
    return {};
}

std::string callKindName(LirCallKind kind) {
    switch (kind) {
    case LirCallKind::Direct:
        return "direct";
    case LirCallKind::ModuleDirect:
        return "module";
    case LirCallKind::Runtime:
        return "runtime";
    case LirCallKind::Builtin:
        return "builtin";
    case LirCallKind::External:
        return "external";
    case LirCallKind::Foreign:
        return "foreign";
    }
    return "direct";
}

std::string safetySuffix(LirSafetyTag tag) {
    switch (tag) {
    case LirSafetyTag::None:
        return {};
    case LirSafetyTag::ProvenSafe:
        return " [proven-safe]";
    case LirSafetyTag::BoundsCheckRequired:
        return " [bounds-check]";
    case LirSafetyTag::UnsafeBoundary:
        return " [unsafe-boundary]";
    }
    return {};
}

bool hasPlaceholderTarget(std::string_view label) {
    return label.empty() || label.front() == '<';
}

void addEdge(
    std::unordered_map<std::string, std::vector<std::string>>& predecessors,
    std::string_view target,
    std::string_view source) {
    if (hasPlaceholderTarget(target)) {
        return;
    }

    auto& inputs = predecessors[std::string(target)];
    const std::string sourceName(source);
    if (std::find(inputs.begin(), inputs.end(), sourceName) == inputs.end()) {
        inputs.push_back(std::move(sourceName));
    }
}

void populatePredecessors(LirFunction& function) {
    std::unordered_map<std::string, std::vector<std::string>> predecessors;

    for (const auto& block : function.blocks) {
        for (const auto& inst : block.insts) {
            std::visit(Overloaded{
                [&](const LirBranchInst& value) {
                    addEdge(predecessors, value.trueLabel, block.label);
                    addEdge(predecessors, value.falseLabel, block.label);
                },
                [&](const LirGotoInst& value) {
                    addEdge(predecessors, value.targetLabel, block.label);
                },
                [&](const LirIterNextInst& value) {
                    addEdge(predecessors, value.bodyLabel, block.label);
                    addEdge(predecessors, value.exitLabel, block.label);
                },
                [&](const LirSwitchInst& value) {
                    for (const auto& item : value.cases) {
                        addEdge(predecessors, item.targetLabel, block.label);
                    }
                    addEdge(predecessors, value.defectLabel, block.label);
                },
                [&](const LirBoundsCheckInst& value) {
                    addEdge(predecessors, value.failLabel, block.label);
                },
                [&](const LirLiftInst& value) {
                    if (!value.successLabel.empty()) {
                        addEdge(predecessors, value.successLabel, block.label);
                    }
                    addEdge(predecessors, value.failLabel, block.label);
                },
                [&](const LirBreakInst& value) {
                    addEdge(predecessors, value.targetLabel, block.label);
                },
                [&](const LirContinueInst& value) {
                    addEdge(predecessors, value.targetLabel, block.label);
                },
                [&](const auto&) {}
            }, inst);
        }
    }

    int nextPhi = 0;
    for (auto& block : function.blocks) {
        const auto it = predecessors.find(block.label);
        if (it != predecessors.end()) {
            block.predecessors = it->second;
        }
        if (block.predecessors.size() <= 1) {
            continue;
        }

        LirPhiInst join;
        join.result = "%phi" + std::to_string(nextPhi++);
        join.type = "Control";
        for (const auto& pred : block.predecessors) {
            join.inputs.push_back(LirPhiInput{pred, LirValue{"ctrl." + pred, "Control", false}});
        }
        block.insts.insert(block.insts.begin(), LirInst{std::move(join)});
    }
}

LirFunction lowerFunction(const OirFunction& fn, const std::unordered_set<std::string>& knownFunctions) {
    LirFunction lowered;
    lowered.name = fn.name;
    lowered.returnType = fn.returnType;
    lowered.returnPassKind = fn.returnPassKind;
    lowered.linkage = fn.linkage;
    lowered.params.reserve(fn.params.size());
    for (const auto& param : fn.params) {
        lowered.params.push_back(LirParam{param.name, param.type, param.passKind});
    }

    int nextDefect = 0;
    int nextLift = 0;
    std::vector<LirBlock> defectBlocks;
    lowered.blocks.reserve(fn.blocks.size());

    for (const auto& block : fn.blocks) {
        LirBlock currentBlock;
        currentBlock.label = block.label;
        currentBlock.rawRegion = block.rawRegion;

        for (size_t instIndex = 0; instIndex < block.insts.size(); ++instIndex) {
            const bool hasTrailingInsts = instIndex + 1 < block.insts.size();
            const auto& inst = block.insts[instIndex];
            std::visit(Overloaded{
                [&](const OirHoldInst& value) {
                    currentBlock.insts.push_back(LirObjectInst{value.isMutable, value.name, value.type, "stack"});
                    if (value.init.has_value()) {
                        currentBlock.insts.push_back(LirStoreInst{value.name, lowerValue(*value.init)});
                    }
                },
                [&](const OirStoreInst& value) {
                    currentBlock.insts.push_back(LirStoreInst{value.target, lowerValue(value.value)});
                },
                [&](const OirStoreFieldInst& value) {
                    currentBlock.insts.push_back(LirStoreFieldInst{
                        lowerValue(value.object),
                        value.field,
                        lowerValue(value.value),
                        LirSafetyTag::ProvenSafe,
                    });
                },
                [&](const OirReturnInst& value) {
                    currentBlock.insts.push_back(LirReturnInst{lowerValue(value.value)});
                },
                [&](const OirDiscardInst& value) {
                    currentBlock.insts.push_back(LirDiscardInst{lowerValue(value.value)});
                },
                [&](const OirDropInst& value) {
                    currentBlock.insts.push_back(LirDropInst{value.name, value.type});
                },
                [&](const OirBranchInst& value) {
                    currentBlock.insts.push_back(LirBranchInst{lowerValue(value.condition), value.trueLabel, value.falseLabel});
                },
                [&](const OirGotoInst& value) {
                    currentBlock.insts.push_back(LirGotoInst{value.targetLabel});
                },
                [&](const OirScanInst& value) {
                    currentBlock.insts.push_back(LirIterNextInst{value.itemName, value.itemType, lowerValue(value.iterable), value.bodyLabel, value.exitLabel});
                },
                [&](const OirPickInst& value) {
                    LirSwitchInst loweredSwitch;
                    loweredSwitch.value = lowerValue(value.value);
                    for (const auto& item : value.cases) {
                        loweredSwitch.cases.push_back(LirSwitchCase{item.tag, item.bindings, item.targetLabel});
                    }
                    const std::string defectLabel = currentBlock.label + ".defect.pick." + std::to_string(nextDefect++);
                    loweredSwitch.defectLabel = defectLabel;
                    currentBlock.insts.push_back(std::move(loweredSwitch));

                    LirBlock defectBlock;
                    defectBlock.label = defectLabel;
                    defectBlock.insts.push_back(LirDefectInst{"unreachable_choice", "switch reached no matching case"});
                    defectBlocks.push_back(std::move(defectBlock));
                },
                [&](const OirLiftInst& value) {
                    std::string successLabel;
                    if (hasTrailingInsts) {
                        successLabel = currentBlock.label + ".lift_ok." + std::to_string(nextLift++);
                    }
                    currentBlock.insts.push_back(LirLiftInst{
                        lowerValue(value.value),
                        value.okName,
                        value.failName,
                        successLabel,
                        value.failLabel,
                        value.autoPropagate});
                    if (!successLabel.empty()) {
                        lowered.blocks.push_back(std::move(currentBlock));
                        currentBlock = LirBlock{};
                        currentBlock.label = successLabel;
                        currentBlock.rawRegion = block.rawRegion;
                    }
                },
                [&](const OirChoiceMakeInst& value) {
                    std::vector<LirValue> payloads;
                    payloads.reserve(value.payloads.size());
                    for (const auto& payload : value.payloads) {
                        payloads.push_back(lowerValue(payload));
                    }
                    currentBlock.insts.push_back(LirChoiceMakeInst{value.result, value.type, value.variantName, std::move(payloads)});
                },
                [&](const OirStopInst& value) {
                    currentBlock.insts.push_back(LirBreakInst{value.targetLabel.empty() ? std::string("<unresolved-break>") : value.targetLabel});
                },
                [&](const OirSkipInst& value) {
                    currentBlock.insts.push_back(LirContinueInst{value.targetLabel.empty() ? std::string("<unresolved-continue>") : value.targetLabel});
                },
                [&](const OirCallInst& value) {
                    const LirCallKind kind = classifyCallKind(value, knownFunctions);
                    std::vector<LirValue> args;
                    args.reserve(value.args.size());
                    for (const auto& arg : value.args) {
                        args.push_back(lowerValue(arg));
                    }

                    LirSafetyTag safety = classifyCallSafety(value);
                    if (safety == LirSafetyTag::BoundsCheckRequired) {
                        const std::string defectLabel = currentBlock.label + ".defect.bounds." + std::to_string(nextDefect++);
                        currentBlock.insts.push_back(LirBoundsCheckInst{
                            tailSegment(value.callee),
                            [&]() {
                                const std::string receiver = receiverSegment(value.callee);
                                return receiver.empty() ? std::string("<receiver>") : receiver;
                            }(),
                            args,
                            defectLabel,
                        });

                        LirBlock defectBlock;
                        defectBlock.label = defectLabel;
                        defectBlock.insts.push_back(LirDefectInst{"bounds", tailSegment(value.callee) + " requires in-range access"});
                        defectBlocks.push_back(std::move(defectBlock));
                        safety = LirSafetyTag::ProvenSafe;
                    }

                    currentBlock.insts.push_back(LirCallInst{
                        value.result,
                        value.callee,
                        std::move(args),
                        value.type,
                        kind,
                        safety,
                        classifyHook(value, kind),
                        lowerExternalInfo(value),
                    });
                },
                [&](const OirFieldInst& value) {
                    currentBlock.insts.push_back(LirFieldInst{value.result, lowerValue(value.object), value.field, value.type, LirSafetyTag::ProvenSafe});
                },
                [&](const OirBinaryInst& value) {
                    currentBlock.insts.push_back(LirBinaryInst{value.result, value.op, lowerValue(value.left), lowerValue(value.right), value.type});
                }
            }, inst);
        }

        lowered.blocks.push_back(std::move(currentBlock));
    }

    for (auto& defect : defectBlocks) {
        lowered.blocks.push_back(std::move(defect));
    }

    populatePredecessors(lowered);
    return lowered;
}

std::unordered_set<std::string> collectKnownFunctions(const OirProgram& program) {
    std::unordered_set<std::string> names;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<OirFunction>(&decl)) {
                names.insert(fn->name);
            }
        }
    }
    return names;
}

std::string formatInst(const LirInst& inst, int indent) {
    const std::string pad = indentText(indent);
    return std::visit(Overloaded{
        [&](const LirObjectInst& value) {
            std::ostringstream out;
            out << pad << "alloc " << value.storage << ' ' << (value.isMutable ? "var " : "val ")
                << value.name << " : " << value.type << "\n";
            return out.str();
        },
        [&](const LirPhiInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = phi " << value.type << " [";
            for (size_t i = 0; i < value.inputs.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << value.inputs[i].label << ": " << formatValue(value.inputs[i].value);
            }
            out << "]\n";
            return out.str();
        },
        [&](const LirBoundsCheckInst& value) {
            std::ostringstream out;
            out << pad << "bounds_check " << value.kind << ' ' << value.subject << '(';
            for (size_t i = 0; i < value.args.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.args[i]);
            }
            out << ") fail -> " << value.failLabel << "\n";
            return out.str();
        },
        [&](const LirStoreInst& value) {
            std::ostringstream out;
            out << pad << "store " << value.target << " <- " << formatValue(value.value)
                << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const LirStoreFieldInst& value) {
            std::ostringstream out;
            out << pad << "store_field " << formatValue(value.object) << '.' << value.field << " <- "
                << formatValue(value.value) << " : " << value.value.type << safetySuffix(value.safety) << "\n";
            return out.str();
        },
        [&](const LirReturnInst& value) {
            std::ostringstream out;
            out << pad << "return " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const LirDiscardInst& value) {
            std::ostringstream out;
            out << pad << "discard " << formatValue(value.value) << " : " << value.value.type << "\n";
            return out.str();
        },
        [&](const LirDropInst& value) {
            std::ostringstream out;
            out << pad << "drop " << value.name << " : " << value.type << "\n";
            return out.str();
        },
        [&](const LirBranchInst& value) {
            std::ostringstream out;
            out << pad << "branch " << formatValue(value.condition) << " -> " << value.trueLabel << ", "
                << value.falseLabel << "\n";
            return out.str();
        },
        [&](const LirGotoInst& value) {
            return pad + "goto " + value.targetLabel + "\n";
        },
        [&](const LirIterNextInst& value) {
            std::ostringstream out;
            out << pad << "iter_next " << value.itemName << ": " << value.itemType << " over "
                << formatValue(value.iterable) << " -> " << value.bodyLabel << ", " << value.exitLabel << "\n";
            return out.str();
        },
        [&](const LirSwitchInst& value) {
            std::ostringstream out;
            out << pad << "switch " << formatValue(value.value) << " : " << value.value.type << "\n";
            for (const auto& item : value.cases) {
                out << pad << "case " << item.tag;
                if (!item.bindings.empty()) {
                    out << '(';
                    for (size_t i = 0; i < item.bindings.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.bindings[i];
                    }
                    out << ')';
                }
                out << " -> " << item.targetLabel << "\n";
            }
            out << pad << "default -> " << value.defectLabel << "\n";
            return out.str();
        },
        [&](const LirLiftInst& value) {
            std::ostringstream out;
            out << pad << "try " << formatValue(value.value) << " -> " << value.okName;
            if (!value.successLabel.empty()) {
                out << ", success " << value.successLabel;
            }
            if (value.autoPropagate) {
                out << ", propagate\n";
            } else {
                out << ", else " << value.failName << " -> " << value.failLabel << "\n";
            }
            return out.str();
        },
        [&](const LirChoiceMakeInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.variantName << '(';
            for (size_t i = 0; i < value.payloads.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.payloads[i]);
            }
            out << ") : " << value.type << "\n";
            return out.str();
        },
        [&](const LirBreakInst& value) {
            return pad + "break -> " + value.targetLabel + "\n";
        },
        [&](const LirContinueInst& value) {
            return pad + "continue -> " + value.targetLabel + "\n";
        },
        [&](const LirCallInst& value) {
            std::ostringstream out;
            if (value.result.has_value()) {
                out << pad << *value.result << " = ";
            } else {
                out << pad;
            }
            out << "call." << callKindName(value.kind) << ' ' << value.callee << '(';
            for (size_t i = 0; i < value.args.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << formatValue(value.args[i]);
            }
            out << ") : " << value.type;
            if (!value.hook.empty()) {
                out << " hook " << value.hook;
            }
            out << externalInfoSuffix(value.externalInfo) << safetySuffix(value.safety) << "\n";
            return out.str();
        },
        [&](const LirFieldInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = field " << formatValue(value.object) << '.' << value.field
                << " : " << value.type << safetySuffix(value.safety) << "\n";
            return out.str();
        },
        [&](const LirBinaryInst& value) {
            std::ostringstream out;
            out << pad << value.result << " = " << value.op << ' ' << formatValue(value.left) << ", "
                << formatValue(value.right) << " : " << value.type << "\n";
            return out.str();
        },
        [&](const LirDefectInst& value) {
            std::ostringstream out;
            out << pad << "defect " << value.kind;
            if (!value.detail.empty()) {
                out << ' ' << '"' << value.detail << '"';
            }
            out << "\n";
            return out.str();
        }
    }, inst);
}

std::string formatDecl(const LirDecl& decl) {
    return std::visit(Overloaded{
        [&](const LirFunction& fn) {
            std::ostringstream out;
            out << "lir.fn " << fn.name << '(';
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
                if (!block.predecessors.empty()) {
                    out << " [preds: ";
                    for (size_t i = 0; i < block.predecessors.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << block.predecessors[i];
                    }
                    out << ']';
                }
                out << ":\n";
                for (const auto& inst : block.insts) {
                    out << formatInst(inst, 2);
                }
            }
            return out.str();
        },
        [&](const LirShape& shape) {
            std::ostringstream out;
            out << "lir.shape " << shape.name << formatTypeParams(shape.typeParams)
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
        [&](const LirChoice& choice) {
            std::ostringstream out;
            out << "lir.choice " << choice.name << formatTypeParams(choice.typeParams)
                << formatSymbolLinkInfo(choice.linkage) << "\n";
            if (choice.layout.has_value()) {
                out << "  " << formatLayoutInfo(*choice.layout) << "\n";
            }
            for (const auto& item : choice.cases) {
                out << "  case " << item.name;
                if (!item.payloadTypes.empty()) {
                    out << '(';
                    for (size_t i = 0; i < item.payloadTypes.size(); ++i) {
                        if (i > 0) {
                            out << ", ";
                        }
                        out << item.payloadTypes[i];
                    }
                    out << ')';
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

} // namespace

LirProgram buildLirProgram(const OirProgram& program) {
    LirProgram lowered;
    lowered.entryRealm = program.entryRealm;
    lowered.entrySymbol = program.entrySymbol;

    const auto knownFunctions = collectKnownFunctions(program);
    lowered.realms.reserve(program.realms.size());
    for (const auto& realm : program.realms) {
        LirRealm loweredRealm;
        loweredRealm.name = realm.name;
        loweredRealm.decls.reserve(realm.decls.size());
        for (const auto& decl : realm.decls) {
            std::visit(Overloaded{
                [&](const OirFunction& fn) {
                    loweredRealm.decls.push_back(lowerFunction(fn, knownFunctions));
                },
                [&](const OirShape& shape) {
                    LirShape loweredShape;
                    loweredShape.name = shape.name;
                    loweredShape.typeParams = shape.typeParams;
                    loweredShape.linkage = shape.linkage;
                    loweredShape.layout = shape.layout;
                    loweredShape.fields.reserve(shape.fields.size());
                    for (const auto& field : shape.fields) {
                        loweredShape.fields.push_back(LirShapeField{field.name, field.type});
                    }
                    loweredRealm.decls.push_back(std::move(loweredShape));
                },
                [&](const OirChoice& choice) {
                    LirChoice loweredChoice;
                    loweredChoice.name = choice.name;
                    loweredChoice.typeParams = choice.typeParams;
                    loweredChoice.linkage = choice.linkage;
                    loweredChoice.layout = choice.layout;
                    loweredChoice.cases.reserve(choice.cases.size());
                    for (const auto& item : choice.cases) {
                        loweredChoice.cases.push_back(LirChoiceCase{item.name, item.payloadTypes});
                    }
                    loweredRealm.decls.push_back(std::move(loweredChoice));
                }
            }, decl);
        }
        lowered.realms.push_back(std::move(loweredRealm));
    }

    return lowered;
}

LirProgram buildLirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return buildLirProgram(buildOirProgram(entryRealm, units));
}

std::string formatLirRealm(const LirRealm& realm) {
    std::ostringstream out;
    out << "lir.module " << (realm.name.empty() ? std::string("<unknown>") : realm.name) << "\n";
    for (const auto& decl : realm.decls) {
        out << formatDecl(decl);
    }
    return out.str();
}

std::string formatLirProgram(const LirProgram& program) {
    std::ostringstream out;
    out << "lir.program entry " << (program.entryRealm.empty() ? std::string("<unknown>") : program.entryRealm)
        << " symbol " << (program.entrySymbol.empty() ? std::string("<unknown>::main") : program.entrySymbol) << "\n";
    for (size_t i = 0; i < program.realms.size(); ++i) {
        if (i > 0) {
            out << "\n";
        }
        out << formatLirRealm(program.realms[i]);
    }
    return out.str();
}

std::string emitLirProgram(const OirProgram& program) {
    return formatLirProgram(buildLirProgram(program));
}

std::string emitLirProgram(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return formatLirProgram(buildLirProgram(entryRealm, units));
}

} // namespace claw::frontend

