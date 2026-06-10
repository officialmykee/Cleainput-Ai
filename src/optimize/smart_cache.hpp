#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>
#include "core/arena.hpp"

namespace cleainput {

// Cache entry priority
enum class CachePriority {
    LOW,      // evict first
    NORMAL,
    HIGH,     // keep longer
    PERMANENT // never evict
};

// Cache entry
struct CacheEntry {
    std::string   key;
    std::string   value;
    uint64_t      created_at;
    uint64_t      expires_at;
    uint64_t      last_accessed;
    uint32_t      access_count = 0;
    uint32_t      size_bytes;
    CachePriority priority;
    bool          compressed = false;

    bool expired() const;
    bool valid()   const {
        return !value.empty()
            && !expired();
    }

    // Cost savings this entry
    // has provided
    float savings_usd = 0.0f;
};

// Cache tier
enum class CacheTier {
    L1_MEMORY,    // fastest  ~0ms
    L2_DISK,      // fast     ~1ms
    L3_REDIS,     // medium   ~5ms
    L4_CLOUDFLARE // global   ~10ms
};

// Semantic cache entry
// Matches similar questions
struct SemanticEntry {
    std::string          key;
    std::string          value;
    std::vector<float>   embedding;
    uint64_t             created_at;
    uint64_t             expires_at;
    uint32_t             hit_count = 0;

    // Similarity score
    float similarity(
        const std::vector<float>& q)
        const;
};

// Cache stats
struct CacheStats {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> bytes_stored{0};
    std::atomic<uint64_t> bytes_saved{0};
    std::atomic<float>    cost_saved_usd{0};

    float hit_rate() const {
        uint64_t total =
            hits.load() + misses.load();
        if (total == 0) return 0.0f;
        return (float)hits.load()
             / (float)total;
    }
};

// Eviction policy
enum class EvictionPolicy {
    LRU,  // least recently used
    LFU,  // least frequently used
    TTL,  // time based
    COST  // evict cheapest to recompute
};

class SmartCache {
public:
    explicit SmartCache(
        size_t max_memory_mb = 512,
        EvictionPolicy policy
            = EvictionPolicy::LFU);

    // ─── Basic Cache ─────────────────

    // Set with TTL seconds
    void set(
        const std::string& key,
        const std::string& value,
        uint32_t ttl_sec = 3600,
        CachePriority priority
            = CachePriority::NORMAL);

    // Get — returns empty if miss
    std::string get(
        const std::string& key);

    // Check exists
    bool has(
        const std::string& key) const;

    // Delete
    void del(
        const std::string& key);

    // ─── Smart Features ───────────────

    // Get or compute
    // If cache miss, runs fn() and caches
    std::string get_or_set(
        const std::string& key,
        std::function<std::string()> fn,
        uint32_t ttl_sec = 3600);

    // Semantic cache
    // Matches similar prompts
    // "What is AI?" matches
    // "Can you explain AI?"
    void set_semantic(
        const std::string& prompt,
        const std::string& response,
        const std::vector<float>& embedding,
        uint32_t ttl_sec = 86400);

    std::string get_semantic(
        const std::vector<float>& query_emb,
        float threshold = 0.92f);

    // Prefix invalidation
    // del_prefix("user:123:")
    // deletes all user 123 entries
    void del_prefix(
        const std::string& prefix);

    // Batch get
    std::unordered_map<
        std::string,
        std::string> mget(
        const std::vector<std::string>&
            keys);

    // Batch set
    void mset(
        const std::unordered_map<
            std::string,
            std::string>& pairs,
        uint32_t ttl_sec = 3600);

    // ─── AI Response Cache ────────────

    // Cache AI response
    // Key = hash of prompt + model
    void cache_ai_response(
        const std::string& prompt,
        const std::string& model,
        const std::string& response,
        float cost_usd,
        uint32_t ttl_sec = 7200);

    // Get cached AI response
    // Returns empty if not found
    std::string get_ai_response(
        const std::string& prompt,
        const std::string& model);

    // Cache voice transcript
    void cache_transcript(
        const std::string& audio_hash,
        const std::string& transcript);

    std::string get_transcript(
        const std::string& audio_hash);

    // Cache generated app
    void cache_generated_app(
        const std::string& prompt_hash,
        const std::vector<uint8_t>& zip,
        uint32_t ttl_sec = 86400);

    std::vector<uint8_t>
        get_generated_app(
        const std::string& prompt_hash);

    // ─── Multi-Tier Cache ─────────────

    // Promote hot entries to L1
    void promote(
        const std::string& key);

    // Demote cold entries to L2
    void demote(
        const std::string& key);

    // Warm cache from disk on startup
    void warm_from_disk(
        const std::string& path);

    // Persist hot entries to disk
    void persist_to_disk(
        const std::string& path);

    // ─── Stats & Monitoring ───────────

    const CacheStats& stats() const {
        return stats_;
    }

    // Memory usage
    size_t memory_used_mb() const;
    size_t entry_count()    const;

    // Top accessed entries
    std::vector<std::string>
        hot_keys(size_t n = 10) const;

    // Estimated cost saved
    float cost_saved_usd() const;

    // Clear everything
    void clear();

    // Evict expired entries
    uint32_t evict_expired();

    // Force eviction to free memory
    uint32_t evict_to_fit(
        size_t target_mb);

private:
    size_t         max_memory_bytes_;
    EvictionPolicy policy_;
    mutable std::mutex mutex_;
    CacheStats     stats_;

    // L1 memory cache
    std::unordered_map<
        std::string,
        CacheEntry> l1_;

    // Semantic cache
    std::vector<SemanticEntry>
        semantic_cache_;

    // LRU order tracking
    std::vector<std::string> lru_order_;

    // LFU frequency map
    std::unordered_map<
        std::string,
        uint32_t> lfu_counts_;

    // Current memory usage
    std::atomic<size_t>
        memory_used_{0};

    // Hash a prompt
    std::string hash(
        const std::string& s) const;

    // Compress value
    std::string compress(
        const std::string& s) const;

    // Decompress value
    std::string decompress(
        const std::string& s) const;

    // Eviction helpers
    void evict_lru();
    void evict_lfu();
    void evict_ttl();
    void evict_cost();

    // Update LRU on access
    void touch_lru(
        const std::string& key);

    // Current time helpers
    static uint64_t now_sec();
    static uint64_t now_ms();

    // Dot product for
    // semantic similarity
    static float cosine_similarity(
        const std::vector<float>& a,
        const std::vector<float>& b);
};

} // namespace cleainput
