#include "module/project.h"

#include "diagnostics/diagnostics.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace claw::frontend {

namespace {

enum class ConfigSection {
    None,
    Project,
    Dependencies,
};

std::string trim(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierContinue(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifier(std::string_view text) {
    if (text.empty() || !isIdentifierStart(text.front())) {
        return false;
    }
    for (size_t i = 1; i < text.size(); ++i) {
        if (!isIdentifierContinue(text[i])) {
            return false;
        }
    }
    return true;
}

std::string removeConfigComment(std::string_view line) {
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"' && !escaped) {
            inString = !inString;
        } else if (c == '#' && !inString) {
            return trim(line.substr(0, i));
        }

        escaped = (c == '\\' && !escaped);
        if (c != '\\') {
            escaped = false;
        }
    }

    return trim(line);
}

size_t findUnquotedChar(std::string_view text, char needle) {
    bool inString = false;
    bool escaped = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"' && !escaped) {
            inString = !inString;
        } else if (c == needle && !inString) {
            return i;
        }

        escaped = (c == '\\' && !escaped);
        if (c != '\\') {
            escaped = false;
        }
    }

    return std::string_view::npos;
}

bool isQuotedString(std::string_view value) {
    return value.size() >= 2 && value.front() == '"' && value.back() == '"';
}

std::string unquoteString(std::string_view value) {
    return std::string(value.substr(1, value.size() - 2));
}

bool isEditionLiteralValid(const std::string& edition) {
    return edition.size() == 4 &&
        std::all_of(edition.begin(), edition.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

void pushConfigDiagnostic(
    std::vector<Diagnostic>* diagnostics,
    const std::filesystem::path& path,
    size_t line,
    size_t column,
    size_t length,
    const std::string& message) {
    diagnostics->push_back(Diagnostic{
        "config",
        message,
        {line, column, std::max<size_t>(1, length)},
        path.string()});
}

ProjectConfig parseTomlConfig(
    const std::filesystem::path& path,
    const std::string& source,
    std::vector<Diagnostic>* diagnostics) {
    ProjectConfig config;
    config.path = path;

    ConfigSection currentSection = ConfigSection::None;
    bool sawProjectSection = false;
    bool sawDependenciesSection = false;
    std::unordered_set<std::string> seenProjectKeys;
    std::unordered_set<std::string> seenDependencies;

    std::istringstream lines(source);
    std::string rawLine;
    size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        const std::string line = removeConfigComment(rawLine);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Invalid TOML section header.");
                continue;
            }

            const std::string sectionName = trim(std::string_view(line).substr(1, line.size() - 2));
            if (sectionName == "project") {
                if (sawProjectSection) {
                    pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Duplicate [project] section.");
                }
                sawProjectSection = true;
                currentSection = ConfigSection::Project;
            } else if (sectionName == "dependencies") {
                if (sawDependenciesSection) {
                    pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Duplicate [dependencies] section.");
                }
                sawDependenciesSection = true;
                currentSection = ConfigSection::Dependencies;
            } else {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Unsupported config section '[" + sectionName + "]'.");
                currentSection = ConfigSection::None;
            }
            continue;
        }

        const size_t equalPos = findUnquotedChar(line, '=');
        if (equalPos == std::string::npos) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Expected 'key = value' in TOML config.");
            continue;
        }

        const std::string key = trim(std::string_view(line).substr(0, equalPos));
        const std::string value = trim(std::string_view(line).substr(equalPos + 1));
        const size_t valueColumn = equalPos + 2;

        if (currentSection == ConfigSection::None) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Config entries must be placed inside [project] or [dependencies].");
            continue;
        }

        if (currentSection == ConfigSection::Project) {
            if (!isIdentifier(key)) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, std::max<size_t>(1, equalPos), "Invalid project key: '" + key + "'.");
                continue;
            }
            if (key == "entry") {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Project config key 'entry' is not allowed. Root main.cat is the fixed workspace entry.");
                continue;
            }
            if (key != "name" && key != "version" && key != "edition") {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, std::max<size_t>(1, equalPos), "Unknown project config key: '" + key + "'.");
                continue;
            }
            if (!seenProjectKeys.insert(key).second) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, std::max<size_t>(1, equalPos), "Duplicate project config key: '" + key + "'.");
                continue;
            }
            if (!isQuotedString(value)) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, valueColumn, std::max<size_t>(1, value.size()), "Project config values must use quoted strings.");
                continue;
            }

            const std::string parsedValue = unquoteString(value);
            if (key == "name") {
                config.name = parsedValue;
            } else if (key == "version") {
                config.version = parsedValue;
            } else {
                config.edition = parsedValue;
            }
            continue;
        }

        if (!isIdentifier(key)) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, std::max<size_t>(1, equalPos), "Dependency names must be valid identifiers so they can be imported as module roots.");
            continue;
        }
        if (!seenDependencies.insert(key).second) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, std::max<size_t>(1, equalPos), "Duplicate dependency declaration: '" + key + "'.");
            continue;
        }
        if (value.empty()) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, valueColumn, 1, "Dependency values must be a quoted version string or an inline table.");
            continue;
        }
        if (isQuotedString(value)) {
            config.dependencies[key] = unquoteString(value);
            continue;
        }
        if (value.front() == '{' && value.back() == '}') {
            config.dependencies[key] = value;
            continue;
        }

        pushConfigDiagnostic(diagnostics, path, lineNumber, valueColumn, std::max<size_t>(1, value.size()), "Dependency values must be a quoted version string or an inline table.");
    }

    if (!sawProjectSection) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Workspace config must contain a [project] section.");
    }
    if (!sawDependenciesSection) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Workspace config must contain a [dependencies] section.");
    }
    if (config.name.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.name.");
    }
    if (config.version.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.version.");
    }
    if (config.edition.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.edition.");
    } else if (!isEditionLiteralValid(config.edition)) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project edition must be a quoted year like \"2025\".");
    }

    return config;
}

ProjectConfig parseYamlConfig(
    const std::filesystem::path& path,
    const std::string& source,
    std::vector<Diagnostic>* diagnostics) {
    ProjectConfig config;
    config.path = path;

    ConfigSection currentSection = ConfigSection::None;
    bool sawProjectSection = false;
    bool sawDependenciesSection = false;
    std::unordered_set<std::string> seenProjectKeys;
    std::unordered_set<std::string> seenDependencies;

    std::istringstream lines(source);
    std::string rawLine;
    size_t lineNumber = 0;
    while (std::getline(lines, rawLine)) {
        ++lineNumber;
        const std::string line = removeConfigComment(rawLine);
        if (line.empty()) {
            continue;
        }

        size_t indent = 0;
        while (indent < line.size() && line[indent] == ' ') {
            ++indent;
        }

        if (indent == 0) {
            if (line.back() != ':') {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Expected a top-level YAML section ending with ':'.");
                currentSection = ConfigSection::None;
                continue;
            }

            const std::string sectionName = trim(std::string_view(line).substr(0, line.size() - 1));
            if (sectionName == "project") {
                if (sawProjectSection) {
                    pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Duplicate project: section.");
                }
                sawProjectSection = true;
                currentSection = ConfigSection::Project;
            } else if (sectionName == "dependencies") {
                if (sawDependenciesSection) {
                    pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Duplicate dependencies: section.");
                }
                sawDependenciesSection = true;
                currentSection = ConfigSection::Dependencies;
            } else {
                pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Unsupported YAML section '" + sectionName + "'.");
                currentSection = ConfigSection::None;
            }
            continue;
        }

        if (currentSection == ConfigSection::None) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "Config entries must be placed inside project: or dependencies:.");
            continue;
        }
        if (indent < 2) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, 1, line.size(), "YAML config entries must be indented under their section.");
            continue;
        }

        const std::string entry = trim(std::string_view(line).substr(indent));
        const size_t colonPos = findUnquotedChar(entry, ':');
        if (colonPos == std::string::npos) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, entry.size(), "Expected 'key: value' in YAML config.");
            continue;
        }

        const std::string key = trim(std::string_view(entry).substr(0, colonPos));
        const std::string value = trim(std::string_view(entry).substr(colonPos + 1));
        const size_t valueColumn = indent + colonPos + 2;

        if (currentSection == ConfigSection::Project) {
            if (!isIdentifier(key)) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, std::max<size_t>(1, colonPos), "Invalid project key: '" + key + "'.");
                continue;
            }
            if (key == "entry") {
                pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, entry.size(), "Project config key 'entry' is not allowed. Root main.cat is the fixed workspace entry.");
                continue;
            }
            if (key != "name" && key != "version" && key != "edition") {
                pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, std::max<size_t>(1, colonPos), "Unknown project config key: '" + key + "'.");
                continue;
            }
            if (!seenProjectKeys.insert(key).second) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, std::max<size_t>(1, colonPos), "Duplicate project config key: '" + key + "'.");
                continue;
            }
            if (!isQuotedString(value)) {
                pushConfigDiagnostic(diagnostics, path, lineNumber, valueColumn, std::max<size_t>(1, value.size()), "Project config values must use quoted strings.");
                continue;
            }

            const std::string parsedValue = unquoteString(value);
            if (key == "name") {
                config.name = parsedValue;
            } else if (key == "version") {
                config.version = parsedValue;
            } else {
                config.edition = parsedValue;
            }
            continue;
        }

        if (!isIdentifier(key)) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, std::max<size_t>(1, colonPos), "Dependency names must be valid identifiers so they can be imported as module roots.");
            continue;
        }
        if (!seenDependencies.insert(key).second) {
            pushConfigDiagnostic(diagnostics, path, lineNumber, indent + 1, std::max<size_t>(1, colonPos), "Duplicate dependency declaration: '" + key + "'.");
            continue;
        }
        if (value.empty()) {
            config.dependencies[key] = "";
            continue;
        }
        if (isQuotedString(value) || value == "{}") {
            config.dependencies[key] = value == "{}" ? std::string{} : unquoteString(value);
            continue;
        }

        pushConfigDiagnostic(diagnostics, path, lineNumber, valueColumn, std::max<size_t>(1, value.size()), "Dependency values must be a quoted version string or {}.");
    }

    if (!sawProjectSection) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Workspace config must contain a project: section.");
    }
    if (!sawDependenciesSection) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Workspace config must contain a dependencies: section.");
    }
    if (config.name.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.name.");
    }
    if (config.version.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.version.");
    }
    if (config.edition.empty()) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project config must define project.edition.");
    } else if (!isEditionLiteralValid(config.edition)) {
        pushConfigDiagnostic(diagnostics, path, 1, 1, 1, "Project edition must be a quoted year like \"2025\".");
    }

    return config;
}

} // namespace

ProjectConfig ProjectLoader::loadProjectConfig(const std::filesystem::path& path) const {
    const auto normalized = normalizePath(path);
    const std::string source = readFileText(normalized);
    std::vector<Diagnostic> diagnostics;

    ProjectConfig config;
    const auto extension = normalized.extension().string();
    if (extension == ".toml") {
        config = parseTomlConfig(normalized, source, &diagnostics);
    } else if (extension == ".yaml" || extension == ".yml") {
        config = parseYamlConfig(normalized, source, &diagnostics);
    } else {
        throwDiagnostic(normalized, "config", "Unsupported workspace config file type.");
    }

    if (!diagnostics.empty()) {
        throwDiagnostics(std::move(diagnostics));
    }

    return config;
}

} // namespace claw::frontend
