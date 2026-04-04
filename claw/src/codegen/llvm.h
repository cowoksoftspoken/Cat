#pragma once

#include "ir/lir.h"

#include <string>
#include <string_view>
#include <vector>

namespace claw::codegen {

std::string emitLlvmIr(const claw::frontend::LirProgram& program);
std::string emitLlvmIr(
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units);
std::string emitNativeLlvmIr(const claw::frontend::LirProgram& program);
std::string emitNativeLlvmIr(
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units);

} // namespace claw::codegen