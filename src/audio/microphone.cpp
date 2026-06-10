#include "microphone.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
// Import Objective-C runtime functions to handle AVAudioSession without requiring .mm extension
extern "C" {
    void* objc_getClass(const char* name);
    void* sel_registerName(const char* name);
    typedef id (*id_objc_msgSend)(id, SEL, ...);
    typedef id (*id_objc_msgSend_bool)(id, SEL, bool, id*);
    typedef bool (*bool_objc_msgSend_id_id)(id, SEL, id, id*);
    #define objc_msgSend_id ((id_objc_msgSend)objc_msgSend)
    #define objc_msgSend_bool ((id_objc_msgSend_bool)objc_msgSend)
    #define objc_msgSend_setCategory ((bool_objc_msgSend_id_id)objc_msgSend)
    extern id objc_msgSend(id self, SEL op, ...);
}
#endif

namespace cleainput {

// ─────────────────────────────────────────
// RingBuffer
// ─────────────────────────────────────────

RingBuffer::RingBuffer(size_t capacity)
    : buffer_(capacity, 0.0f)
    , capacity_(capacity)
    , read_idx_(0)
    , write_idx_(0)
    , size_(0) {}

size_t RingBuffer::write(const float* data, size_t count) {
    size_t written = 0;
    for (size_t i = 0; i < count; i++) {
        size_t current_size = size_.load(std::memory_order_acquire);

        if (current_size >= capacity_) {
            // Overrun — drop oldest sample
            read_idx_.store((read_idx_.load(std::memory_order_relaxed) + 1) % capacity_, std::memory_order_relaxed);
            size_.fetch_sub(1, std::memory_order_relaxed);
        }

        size_t w = write_idx_.load(std::memory_order_relaxed);
        buffer_[w] = data[i];
        write_idx_.store((w + 1) % capacity_, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_release);
        written++;
    }
    return written;
}

size_t RingBuffer::read(float* data, size_t count) {
    size_t available = size_.load(std::memory_order_acquire);
    size_t to_read = std::min(count, available);

    for (size_t i = 0; i < to_read; i++) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        data[i] = buffer_[r];
        read_idx_.store((r + 1) % capacity_, std::memory_order_release);
        size_.fetch_sub(1, std::memory_order_release);
    }
    return to_read;
}

size_t RingBuffer::available() const {
    return size_.load(std::memory_order_acquire);
}

void RingBuffer::clear() {
    read_idx_.store(0,  std::memory_order_relaxed);
    write_idx_.store(0, std::memory_order_relaxed);
    size_.store(0,      std::memory_order_relaxed);
}


// ─────────────────────────────────────────
// Microphone — shared logic
// ─────────────────────────────────────────

Microphone::Microphone(const MicConfig& cfg)
    : cfg_(cfg)
    , ring_buffer_(cfg.sample_rate * cfg.ring_buffer_seconds)
    , running_(false)
    , frames_captured_(0)
    , overruns_(0) {

    frame_samples_ = static_cast<size_t>(cfg_.sample_rate * cfg_.frame_ms / 1000.0f);

    // Change 2: Pre-allocate all scratch vectors at construction to avoid heap allocations in audio callbacks
    size_t estimated_max_callback_samples = 4096; // Safe buffer margin for standard high-frequency blocks
    scratch_.reserve(estimated_max_callback_samples);
    frame_scratch_.resize(frame_samples_);
    convert_scratch_.reserve(estimated_max_callback_samples);

    std::cout
        << "[OK] Microphone created\n"
        << "     Sample rate: " << cfg_.sample_rate << "\n"
        << "     Frame ms: "    << cfg_.frame_ms    << "\n"
        << "     Channels: "    << cfg_.channels    << "\n"
        << "     Ring buffer: " << cfg.ring_buffer_seconds << "s\n";
}

Microphone::~Microphone() { stop(); }

void Microphone::set_callback(AudioCallback cb) {
    callback_ = std::move(cb);
}

size_t Microphone::read(float* out, size_t count) {
    return ring_buffer_.read(out, count);
}

size_t Microphone::available() const {
    return ring_buffer_.available();
}

bool Microphone::is_running() const {
    return running_.load(std::memory_order_acquire);
}

uint64_t Microphone::frames_captured() const {
    return frames_captured_.load(std::memory_order_relaxed);
}

uint32_t Microphone::overruns() const {
    return overruns_.load(std::memory_order_relaxed);
}

void Microphone::print_stats() const {
    std::cout
        << "[MIC] Stats:\n"
        << "  Running: "
        << (is_running() ? "yes" : "no") << "\n"
        << "  Frames captured: "
        << frames_captured() << "\n"
        << "  Ring available: "
        << available() << " samples\n"
        << "  Overruns: " << overruns() << "\n";
}

void Microphone::push_audio(const float* samples, size_t count) {
    // Mix down to mono if needed (Uses preallocated scratch capacity)
    if (cfg_.channels > 1) {
        scratch_.resize(count / cfg_.channels);
        for (size_t i = 0; i < scratch_.size(); i++) {
            float mix = 0.0f;
            for (uint32_t ch = 0; ch < cfg_.channels; ch++) {
                mix += samples[i * cfg_.channels + ch];
            }
            scratch_[i] = mix / cfg_.channels;
        }
        samples = scratch_.data();
        count   = scratch_.size();
    }

    size_t written = ring_buffer_.write(samples, count);
    if (written < count) {
        overruns_.fetch_add(1, std::memory_order_relaxed);
    }

    frames_captured_.fetch_add(count, std::memory_order_relaxed);

    // Fire frame callback when enough samples ready (Uses preallocated frame_scratch_)
    if (callback_) {
        size_t avail = ring_buffer_.available();
        if (avail >= frame_samples_) {
            ring_buffer_.read(frame_scratch_.data(), frame_samples_);
            callback_(frame_scratch_.data(), frame_samples_, cfg_.sample_rate);
        }
    }
}

void Microphone::push_audio_int16(const int16_t* samples, size_t count) {
    convert_scratch_.resize(count);
    for (size_t i = 0; i < count; i++) {
        convert_scratch_[i] = samples[i] / 32768.0f;
    }
    push_audio(convert_scratch_.data(), count);
}


// ─────────────────────────────────────────
// Android — AAudio
// ─────────────────────────────────────────

#if defined(__ANDROID__)

#include <aaudio/AAudio.h>

struct Microphone::PlatformData {
    AAudioStream* stream = nullptr;
};

static aaudio_data_callback_result_t aaudio_callback(
    AAudioStream* /*stream*/,
    void* user_data,
    void* audio_data,
    int32_t  num_frames) {

    auto* mic = static_cast<Microphone*>(user_data);
    mic->push_audio(
        static_cast<const float*>(audio_data),
        static_cast<size_t>(num_frames));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool Microphone::start() {
    if (running_) return true;

    platform_ = std::make_unique<PlatformData>();

    AAudioStreamBuilder* builder;
    aaudio_result_t res = AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK) {
        std::cerr << "[MIC] AAudio builder failed: " << AAudio_convertResultToText(res) << "\n";
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setSampleRate(builder, cfg_.sample_rate);
    AAudioStreamBuilder_setChannelCount(builder, cfg_.channels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, aaudio_callback, this);

    res = AAudioStreamBuilder_openStream(builder, &platform_->stream);
    AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK) {
        std::cerr << "[MIC] AAudio open failed: " << AAudio_convertResultToText(res) << "\n";
        return false;
    }

    res = AAudioStream_requestStart(platform_->stream);
    if (res != AAUDIO_OK) {
        std::cerr << "[MIC] AAudio start failed: " << AAudio_convertResultToText(res) << "\n";
        AAudioStream_close(platform_->stream);
        return false;
    }

    running_.store(true, std::memory_order_release);
    std::cout << "[MIC] AAudio started\n";
    return true;
}

void Microphone::stop() {
    if (!running_) return;
    running_.store(false, std::memory_order_release);

    if (platform_ && platform_->stream) {
        AAudioStream_requestStop(platform_->stream);
        AAudioStream_close(platform_->stream);
        platform_->stream = nullptr;
    }
    std::cout << "[MIC] AAudio stopped\n";
}


// ─────────────────────────────────────────
// iOS — CoreAudio / RemoteIO AudioUnit
// ─────────────────────────────────────────

#elif defined(__APPLE__)

struct Microphone::PlatformData {
    AudioUnit audio_unit = nullptr;
    std::vector<float> callback_scratch; // Platform scratch allocation to prevent heap allocations inside callback
};

static OSStatus audio_unit_callback(
    void* inRefCon,
    AudioUnitRenderActionFlags* ioActionFlags,
    const AudioTimeStamp* inTimeStamp,
    UInt32                      inBusNumber,
    UInt32                      inNumberFrames,
    AudioBufferList* /*ioData*/) {

    auto* mic = static_cast<Microphone*>(inRefCon);
    auto* pd  = mic->platform_.get();

    size_t needed_samples = inNumberFrames * mic->cfg_.channels;
    if (pd->callback_scratch.size() < needed_samples) {
        // Fallback safety (should rarely hit if correctly sized initially)
        pd->callback_scratch.resize(needed_samples);
    }

    AudioBufferList buf_list;
    buf_list.mNumberBuffers = 1;
    buf_list.mBuffers[0].mNumberChannels = mic->cfg_.channels;
    buf_list.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(float) * mic->cfg_.channels;
    buf_list.mBuffers[0].mData = pd->callback_scratch.data();

    OSStatus status = AudioUnitRender(
        pd->audio_unit,
        ioActionFlags,
        inTimeStamp,
        inBusNumber,
        inNumberFrames,
        &buf_list);

    if (status == noErr) {
        mic->push_audio(pd->callback_scratch.data(), needed_samples);
    }

    return noErr;
}

bool Microphone::start() {
    if (running_) return true;

    platform_ = std::make_unique<PlatformData>();
    // Pre-allocate platform scratch vector buffer (e.g. 1024 frames default)
    platform_->callback_scratch.resize(1024 * cfg_.channels);

    AudioComponentDescription desc{};
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) {
        std::cerr << "[MIC] No RemoteIO component\n";
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &platform_->audio_unit);
    if (status != noErr) {
        std::cerr << "[MIC] AudioUnit init failed: " << status << "\n";
        return false;
    }

    UInt32 enable  = 1;
    UInt32 disable = 0;
    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input,  1,
        &enable,  sizeof(enable));
    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0,
        &disable, sizeof(disable));

    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = cfg_.sample_rate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    fmt.mChannelsPerFrame = cfg_.channels;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = sizeof(float) * cfg_.channels;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;

    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 1,
        &fmt, sizeof(fmt));

    AURenderCallbackStruct cb{};
    cb.inputProc       = audio_unit_callback;
    cb.inputProcRefCon = this;
    AudioUnitSetProperty(
        platform_->audio_unit,
        kAudioOutputUnitProperty_SetInputCallback,
        kAudioUnitScope_Global, 1,
        &cb, sizeof(cb));

    // Change 3: iOS start() function AVAudioSession configuration via pure C/C++ Runtime
    // (Ensures hardware mic functions correctly on physical devices without requiring .mm objective-C builds)
    void* cls_AVAudioSession = objc_getClass("AVAudioSession");
    if (cls_AVAudioSession) {
        void* sharedSession = objc_msgSend_id((id)cls_AVAudioSession, sel_registerName("sharedInstance"));
        if (sharedSession) {
            void* categoryRecord = objc_getClass("NSString");
            void* categoryString = objc_msgSend_id((id)categoryRecord, sel_registerName("stringWithUTF8String:"), "AVAudioSessionCategoryPlayAndRecord");
            
            // Set the audio session category to PlayAndRecord
            id error = nullptr;
            objc_msgSend_setCategory((id)sharedSession, sel_registerName("setCategory:error:"), categoryString, &error);
            
            // Activate the audio session
            objc_msgSend_bool((id)sharedSession, sel_registerName("setActive:error:"), true, &error);
        }
    }

    status = AudioUnitInitialize(platform_->audio_unit);
    if (status != noErr) {
        std::cerr << "[MIC] AudioUnit initialize failed: " << status << "\n";
        return false;
    }

    status = AudioOutputUnitStart(platform_->audio_unit);
    if (status != noErr) {
        std::cerr << "[MIC] AudioUnit start failed: " << status << "\n";
        return false;
    }

    running_.store(true, std::memory_order_release);
    std::cout << "[MIC] CoreAudio started\n";
    return true;
}

void Microphone::stop() {
    if (!running_) return;
    running_.store(false, std::memory_order_release);

    if (platform_ && platform_->audio_unit) {
        AudioOutputUnitStop(platform_->audio_unit);
        AudioUnitUninitialize(platform_->audio_unit);
        AudioComponentInstanceDispose(platform_->audio_unit);
        platform_->audio_unit = nullptr;
    }
    std::cout << "[MIC] CoreAudio stopped\n";
}

#else
#error "Unsupported platform. Define __ANDROID__ or __APPLE__."
#endif

} // namespace cleainput


