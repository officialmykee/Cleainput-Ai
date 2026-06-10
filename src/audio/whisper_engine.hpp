#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>
#include "core/arena.hpp"

namespace cleainput {

// ─────────────────────────────────────────
// WhisperConfig
// ─────────────────────────────────────────

struct WhisperConfig {
    // Model path
    std::string model_path;

    // Language — "en", "fr" etc
    // "auto" for auto detect
    std::string language   = "en";

    // Beam search size
    // Higher = more accurate
    // but slower
    uint32_t beam_size     = 5;

    // Max tokens to generate
    uint32_t max_tokens    = 448;

    // Translate to English
    bool translate         = false;

    // Print timestamps
    bool timestamps        = false;

    // Single segment output
    bool single_segment    = true;

    // Speed vs accuracy
    // 0 = fastest, 1 = balanced
    // 2 = most accurate
    uint32_t quality       = 1;

    // Minimum audio length ms
    // Ignore shorter clips
    uint32_t min_audio_ms  = 100;

    // VAD threshold
    // Skip silence chunks
    float vad_threshold    = 0.6f;

    bool valid() const {
        return !model_path.empty()
            && beam_size >= 1
            && beam_size <= 10
            && quality <= 2;
    }
};

// ─────────────────────────────────────────
// Transcription result
// ─────────────────────────────────────────

struct TranscribeResult {
    std::string text;
    std::string language;
    float       confidence     = 0.0f;
    float       latency_ms     = 0.0f;
    float       audio_duration_ms = 0.0f;
    bool        success        = false;

    // Word level timestamps
    struct Word {
        std::string text;
        float       start_ms;
        float       end_ms;
        float       confidence;
    };
    std::vector<Word> words;

    // Was this from cache
    bool cached = false;

    bool empty() const {
        return text.empty();
    }
};

// ─────────────────────────────────────────
// Internal model dimensions
// ─────────────────────────────────────────

struct WhisperModel {
    uint32_t n_vocab       = 0;
    uint32_t n_audio_ctx   = 0;
    uint32_t n_audio_state = 0;
    uint32_t n_audio_head  = 0;
    uint32_t n_audio_layer = 0;
    uint32_t n_text_ctx    = 0;
    uint32_t n_text_state  = 0;
    uint32_t n_text_head   = 0;
    uint32_t n_text_layer  = 0;
    uint32_t n_mels        = 0;

    bool loaded() const {
        return n_vocab > 0
            && n_audio_layer > 0;
    }

    size_t param_count() const {
        return n_audio_layer
             * n_audio_state
             * n_audio_state * 4
             + n_text_layer
             * n_text_state
             * n_text_state * 4;
    }
};

// ─────────────────────────────────────────
// Stream callback
// Called as audio comes in
// ─────────────────────────────────────────

using StreamCallback = std::function<
    void(const TranscribeResult&)>;

// ─────────────────────────────────────────
// WhisperEngine
//
// Speech to text engine.
// Implements Whisper architecture
// from scratch in C++.
//
// Usage:
//   WhisperConfig cfg;
//   cfg.model_path = "whisper.bin";
//   cfg.language   = "en";
//
//   WhisperEngine engine(cfg, &arena);
//   engine.load(cfg.model_path);
//
//   // Transcribe audio
//   auto result = engine.transcribe(
//       samples, n_samples);
//   std::cout << result.text << "\n";
//
//   // Stream mode
//   engine.set_stream_callback(
//       [](const TranscribeResult& r) {
//           std::cout << r.text << "\n";
//       });
//   engine.feed(samples, n_samples);
// ─────────────────────────────────────────

class WhisperEngine {
public:
    explicit WhisperEngine(
        const WhisperConfig& cfg,
        Arena* arena);

    // Not copyable or movable
    WhisperEngine(
        const WhisperEngine&) = delete;
    WhisperEngine& operator=(
        const WhisperEngine&) = delete;
    WhisperEngine(
        WhisperEngine&&) = delete;
    WhisperEngine& operator=(
        WhisperEngine&&) = delete;

    // ── Model Loading ─────────────────

    // Load model weights from file
    bool load(
        const std::string& model_path);

    bool is_ready() const;

    const WhisperModel& model()
        const { return model_; }

    // ── Transcription ─────────────────

    // Transcribe float PCM
    // 16kHz mono required
    TranscribeResult transcribe(
        const float* samples,
        size_t n_samples);

    // Transcribe int16 PCM
    TranscribeResult transcribe_int16(
        const int16_t* samples,
        size_t n_samples);

    // ── Streaming ─────────────────────

    // Feed audio chunks
    // Transcribes automatically
    // when enough audio buffered
    void feed(
        const float* samples,
        size_t n_samples);

    // Set streaming callback
    void set_stream_callback(
        StreamCallback cb);

    // Reset stream buffer
    void reset_stream();

    // ── Config ────────────────────────

    const WhisperConfig& config()
        const { return cfg_; }

    void set_language(
        const std::string& lang) {
        cfg_.language = lang;
    }

    // ── Stats ─────────────────────────

    uint64_t transcription_count()
        const;
    void print_stats() const;

private:
    WhisperConfig cfg_;
    Arena*        arena_;
    WhisperModel  model_;

    // Ready flag
    std::atomic<bool> ready_;

    // Stats
    std::atomic<uint64_t>
        transcriptions_;

    // ── Audio Processing ──────────────

    // Mel filterbank
    std::vector<std::vector<float>>
        mel_filters_;

    // Hann window
    std::vector<float> hann_window_;

    // Pre-allocated buffers
    std::vector<float> mel_buffer_;
    std::vector<float> fft_re_;
    std::vector<float> fft_im_;

    // ── Streaming state ───────────────

    std::vector<float> stream_buffer_;
    StreamCallback     stream_callback_;

    // ── Internal methods ──────────────

    // Compute log mel spectrogram
    std::vector<float> compute_mel(
        const float* samples,
        size_t n_samples);

    // Greedy token decode
    std::string greedy_decode(
        const std::vector<float>& mel);

    // Beam search decode
    std::string beam_search_decode(
        const std::vector<float>& mel,
        uint32_t beam_size);

    // Audio encoder forward pass
    std::vector<float> encode_audio(
        const std::vector<float>& mel);

    // Text decoder forward pass
    std::string decode_text(
        const std::vector<float>&
            audio_features,
        const std::string& language);

    // Token to text
    std::string token_to_text(
        uint32_t token_id) const;

    // Detect language from audio
    std::string detect_language(
        const std::vector<float>& mel);

    // Validate audio input
    bool validate_audio(
        const float* samples,
        size_t n_samples) const;

    // Normalize audio
    void normalize_audio(
        float* samples,
        size_t n_samples) const;
};

} // namespace cleainput
