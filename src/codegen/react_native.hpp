#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "code_generator.hpp"

namespace cleainput {

// Screen types
enum class ScreenType {
    HOME,
    LIST,
    DETAIL,
    FORM,
    PROFILE,
    SETTINGS,
    AUTH,
    SPLASH,
    CUSTOM
};

// Component types
enum class ComponentType {
    BUTTON,
    CARD,
    INPUT,
    HEADER,
    FOOTER,
    MODAL,
    LIST_ITEM,
    CHART,
    AVATAR,
    CUSTOM
};

// Navigation types
enum class NavType {
    STACK,
    BOTTOM_TAB,
    DRAWER,
    TOP_TAB
};

// A single screen definition
struct Screen {
    std::string name;
    ScreenType  type;
    std::string title;
    std::vector<std::string> components;
    std::vector<std::string> actions;
    bool requires_auth = false;
};

// A reusable component
struct Component {
    std::string   name;
    ComponentType type;
    std::vector<std::string> props;
    std::string   description;
};

// Database/state schema
struct StateSchema {
    struct Field {
        std::string name;
        std::string type; // string, number, bool
        bool required = true;
    };
    struct Model {
        std::string name;
        std::vector<Field> fields;
    };
    std::vector<Model> models;
};

// Full React Native app blueprint
struct RNBlueprint {
    std::string     app_name;
    std::string     bundle_id;
    std::string     version = "1.0.0";
    NavType         nav_type;
    std::string     theme; // dark/light
    std::vector<Screen>    screens;
    std::vector<Component> components;
    StateSchema     state;
    std::vector<std::string> permissions;
    std::vector<std::string> dependencies;
};

class ReactNativeGenerator {
public:
    ReactNativeGenerator() = default;

    // Build from intent
    GeneratedApp build(
        const BuildIntent& intent);

    // Individual generators
    std::string gen_app_js(
        const RNBlueprint& bp);

    std::string gen_package_json(
        const RNBlueprint& bp);

    std::string gen_screen(
        const Screen& screen,
        const RNBlueprint& bp);

    std::string gen_component(
        const Component& comp,
        const RNBlueprint& bp);

    std::string gen_navigator(
        const RNBlueprint& bp);

    std::string gen_theme(
        const RNBlueprint& bp);

    std::string gen_store(
        const RNBlueprint& bp);

    std::string gen_app_json(
        const RNBlueprint& bp);

    std::string gen_babel_config();
    std::string gen_metro_config();
    std::string gen_readme(
        const RNBlueprint& bp);

private:
    // Blueprint builder
    RNBlueprint build_blueprint(
        const BuildIntent& intent);

    // Screen type detector
    ScreenType detect_screen_type(
        const std::string& name);

    // Dependency resolver
    std::vector<std::string> resolve_deps(
        const RNBlueprint& bp);

    // Theme generator
    std::string dark_theme();
    std::string light_theme();

    // Common patterns
    std::string stack_navigator(
        const RNBlueprint& bp);
    std::string tab_navigator(
        const RNBlueprint& bp);
    std::string drawer_navigator(
        const RNBlueprint& bp);

    // Styles
    std::string base_styles(
        const RNBlueprint& bp);
};

} // namespace cleainput
