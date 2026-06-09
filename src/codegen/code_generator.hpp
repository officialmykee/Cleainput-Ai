#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "ai/transformer.hpp"
#include "ai/tokenizer.hpp"

namespace cleainput {

// Types of apps we can generate
enum class AppType {
    REACT_NATIVE,
    REACT_WEB,
    EXPO,
    UNKNOWN
};

// A single file to be generated
struct GeneratedFile {
    std::string path;      // e.g. "screens/HomeScreen.js"
    std::string content;   // actual code
    std::string language;  // "javascript", "json" etc
};

// A complete generated app
struct GeneratedApp {
    std::string name;
    std::string description;
    AppType type;
    std::vector<GeneratedFile> files;
    std::vector<std::string> dependencies;
    std::string entry_point; // "App.js"

    // Stats
    size_t total_files() const {
        return files.size();
    }
    size_t total_lines() const;
};

// What the user wants to build
struct BuildIntent {
    std::string raw_prompt;  // original text
    std::string app_name;
    std::string description;
    AppType type = AppType::REACT_NATIVE;

    // Screens to generate
    std::vector<std::string> screens;

    // Features requested
    std::vector<std::string> features;

    // Style preferences
    std::string theme;      // "dark" or "light"
    std::string style;      // "minimal", "modern"

    // Navigation type
    std::string navigation; // "stack", "tab", "drawer"
};

class CodeGenerator {
public:
    explicit CodeGenerator(
        Transformer* transformer,
        Tokenizer*   tokenizer);

    // Main entry — text prompt → full app
    GeneratedApp generate(
        const std::string& prompt);

    // Parse prompt into intent
    BuildIntent parse_intent(
        const std::string& prompt);

    // Generate from structured intent
    GeneratedApp generate_from_intent(
        const BuildIntent& intent);

    // Package as downloadable ZIP
    std::vector<uint8_t> package_zip(
        const GeneratedApp& app);

    // Validate generated code
    bool validate(
        const GeneratedApp& app);

private:
    Transformer* transformer_;
    Tokenizer*   tokenizer_;

    // Generate individual files
    GeneratedFile gen_app_js(
        const BuildIntent& intent);
    GeneratedFile gen_package_json(
        const BuildIntent& intent);
    GeneratedFile gen_screen(
        const std::string& name,
        const BuildIntent& intent);
    GeneratedFile gen_navigator(
        const BuildIntent& intent);
    GeneratedFile gen_component(
        const std::string& name,
        const BuildIntent& intent);

    // AI-powered generation
    std::string ai_generate(
        const std::string& system_prompt,
        const std::string& user_prompt,
        uint32_t max_tokens = 512);

    // Extract keywords from prompt
    std::vector<std::string> extract_keywords(
        const std::string& prompt);

    // Detect app type from prompt
    AppType detect_app_type(
        const std::string& prompt);

    // Clean AI output
    std::string clean_code(
        const std::string& raw);
};

} // namespace cleainput
