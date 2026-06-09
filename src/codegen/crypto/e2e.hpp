#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include "aes256.hpp"
#include "keypair.hpp"

namespace cleainput {

// Session states
enum class SessionState {
    INIT,        // just created
    HANDSHAKE,   // exchanging keys
    ACTIVE,      // fully encrypted
    EXPIRED,     // needs renewal
    REVOKED      // terminated
};

// A single E2E session
// between two parties
struct E2ESession {
    std::string  session_id;
    std::string  user_id;
    PublicKey    their_pub;
    AESKey       session_key;
    SessionState state;
    uint64_t     created_at;
    uint64_t     expires_at;
    uint64_t     message_count = 0;

    // Rotate key after N messages
    static constexpr uint64_t
        KEY_ROTATION_INTERVAL = 100;

    bool needs_rotation() const {
        return message_count >=
            KEY_ROTATION_INTERVAL;
    }

    bool expired() const;
    bool valid()   const {
        return state == SessionState::ACTIVE
            && !expired();
    }
};

// An encrypted message
struct E2EMessage {
    std::string    session_id;
    std::string    sender_id;
    std::string    recipient_id;
    EncryptedData  payload;
    uint64_t       timestamp;
    uint64_t       sequence_num;
    bool           is_voice = false;

    // Message types
    enum class Type {
        TEXT,
        VOICE,
        CODE,
        FILE,
        SYSTEM
    };
    Type type = Type::TEXT;

    bool valid() const {
        return !payload.empty()
            && !session_id.empty();
    }
};

// Handshake message for
// establishing E2E session
struct HandshakeMessage {
    std::string sender_id;
    PublicKey   sender_pub;
    uint64_t    timestamp;
    Signature   signature;

    // Verify handshake is authentic
    bool verify(
        const KeyPairManager& km) const;
};

// Key bundle for registration
// Stored on server (all public)
struct KeyBundle {
    std::string user_id;
    PublicKey   identity_key;
    PublicKey   signed_prekey;
    Signature   prekey_signature;
    std::vector<PublicKey> one_time_keys;
    uint64_t    timestamp;
};

class E2EEncryption {
public:
    explicit E2EEncryption(
        const std::string& user_id,
        const KeyPair& identity);

    // Initialize a new session
    // with another user
    E2ESession init_session(
        const std::string& their_id,
        const KeyBundle& their_bundle);

    // Accept incoming session
    E2ESession accept_session(
        const HandshakeMessage& msg,
        const KeyBundle& their_bundle);

    // Encrypt a message
    E2EMessage encrypt_message(
        const std::string& plaintext,
        E2ESession& session);

    // Encrypt voice audio
    E2EMessage encrypt_voice(
        const std::vector<uint8_t>& audio,
        E2ESession& session);

    // Decrypt a message
    std::string decrypt_message(
        const E2EMessage& msg,
        E2ESession& session);

    // Decrypt voice audio
    std::vector<uint8_t> decrypt_voice(
        const E2EMessage& msg,
        E2ESession& session);

    // Rotate session key
    void rotate_key(
        E2ESession& session);

    // Revoke session
    void revoke_session(
        E2ESession& session);

    // Generate key bundle
    // for server registration
    KeyBundle generate_key_bundle(
        size_t one_time_keys = 10);

    // Verify key bundle
    bool verify_key_bundle(
        const KeyBundle& bundle) const;

    // Get identity public key
    PublicKey identity_pub() const {
        return identity_.pub;
    }

    // Key fingerprint for
    // user verification
    std::string get_fingerprint() const;

    // Safety number — shows users
    // a number to verify E2E is real
    std::string safety_number(
        const PublicKey& their_pub) const;

private:
    std::string  user_id_;
    KeyPair      identity_;
    KeyPairManager km_;
    AES256       aes_;

    // Active sessions
    std::unordered_map<
        std::string,
        E2ESession> sessions_;

    // Double ratchet algorithm
    // Forward secrecy — like Signal
    struct Ratchet {
        AESKey root_key;
        AESKey chain_key;
        uint32_t counter = 0;

        // Advance ratchet
        AESKey next_message_key();
        void advance(
            const SharedKey& dh_output);
    };

    std::unordered_map<
        std::string,
        Ratchet> ratchets_;

    // X3DH key agreement
    // Extended Triple DH
    SharedKey x3dh_sender(
        const KeyBundle& their_bundle,
        const KeyPair& ephemeral) const;

    SharedKey x3dh_receiver(
        const HandshakeMessage& msg,
        const PublicKey& their_ephemeral)
        const;

    // KDF chain
    std::pair<AESKey, AESKey> kdf_chain(
        const AESKey& chain_key) const;

    // Message key derivation
    AESKey derive_message_key(
        const AESKey& chain_key,
        uint32_t counter) const;

    // Sequence number tracking
    // prevents replay attacks
    std::unordered_map<
        std::string,
        uint64_t> seq_counters_;

    bool check_sequence(
        const E2EMessage& msg);
};

} // namespace cleainput
