#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace cleainput {

// A single template variable
struct TemplateVar {
    std::string key;
    std::string value;
};

// Template block — reusable code chunk
struct TemplateBlock {
    std::string name;
    std::string content;
    std::vector<std::string> required_vars;
};

// Template for a complete file
struct FileTemplate {
    std::string name;
    std::string language;
    std::string content;
    std::vector<std::string> vars;

    // Render with variables
    std::string render(
        const std::unordered_map<
            std::string,
            std::string>& values) const;
};

class TemplateEngine {
public:
    TemplateEngine();

    // Render a template string
    std::string render(
        const std::string& tmpl,
        const std::unordered_map<
            std::string,
            std::string>& vars) const;

    // Register a template
    void register_template(
        const std::string& name,
        const std::string& content);

    // Render registered template
    std::string render_named(
        const std::string& name,
        const std::unordered_map<
            std::string,
            std::string>& vars) const;

    // Register a helper function
    void register_helper(
        const std::string& name,
        std::function<std::string(
            const std::string&)> fn);

    // Built-in React Native templates
    std::string screen_template(
        const std::string& screen_name,
        const std::string& theme,
        const std::string& content);

    std::string component_template(
        const std::string& comp_name,
        const std::string& props,
        const std::string& content);

    std::string hook_template(
        const std::string& hook_name,
        const std::string& state,
        const std::string& logic);

    std::string api_template(
        const std::string& endpoint,
        const std::string& method,
        const std::string& payload);

    std::string style_template(
        const std::string& theme);

    std::string nav_template(
        const std::string& nav_type,
        const std::vector<std::string>&
            screens);

    // Utility
    std::string to_pascal_case(
        const std::string& str) const;
    std::string to_camel_case(
        const std::string& str) const;
    std::string to_kebab_case(
        const std::string& str) const;
    std::string indent(
        const std::string& code,
        int spaces = 2) const;
    std::string strip_blank_lines(
        const std::string& code) const;

private:
    // Registered templates
    std::unordered_map<
        std::string,
        std::string> templates_;

    // Helper functions
    std::unordered_map<
        std::string,
        std::function<std::string(
            const std::string&)>> helpers_;

    // Variable substitution
    // {{VAR_NAME}} → value
    std::string substitute(
        const std::string& tmpl,
        const std::unordered_map<
            std::string,
            std::string>& vars) const;

    // Handle conditionals
    // {{#if VAR}}...{{/if}}
    std::string process_conditionals(
        const std::string& tmpl,
        const std::unordered_map<
            std::string,
            std::string>& vars) const;

    // Handle loops
    // {{#each ITEMS}}...{{/each}}
    std::string process_loops(
        const std::string& tmpl,
        const std::unordered_map<
            std::string,
            std::string>& vars) const;

    // Load built-in templates
    void load_builtin_templates();
};

} // namespace cleainput
