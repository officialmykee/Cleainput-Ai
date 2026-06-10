#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

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
    size_t write(
        const float* data, size_t count);

    // Processing thread: read samples
    size_t read(
        float* data, size_t count);

    size_t available() const;
    void   clear();

private:
    std::vector<float>   buffer_;
    size_t               capacity_;
    std::atomic<size_t>  read_idx_;
    std::atomic<size_t>  write_idx_;
    std::atomic<size_t>  size_;
};


// ─────────────────────────────────────────
// MicConfig
// ─────────────────────────────────────────

struct MicConfig {
    uint32_t sample_rate         = 16000;
    uint32_t channels            = 1;
    float    frame_ms            = 20.0f;
    float    ring_buffer_seconds = 5.0f;
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
//   mic.set_callback([&](
//       const float* s, size_t n,
//       uint32_t sr) {
//     vad.process(s, n);
//   });
//
//   // Option B — polling (pull)
//   mic.start();
//   while (running) {
//     if (mic.available() >= frame_size)
//       mic.read(buf, frame_size);
//   }
// ─────────────────────────────────────────

using AudioCallback = std::function<
    void(const float*, size_t, uint32_t)>;

class Microphone {
public:
    explicit Microphone(
        const MicConfig& cfg);
    ~Microphone();

    Microphone(const Microphone&) = delete;
    Microphone& operator=(
        const Microphone&) = delete;

    void set_callback(AudioCallback cb);

    bool start();
    void stop();

    size_t read(float* out, size_t count);
    size_t available() const;

    bool     is_running()      const;
    uint64_t frames_captured() const;
    uint32_t overruns()        const;
    void     print_stats()     const;

    // Platform callbacks only — do not call
    void push_audio(
        const float* samples, size_t count);
    void push_audio_int16(
        const int16_t* samples, size_t count);

    struct PlatformData;
    std::unique_ptr<PlatformData> platform_;
    MicConfig cfg_;

private:
    RingBuffer            ring_buffer_;
    AudioCallback         callback_;
    std::atomic<bool>     running_;
    std::atomic<uint64_t> frames_captured_;
    std::atomic<uint32_t> overruns_;
    size_t                frame_samples_;

    std::vector<float> scratch_;
    std::vector<float> frame_scratch_;
    std::vector<float> convert_scratch_;
};

} // namespace cleainput
