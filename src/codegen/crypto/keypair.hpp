#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include "aes256.hpp"

namespace cleainput {

// Key sizes
static constexpr size_t EC_KEY_SIZE      = 32; // 256 bits
static constexpr size_t EC_SIG_SIZE      = 64; // 512 bits
static constexpr size_t EC_SHARED_SIZE   = 32; // 256 bits

// Key types
using PublicKey  = std::array<uint8_t, EC_KEY_SIZE>;
using PrivateKey = std::array<uint8_t, EC_KEY_SIZE>;
using Signature  = std::array<uint8_t, EC_SIG_SIZE>;
using SharedKey  = std::array<uint8_t, EC_SHARED_SIZE>;

// A complete key pair
struct KeyPair {
    PublicKey  pub;
    PrivateKey priv;

    // Serialize public key to hex string
    std::string pub_to_hex() const;

    // Serialize public key to base64
    std::string pub_to_base64() const;

    // Check if valid
    bool valid() const;

    // Zero out private key from memory
    // Call when done using
    void zero_private() {
        priv.fill(0);
    }
};

// Stored encrypted key pair
// Private key never stored in plaintext
struct StoredKeyPair {
    PublicKey        pub;
    EncryptedData    encrypted_priv;
    std::string      key_id;
    uint64_t         created_at;

    // Unlock private key with password
    PrivateKey unlock(
        const std::string& password,
        const std::string& salt) const;

    // Lock private key with password
    static StoredKeyPair lock(
        const KeyPair& kp,
        const std::string& password,
        const std::string& salt);
};

// Diffie-Hellman shared secret
struct DHExchange {
    PublicKey  our_public;
    PrivateKey our_private;
    PublicKey  their_public;
    SharedKey  shared_secret;

    bool complete() const {
        return shared_secret !=
            SharedKey{};
    }
};

class KeyPairManager {
public:
    KeyPairManager() = default;

    // Generate new key pair
    // Uses Curve25519
    KeyPair generate() const;

    // Sign data with private key
    Signature sign(
        const std::vector<uint8_t>& data,
        const PrivateKey& priv) const;

    // Verify signature
    bool verify(
        const std::vector<uint8_t>& data,
        const Signature& sig,
        const PublicKey& pub) const;

    // Diffie-Hellman key exchange
    // Used for E2E encryption setup
    DHExchange dh_exchange(
        const PrivateKey& our_priv,
        const PublicKey& their_pub) const;

    // Derive shared AES key from
    // DH shared secret
    AESKey derive_shared_key(
        const SharedKey& shared) const;

    // Encrypt message for recipient
    // Only recipient's private key
    // can decrypt
    EncryptedData encrypt_for(
        const std::string& message,
        const PublicKey& recipient_pub,
        const PrivateKey& sender_priv) const;

    // Decrypt message from sender
    std::string decrypt_from(
        const EncryptedData& data,
        const PrivateKey& our_priv,
        const PublicKey& sender_pub) const;

    // Key serialization
    std::string pub_to_hex(
        const PublicKey& pub) const;
    std::string pub_to_base64(
        const PublicKey& pub) const;
    PublicKey pub_from_hex(
        const std::string& hex) const;
    PublicKey pub_from_base64(
        const std::string& b64) const;

    // Fingerprint for key verification
    // Shows user a short string to
    // verify they have the right key
    std::string fingerprint(
        const PublicKey& pub) const;

private:
    AES256 aes_;

    // Curve25519 operations
    void curve25519_mul(
        uint8_t* result,
        const uint8_t* scalar,
        const uint8_t* point) const;

    void curve25519_base(
        uint8_t* result,
        const uint8_t* scalar) const;

    // Ed25519 signing
    void ed25519_sign(
        uint8_t* sig,
        const uint8_t* msg,
        size_t msg_len,
        const uint8_t* priv) const;

    bool ed25519_verify(
        const uint8_t* sig,
        const uint8_t* msg,
        size_t msg_len,
        const uint8_t* pub) const;

    // HKDF key derivation
    SharedKey hkdf(
        const SharedKey& input,
        const std::string& info) const;

    // Random scalar generation
    PrivateKey random_scalar() const;

    // Clamp scalar for Curve25519
    void clamp_scalar(
        PrivateKey& scalar) const;
};

} // namespace cleainput
