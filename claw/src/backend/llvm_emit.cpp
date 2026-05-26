#include "backend/llvm_internal.h"

namespace claw::frontend {

std::string llvmFunctionLinkage(const SymbolLinkInfo& linkage) {
    switch (linkage.linkage) {
    case LinkageKind::Internal:
        return "internal ";
    case LinkageKind::Shared:
    case LinkageKind::Runtime:
    case LinkageKind::External:
        return {};
    }
    return {};
}

std::string LlvmEmitter::symbolForDirectCall(std::string_view callee) const {
    const auto directIt = functionsByName.find(std::string(callee));
    if (directIt != functionsByName.end()) {
        return quoteGlobal(directIt->second->linkage.symbol);
    }

    const std::string tail = tailSegment(callee);
    const auto tailIt = functionsByName.find(tail);
    if (tailIt != functionsByName.end()) {
        return quoteGlobal(tailIt->second->linkage.symbol);
    }

    throw std::runtime_error("Unable to resolve direct call target '" + std::string(callee) + "' in LLVM lowering.");
}

void LlvmEmitter::emitCallInst(const LirCallInst& value, FunctionState& state, std::vector<std::string>& lines) {
    if (value.kind == LirCallKind::Builtin) {
        emitBuiltinCall(value, state, lines);
        return;
    }

    const LirFunction* directTarget = nullptr;
    std::string calleeSymbol;
    switch (value.kind) {
    case LirCallKind::Direct:
    case LirCallKind::ModuleDirect:
        directTarget = lookupDirectFunction(value.callee);
        calleeSymbol = symbolForDirectCall(value.callee);
        break;
    case LirCallKind::Runtime:
        if (value.args.size() != 1) {
            throw std::runtime_error("LLVM lowering currently supports exactly one runtime call argument.");
        }
        calleeSymbol = runtimeSymbol(value.callee, value.args.front().type);
        break;
    case LirCallKind::External:
        calleeSymbol = externalSymbol(value);
        break;
    case LirCallKind::Builtin:
        throw std::runtime_error("LLVM lowering routed builtin call incorrectly for '" + value.callee + "'.");
    case LirCallKind::Foreign:
        throw std::runtime_error("LLVM lowering does not yet support foreign calls without typed contracts.");
    }

    std::vector<std::string> argTypes;
    std::vector<std::string> args;
    std::optional<std::string> resultSlot;

    if (directTarget != nullptr && usesIndirectAbi(directTarget->returnType, directTarget->returnPassKind)) {
        const std::string returnType = llvmType(directTarget->returnType);
        const auto returnLayout = abiLayoutForType(directTarget->returnType);
        const size_t returnAlign = returnLayout.has_value() ? returnLayout->align : 1;
        resultSlot = state.temp("call.ret.addr");
        lines.push_back("  " + *resultSlot + " = alloca " + returnType + ", align " + std::to_string(returnAlign));
        argTypes.push_back("ptr");
        args.push_back(*resultSlot);
    }

    for (size_t i = 0; i < value.args.size(); ++i) {
        const auto& arg = value.args[i];
        if (value.kind == LirCallKind::Runtime && usesRuntimePointerAbi(arg.type)) {
            argTypes.push_back(llvmRuntimeParamType(arg.type));
            args.push_back(ensureAddress(arg, state, lines));
            continue;
        }
        if (directTarget != nullptr && i < directTarget->params.size() && usesPointerAbi(directTarget->params[i].type, directTarget->params[i].passKind)) {
            argTypes.push_back("ptr");
            args.push_back(ensureAddress(arg, state, lines));
            continue;
        }
        argTypes.push_back(llvmType(arg.type));
        args.push_back(ensureValue(arg, state, lines));
    }
    std::ostringstream call;
    const std::string returnType = directTarget != nullptr ? llvmFunctionReturnType(*directTarget) : llvmType(value.type);
    if (value.result.has_value() && returnType != "void") {
        call << "  " << localName(*value.result) << " = ";
    } else {
        call << "  ";
    }
    call << "call " << returnType << " " << calleeSymbol << "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            call << ", ";
        }
        call << argTypes[i] << " " << args[i];
    }
    call << ")";
    lines.push_back(call.str());

    if (!value.result.has_value()) {
        return;
    }
    state.valueTypes[*value.result] = value.type;
    if (resultSlot.has_value()) {
        state.namedAddresses[*value.result] = *resultSlot;
        state.namedValues.erase(*value.result);
        state.params.erase(*value.result);
        return;
    }
    if (returnType != "void") {
        state.namedValues[*value.result] = localName(*value.result);
    }
}

void LlvmEmitter::emitStoreField(const LirStoreFieldInst& value, FunctionState& state, std::vector<std::string>& lines) {
    const auto* layoutField = findShapeFieldLayout(value.object.type, value.field);
    if (layoutField == nullptr) {
        throw std::runtime_error("LLVM lowering only supports field assignment on concrete shapes, got '" + value.object.type + "'.");
    }

    const std::string fieldAddress = emitFieldAddress(value.object, value.field, state, lines);
    const std::string operand = ensureValue(value.value, state, lines);
    storeValueToAddress(fieldAddress, value.value.type, operand, layoutField->alignBytes, lines);
}

std::string LlvmEmitter::emitFunction(const LirFunction& fn) {
    FunctionState state{fn};
    for (const auto& block : fn.blocks) {
        for (const auto& inst : block.insts) {
            if (const auto* iter = std::get_if<LirIterNextInst>(&inst)) {
                ensureIteratorState(block.label + "::" + iter->itemName, iter->itemName, iter->itemType, state);
            }
        }
    }

    std::ostringstream out;
    out << "define " << llvmFunctionLinkage(fn.linkage) << llvmFunctionReturnType(fn) << " "
        << quoteGlobal(fn.linkage.symbol) << "(";

    bool needsComma = false;
    if (usesIndirectAbi(fn.returnType, fn.returnPassKind)) {
        state.returnSlot = localName("ret.slot");
        out << "ptr " << *state.returnSlot;
        needsComma = true;
    }

    for (const auto& param : fn.params) {
        const bool pointerAbi = usesPointerAbi(param.type, param.passKind);
        const std::string paramName = pointerAbi
            ? localName("arg." + param.name + ".addr")
            : localName("arg." + param.name);
        state.valueTypes[param.name] = param.type;
        if (pointerAbi) {
            state.namedAddresses[param.name] = paramName;
        } else {
            state.params[param.name] = paramName;
        }

        if (needsComma) {
            out << ", ";
        }
        out << llvmParamAbiType(param) << " " << paramName;
        needsComma = true;
    }
    out << ") {\n";

    for (size_t blockIndex = 0; blockIndex < fn.blocks.size(); ++blockIndex) {
        const auto& block = fn.blocks[blockIndex];
        out << blockLabel(block.label) << ":\n";
        bool hasTerminator = false;
        std::vector<std::string> blockLines;
        if (blockIndex == 0) {
            for (const auto& line : state.entryAllocas) {
                blockLines.push_back(line);
            }
        }
        materializePendingBlockBindings(block.label, state, blockLines);

        for (const auto& inst : block.insts) {
            std::visit(Overloaded{
                [&](const LirObjectInst& value) {
                    const auto layout = abiLayoutForType(value.type);
                    const size_t align = layout.has_value() ? layout->align : 1;
                    const std::string pointerName = localName(value.name + ".addr");
                    state.stackPointers[value.name] = pointerName;
                    state.valueTypes[value.name] = value.type;
                    blockLines.push_back("  " + pointerName + " = alloca " + llvmType(value.type) + ", align " + std::to_string(align));
                },
                [&](const LirStoreInst& value) {
                    const auto targetIt = state.stackPointers.find(value.target);
                    if (targetIt == state.stackPointers.end()) {
                        throw std::runtime_error("LLVM lowering expected stack storage for '" + value.target + "'.");
                    }
                    const std::string targetType = valueTypeForName(value.target, state).empty()
                        ? value.value.type
                        : valueTypeForName(value.target, state);
                    const auto layout = abiLayoutForType(targetType);
                    const size_t align = layout.has_value() ? layout->align : 1;
                    const std::string operand = ensureValue(value.value, state, blockLines);
                    storeValueToAddress(targetIt->second, targetType, operand, align, blockLines);
                },
                [&](const LirReturnInst& value) {
                    if (state.returnSlot.has_value()) {
                        const auto layout = abiLayoutForType(fn.returnType);
                        const size_t align = layout.has_value() ? layout->align : 1;
                        const std::string operand = ensureValue(value.value, state, blockLines);
                        storeValueToAddress(*state.returnSlot, fn.returnType, operand, align, blockLines);
                        blockLines.push_back("  ret void");
                    } else {
                        const std::string returnType = llvmType(fn.returnType);
                        if (returnType == "void") {
                            blockLines.push_back("  ret void");
                        } else {
                            const std::string operand = ensureValue(value.value, state, blockLines);
                            blockLines.push_back("  ret " + returnType + " " + operand);
                        }
                    }
                    hasTerminator = true;
                },
                [&](const LirDiscardInst& value) {
                    (void)ensureValue(value.value, state, blockLines);
                },
                [&](const LirDropInst& value) {
                    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.type)));
                    if (baseType == "Anchor") {
                        const std::string freeSymbol = quoteGlobal("claw.runtime.anchor.free");
                        addRuntimeDecl("declare void " + freeSymbol + "(ptr)");
                        const std::string operand = ensureValue(LirValue{value.name, value.type, false}, state, blockLines);
                        blockLines.push_back("  call void " + freeSymbol + "(ptr " + operand + ")");
                    } else {
                        blockLines.push_back("  ; drop " + value.name + " : " + value.type);
                    }
                },
                [&](const LirBranchInst& value) {
                    std::string operand = ensureValue(value.condition, state, blockLines);
                    std::string conditionType = llvmType(value.condition.type);
                    if (conditionType != "i1") {
                        const std::string compareReg = state.temp("cond");
                        blockLines.push_back("  " + compareReg + " = icmp ne " + conditionType + " " + operand + ", 0");
                        operand = compareReg;
                    }
                    blockLines.push_back("  br i1 " + operand + ", label %" + blockLabel(value.trueLabel) + ", label %" + blockLabel(value.falseLabel));
                    hasTerminator = true;
                },
                [&](const LirGotoInst& value) {
                    blockLines.push_back("  br label %" + blockLabel(value.targetLabel));
                    hasTerminator = true;
                },
                [&](const LirCallInst& value) {
                    if (value.safety == LirSafetyTag::UnsafeBoundary) {
                        if (!block.rawRegion.has_value()) {
                            throw std::runtime_error(
                                "LLVM lowering encountered unsafe-boundary call outside raw region: '" + value.callee + "'.");
                        }
                        blockLines.push_back("  ; unsafe_boundary " + value.callee + " [raw: " + *block.rawRegion + "]");
                    }
                    emitCallInst(value, state, blockLines);
                },
                [&](const LirBinaryInst& value) {
                    const std::string resultType = llvmType(value.type);
                    const std::string operandType = llvmType(value.left.type);
                    const std::string left = ensureValue(value.left, state, blockLines);
                    const std::string right = ensureValue(value.right, state, blockLines);
                    const std::string baseType = canonicalBackendTypeBase(stripGenericArgs(stripViewPrefix(value.left.type)));
                    std::ostringstream line;
                    line << "  " << localName(value.result) << " = ";
                    if (value.op == "add") {
                        line << (isFloatingType(baseType) ? "fadd " : "add ") << resultType;
                    } else if (value.op == "sub") {
                        line << (isFloatingType(baseType) ? "fsub " : "sub ") << resultType;
                    } else if (value.op == "mul") {
                        line << (isFloatingType(baseType) ? "fmul " : "mul ") << resultType;
                    } else if (value.op == "div") {
                        if (isFloatingType(baseType)) {
                            line << "fdiv " << resultType;
                        } else {
                            line << (isSignedIntegerType(baseType) ? "sdiv " : "udiv ") << resultType;
                        }
                    } else if (value.op == "eq" || value.op == "neq" || value.op == "lt" || value.op == "lte" || value.op == "gt" || value.op == "gte") {
                        if (isFloatingType(baseType)) {
                            line << "fcmp ";
                            if (value.op == "eq") line << "oeq ";
                            else if (value.op == "neq") line << "one ";
                            else if (value.op == "lt") line << "olt ";
                            else if (value.op == "lte") line << "ole ";
                            else if (value.op == "gt") line << "ogt ";
                            else line << "oge ";
                        } else {
                            line << "icmp ";
                            if (value.op == "eq") line << "eq ";
                            else if (value.op == "neq") line << "ne ";
                            else if (value.op == "lt") line << (isSignedIntegerType(baseType) ? "slt " : "ult ");
                            else if (value.op == "lte") line << (isSignedIntegerType(baseType) ? "sle " : "ule ");
                            else if (value.op == "gt") line << (isSignedIntegerType(baseType) ? "sgt " : "ugt ");
                            else line << (isSignedIntegerType(baseType) ? "sge " : "uge ");
                        }
                        line << operandType;
                    } else {
                        throw std::runtime_error("LLVM lowering does not yet support binary op '" + value.op + "'.");
                    }
                    line << " " << left << ", " << right;
                    blockLines.push_back(line.str());
                    state.valueTypes[value.result] = value.type;
                    state.namedValues[value.result] = localName(value.result);
                },
                [&](const LirFieldInst& value) {
                    const auto* layoutField = findShapeFieldLayout(value.object.type, value.field);
                    if (layoutField == nullptr) {
                        throw std::runtime_error("LLVM lowering only supports field access on concrete shapes, got '" + value.object.type + "'.");
                    }
                    const std::string fieldAddress = emitFieldAddress(value.object, value.field, state, blockLines);
                    blockLines.push_back(
                        "  " + localName(value.result) + " = load " + llvmType(value.type) + ", ptr " + fieldAddress +
                        ", align " + std::to_string(layoutField->alignBytes));
                    state.valueTypes[value.result] = value.type;
                    state.namedValues[value.result] = localName(value.result);
                },
                [&](const LirPhiInst& value) {
                    state.valueTypes[value.result] = value.type;
                    blockLines.push_back("  ; phi " + value.result + " omitted in initial LLVM lowering");
                },
                [&](const LirDefectInst& value) {
                    needsTrap = true;
                    blockLines.push_back("  ; defect " + value.kind + " \"" + value.detail + "\"");
                    blockLines.push_back("  call void @llvm.trap()");
                    blockLines.push_back("  unreachable");
                    hasTerminator = true;
                },
                [&](const LirBoundsCheckInst& value) {
                    emitBoundsCheck(value, state, blockLines);
                },
                [&](const LirStoreFieldInst& value) {
                    emitStoreField(value, state, blockLines);
                },
                [&](const LirIterNextInst& value) {
                    emitIterNext(value, block.label, state, blockLines);
                    hasTerminator = true;
                },
                [&](const LirSwitchInst& value) {
                    emitSwitch(value, state, blockLines);
                    hasTerminator = true;
                },
                [&](const LirLiftInst& value) {
                    if (emitLift(value, state, blockLines)) {
                        hasTerminator = true;
                    }
                },
                [&](const LirChoiceMakeInst& value) {
                    emitChoiceMakeInst(value, state, blockLines);
                },
                [&](const LirBreakInst& value) {
                    blockLines.push_back("  br label %" + blockLabel(value.targetLabel));
                    hasTerminator = true;
                },
                [&](const LirContinueInst& value) {
                    blockLines.push_back("  br label %" + blockLabel(value.targetLabel));
                    hasTerminator = true;
                }
            }, inst);
        }

        for (const auto& line : blockLines) {
            out << line << "\n";
        }
        if (!hasTerminator) {
            const bool nextBlockAcceptsFallthrough =
                blockIndex + 1 < fn.blocks.size() &&
                std::find(
                    fn.blocks[blockIndex + 1].predecessors.begin(),
                    fn.blocks[blockIndex + 1].predecessors.end(),
                    block.label) != fn.blocks[blockIndex + 1].predecessors.end();
            if (nextBlockAcceptsFallthrough) {
                out << "  br label %" << blockLabel(fn.blocks[blockIndex + 1].label) << "\n";
            } else if (llvmFunctionReturnType(fn) == "void") {
                out << "  ret void\n";
            } else {
                out << "  unreachable\n";
            }
        }
    }

    out << "}\n";
    return out.str();
}

std::string LlvmEmitter::emitFunctionDefs() {
    std::ostringstream out;
    bool first = true;
    for (const auto& realm : program.realms) {
        for (const auto& decl : realm.decls) {
            if (const auto* fn = std::get_if<LirFunction>(&decl)) {
                if (!first) {
                    out << "\n";
                }
                first = false;
                out << emitFunction(*fn);
            }
        }
    }
    return out.str();
}

std::string LlvmEmitter::emitNativeEntryWrapper() const {
    const auto* entry = entryFunction();
    if (entry == nullptr) {
        throw std::runtime_error("LLVM lowering could not resolve the program entry function '" + program.entrySymbol + "'.");
    }

    std::ostringstream out;
    out << "define i32 @main() {\n";
    out << "entry:\n";

    const std::string entrySymbol = quoteGlobal(entry->linkage.symbol);
    if (llvmFunctionReturnType(*entry) == "void") {
        out << "  call void " << entrySymbol << "()\n";
        out << "  ret i32 0\n";
    } else {
        out << "  %entry.result.0 = call i32 " << entrySymbol << "()\n";
        out << "  ret i32 %entry.result.0\n";
    }

    out << "}\n";
    return out.str();
}

std::string LlvmEmitter::emit() {
    std::ostringstream out;
    out << "; ModuleID = 'claw'\n";
    out << "target triple = \"x86_64-w64-windows-gnu\"\n\n";
    out << "%claw.slice = type { ptr, i64 }\n";
    out << "%claw.buffer = type { ptr, i64, i64 }\n";

    const auto typeDecls = emitTypeDecls();
    if (!typeDecls.empty()) {
        out << typeDecls << "\n";
    }
    const auto globals = emitStringGlobals();
    if (!globals.empty()) {
        out << globals << "\n";
    }
    const auto definitions = emitFunctionDefs();
    const auto declarations = emitDeclarations();
    if (!declarations.empty()) {
        out << declarations << "\n\n";
    }
    out << definitions;
    if (emitNativeEntry) {
        out << "\n" << emitNativeEntryWrapper();
    }
    return out.str();
}

} // namespace claw::frontend




