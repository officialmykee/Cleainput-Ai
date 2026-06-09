#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include "crypto/e2e.hpp"
#include "crypto/aes256.hpp"

namespace cleainput {

// Supabase config
struct SupabaseConfig {
    std::string url;
    std::string anon_key;
    std::string service_key;
    std::string db_password;
};

// Supabase auth result
struct SupabaseAuth {
    bool        success = false;
    std::string user_id;
    std::string access_token;
    std::string refresh_token;
    uint64_t    expires_at;
    std::string error;

    bool valid() const {
        return success &&
            !access_token.empty();
    }
};

// Database row
struct DBRow {
    std::string id;
    std::unordered_map<
        std::string,
        std::string> columns;
    uint64_t created_at;
    uint64_t updated_at;

    std::string get(
        const std::string& col,
        const std::string& def = "")
        const {
        auto it = columns.find(col);
        return it != columns.end()
            ? it->second : def;
    }

    bool has(const std::string& col)
        const {
        return columns.count(col) > 0;
    }
};

// Query builder
struct SupabaseQuery {
    std::string table;

    struct Filter {
        std::string column;
        std::string op;
        std::string value;
    };

    std::vector<Filter> filters;
    std::vector<std::string> select_cols;
    std::string order_col;
    bool ascending = true;
    size_t limit_val = 100;
    size_t offset_val = 0;

    // Builder pattern
    SupabaseQuery& select(
        const std::string& cols) {
        select_cols.push_back(cols);
        return *this;
    }
    SupabaseQuery& eq(
        const std::string& col,
        const std::string& val) {
        filters.push_back({col,"eq",val});
        return *this;
    }
    SupabaseQuery& gt(
        const std::string& col,
        const std::string& val) {
        filters.push_back({col,"gt",val});
        return *this;
    }
    SupabaseQuery& lt(
        const std::string& col,
        const std::string& val) {
        filters.push_back({col,"lt",val});
        return *this;
    }
    SupabaseQuery& order(
        const std::string& col,
        bool asc = true) {
        order_col = col;
        ascending = asc;
        return *this;
    }
    SupabaseQuery& limit(size_t n) {
        limit_val = n;
        return *this;
    }
    SupabaseQuery& offset(size_t n) {
        offset_val = n;
        return *this;
    }
};

// Realtime subscription
struct RealtimeEvent {
    enum class Type {
        INSERT,
        UPDATE,
        DELETE
    };
    Type    type;
    DBRow   old_row;
    DBRow   new_row;
    std::string table;
};

using RealtimeCallback = std::function<
    void(const RealtimeEvent&)>;

// Vector embedding for
// semantic search
struct Embedding {
    std::string id;
    std::string text;
    std::vector<float> vector;
    std::unordered_map<
        std::string,
        std::string> metadata;
};

class SupabaseStorage {
public:
    explicit SupabaseStorage(
        const SupabaseConfig& config,
        const AESKey& master_key);

    // Authentication
    SupabaseAuth sign_up(
        const std::string& email,
        const std::string& password);

    SupabaseAuth sign_in(
        const std::string& email,
        const std::string& password);

    SupabaseAuth sign_in_oauth(
        const std::string& provider);

    void sign_out();
    bool refresh_token();

    bool is_authenticated() const {
        return auth_.valid();
    }

    // Database operations
    std::vector<DBRow> query(
        const SupabaseQuery& q);

    std::string insert(
        const std::string& table,
        const DBRow& row);

    bool update(
        const std::string& table,
        const std::string& id,
        const DBRow& row);

    bool remove(
        const std::string& table,
        const std::string& id);

    // Upsert — insert or update
    std::string upsert(
        const std::string& table,
        const DBRow& row);

    // Encrypted storage
    // Stores encrypted data
    // Server sees only ciphertext
    bool store_encrypted(
        const std::string& table,
        const std::string& id,
        const std::string& data,
        const AESKey& key);

    std::string load_encrypted(
        const std::string& table,
        const std::string& id,
        const AESKey& key);

    // Vector search for RAG
    // Semantic similarity search
    std::vector<Embedding>
        vector_search(
            const std::vector<float>&
                query_vec,
            const std::string& table,
            size_t top_k = 5,
            float threshold = 0.7f);

    bool store_embedding(
        const Embedding& emb,
        const std::string& table);

    // Conversation history
    bool save_conversation(
        const std::string& conv_id,
        const std::string& user_id,
        const EncryptedData& content);

    std::vector<DBRow>
        get_conversations(
            const std::string& user_id,
            size_t limit = 50);

    // Generated apps storage
    bool save_app_metadata(
        const std::string& app_id,
        const std::string& user_id,
        const std::string& metadata);

    std::vector<DBRow> get_user_apps(
        const std::string& user_id);

    // Realtime subscriptions
    void subscribe(
        const std::string& table,
        const std::string& filter,
        RealtimeCallback callback);

    void unsubscribe(
        const std::string& table);

    // Storage bucket operations
    bool upload_file(
        const std::string& bucket,
        const std::string& path,
        const std::vector<uint8_t>& data);

    std::vector<uint8_t> download_file(
        const std::string& bucket,
        const std::string& path);

    std::string get_public_url(
        const std::string& bucket,
        const std::string& path);

    bool delete_file(
        const std::string& bucket,
        const std::string& path);

    // Health check
    bool ping() const;

private:
    SupabaseConfig config_;
    AESKey         master_key_;
    SupabaseAuth   auth_;
    AES256         aes_;

    // REST API base
    std::string rest_url() const {
        return config_.url
            + "/rest/v1/";
    }

    std::string auth_url() const {
        return config_.url
            + "/auth/v1/";
    }

    std::string storage_url() const {
        return config_.url
            + "/storage/v1/";
    }

    std::string realtime_url() const {
        return config_.url
            + "/realtime/v1/";
    }

    // Auth headers
    std::string auth_header() const {
        return "Authorization: Bearer "
            + auth_.access_token;
    }

    std::string api_key_header() const {
        return "apikey: "
            + config_.anon_key;
    }

    // HTTP operations
    std::string http_get(
        const std::string& url) const;
    std::string http_post(
        const std::string& url,
        const std::string& body) const;
    std::string http_patch(
        const std::string& url,
        const std::string& body) const;
    bool http_delete(
        const std::string& url) const;

    // Encryption helpers
    std::string encrypt_field(
        const std::string& val) const;
    std::string decrypt_field(
        const std::string& enc) const;

    // Query builder to URL params
    std::string build_query_params(
        const SupabaseQuery& q) const;

    // JSON helpers
    std::string row_to_json(
        const DBRow& row) const;
    DBRow json_to_row(
        const std::string& json) const;
    std::vector<DBRow> json_to_rows(
        const std::string& json) const;

    // Active subscriptions
    std::unordered_map<
        std::string,
        RealtimeCallback> subscriptions_;

    // Connection retry
    template<typename Fn>
    auto with_retry(Fn fn,
        int max_retries = 3);
};

} // namespace cleainput
