#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>

namespace cleainput {

// ─────────────────────────────────────────
// RingBuffer — lock-free, audio-thread safe
// Single producer (audio callback),
// single consumer (processing thread)
// ─────────────────────────────────────────

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity);

    // Audio thread: write samples
    // noexcept — safe in callbacks
    size_t write(
        const float* data,
        size_t count) noexcept;

    // Processing thread: read samples
    size_t read(
        float* data,
        size_t count) noexcept;

    // Look ahead without consuming
    size_t peek(
        float* out,
        size_t count) const noexcept;

    size_t available() const noexcept;
    void   clear()     noexcept;

private:
    // Cache line padding prevents
    // false sharing between threads
    // Each atomic on its own 64-byte line
    alignas(64) std::atomic<size_t>
        read_idx_;
    alignas(64) std::atomic<size_t>
        write_idx_;
    alignas(64) std::atomic<size_t>
        size_;

    std::vector<float> buffer_;
    size_t             capacity_;
};


// ─────────────────────────────────────────
// MicConfig
// ─────────────────────────────────────────

struct MicConfig {
    uint32_t sample_rate         = 16000;
    uint32_t channels            = 1;

    // Integer ms — avoids float math
    // in frame size calculation
    // Supported: 10, 20, 30, 40, 60
    uint32_t frame_ms            = 20;

    float    ring_buffer_seconds = 5.0f;

    // Supported sample rates only:
    // 8000, 16000, 44100, 48000
    bool valid() const {
        return sample_rate == 8000
            || sample_rate == 16000
            || sample_rate == 44100
            || sample_rate == 48000;
    }
};


// ─────────────────────────────────────────
// Microphone state
// ─────────────────────────────────────────

enum class MicState {
    STOPPED,   // not started
    STARTING,  // initializing hardware
    RUNNING,   // capturing audio
    ERROR      // hardware failure
};


// ─────────────────────────────────────────
// Microphone
//
// Cross-platform audio capture.
// Compiles to AAudio on Android,
// CoreAudio/AudioUnit on iOS.
//
// Usage:
//   MicConfig cfg;
//   Microphone mic(cfg);
//
//   // Option A — callback (push)
//   // Set callback BEFORE start()
//   mic.set_callback([&](
//       const float* s, size_t n,
//       uint32_t sr) {
//     vad.process(s, n);
//   });
//   mic.start();
//
//   // Option B — polling (pull)
//   mic.start();
//   while (running) {
//     if (mic.available() >= frame_size)
//       mic.read(buf, frame_size);
//   }
// ─────────────────────────────────────────

using AudioCallback = std::function<
    void(const float*,
         size_t,
         uint32_t)>;

// Forward declaration — platform
// specific data hidden from user
struct PlatformData;

class Microphone {
public:
    explicit Microphone(
        const MicConfig& cfg);
    ~Microphone();

    // Not copyable or movable
    // Audio hardware is exclusive
    Microphone(
        const Microphone&) = delete;
    Microphone& operator=(
        const Microphone&) = delete;
    Microphone(
        Microphone&&) = delete;
    Microphone& operator=(
        Microphone&&) = delete;

    // Set callback BEFORE start()
    // Not safe to call after start()
    void set_callback(
        AudioCallback cb);

    bool start() noexcept;
    void stop()  noexcept;

    size_t read(
        float* out,
        size_t count) noexcept;

    size_t available() const noexcept;

    // Read without consuming
    // Safe for VAD lookahead
    size_t peek(
        float* out,
        size_t count) const noexcept;

    // State and stats
    MicState state()          const noexcept;
    bool     is_running()     const noexcept;
    uint64_t frames_captured()const noexcept;
    uint32_t overruns()       const noexcept;
    void     print_stats()    const;

    // Config getter — read only
    const MicConfig& config() const noexcept {
        return cfg_;
    }

private:
    // ── Core state ───────────────────
    MicConfig             cfg_;
    RingBuffer            ring_buffer_;
    std::atomic<MicState> state_;
    std::atomic<uint64_t> frames_captured_;
    std::atomic<uint32_t> overruns_;
    size_t                frame_samples_;

    // ── Callback ─────────────────────
    // Protected by callback_mutex_
    // Must be set before start()
    AudioCallback         callback_;
    std::mutex            callback_mutex_;

    // ── Pre-allocated scratch ─────────
    // Allocated in constructor
    // Never allocated in audio callback
    std::vector<float>    scratch_;
    std::vector<float>    frame_scratch_;
    std::vector<float>    convert_scratch_;

    // ── Platform data ─────────────────
    // Hidden platform implementation
    std::unique_ptr<PlatformData>
        platform_;

    // ── Internal audio push ───────────
    // Called ONLY by platform callbacks
    // Not accessible outside class
    void push_audio(
        const float* samples,
        size_t count) noexcept;

    void push_audio_int16(
        const int16_t* samples,
        size_t count) noexcept;

    // ── Platform callbacks ────────────
    // Declared as friends so they can
    // call push_audio without it
    // being public

#if defined(__ANDROID__)
    friend aaudio_data_callback_result_t
        aaudio_callback(
            AAudioStream*,
            void*, void*, int32_t);
#elif defined(__APPLE__)
    friend OSStatus
        audio_unit_callback(
            void*,
            AudioUnitRenderActionFlags*,
            const AudioTimeStamp*,
            UInt32, UInt32,
            AudioBufferList*);
#endif
};

} // namespace cleainput


