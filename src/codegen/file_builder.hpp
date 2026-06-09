#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "code_generator.hpp"
#include "template_engine.hpp"

namespace cleainput {

// A file node in the project tree
struct FileNode {
    std::string name;
    std::string path;
    std::string content;
    bool is_directory = false;
    std::vector<FileNode> children;

    // Add child file/folder
    FileNode& add_child(
        const std::string& name,
        bool is_dir = false);

    // Add file with content
    FileNode& add_file(
        const std::string& name,
        const std::string& content);
};

// Project folder structure
struct ProjectStructure {
    std::string root_name;
    FileNode    root;

    // Stats
    size_t file_count()   const;
    size_t folder_count() const;
    size_t total_lines()  const;

    // Find file by path
    FileNode* find(
        const std::string& path);
};

// ZIP entry
struct ZipEntry {
    std::string path;
    std::vector<uint8_t> data;
    bool is_directory = false;
};

class FileBuilder {
public:
    explicit FileBuilder(
        TemplateEngine* engine);

    // Build complete project structure
    ProjectStructure build(
        const GeneratedApp& app);

    // Package to ZIP bytes
    std::vector<uint8_t> to_zip(
        const ProjectStructure& proj);

    // Write to disk (for server)
    bool write_to_disk(
        const ProjectStructure& proj,
        const std::string& output_path);

    // Generate project tree string
    // for display to user
    std::string tree_view(
        const ProjectStructure& proj) const;

    // Validate all files
    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult validate(
        const ProjectStructure& proj) const;

private:
    TemplateEngine* engine_;

    // Build folder structure
    ProjectStructure build_rn_structure(
        const GeneratedApp& app);

    // Create standard RN folders
    void create_src_folder(
        FileNode& root,
        const GeneratedApp& app);
    void create_screens_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_components_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_navigation_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_hooks_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_store_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_utils_folder(
        FileNode& src,
        const GeneratedApp& app);
    void create_assets_folder(
        FileNode& root);

    // Root files
    void add_root_files(
        FileNode& root,
        const GeneratedApp& app);

    // ZIP helpers
    std::vector<uint8_t> compress_file(
        const std::string& content) const;
    void add_zip_entry(
        std::vector<ZipEntry>& entries,
        const FileNode& node,
        const std::string& base_path) const;

    // Recursive tree builder
    void build_tree(
        const FileNode& node,
        std::string& output,
        const std::string& prefix,
        bool is_last) const;

    // Validate single file
    bool validate_js(
        const std::string& content,
        std::vector<std::string>& errors)
        const;
    bool validate_json(
        const std::string& content,
        std::vector<std::string>& errors)
        const;
};

} // namespace cleainput
