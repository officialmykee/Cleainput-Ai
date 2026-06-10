#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace cleainput {

// Compression algorithms
enum class CompressAlgo {
    NONE,     // no compression
    ZSTD,     // best ratio + speed
    LZ4,      // fastest
    BROTLI,   // best for text/HTML
    OPUS,     // audio only
    DEFLATE   // compatibility
};

// Compression level
enum class CompressLevel {
    FASTEST,  // lowest CPU
    BALANCED, // good tradeoff
    BEST,     // smallest size
};

// Compression result
struct CompressResult {
    std::vector<uint8_t> data;
    size_t   original_size;
    size_t   compressed_size;
    float    ratio;          // < 1 = smaller
    float    time_ms;
    CompressAlgo algo;

    float savings_pct() const {
        return (1.0f - ratio) * 100.0f;
    }

    bool worth_it() const {
        // Only use if saves >10%
        return ratio < 0.90f;
    }
};

// Audio compression config
struct AudioCompressConfig {
    uint32_t sample_rate  = 16000;
    uint8_t  channels     = 1;
    uint32_t bitrate      = 16000; // 16kbps
    bool     vbr          = true;  // variable
    bool     dtx          = true;  // silence skip
    uint32_t frame_size   = 960;   // 60ms
};

// Text compression stats
struct TextStats {
    size_t   original_bytes;
    size_t   compressed_bytes;
    float    ratio;
    uint32_t tokens_saved;
    float    cost_saved_usd;
};

class Compression {
public:
    Compression() = default;

    // ─── General Compression ─────────

    // Auto-pick best algorithm
    CompressResult compress(
        const std::vector<uint8_t>& data,
        CompressLevel level
            = CompressLevel::BALANCED);

    // Compress string
    CompressResult compress_str(
        const std::string& text,
        CompressAlgo algo
            = CompressAlgo::ZSTD,
        CompressLevel level
            = CompressLevel::BALANCED);

    // Decompress
    std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& data,
        CompressAlgo algo,
        size_t original_size = 0);

    // Decompress to string
    std::string decompress_str(
        const std::vector<uint8_t>& data,
        CompressAlgo algo,
        size_t original_size = 0);

    // ─── ZSTD ────────────────────────
    // Best general purpose
    // Facebook's algorithm
    // 3-5x compression on text

    std::vector<uint8_t> zstd_compress(
        const uint8_t* data,
        size_t size,
        int level = 3) const;

    std::vector<uint8_t> zstd_decompress(
        const uint8_t* data,
        size_t size,
        size_t original_size) const;

    // Train ZSTD dictionary
    // on your specific data
    // Gets 10x better compression
    std::vector<uint8_t> train_zstd_dict(
        const std::vector<
            std::string>& samples,
        size_t dict_size = 112640);

    // Compress with dictionary
    std::vector<uint8_t>
        zstd_compress_with_dict(
        const uint8_t* data,
        size_t size,
        const std::vector<uint8_t>& dict)
        const;

    // ─── LZ4 ─────────────────────────
    // Fastest compression
    // Use for real-time data

    std::vector<uint8_t> lz4_compress(
        const uint8_t* data,
        size_t size) const;

    std::vector<uint8_t> lz4_decompress(
        const uint8_t* data,
        size_t size,
        size_t original_size) const;

    // LZ4 streaming for audio
    class LZ4Stream {
    public:
        LZ4Stream();
        ~LZ4Stream();
        std::vector<uint8_t> compress_chunk(
            const uint8_t* data,
            size_t size);
        std::vector<uint8_t> decompress_chunk(
            const uint8_t* data,
            size_t size,
            size_t original_size);
    private:
        void* stream_  = nullptr;
        void* dstream_ = nullptr;
    };

    // ─── Brotli ──────────────────────
    // Best for text/HTML/JSON
    // Use for API responses

    std::vector<uint8_t> brotli_compress(
        const uint8_t* data,
        size_t size,
        int quality = 6) const;

    std::vector<uint8_t> brotli_decompress(
        const uint8_t* data,
        size_t size) const;

    // ─── Audio (Opus) ─────────────────
    // 16x smaller than raw PCM
    // Perfect for voice AI

    std::vector<uint8_t> opus_encode(
        const std::vector<float>& pcm,
        const AudioCompressConfig& cfg
            = AudioCompressConfig{});

    std::vector<float> opus_decode(
        const std::vector<uint8_t>& opus,
        const AudioCompressConfig& cfg
            = AudioCompressConfig{});

    // Encode audio stream
    class OpusStream {
    public:
        explicit OpusStream(
            const AudioCompressConfig& cfg);
        ~OpusStream();

        // Encode chunk of PCM
        std::vector<uint8_t> encode(
            const float* pcm,
            size_t samples);

        // Decode chunk of Opus
        std::vector<float> decode(
            const uint8_t* data,
            size_t size);

        // Stats
        float compression_ratio() const;
        uint64_t bytes_saved() const;

    private:
        void* encoder_ = nullptr;
        void* decoder_ = nullptr;
        AudioCompressConfig cfg_;
        uint64_t pcm_bytes_in_  = 0;
        uint64_t opus_bytes_out_ = 0;
    };

    // ─── AI Token Compression ─────────
    // Compress prompts before
    // sending to LLM
    // Reduces token count = cheaper

    struct TokenCompressResult {
        std::string compressed_prompt;
        uint32_t    original_tokens;
        uint32_t    compressed_tokens;
        float       compression_ratio;
        float       cost_saved_usd;
    };

    // Remove redundant words
    // "Please can you kindly explain
    //  to me what is AI"
    // → "explain AI"
    TokenCompressResult compress_prompt(
        const std::string& prompt) const;

    // Compress conversation history
    // Summarize old messages
    TokenCompressResult compress_history(
        const std::vector<std::string>&
            messages,
        size_t keep_last = 5) const;

    // ─── Delta Compression ───────────
    // Only send what changed
    // 10x savings on updates

    std::vector<uint8_t> delta_compress(
        const std::string& old_text,
        const std::string& new_text) const;

    std::string delta_decompress(
        const std::string& old_text,
        const std::vector<uint8_t>& delta)
        const;

    // ─── Stats ───────────────────────

    struct GlobalStats {
        uint64_t bytes_in;
        uint64_t bytes_out;
        float    avg_ratio;
        float    bandwidth_saved_mb;
        float    cost_saved_usd;
        uint64_t compress_calls;
    };

    GlobalStats get_stats() const {
        return stats_;
    }

    // Reset stats
    void reset_stats();

    // Best algorithm for data type
    static CompressAlgo best_algo(
        const std::string& content_type);

    // Estimate compressed size
    static size_t estimate_size(
        size_t original,
        CompressAlgo algo);

private:
    mutable GlobalStats stats_{};

    // Update stats after compression
    void update_stats(
        size_t in,
        size_t out) const;

    // Detect data type
    static bool is_text(
        const uint8_t* data,
        size_t size);
    static bool is_audio(
        const uint8_t* data,
        size_t size);
    static bool is_json(
        const std::string& s);

    // Remove filler words
    static std::string remove_fillers(
        const std::string& text);

    // Count tokens estimate
    static uint32_t estimate_tokens(
        const std::string& text);
};

} // namespace cleainput
