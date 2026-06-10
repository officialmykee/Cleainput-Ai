#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <cstdint>
#include "rate_limiter.hpp"
#include "smart_cache.hpp"
#include "compression.hpp"

namespace cleainput {

// Where to send the request
enum class RouteTarget {
    CACHE,           // free — cached answer
    ON_DEVICE,       // free — runs on phone
    TINY_MODEL,      // $0.0001 per request
    SMALL_MODEL,     // $0.001  per request
    FULL_MODEL,      // $0.01   per request
    WEB_CRAWLER,     // $0.002  per request
    CODE_GENERATOR,  // $0.005  per request
    REJECT           // blocked/rate limited
};

// Complexity of a request
enum class RequestComplexity {
    TRIVIAL,    // "hi", "thanks"
    SIMPLE,     // factual questions
    MEDIUM,     // reasoning needed
    COMPLEX,    // multi-step reasoning
    VERY_HIGH   // code gen, analysis
};

// Request classification
struct RequestClass {
    RequestComplexity complexity;
    RouteTarget       target;
    float             estimated_cost;
    uint32_t          estimated_tokens;
    bool              needs_web_search;
    bool              needs_code_gen;
    bool              needs_voice;
    bool              is_cached;
    float             cache_confidence;
    std::string       cache_key;

    // How confident we are
    // in this classification
    float confidence = 0.0f;
};

// Routing decision
struct RoutingDecision {
    RouteTarget  target;
    RequestClass classification;
    float        estimated_cost_usd;
    bool         use_cache;
    bool         use_compression;
    bool         use_streaming;
    uint32_t     priority;
    std::string  reason;

    // Model to use
    std::string model_id;

    // Max tokens to generate
    uint32_t max_tokens = 256;

    // Temperature
    float temperature = 0.7f;
};

// Router config
struct RouterConfig {
    // Cost thresholds
    float tiny_model_max_cost  = 0.0001f;
    float small_model_max_cost = 0.001f;
    float full_model_max_cost  = 0.01f;

    // Complexity thresholds
    uint32_t trivial_token_limit  = 10;
    uint32_t simple_token_limit   = 100;
    uint32_t medium_token_limit   = 500;
    uint32_t complex_token_limit  = 2000;

    // Cache settings
    float cache_confidence_threshold
        = 0.92f;
    bool  enable_semantic_cache = true;

    // Cost budget per user per day
    float free_daily_budget    = 0.05f;
    float pro_daily_budget     = 0.50f;
    float team_daily_budget    = 5.00f;

    // Routing weights
    float cache_weight         = 1.0f;
    float on_device_weight     = 0.9f;
    float tiny_model_weight    = 0.7f;
    float small_model_weight   = 0.5f;
    float full_model_weight    = 0.3f;
};

// Per user cost tracking
struct UserBudget {
    std::string user_id;
    UserTier    tier;
    float       spent_today_usd  = 0.0f;
    float       spent_month_usd  = 0.0f;
    float       total_spent_usd  = 0.0f;
    uint64_t    reset_at;
    uint32_t    requests_today   = 0;
    float       daily_budget_usd = 0.05f;

    bool within_budget(float cost) const {
        return spent_today_usd + cost
            <= daily_budget_usd;
    }

    float remaining_budget() const {
        return daily_budget_usd
            - spent_today_usd;
    }
};

// Global routing stats
struct RouterStats {
    std::atomic<uint64_t>
        cache_hits{0};
    std::atomic<uint64_t>
        on_device_routes{0};
    std::atomic<uint64_t>
        tiny_model_routes{0};
    std::atomic<uint64_t>
        small_model_routes{0};
    std::atomic<uint64_t>
        full_model_routes{0};
    std::atomic<uint64_t>
        rejected{0};
    std::atomic<float>
        total_cost_usd{0};
    std::atomic<float>
        total_saved_usd{0};

    // Cost breakdown
    float avg_cost_per_request() const;
    float cache_hit_rate() const;
    float savings_rate() const;
};

class RequestRouter {
public:
    explicit RequestRouter(
        const RouterConfig& config,
        SmartCache*   cache,
        RateLimiter*  limiter,
        Compression*  compressor);

    // Main routing function
    // Call this for EVERY request
    RoutingDecision route(
        const std::string& user_id,
        const std::string& ip,
        const std::string& prompt,
        UserTier           tier,
        RequestType        type);

    // After request completes
    // update costs and stats
    void record_result(
        const std::string& user_id,
        const RoutingDecision& decision,
        float actual_cost_usd,
        uint32_t actual_tokens,
        bool     success);

    // Check user budget
    UserBudget get_budget(
        const std::string& user_id) const;

    // Set user tier
    void set_user_tier(
        const std::string& user_id,
        UserTier tier);

    // Force route to specific target
    // For testing/admin
    void override_route(
        const std::string& user_id,
        RouteTarget target);

    // Get routing stats
    const RouterStats& stats() const {
        return stats_;
    }

    // Cost estimate for prompt
    float estimate_cost(
        const std::string& prompt,
        RouteTarget target) const;

    // Cheapest route that meets
    // quality requirements
    RouteTarget cheapest_route(
        RequestComplexity complexity,
        UserTier tier) const;

private:
    RouterConfig config_;
    SmartCache*  cache_;
    RateLimiter* limiter_;
    Compression* compressor_;
    RouterStats  stats_;

    // User budgets
    std::unordered_map<
        std::string,
        UserBudget> budgets_;

    // User overrides
    std::unordered_map<
        std::string,
        RouteTarget> overrides_;

    // Classify request complexity
    RequestComplexity classify(
        const std::string& prompt) const;

    // Check cache first
    bool check_cache(
        const std::string& prompt,
        RequestClass& cls) const;

    // Detect intent
    bool needs_web_search(
        const std::string& prompt) const;
    bool needs_code_gen(
        const std::string& prompt) const;
    bool is_greeting(
        const std::string& prompt) const;
    bool is_factual(
        const std::string& prompt) const;
    bool needs_reasoning(
        const std::string& prompt) const;

    // Token count estimate
    uint32_t estimate_tokens(
        const std::string& text) const;

    // Cost per token per model
    static float cost_per_token(
        RouteTarget target);

    // Update budget
    void update_budget(
        const std::string& user_id,
        float cost);

    // Reset daily budgets
    void reset_expired_budgets();

    // Keyword lists for
    // intent detection
    static const std::vector<std::string>
        CODE_KEYWORDS;
    static const std::vector<std::string>
        SEARCH_KEYWORDS;
    static const std::vector<std::string>
        GREETING_KEYWORDS;
    static const std::vector<std::string>
        COMPLEX_KEYWORDS;
};

} // namespace cleainput
