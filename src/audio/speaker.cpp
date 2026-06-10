#include "speaker.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace cleainput {

// ─────────────────────────────────────────
// Speaker — shared logic
// ─────────────────────────────────────────

Speaker::Speaker(const SpeakerConfig& cfg)
    : cfg_(cfg)
    , ring_buffer_(
        cfg.sample_rate *
        cfg.ring_buffer_seconds)
    , state_(PlaybackState::IDLE)
    , frames_played_(0)
    , underruns_(0)
    , volume_(cfg.volume) {

    frame_samples_ = static_cast<size_t>(
        cfg_.sample_rate *
        cfg_.frame_ms / 1000.0f);

    // Pre-allocate all buffers
    // Never allocate in callback
    output_scratch_.resize(
        frame_samples_ * cfg_.channels,
        0.0f);
    silence_buffer_.resize(
        frame_samples_ * cfg_.channels,
        0.0f);
    float_buffer_.resize(
        frame_samples_ * cfg_.channels,
        0.0f);
    convert_scratch_.resize(
        cfg_.sample_rate * cfg_.channels,
        0.0f);

    std::cout
        << "[OK] Speaker created\n"
        << "     Sample rate: "
        << cfg_.sample_rate << "\n"
        << "     Frame ms: "
        << cfg_.frame_ms    << "\n"
        << "     Channels: "
        << cfg_.channels    << "\n"
        << "     Volume: "
        << cfg_.volume      << "\n";
}

Speaker::~Speaker() { stop(); }

void Speaker::set_completion_callback(
    CompletionCallback cb) {
    std::lock_guard<std::mutex>
        lock(callback_mutex_);
    completion_cb_ = std::move(cb);
}

bool Speaker::is_playing() const noexcept {
    return state_.load(
        std::memory_order_acquire)
        == PlaybackState::PLAYING;
}

bool Speaker::is_running() const noexcept {
    return state_.load(
        std::memory_order_acquire)
        != PlaybackState::STOPPED
        && state_.load(
            std::memory_order_acquire)
        != PlaybackState::IDLE;
}

PlaybackState Speaker::state()
    const noexcept {
    return state_.load(
        std::memory_order_acquire);
}

uint64_t Speaker::frames_played()
    const noexcept {
    return frames_played_.load(
        std::memory_order_relaxed);
}

uint32_t Speaker::underruns()
    const noexcept {
    return underruns_.load(
        std::memory_order_relaxed);
}

void Speaker::set_volume(
    float vol) noexcept {
    // Clamp 0.0 to 1.0
    vol = std::max(0.0f,
          std::min(1.0f, vol));
    volume_.store(vol,
        std::memory_order_relaxed);
}

float Speaker::volume() const noexcept {
    return volume_.load(
        std::memory_order_relaxed);
}

void Speaker::pause() noexcept {
    PlaybackState expected =
        PlaybackState::PLAYING;
    state_.compare_exchange_strong(
        expected,
        PlaybackState::PAUSED,
        std::memory_order_acq_rel);
}

void Speaker::resume() noexcept {
    PlaybackState expected =
        PlaybackState::PAUSED;
    state_.compare_exchange_strong(
        expected,
        PlaybackState::PLAYING,
        std::memory_order_acq_rel);
}

void Speaker::drain() noexcept {
    // Wait until ring buffer empty
    while (ring_buffer_.available() > 0
        && is_running()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(5));
    }
}

bool Speaker::play(
    const float* samples,
    size_t count) noexcept {

    if (!samples || count == 0)
        return false;

    // Write to ring buffer
    size_t written =
        ring_buffer_.write(
            samples, count);

    if (written < count)
        underruns_.fetch_add(
            1,
            std::memory_order_relaxed);

    // Start playing if idle
    PlaybackState expected =
        PlaybackState::IDLE;
    state_.compare_exchange_strong(
        expected,
        PlaybackState::PLAYING,
        std::memory_order_acq_rel);

    return written > 0;
}

bool Speaker::play_int16(
    const int16_t* samples,
    size_t count) noexcept {

    if (!samples || count == 0)
        return false;

    // Convert int16 → float
    // Reuse pre-allocated buffer
    if (count > convert_scratch_.size())
        return false;

    for (size_t i = 0; i < count; i++)
        convert_scratch_[i] =
            samples[i] / 32768.0f;

    return play(
        convert_scratch_.data(), count);
}

void Speaker::print_stats() const {
    std::cout
        << "[SPK] Stats:\n"
        << "  State: "
        << (is_playing()
            ? "PLAYING" : "IDLE")
        << "\n"
        << "  Frames played: "
        << frames_played() << "\n"
        << "  Buffer available: "
        << ring_buffer_.available()
        << " samples\n"
        << "  Underruns: "
        << underruns() << "\n"
        << "  Volume: "
        << volume() << "\n";
}

// Core audio processing
// Called from platform callback
// NEVER allocate memory here
void Speaker::pull_audio(
    float* output,
    size_t count) noexcept {

    if (state_.load(
            std::memory_order_acquire)
            != PlaybackState::PLAYING) {
        // Output silence
        memset(output, 0,
            count * sizeof(float));
        return;
    }

    size_t available =
        ring_buffer_.available();

    if (available >= count) {
        // Read from ring buffer
        ring_buffer_.read(
            output, count);
    } else if (available > 0) {
        // Partial read — fill rest
        // with silence
        ring_buffer_.read(
            output, available);
        memset(
            output + available,
            0,
            (count - available)
            * sizeof(float));
        underruns_.fetch_add(
            1,
            std::memory_order_relaxed);
    } else {
        // Underrun — output silence
        memset(output, 0,
            count * sizeof(float));
        underruns_.fetch_add(
            1,
            std::memory_order_relaxed);

        // Buffer empty — go idle
        state_.store(
            PlaybackState::DRAINING,
            std::memory_order_release);
    }

    float vol = volume_.load(
        std::memory_order_relaxed);

    // Apply volume + processing
    for (size_t i = 0; i < count; i++) {
        // Apply volume
        float s = output[i] * vol;

        // DC offset removal
        dc_offset_ +=
            (s - dc_offset_) * 0.001f;
        s -= dc_offset_;

        // Soft clipping
        // Prevents harsh distortion
        if (s > 0.95f || s < -0.95f)
            s = tanhf(s * 0.95f);

        // Hard clip safety
        s = std::max(-1.0f,
            std::min(1.0f, s));

        output[i] = s;
    }

    frames_played_.fetch_add(
        count,
        std::memory_order_relaxed);

    // Fire completion callback
    // when buffer drains
    if (state_.load(
            std::memory_order_acquire)
            == PlaybackState::DRAINING) {
        state_.store(
            PlaybackState::IDLE,
            std::memory_order_release);
        std::lock_guard<std::mutex>
            lock(callback_mutex_);
        if (completion_cb_)
            completion_cb_();
    }
}


// ─────────────────────────────────────────
// Android — AAudio Output
// ─────────────────────────────────────────

#if defined(__ANDROID__)

#include <aaudio/AAudio.h>

struct Speaker::PlatformData {
    AAudioStream* stream = nullptr;
};

static aaudio_data_callback_result_t
aaudio_out_callback(
    AAudioStream* /*stream*/,
    void*    user_data,
    void*    audio_data,
    int32_t  num_frames) {

    auto* spk =
        static_cast<Speaker*>(user_data);
    spk->pull_audio(
        static_cast<float*>(audio_data),
        static_cast<size_t>(num_frames));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool Speaker::start() noexcept {
    if (is_running()) return true;

    platform_ =
        std::make_unique<PlatformData>();

    AAudioStreamBuilder* builder;
    aaudio_result_t res =
        AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK) {
        std::cerr
            << "[SPK] AAudio builder "
               "failed: "
            << AAudio_convertResultToText(
                res) << "\n";
        return false;
    }

    // Output direction
    AAudioStreamBuilder_setDirection(
        builder,
        AAUDIO_DIRECTION_OUTPUT);

    // Voice communication usage
    AAudioStreamBuilder_setUsage(
        builder,
        AAUDIO_USAGE_VOICE_COMMUNICATION);

    // Speech content type
    AAudioStreamBuilder_setContentType(
        builder,
        AAUDIO_CONTENT_TYPE_SPEECH);

    AAudioStreamBuilder_setSampleRate(
        builder, cfg_.sample_rate);
    AAudioStreamBuilder_setChannelCount(
        builder, cfg_.channels);
    AAudioStreamBuilder_setFormat(
        builder,
        AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setPerformanceMode(
        builder,
        AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(
        builder,
        aaudio_out_callback,
        this);

    res = AAudioStreamBuilder_openStream(
        builder, &platform_->stream);
    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK) {
        std::cerr
            << "[SPK] AAudio open "
               "failed: "
            << AAudio_convertResultToText(
                res) << "\n";
        return false;
    }

    res = AAudioStream_requestStart(
        platform_->stream);
    if (res != AAUDIO_OK) {
        std::cerr
            << "[SPK] AAudio start "
               "failed: "
            << AAudio_convertResultToText(
                res) << "\n";
        AAudioStream_close(
            platform_->stream);
        return false;
    }

    state_.store(
        PlaybackState::IDLE,
        std::memory_order_release);

    std::cout
        << "[SPK] AAudio output "
           "started\n";
    return true;
}

void Speaker::stop() noexcept {
    if (!is_running()) return;

    drain();

    state_.store(
        PlaybackState::STOPPED,
        std::memory_order_release);

    if (platform_ &&
        platform_->stream) {
        AAudioStream_requestStop(
            platform_->stream);
        AAudioStream_close(
            platform_->stream);
        platform_->stream = nullptr;
    }

    std::cout
        << "[SPK] AAudio stopped\n";
}


// ─────────────────────────────────────────
// iOS — CoreAudio Output
// ─────────────────────────────────────────

#elif defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <AVFoundation/AVFoundation.h>

struct Speaker::PlatformData {
    AudioUnit audio_unit = nullptr;
};

static OSStatus speaker_unit_callback(
    void* inRefCon,
    AudioUnitRenderActionFlags*,
    const AudioTimeStamp*,
    UInt32,
    UInt32   inNumberFrames,
    AudioBufferList* ioData) {

    auto* spk =
        static_cast<Speaker*>(inRefCon);

    float* output =
        static_cast<float*>(
            ioData->mBuffers[0].mData);

    spk->pull_audio(
        output,
        inNumberFrames);

    return noErr;
}

bool Speaker::start() noexcept {
    if (is_running()) return true;

    platform_ =
        std::make_unique<PlatformData>();

    // Setup AVAudioSession
    // for playback
    AVAudioSession* session =
        [AVAudioSession sharedInstance];
    [session
        setCategory:
            AVAudioSessionCategoryPlayback
        error:nil];

    // Set preferred buffer duration
    // 20ms for low latency
    [session
        setPreferredIOBufferDuration:0.02
        error:nil];
    [session setActive:YES error:nil];

    // Find RemoteIO component
    AudioComponentDescription desc{};
    desc.componentType =
        kAudioUnitType_Output;
    desc.componentSubType =
        kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer =
        kAudioUnitManufacturer_Apple;

    AudioComponent comp =
        AudioComponentFindNext(
            nullptr, &desc);
    if (!comp) {
        std::cerr
            << "[SPK] No RemoteIO "
               "component\n";
        return false;
    }

    OSStatus status =
        AudioComponentInstanceNew(
            comp,
            &platform_->audio_unit);
    if (status != noErr) {
        std::cerr
            << "[SPK] AudioUnit init "
               "failed: "
            << status << "\n";
        return false;
    }

    // Enable output bus 0
    UInt32 enable  = 1;
    UInt32 disable = 0;
    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0,
        &enable, sizeof(enable));

    // Disable input bus 1
    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input, 1,
        &disable, sizeof(disable));

    // Set output format
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = cfg_.sample_rate;
    fmt.mFormatID =
        kAudioFormatLinearPCM;
    fmt.mFormatFlags =
        kAudioFormatFlagIsFloat
        | kAudioFormatFlagIsPacked
        | kAudioFormatFlagsNativeEndian;
    fmt.mChannelsPerFrame =
        cfg_.channels;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    =
        sizeof(float) * cfg_.channels;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerPacket   =
        fmt.mBytesPerFrame;

    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0,
        &fmt, sizeof(fmt));

    // Set render callback
    AURenderCallbackStruct cb{};
    cb.inputProc = speaker_unit_callback;
    cb.inputProcRefCon = this;

    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0,
        &cb, sizeof(cb));

    // Initialize and start
    status = AudioUnitInitialize(
        platform_->audio_unit);
    if (status != noErr) {
        std::cerr
            << "[SPK] AudioUnit "
               "initialize failed: "
            << status << "\n";
        return false;
    }

    status = AudioOutputUnitStart(
        platform_->audio_unit);
    if (status != noErr) {
        std::cerr
            << "[SPK] AudioUnit start "
               "failed: "
            << status << "\n";
        return false;
    }

    state_.store(
        PlaybackState::IDLE,
        std::memory_order_release);

    std::cout
        << "[SPK] CoreAudio output "
           "started\n";
    return true;
}

void Speaker::stop() noexcept {
    if (!is_running()) return;

    drain();

    state_.store(
        PlaybackState::STOPPED,
        std::memory_order_release);

    if (platform_ &&
        platform_->audio_unit) {
        AudioOutputUnitStop(
            platform_->audio_unit);
        AudioUnitUninitialize(
            platform_->audio_unit);
        AudioComponentInstanceDispose(
            platform_->audio_unit);
        platform_->audio_unit = nullptr;
    }

    std::cout
        << "[SPK] CoreAudio stopped\n";
}

#else
#error "Unsupported platform. \
Define __ANDROID__ or __APPLE__."
#endif

} // namespace cleainput
