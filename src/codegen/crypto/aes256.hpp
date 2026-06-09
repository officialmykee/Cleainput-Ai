#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string>

namespace cleainput {

// AES-256 constants
static constexpr size_t AES_KEY_SIZE    = 32; // 256 bits
static constexpr size_t AES_BLOCK_SIZE  = 16; // 128 bits
static constexpr size_t AES_IV_SIZE     = 16; // 128 bits
static constexpr size_t AES_TAG_SIZE    = 16; // GCM auth tag
static constexpr size_t AES_ROUNDS     = 14; // AES-256

// Key types
using AESKey = std::array<uint8_t, AES_KEY_SIZE>;
using AESIV  = std::array<uint8_t, AES_IV_SIZE>;
using AESTag = std::array<uint8_t, AES_TAG_SIZE>;

// Encrypted payload
struct EncryptedData {
    AESIV  iv;          // random IV
    AESTag tag;         // auth tag (GCM)
    std::vector<uint8_t> ciphertext;

    // Serialize to bytes
    std::vector<uint8_t> to_bytes() const;

    // Deserialize from bytes
    static EncryptedData from_bytes(
        const std::vector<uint8_t>& data);

    // Base64 encode for storage
    std::string to_base64() const;

    // Base64 decode from storage
    static EncryptedData from_base64(
        const std::string& b64);

    bool empty() const {
        return ciphertext.empty();
    }
};

class AES256 {
public:
    AES256() = default;

    // Encrypt plaintext → EncryptedData
    // Uses AES-256-GCM
    // Random IV generated automatically
    EncryptedData encrypt(
        const std::vector<uint8_t>& plaintext,
        const AESKey& key) const;

    // Encrypt string
    EncryptedData encrypt(
        const std::string& plaintext,
        const AESKey& key) const;

    // Decrypt EncryptedData → plaintext
    std::vector<uint8_t> decrypt(
        const EncryptedData& data,
        const AESKey& key) const;

    // Decrypt to string
    std::string decrypt_str(
        const EncryptedData& data,
        const AESKey& key) const;

    // Generate random key
    static AESKey generate_key();

    // Generate random IV
    static AESIV generate_iv();

    // Derive key from password
    // Uses PBKDF2-SHA256
    static AESKey derive_key(
        const std::string& password,
        const std::string& salt,
        uint32_t iterations = 100000);

    // Hash data with SHA-256
    static std::array<uint8_t, 32> sha256(
        const std::vector<uint8_t>& data);

    // HMAC-SHA256
    static std::array<uint8_t, 32> hmac(
        const std::vector<uint8_t>& data,
        const AESKey& key);

private:
    // AES state type
    using State = std::array<
        std::array<uint8_t, 4>, 4>;

    // Round keys
    using RoundKeys = std::array<
        std::array<uint8_t, 16>,
        AES_ROUNDS + 1>;

    // Core AES operations
    void sub_bytes(State& s) const;
    void shift_rows(State& s) const;
    void mix_columns(State& s) const;
    void add_round_key(
        State& s,
        const std::array<uint8_t, 16>& rk)
        const;

    // Inverse operations
    void inv_sub_bytes(State& s) const;
    void inv_shift_rows(State& s) const;
    void inv_mix_columns(State& s) const;

    // Key expansion
    RoundKeys key_expansion(
        const AESKey& key) const;

    // Encrypt single block
    void encrypt_block(
        const uint8_t* in,
        uint8_t* out,
        const RoundKeys& rk) const;

    // Decrypt single block
    void decrypt_block(
        const uint8_t* in,
        uint8_t* out,
        const RoundKeys& rk) const;

    // GCM mode
    void gcm_encrypt(
        const uint8_t* plaintext,
        size_t len,
        const AESKey& key,
        const AESIV& iv,
        uint8_t* ciphertext,
        AESTag& tag) const;

    bool gcm_decrypt(
        const uint8_t* ciphertext,
        size_t len,
        const AESKey& key,
        const AESIV& iv,
        const AESTag& tag,
        uint8_t* plaintext) const;

    // GCM helpers
    void ghash(
        const uint8_t* h,
        const uint8_t* data,
        size_t len,
        uint8_t* output) const;

    void gctr(
        const uint8_t* key,
        const uint8_t* icb,
        const uint8_t* input,
        size_t len,
        uint8_t* output,
        const RoundKeys& rk) const;

    // S-Box lookup tables
    static const uint8_t SBOX[256];
    static const uint8_t INV_SBOX[256];
    static const uint8_t RCON[11];

    // GF(2^8) multiplication
    uint8_t gf_mul(
        uint8_t a, uint8_t b) const;

    // Random bytes
    static void random_bytes(
        uint8_t* buf, size_t len);

    // Base64
    static std::string base64_encode(
        const uint8_t* data,
        size_t len);
    static std::vector<uint8_t> base64_decode(
        const std::string& b64);
};

} // namespace cleainput
