#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <functional>
#include "rate_limiter.hpp"

namespace cleainput {

// Cost categories
enum class CostCategory {
    INFERENCE,     // LLM compute
    VOICE_STT,     // speech to text
    VOICE_TTS,     // text to speech
    WEB_CRAWL,     // crawler
    CODE_GEN,      // code generation
    STORAGE,       // Firebase/Supabase
    BANDWIDTH,     // data transfer
    CLOUDFLARE,    // edge compute
    CACHE_MISS,    // uncached request
    CACHE_HIT,     // free — cached
    OTHER
};

// A single cost event
struct CostEvent {
    std::string  event_id;
    std::string  user_id;
    CostCategory category;
    RouteTarget  target;
    float        cost_usd;
    float        saved_usd;  // vs full cost
    uint32_t     tokens_used;
    uint32_t     audio_seconds;
    uint64_t     timestamp;
    bool         cached;
    bool         compressed;
    float        latency_ms;

    // What model was used
    std::string model_id;

    // Request hash for dedup
    std::string request_hash;
};

// Daily cost summary
struct DailyCost {
    std::string date; // YYYY-MM-DD
    float total_usd;
    float inference_usd;
    float voice_usd;
    float storage_usd;
    float bandwidth_usd;
    float saved_usd;    // through caching
    uint64_t requests;
    uint64_t cache_hits;
    float cache_hit_rate;
    float avg_cost_per_request;
};

// Per user cost summary
struct UserCostSummary {
    std::string user_id;
    UserTier    tier;
    float       today_usd;
    float       this_month_usd;
    float       total_usd;
    uint64_t    total_requests;
    uint64_t    cache_hits;
    float       avg_cost_per_request;
    float       daily_budget;
    float       budget_used_pct;

    // Top cost categories
    std::unordered_map<
        std::string,
        float> by_category;

    bool over_budget() const {
        return today_usd >= daily_budget;
    }
};

// Cost alert
struct CostAlert {
    enum class Level {
        INFO,
        WARNING,
        CRITICAL
    };

    Level       level;
    std::string message;
    float       current_cost;
    float       threshold;
    uint64_t    timestamp;
};

// Cost forecast
struct CostForecast {
    float today_projected_usd;
    float this_month_projected_usd;
    float this_year_projected_usd;
    float daily_avg_usd;
    float monthly_avg_usd;
    float trend; // positive = increasing
    std::string recommendation;
};

// Infrastructure cost breakdown
struct InfraCosts {
    float cerebrium_usd;    // GPU compute
    float firebase_usd;     // database
    float supabase_usd;     // postgres
    float cloudflare_usd;   // edge/CDN
    float bandwidth_usd;    // data transfer
    float storage_usd;      // model weights
    float total_usd;

    // vs competitor pricing
    float vs_openai_usd;    // what it would cost
    float vs_python_usd;    // Python equivalent
    float savings_pct;      // your savings
};

// Alert callback
using AlertCallback = std::function<
    void(const CostAlert&)>;

class CostTracker {
public:
    explicit CostTracker(
        float daily_budget_usd   = 10.0f,
        float monthly_budget_usd = 200.0f);

    // ─── Record Costs ─────────────────

    // Record a cost event
    void record(
        const CostEvent& event);

    // Quick record shorthand
    void record(
        const std::string& user_id,
        CostCategory category,
        float cost_usd,
        uint32_t tokens = 0);

    // Record cache hit (free)
    void record_cache_hit(
        const std::string& user_id,
        float saved_usd);

    // Record compression saving
    void record_compression_saving(
        size_t bytes_saved,
        float cost_saved_usd);

    // ─── Query Costs ──────────────────

    // Total cost today
    float today_total() const;

    // Total cost this month
    float month_total() const;

    // Total cost all time
    float all_time_total() const;

    // Total saved all time
    float all_time_saved() const;

    // Cost by category today
    std::unordered_map<
        std::string,
        float> today_by_category() const;

    // Per user summary
    UserCostSummary user_summary(
        const std::string& user_id) const;

    // Top spending users
    std::vector<UserCostSummary>
        top_spenders(size_t n = 10) const;

    // Daily history
    std::vector<DailyCost>
        daily_history(
            uint32_t days = 30) const;

    // ─── Budget Management ────────────

    // Set global budget
    void set_daily_budget(
        float usd);
    void set_monthly_budget(
        float usd);

    // Set per user budget
    void set_user_budget(
        const std::string& user_id,
        float daily_usd);

    // Check if within budget
    bool within_daily_budget(
        float additional_cost = 0) const;
    bool within_monthly_budget(
        float additional_cost = 0) const;

    // Remaining budget
    float daily_remaining() const;
    float monthly_remaining() const;

    // ─── Forecasting ─────────────────

    CostForecast forecast() const;

    // When will budget run out
    // at current spend rate
    uint64_t budget_exhaustion_time()
        const;

    // ─── Infrastructure Costs ─────────

    InfraCosts infrastructure_costs()
        const;

    // Cost per 1000 requests
    float cost_per_1k_requests() const;

    // Cost vs competitors
    float savings_vs_openai() const;
    float savings_vs_python() const;

    // ─── Alerts ──────────────────────

    // Register alert callback
    void on_alert(
        AlertCallback callback);

    // Set alert thresholds
    void set_alert_threshold(
        CostAlert::Level level,
        float pct_of_budget);

    // Get active alerts
    std::vector<CostAlert>
        active_alerts() const;

    // ─── Optimization Tips ────────────

    struct OptimizationTip {
        std::string title;
        std::string description;
        float       potential_saving_usd;
        std::string action;
    };

    // AI-generated tips to reduce cost
    std::vector<OptimizationTip>
        get_optimization_tips() const;

    // ─── Export ──────────────────────

    // Export as JSON
    std::string to_json() const;

    // Export as CSV
    std::string to_csv(
        uint32_t days = 30) const;

    // Reset all stats
    void reset();

private:
    float daily_budget_usd_;
    float monthly_budget_usd_;
    mutable std::mutex mutex_;

    // Cost events log
    std::vector<CostEvent> events_;

    // Per user costs
    std::unordered_map<
        std::string,
        UserCostSummary> user_costs_;

    // Daily summaries
    std::unordered_map<
        std::string,
        DailyCost> daily_costs_;

    // Running totals
    std::atomic<float>
        today_total_{0};
    std::atomic<float>
        month_total_{0};
    std::atomic<float>
        all_time_total_{0};
    std::atomic<float>
        all_time_saved_{0};

    // Alert system
    std::vector<AlertCallback>
        alert_callbacks_;
    std::unordered_map<
        CostAlert::Level,
        float> alert_thresholds_;

    // Fire alert
    void fire_alert(
        CostAlert::Level level,
        const std::string& msg,
        float current,
        float threshold);

    // Check budget alerts
    void check_alerts();

    // Today's date string
    static std::string today_str();

    // Current month string
    static std::string month_str();

    // Cost per token by target
    static float token_cost(
        RouteTarget target);

    // Cost per audio second
    static float audio_cost(
        RouteTarget target);

    // Update daily summary
    void update_daily(
        const CostEvent& event);

    // Update user summary
    void update_user(
        const CostEvent& event);

    // Trim old events
    // Keep last 30 days only
    void trim_old_events();
};

} // namespace cleainput
