#include "codegen/llvm_internal.h"

namespace claw::codegen {

std::string emitLlvmIr(const LirProgram& program) {
    return LlvmEmitter(program).emit();
}

std::string emitLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return emitLlvmIr(buildLirProgram(entryRealm, units));
}

std::string emitNativeLlvmIr(const LirProgram& program) {
    return LlvmEmitter(program, true).emit();
}

std::string emitNativeLlvmIr(std::string_view entryRealm, const std::vector<OirUnitView>& units) {
    return emitNativeLlvmIr(buildLirProgram(entryRealm, units));
}

} // namespace claw::codegen
