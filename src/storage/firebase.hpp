#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include "crypto/e2e.hpp"
#include "crypto/aes256.hpp"

namespace cleainput {

// Firebase config
struct FirebaseConfig {
    std::string api_key;
    std::string project_id;
    std::string database_url;
    std::string storage_bucket;
    std::string app_id;
};

// Firebase document
struct FirebaseDoc {
    std::string id;
    std::unordered_map<
        std::string,
        std::string> fields;
    uint64_t created_at;
    uint64_t updated_at;

    bool has(const std::string& key)
        const {
        return fields.count(key) > 0;
    }

    std::string get(
        const std::string& key,
        const std::string& def = "")
        const {
        auto it = fields.find(key);
        return it != fields.end()
            ? it->second : def;
    }
};

// Firebase query
struct FirebaseQuery {
    std::string collection;
    std::string field;
    std::string op;  // ==, >, <, >=, <=
    std::string value;
    size_t limit = 100;
    std::string order_by;
    bool descending = false;
};

// Realtime listener callback
using FirebaseCallback = std::function<
    void(const FirebaseDoc&)>;

// Firebase auth result
struct AuthResult {
    bool        success = false;
    std::string user_id;
    std::string id_token;
    std::string refresh_token;
    uint64_t    expires_at;
    std::string error;

    bool valid() const {
        return success &&
            !id_token.empty();
    }
};

// User profile — all encrypted
struct UserProfile {
    std::string user_id;
    EncryptedData encrypted_name;
    EncryptedData encrypted_email;
    PublicKey     public_key;
    std::string   key_bundle_id;
    uint64_t      created_at;
    uint64_t      last_seen;

    // Everything stored encrypted
    // Server sees only public_key
    // and timestamps
};

// Conversation metadata
struct ConversationMeta {
    std::string  conv_id;
    std::string  user_id;
    uint64_t     created_at;
    uint64_t     updated_at;
    uint32_t     message_count;

    // Title encrypted — server
    // cannot read it
    EncryptedData encrypted_title;
    EncryptedData encrypted_summary;
};

class FirebaseStorage {
public:
    explicit FirebaseStorage(
        const FirebaseConfig& config,
        const AESKey& master_key);

    // Authentication
    AuthResult sign_up(
        const std::string& email,
        const std::string& password);

    AuthResult sign_in(
        const std::string& email,
        const std::string& password);

    AuthResult sign_in_anonymous();

    void sign_out();

    bool refresh_token();

    bool is_authenticated() const {
        return auth_.valid();
    }

    // User profile
    bool save_profile(
        const UserProfile& profile);

    UserProfile load_profile(
        const std::string& user_id);

    // Conversations
    std::string create_conversation(
        const std::string& title);

    bool update_conversation(
        const ConversationMeta& meta);

    std::vector<ConversationMeta>
        list_conversations(
            size_t limit = 50);

    bool delete_conversation(
        const std::string& conv_id);

    // Messages — all E2E encrypted
    bool save_message(
        const E2EMessage& msg,
        const std::string& conv_id);

    std::vector<E2EMessage>
        load_messages(
            const std::string& conv_id,
            size_t limit = 100,
            uint64_t before = 0);

    // Key bundles for E2E
    bool upload_key_bundle(
        const KeyBundle& bundle);

    KeyBundle download_key_bundle(
        const std::string& user_id);

    // Realtime listeners
    void listen_messages(
        const std::string& conv_id,
        FirebaseCallback callback);

    void stop_listening(
        const std::string& conv_id);

    // Generated code storage
    bool save_generated_app(
        const std::string& app_id,
        const std::vector<uint8_t>& zip,
        const std::string& metadata);

    std::string get_download_url(
        const std::string& app_id);

    // Sync status
    bool is_online() const;
    void set_offline_mode(bool offline);

private:
    FirebaseConfig config_;
    AESKey         master_key_;
    AuthResult     auth_;
    AES256         aes_;

    // HTTP client for REST API
    std::string base_url() const {
        return "https://firestore."
               "googleapis.com/v1/projects/"
               + config_.project_id
               + "/databases/(default)"
               "/documents/";
    }

    std::string auth_url() const {
        return "https://identitytoolkit"
               ".googleapis.com/v1/accounts:";
    }

    // REST operations
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

    // Auth header
    std::string auth_header() const {
        return "Authorization: Bearer "
            + auth_.id_token;
    }

    // Encrypt before storing
    std::string encrypt_field(
        const std::string& value) const;

    // Decrypt after loading
    std::string decrypt_field(
        const std::string& encrypted) const;

    // JSON helpers
    std::string to_json(
        const FirebaseDoc& doc) const;
    FirebaseDoc from_json(
        const std::string& json) const;

    // Retry logic
    template<typename Fn>
    auto with_retry(Fn fn,
        int max_retries = 3);

    // Active listeners
    std::unordered_map<
        std::string,
        FirebaseCallback> listeners_;
};

} // namespace cleainput
