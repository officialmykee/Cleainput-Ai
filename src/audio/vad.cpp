#include "vad.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numeric>

namespace cleainput {

// Constants
static constexpr float
    ENERGY_FLOOR = 1e-10f;
static constexpr float
    LOG_ENERGY_FLOOR =
        -10.0f * log10f(ENERGY_FLOOR);

VAD::VAD(const VADConfig& cfg)
    : cfg_(cfg) {
    reset();
    std::cout << "[OK] VAD initialized\n"
              << "     Sample rate: "
              << cfg_.sample_rate << "\n"
              << "     Frame ms: "
              << cfg_.frame_ms << "\n"
              << "     Threshold: "
              << cfg_.energy_threshold
              << "\n";
}

void VAD::reset() {
    state_          = VADState::SILENCE;
    silence_ms_     = 0;
    speech_ms_      = 0;
    energy_history_.clear();
    energy_history_.resize(
        cfg_.history_frames, 0.0f);
    history_idx_    = 0;
    is_speech_      = false;
    noise_floor_    = 0.01f;
    speech_detected_ = false;
}

// Compute frame energy (RMS)
float VAD::compute_energy(
    const float* samples,
    size_t count) const {

    if (count == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < count; i++)
        sum += samples[i] * samples[i];

    return sqrtf(sum / count);
}

// Compute zero crossing rate
float VAD::compute_zcr(
    const float* samples,
    size_t count) const {

    if (count < 2) return 0.0f;

    uint32_t crossings = 0;
    for (size_t i = 1; i < count; i++) {
        if ((samples[i] >= 0.0f) !=
            (samples[i-1] >= 0.0f))
            crossings++;
    }

    return (float)crossings
         / (float)(count - 1);
}

// Compute spectral centroid
// Higher = more likely speech
float VAD::compute_spectral(
    const float* samples,
    size_t count) const {

    float weighted_sum = 0.0f;
    float total_energy = 0.0f;

    for (size_t i = 0;
         i < count; i++) {
        float mag = fabsf(samples[i]);
        weighted_sum += (float)i * mag;
        total_energy += mag;
    }

    if (total_energy < ENERGY_FLOOR)
        return 0.0f;

    return weighted_sum / total_energy;
}

// Update noise floor estimate
void VAD::update_noise_floor(
    float energy) {

    // Slow adaptation to noise
    // Fast adaptation downward
    if (energy < noise_floor_) {
        noise_floor_ = noise_floor_
            * 0.99f + energy * 0.01f;
    } else {
        noise_floor_ = noise_floor_
            * 0.999f + energy * 0.001f;
    }

    // Clamp noise floor
    noise_floor_ = std::max(
        noise_floor_, 0.001f);
}

// Main VAD processing
VADResult VAD::process(
    const float* samples,
    size_t count) {

    VADResult result;

    // Compute features
    float energy = compute_energy(
        samples, count);
    float zcr    = compute_zcr(
        samples, count);

    // Update noise floor
    update_noise_floor(energy);

    // Dynamic threshold
    float threshold =
        noise_floor_ *
        cfg_.energy_threshold * 10.0f;

    // Update energy history
    energy_history_[history_idx_] =
        energy;
    history_idx_ = (history_idx_ + 1)
        % cfg_.history_frames;

    // Smooth energy over history
    float avg_energy = 0.0f;
    for (float e : energy_history_)
        avg_energy += e;
    avg_energy /= energy_history_.size();

    // Speech detection logic
    bool raw_speech =
        energy > threshold &&
        zcr > cfg_.zcr_min &&
        zcr < cfg_.zcr_max;

    // Frame duration in ms
    float frame_ms =
        (float)count /
        (float)cfg_.sample_rate * 1000.0f;

    // State machine
    switch (state_) {

    case VADState::SILENCE:
        if (raw_speech) {
            speech_ms_ += frame_ms;
            if (speech_ms_ >=
                cfg_.speech_onset_ms) {
                state_ = VADState::SPEECH;
                is_speech_ = true;
                speech_detected_ = true;
                silence_ms_ = 0;
                std::cout <<
                    "[VAD] Speech start\n";
            }
        } else {
            speech_ms_ = 0;
        }
        result.is_speech = false;
        break;

    case VADState::SPEECH:
        if (!raw_speech) {
            silence_ms_ += frame_ms;
            state_ = VADState::ENDING;
        } else {
            speech_ms_ += frame_ms;
            silence_ms_ = 0;
        }
        result.is_speech = true;
        break;

    case VADState::ENDING:
        if (raw_speech) {
            // False ending — back to speech
            state_      = VADState::SPEECH;
            silence_ms_ = 0;
            result.is_speech = true;
        } else {
            silence_ms_ += frame_ms;
            result.is_speech = true;

            if (silence_ms_ >=
                cfg_.silence_duration_ms) {
                // Speech is complete
                state_      =
                    VADState::SILENCE;
                is_speech_  = false;
                speech_ms_  = 0;
                silence_ms_ = 0;

                result.speech_complete
                    = true;
                result.is_speech = false;

                std::cout <<
                    "[VAD] Speech end — "
                    "complete!\n";
            }
        }
        break;
    }

    result.energy      = energy;
    result.noise_floor = noise_floor_;
    result.speech_ms   = speech_ms_;
    result.silence_ms  = silence_ms_;
    result.state       = state_;

    // Confidence score
    float snr = energy /
        (noise_floor_ + ENERGY_FLOOR);
    result.confidence = std::min(
        1.0f,
        (snr - 1.0f) / 9.0f);
    result.confidence = std::max(
        0.0f,
        result.confidence);

    return result;
}

// Process int16 audio
VADResult VAD::process_int16(
    const int16_t* samples,
    size_t count) {

    // Convert to float
    std::vector<float> f(count);
    for (size_t i = 0; i < count; i++)
        f[i] = samples[i] / 32768.0f;

    return process(f.data(), count);
}

// Check if currently in speech
bool VAD::is_speech() const {
    return is_speech_;
}

// Check if speech just completed
bool VAD::speech_complete() const {
    return !is_speech_ &&
           speech_detected_;
}

// Get current state
VADState VAD::state() const {
    return state_;
}

// Get silence duration ms
float VAD::silence_ms() const {
    return silence_ms_;
}

// Get speech duration ms
float VAD::speech_ms() const {
    return speech_ms_;
}

// Print VAD stats
void VAD::print_stats() const {
    std::cout << "[VAD] Stats:\n"
              << "  State: "
              << (state_ ==
                  VADState::SPEECH
                  ? "SPEECH" :
                  state_ ==
                  VADState::ENDING
                  ? "ENDING"
                  : "SILENCE")
              << "\n"
              << "  Speech ms: "
              << speech_ms_ << "\n"
              << "  Silence ms: "
              << silence_ms_ << "\n"
              << "  Noise floor: "
              << noise_floor_ << "\n";
}

} // namespace cleainput
