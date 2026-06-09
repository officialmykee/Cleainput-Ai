#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include "crypto/e2e.hpp"
#include "crypto/aes256.hpp"

namespace cleainput {

// Cloudflare config
struct CloudflareConfig {
    std::string account_id;
    std::string api_token;
    std::string zone_id;

    // KV namespace IDs
    std::string kv_cache_id;
    std::string kv_session_id;
    std::string kv_config_id;

    // R2 bucket names
    std::string r2_models_bucket;
    std::string r2_audio_bucket;
    std::string r2_apps_bucket;

    // Workers URLs
    std::string worker_inference_url;
    std::string worker_crawler_url;
    std::string worker_tts_url;

    // D1 database
    std::string d1_database_id;
};

// KV store entry
struct KVEntry {
    std::string key;
    std::string value;
    uint32_t    ttl = 0; // 0 = no expiry
    std::unordered_map<
        std::string,
        std::string> metadata;
};

// R2 object
struct R2Object {
    std::string key;
    std::vector<uint8_t> data;
    std::string content_type;
    std::unordered_map<
        std::string,
        std::string> custom_metadata;
    uint64_t size = 0;
    uint64_t uploaded_at = 0;
    std::string etag;
};

// R2 list result
struct R2ListResult {
    std::vector<std::string> keys;
    bool truncated = false;
    std::string cursor;
};

// Worker request
struct WorkerRequest {
    std::string url;
    std::string method = "POST";
    std::unordered_map<
        std::string,
        std::string> headers;
    std::string body;
    uint32_t timeout_ms = 30000;
};

// Worker response
struct WorkerResponse {
    int         status = 0;
    std::string body;
    std::unordered_map<
        std::string,
        std::string> headers;
    bool success() const {
        return status >= 200
            && status < 300;
    }
};

// Cache entry
struct CacheEntry {
    std::string key;
    std::string value;
    uint64_t    expires_at;
    bool        encrypted;

    bool expired() const;
};

// Edge inference request
struct InferenceRequest {
    std::string prompt;
    uint32_t    max_tokens = 256;
    float       temperature = 0.7f;
    float       top_p = 0.9f;
    bool        stream = false;
    std::string model = "cleainput-v1";
};

// Edge inference response
struct InferenceResponse {
    std::string text;
    uint32_t    tokens_used;
    float       latency_ms;
    bool        cached;
    std::string error;

    bool valid() const {
        return !text.empty()
            && error.empty();
    }
};

// Crawl request for worker
struct CrawlRequest {
    std::string url;
    uint32_t    max_depth = 2;
    uint32_t    max_pages = 10;
    bool        js_render = false;
    std::vector<std::string>
        allowed_domains;
};

// Crawl result
struct CrawlResult {
    std::string url;
    std::string title;
    std::string content;
    std::vector<std::string> links;
    uint64_t    crawled_at;
    bool        success;
    std::string error;
};

class CloudflareStorage {
public:
    explicit CloudflareStorage(
        const CloudflareConfig& config,
        const AESKey& master_key);

    // ─── KV Store ───────────────────
    // Ultra fast key-value cache
    // Globally distributed edge cache

    bool kv_set(
        const std::string& ns_id,
        const std::string& key,
        const std::string& value,
        uint32_t ttl = 0);

    std::string kv_get(
        const std::string& ns_id,
        const std::string& key);

    bool kv_delete(
        const std::string& ns_id,
        const std::string& key);

    std::vector<std::string> kv_list(
        const std::string& ns_id,
        const std::string& prefix = "",
        size_t limit = 100);

    // Encrypted KV operations
    bool kv_set_encrypted(
        const std::string& ns_id,
        const std::string& key,
        const std::string& value,
        const AESKey& key_enc,
        uint32_t ttl = 0);

    std::string kv_get_encrypted(
        const std::string& ns_id,
        const std::string& key,
        const AESKey& key_enc);

    // ─── R2 Object Storage ───────────
    // Store model weights, audio,
    // generated apps

    bool r2_put(
        const std::string& bucket,
        const std::string& key,
        const std::vector<uint8_t>& data,
        const std::string& content_type
            = "application/octet-stream");

    std::vector<uint8_t> r2_get(
        const std::string& bucket,
        const std::string& key);

    bool r2_delete(
        const std::string& bucket,
        const std::string& key);

    R2ListResult r2_list(
        const std::string& bucket,
        const std::string& prefix = "",
        size_t limit = 100);

    std::string r2_presigned_url(
        const std::string& bucket,
        const std::string& key,
        uint32_t expires_sec = 3600);

    // Store generated app ZIP
    std::string store_app(
        const std::string& app_id,
        const std::vector<uint8_t>& zip);

    // Get download URL for app
    std::string get_app_url(
        const std::string& app_id);

    // Store AI model weights
    bool store_model(
        const std::string& model_id,
        const std::vector<uint8_t>& weights);

    // Load AI model weights
    std::vector<uint8_t> load_model(
        const std::string& model_id);

    // ─── Workers ────────────────────
    // Run AI inference at the edge
    // Near user = low latency

    InferenceResponse run_inference(
        const InferenceRequest& req);

    // Stream inference response
    void stream_inference(
        const InferenceRequest& req,
        std::function<void(
            const std::string&)> callback);

    // Run web crawler worker
    CrawlResult crawl_url(
        const CrawlRequest& req);

    // Run TTS worker
    std::vector<uint8_t> run_tts(
        const std::string& text,
        const std::string& voice = "en");

    // Generic worker call
    WorkerResponse call_worker(
        const WorkerRequest& req);

    // ─── D1 Database ─────────────────
    // SQLite at the edge
    // For structured data

    struct D1Result {
        std::vector<std::unordered_map<
            std::string,
            std::string>> rows;
        size_t rows_read;
        size_t rows_written;
        bool success;
        std::string error;
    };

    D1Result d1_query(
        const std::string& sql,
        const std::vector<std::string>&
            params = {});

    bool d1_execute(
        const std::string& sql,
        const std::vector<std::string>&
            params = {});

    // ─── Cache Layer ─────────────────
    // Response caching to save
    // GPU compute costs

    bool cache_response(
        const std::string& prompt_hash,
        const std::string& response,
        uint32_t ttl = 3600);

    std::string get_cached_response(
        const std::string& prompt_hash);

    bool is_cached(
        const std::string& prompt_hash);

    // Cache hit rate stats
    struct CacheStats {
        uint64_t hits;
        uint64_t misses;
        float    hit_rate;
        uint64_t bytes_saved;
    };
    CacheStats get_cache_stats() const;

    // ─── Analytics ───────────────────
    // Privacy-preserving analytics
    // No user data stored

    void track_event(
        const std::string& event,
        const std::unordered_map<
            std::string,
            std::string>& props = {});

    struct UsageStats {
        uint64_t total_requests;
        uint64_t total_tokens;
        uint64_t total_audio_seconds;
        float    avg_latency_ms;
        uint64_t cache_hits;
    };
    UsageStats get_usage_stats() const;

    // Health check
    bool ping() const;

    // Edge location info
    std::string get_edge_location() const;

private:
    CloudflareConfig config_;
    AESKey           master_key_;
    AES256           aes_;

    // API base URLs
    std::string api_base() const {
        return "https://api.cloudflare"
               ".com/client/v4/accounts/"
               + config_.account_id;
    }

    std::string kv_url(
        const std::string& ns_id) const {
        return api_base()
            + "/storage/kv/namespaces/"
            + ns_id + "/values/";
    }

    std::string r2_url(
        const std::string& bucket) const {
        return api_base()
            + "/r2/buckets/"
            + bucket + "/objects/";
    }

    std::string d1_url() const {
        return api_base()
            + "/d1/database/"
            + config_.d1_database_id
            + "/query";
    }

    // Auth header
    std::string auth_header() const {
        return "Authorization: Bearer "
            + config_.api_token;
    }

    // HTTP operations
    std::string http_get(
        const std::string& url) const;
    std::string http_post(
        const std::string& url,
        const std::string& body,
        const std::string& content_type
            = "application/json") const;
    bool http_put(
        const std::string& url,
        const std::vector<uint8_t>& data,
        const std::string& content_type)
        const;
    bool http_delete(
        const std::string& url) const;

    // Encryption helpers
    std::string encrypt_value(
        const std::string& val) const;
    std::string decrypt_value(
        const std::string& enc) const;

    // Hash prompt for cache key
    std::string hash_prompt(
        const std::string& prompt) const;

    // JSON helpers
    std::string to_json(
        const std::unordered_map<
            std::string,
            std::string>& map) const;
    std::unordered_map<
        std::string,
        std::string>
    from_json(
        const std::string& json) const;

    // Retry with backoff
    template<typename Fn>
    auto with_retry(
        Fn fn,
        int max_retries = 3,
        int backoff_ms = 100);

    // Cache stats tracking
    mutable uint64_t cache_hits_   = 0;
    mutable uint64_t cache_misses_ = 0;
    mutable uint64_t bytes_saved_  = 0;
};

} // namespace cleainput
