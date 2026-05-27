#pragma once

#include "ir/oir.h"
#include "workspace/workspace.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace claw::driver {

std::filesystem::path defaultNativeOutputPath(const claw::workspace::LoadedProject& project);

std::filesystem::path buildNativeExecutable(
    const claw::workspace::LoadedProject& project,
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units,
    const std::filesystem::path& compilerBinaryPath,
    const std::filesystem::path& outputPath);

} // namespace claw::driver
