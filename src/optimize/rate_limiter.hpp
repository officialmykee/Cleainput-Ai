#pragma once
#include <string>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <array>
#include <mutex>

namespace cleainput {

// User tier — determines limits
enum class UserTier {
    FREE,       // 10 req/min
    PRO,        // 100 req/min
    TEAM,       // 500 req/min
    ENTERPRISE, // unlimited
    INTERNAL    // system calls
};

// Request type costs
enum class RequestType {
    TEXT_SIMPLE,   // 1 credit
    TEXT_COMPLEX,  // 5 credits
    VOICE_SHORT,   // 3 credits
    VOICE_LONG,    // 10 credits
    CODE_GEN,      // 8 credits
    WEB_CRAWL,     // 4 credits
    IMAGE_GEN,     // 15 credits
    EMBEDDING,     // 2 credits
};

// Rate limit config per tier
struct TierLimits {
    uint32_t requests_per_min;
    uint32_t requests_per_hour;
    uint32_t requests_per_day;
    uint32_t credits_per_day;
    uint32_t max_tokens;
    uint32_t max_audio_seconds;
    bool     can_use_big_model;
    bool     can_use_web_crawl;
    bool     priority_queue;
};

// Per user rate state
struct RateState {
    std::string user_id;
    UserTier    tier;
    uint32_t    requests_this_min  = 0;
    uint32_t    requests_this_hour = 0;
    uint32_t    requests_this_day  = 0;
    uint32_t    credits_used_today = 0;
    uint64_t    min_reset_at;
    uint64_t    hour_reset_at;
    uint64_t    day_reset_at;
    uint64_t    last_request_at;

    // Sliding window counters
    std::array<uint32_t, 60>
        per_second_counts{};
    uint8_t current_second = 0;
};

// Result of rate check
struct RateLimitResult {
    bool     allowed = true;
    uint32_t retry_after_ms = 0;
    uint32_t remaining_requests;
    uint32_t remaining_credits;
    std::string reason;

    // HTTP headers to send back
    std::unordered_map<
        std::string,
        std::string> headers() const;
};

// Abuse detection
struct AbuseSignal {
    std::string ip;
    std::string user_id;
    uint32_t    rapid_requests = 0;
    uint32_t    failed_auths   = 0;
    uint32_t    large_payloads = 0;
    bool        is_bot         = false;
    bool        is_vpn         = false;
    float       abuse_score    = 0.0f;

    bool is_abusive() const {
        return abuse_score > 0.7f;
    }
};

class RateLimiter {
public:
    explicit RateLimiter(
        size_t max_users = 1000000);

    // Main check — call before
    // every request
    RateLimitResult check(
        const std::string& user_id,
        const std::string& ip,
        RequestType type,
        UserTier tier);

    // Deduct credits after
    // successful request
    void deduct(
        const std::string& user_id,
        RequestType type);

    // Block an IP
    void block_ip(
        const std::string& ip,
        uint32_t duration_sec = 3600);

    // Block a user
    void block_user(
        const std::string& user_id,
        uint32_t duration_sec = 86400);

    // Unblock
    void unblock_ip(
        const std::string& ip);
    void unblock_user(
        const std::string& user_id);

    // Check abuse signals
    AbuseSignal analyze_abuse(
        const std::string& ip,
        const std::string& user_id);

    // Auto block abusers
    void auto_block_if_abusive(
        const AbuseSignal& signal);

    // Get user state
    RateState get_state(
        const std::string& user_id) const;

    // Reset user limits
    void reset_user(
        const std::string& user_id);

    // Stats
    struct Stats {
        uint64_t total_allowed;
        uint64_t total_blocked;
        uint64_t total_abuse_blocked;
        float    block_rate;
        uint32_t active_users;
        uint32_t blocked_ips;
    };
    Stats get_stats() const;

    // Cost of request type
    static uint32_t credit_cost(
        RequestType type);

    // Tier limits
    static TierLimits tier_limits(
        UserTier tier);

private:
    // User states
    std::unordered_map<
        std::string,
        RateState> states_;

    // Blocked IPs
    std::unordered_map<
        std::string,
        uint64_t> blocked_ips_;

    // Blocked users
    std::unordered_map<
        std::string,
        uint64_t> blocked_users_;

    // IP request counts
    std::unordered_map<
        std::string,
        uint32_t> ip_counts_;

    // Abuse tracking
    std::unordered_map<
        std::string,
        AbuseSignal> abuse_signals_;

    // Thread safety
    mutable std::mutex mutex_;

    // Stats counters
    std::atomic<uint64_t>
        total_allowed_{0};
    std::atomic<uint64_t>
        total_blocked_{0};
    std::atomic<uint64_t>
        abuse_blocked_{0};

    // Check if blocked
    bool is_ip_blocked(
        const std::string& ip) const;
    bool is_user_blocked(
        const std::string& user_id) const;

    // Reset expired windows
    void reset_if_expired(
        RateState& state);

    // Sliding window check
    bool sliding_window_check(
        RateState& state,
        uint32_t max_per_sec = 5);

    // Update abuse score
    void update_abuse_score(
        AbuseSignal& signal);

    // Current timestamp ms
    static uint64_t now_ms();
    static uint64_t now_sec();
};

} // namespace cleainput
