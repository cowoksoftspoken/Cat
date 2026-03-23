#include "driver/native_build.h"

#include "backend/llvm_ir.h"

#include <fstream>
#include <process.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string sanitizeStem(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(c);
        } else if (out.empty() || out.back() != '_') {
            out.push_back('_');
        }
    }
    return out.empty() ? std::string("main") : out;
}

std::wstring widen(const std::filesystem::path& path) {
    return path.wstring();
}

std::filesystem::path makeTempLlPath() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const int pid = _getpid();
    for (int attempt = 0; attempt < 256; ++attempt) {
        const auto candidate = dir / ("claw-native-" + std::to_string(pid) + "-" + std::to_string(attempt) + ".ll");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("Unable to allocate a temporary LLVM IR path for native build.");
}

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to write temporary file '" + path.string() + "'.");
    }
    out << content;
    if (!out.good()) {
        throw std::runtime_error("Failed to flush temporary file '" + path.string() + "'.");
    }
}

std::filesystem::path locateRuntimeSource(const std::filesystem::path& compilerBinaryPath) {
    const auto normalizedCompiler = std::filesystem::absolute(compilerBinaryPath);
    const auto exeDir = normalizedCompiler.parent_path();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "runtime" / "native_runtime.c",
        exeDir.parent_path() / "runtime" / "native_runtime.c",
        exeDir.parent_path().parent_path() / "runtime" / "native_runtime.c",
        std::filesystem::current_path() / "runtime" / "native_runtime.c",
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }

    throw std::runtime_error(
        "Unable to locate bundled native runtime source 'runtime/native_runtime.c' relative to compiler executable.");
}

int runClangLink(
    const std::filesystem::path& llvmPath,
    const std::filesystem::path& runtimePath,
    const std::filesystem::path& outputPath) {
    const std::vector<std::wstring> argsStorage = {
        L"clang",
        L"-O2",
        L"-std=c11",
        widen(llvmPath),
        widen(runtimePath),
        L"-o",
        widen(outputPath),
    };

    std::vector<const wchar_t*> argv;
    argv.reserve(argsStorage.size() + 1);
    for (const auto& arg : argsStorage) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    const int result = _wspawnvp(_P_WAIT, L"clang", argv.data());
    if (result == -1) {
        throw std::runtime_error("Failed to launch `clang` from PATH while building native executable.");
    }
    return result;
}

} // namespace

namespace claw::driver {

std::filesystem::path defaultNativeOutputPath(const claw::frontend::LoadedProject& project) {
    const std::string preferredName =
        project.config.has_value() && !project.config->name.empty()
            ? project.config->name
            : project.units[project.entryIndex].path.stem().string();
    return project.packageRoot / "build" / (sanitizeStem(preferredName) + ".exe");
}

std::filesystem::path buildNativeExecutable(
    const claw::frontend::LoadedProject& project,
    std::string_view entryRealm,
    const std::vector<claw::frontend::OirUnitView>& units,
    const std::filesystem::path& compilerBinaryPath,
    const std::filesystem::path& outputPath) {
    (void)project;
    const auto runtimePath = locateRuntimeSource(compilerBinaryPath);
    const auto absoluteOutput = std::filesystem::absolute(outputPath);
    const auto outputDir = absoluteOutput.parent_path();
    if (!outputDir.empty()) {
        std::filesystem::create_directories(outputDir);
    }

    const auto tempLlPath = makeTempLlPath();
    const std::string llvmIr = claw::frontend::emitNativeLlvmIr(entryRealm, units);
    writeTextFile(tempLlPath, llvmIr);

    const int result = runClangLink(tempLlPath, runtimePath, absoluteOutput);
    if (result != 0) {
        throw std::runtime_error(
            "Native build failed while invoking clang. Temporary LLVM IR was left at '" + tempLlPath.string() + "'.");
    }

    std::filesystem::remove(tempLlPath);
    return absoluteOutput;
}

} // namespace claw::driver
