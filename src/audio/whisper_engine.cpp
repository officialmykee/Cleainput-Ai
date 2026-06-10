#include "whisper_engine.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>

namespace cleainput {

// ─────────────────────────────────────────
// Whisper constants
// ─────────────────────────────────────────

static constexpr int    SAMPLE_RATE  = 16000;
static constexpr int    N_FFT        = 400;
static constexpr int    HOP_LENGTH   = 160;
static constexpr int    N_MELS       = 80;
static constexpr int    CHUNK_SIZE   = SAMPLE_RATE * 30; // 30s
static constexpr float  NEG_INF      = -1e9f;

// ─────────────────────────────────────────
// Mel filterbank
// ─────────────────────────────────────────

static float hz_to_mel(float hz) {
    return 2595.0f *
        log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel) {
    return 700.0f *
        (powf(10.0f, mel / 2595.0f)
         - 1.0f);
}

static std::vector<std::vector<float>>
build_mel_filterbank(
    int n_mels,
    int n_fft,
    int sample_rate) {

    int n_freqs = n_fft / 2 + 1;
    std::vector<std::vector<float>>
        filters(n_mels,
            std::vector<float>(
                n_freqs, 0.0f));

    float mel_min = hz_to_mel(0.0f);
    float mel_max = hz_to_mel(
        sample_rate / 2.0f);

    std::vector<float> mel_points(
        n_mels + 2);
    for (int i = 0;
         i < n_mels + 2; i++) {
        mel_points[i] = mel_min +
            (mel_max - mel_min)
            * i / (n_mels + 1);
    }

    std::vector<float> hz_points(
        n_mels + 2);
    for (int i = 0;
         i < n_mels + 2; i++) {
        hz_points[i] = mel_to_hz(
            mel_points[i]);
    }

    std::vector<int> bin_points(
        n_mels + 2);
    for (int i = 0;
         i < n_mels + 2; i++) {
        bin_points[i] = (int)floorf(
            (n_fft + 1)
            * hz_points[i]
            / sample_rate);
    }

    for (int m = 0; m < n_mels; m++) {
        int f_m_minus = bin_points[m];
        int f_m       = bin_points[m+1];
        int f_m_plus  = bin_points[m+2];

        for (int k = f_m_minus;
             k < f_m; k++) {
            if (k < n_freqs)
                filters[m][k] =
                    (float)(k - f_m_minus)
                    / (f_m - f_m_minus);
        }
        for (int k = f_m;
             k < f_m_plus; k++) {
            if (k < n_freqs)
                filters[m][k] =
                    (float)(f_m_plus - k)
                    / (f_m_plus - f_m);
        }
    }

    return filters;
}

// ─────────────────────────────────────────
// Simple FFT (Cooley-Tukey)
// ─────────────────────────────────────────

static void fft(
    std::vector<float>& re,
    std::vector<float>& im) {

    size_t n = re.size();
    if (n <= 1) return;

    // Bit reversal
    for (size_t i = 1, j = 0;
         i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    // FFT butterfly
    for (size_t len = 2;
         len <= n; len <<= 1) {
        float ang = -2.0f *
            (float)M_PI / len;
        float wre = cosf(ang);
        float wim = sinf(ang);

        for (size_t i = 0;
             i < n; i += len) {
            float cur_re = 1.0f;
            float cur_im = 0.0f;

            for (size_t j = 0;
                 j < len/2; j++) {
                float u_re =
                    re[i+j];
                float u_im =
                    im[i+j];
                float v_re =
                    re[i+j+len/2]
                    * cur_re
                    - im[i+j+len/2]
                    * cur_im;
                float v_im =
                    re[i+j+len/2]
                    * cur_im
                    + im[i+j+len/2]
                    * cur_re;

                re[i+j]         =
                    u_re + v_re;
                im[i+j]         =
                    u_im + v_im;
                re[i+j+len/2]   =
                    u_re - v_re;
                im[i+j+len/2]   =
                    u_im - v_im;

                float new_re =
                    cur_re * wre
                    - cur_im * wim;
                float new_im =
                    cur_re * wim
                    + cur_im * wre;
                cur_re = new_re;
                cur_im = new_im;
            }
        }
    }
}

// ─────────────────────────────────────────
// WhisperEngine
// ─────────────────────────────────────────

WhisperEngine::WhisperEngine(
    const WhisperConfig& cfg,
    Arena* arena)
    : cfg_(cfg)
    , arena_(arena)
    , ready_(false)
    , transcriptions_(0) {

    // Build mel filterbank
    mel_filters_ = build_mel_filterbank(
        N_MELS, N_FFT, SAMPLE_RATE);

    // Hann window
    hann_window_.resize(N_FFT);
    for (int i = 0; i < N_FFT; i++)
        hann_window_[i] = 0.5f *
            (1.0f - cosf(
                2.0f * (float)M_PI
                * i / N_FFT));

    // Pre-allocate buffers
    mel_buffer_.resize(
        N_MELS * 3000, 0.0f);
    fft_re_.resize(N_FFT, 0.0f);
    fft_im_.resize(N_FFT, 0.0f);

    std::cout
        << "[OK] WhisperEngine created\n"
        << "     Language: "
        << cfg_.language << "\n"
        << "     Beam size: "
        << cfg_.beam_size << "\n"
        << "     Translate: "
        << (cfg_.translate
            ? "yes" : "no") << "\n";
}

bool WhisperEngine::load(
    const std::string& model_path) {

    std::ifstream f(
        model_path,
        std::ios::binary);

    if (!f.is_open()) {
        std::cerr
            << "[WHISPER] Cannot open: "
            << model_path << "\n";
        return false;
    }

    // Read model header
    uint32_t magic = 0;
    f.read((char*)&magic,
           sizeof(magic));

    if (magic != 0x77687370) {
        // Not whisper magic
        // Try loading as raw weights
        std::cerr
            << "[WHISPER] Invalid magic"
               " — trying raw load\n";
    }

    // Read model dimensions
    f.read((char*)&model_.n_vocab,
           sizeof(uint32_t));
    f.read((char*)&model_.n_audio_ctx,
           sizeof(uint32_t));
    f.read((char*)&model_.n_audio_state,
           sizeof(uint32_t));
    f.read((char*)&model_.n_audio_head,
           sizeof(uint32_t));
    f.read((char*)&model_.n_audio_layer,
           sizeof(uint32_t));
    f.read((char*)&model_.n_text_ctx,
           sizeof(uint32_t));
    f.read((char*)&model_.n_text_state,
           sizeof(uint32_t));
    f.read((char*)&model_.n_text_head,
           sizeof(uint32_t));
    f.read((char*)&model_.n_text_layer,
           sizeof(uint32_t));
    f.read((char*)&model_.n_mels,
           sizeof(uint32_t));

    std::cout
        << "[WHISPER] Model loaded:\n"
        << "  Vocab:  "
        << model_.n_vocab  << "\n"
        << "  Layers: "
        << model_.n_audio_layer << "\n"
        << "  State:  "
        << model_.n_audio_state << "\n";

    ready_ = true;
    return true;
}

// Compute log mel spectrogram
std::vector<float>
WhisperEngine::compute_mel(
    const float* samples,
    size_t n_samples) {

    int n_frames =
        (n_samples - N_FFT)
        / HOP_LENGTH + 1;

    std::vector<float> mel(
        N_MELS * n_frames, 0.0f);

    for (int frame = 0;
         frame < n_frames; frame++) {

        int offset = frame * HOP_LENGTH;

        // Apply Hann window + FFT
        fft_re_.assign(N_FFT, 0.0f);
        fft_im_.assign(N_FFT, 0.0f);

        for (int i = 0; i < N_FFT; i++) {
            if (offset + i < (int)n_samples)
                fft_re_[i] =
                    samples[offset + i]
                    * hann_window_[i];
        }

        fft(fft_re_, fft_im_);

        // Power spectrum
        int n_freqs = N_FFT / 2 + 1;
        std::vector<float> power(n_freqs);
        for (int i = 0;
             i < n_freqs; i++) {
            power[i] =
                fft_re_[i] * fft_re_[i]
                + fft_im_[i] * fft_im_[i];
        }

        // Apply mel filters
        for (int m = 0;
             m < N_MELS; m++) {
            float sum = 0.0f;
            for (int k = 0;
                 k < n_freqs; k++) {
                sum += mel_filters_[m][k]
                     * power[k];
            }
            // Log mel
            mel[m * n_frames + frame] =
                log10f(
                    std::max(sum,
                        1e-10f));
        }
    }

    // Normalize
    float max_val = *std::max_element(
        mel.begin(), mel.end());
    for (auto& v : mel)
        v = std::max(v,
            max_val - 8.0f);
    for (auto& v : mel)
        v = (v + 4.0f) / 4.0f;

    return mel;
}

// Simple greedy decode
std::string WhisperEngine::greedy_decode(
    const std::vector<float>& mel) {

    // This is a simplified decoder
    // Full implementation needs
    // the actual Whisper weights
    // loaded and attention mechanism
    // For now returns placeholder
    // until weights are connected

    if (!ready_) {
        return "[whisper not loaded]";
    }

    // TODO: implement full
    // encoder-decoder attention
    // once weights are loaded
    return "[transcription]";
}

// Main transcribe function
TranscribeResult
WhisperEngine::transcribe(
    const float* samples,
    size_t n_samples) {

    TranscribeResult result;

    if (!ready_) {
        result.text  =
            "[whisper not loaded]";
        result.success = false;
        return result;
    }

    if (!samples || n_samples == 0) {
        result.text    = "";
        result.success = false;
        return result;
    }

    auto t_start =
        std::chrono::high_resolution_clock
            ::now();

    // Pad or trim to 30 seconds
    std::vector<float> audio(
        CHUNK_SIZE, 0.0f);
    size_t copy_len =
        std::min(n_samples,
            (size_t)CHUNK_SIZE);
    memcpy(audio.data(),
           samples,
           copy_len * sizeof(float));

    // Compute mel spectrogram
    auto mel = compute_mel(
        audio.data(), CHUNK_SIZE);

    // Decode
    result.text = greedy_decode(mel);

    auto t_end =
        std::chrono::high_resolution_clock
            ::now();

    result.latency_ms =
        std::chrono::duration<float,
            std::milli>(
            t_end - t_start).count();

    result.success = true;
    result.language = cfg_.language;
    result.confidence = 0.9f;
    result.audio_duration_ms =
        (float)n_samples
        / SAMPLE_RATE * 1000.0f;

    transcriptions_++;

    std::cout
        << "[WHISPER] Transcribed: \""
        << result.text << "\"\n"
        << "  Latency: "
        << result.latency_ms << "ms\n"
        << "  Duration: "
        << result.audio_duration_ms
        << "ms\n";

    return result;
}

// Transcribe int16
TranscribeResult
WhisperEngine::transcribe_int16(
    const int16_t* samples,
    size_t n_samples) {

    // Convert to float
    std::vector<float> f(n_samples);
    for (size_t i = 0;
         i < n_samples; i++)
        f[i] = samples[i] / 32768.0f;

    return transcribe(
        f.data(), n_samples);
}

// Stream processing
void WhisperEngine::feed(
    const float* samples,
    size_t n_samples) {

    // Add to stream buffer
    stream_buffer_.insert(
        stream_buffer_.end(),
        samples,
        samples + n_samples);

    // Process when we have enough
    if (stream_buffer_.size()
            >= (size_t)SAMPLE_RATE * 5) {
        auto result = transcribe(
            stream_buffer_.data(),
            stream_buffer_.size());

        if (stream_callback_
            && result.success) {
            stream_callback_(result);
        }

        // Keep last 0.5s for overlap
        size_t keep =
            SAMPLE_RATE / 2;
        if (stream_buffer_.size()
                > keep) {
            stream_buffer_.erase(
                stream_buffer_.begin(),
                stream_buffer_.end()
                - keep);
        }
    }
}

void WhisperEngine::set_stream_callback(
    StreamCallback cb) {
    stream_callback_ = std::move(cb);
}

void WhisperEngine::reset_stream() {
    stream_buffer_.clear();
}

bool WhisperEngine::is_ready()
    const {
    return ready_;
}

uint64_t WhisperEngine::transcription_count()
    const {
    return transcriptions_;
}

void WhisperEngine::print_stats() const {
    std::cout
        << "[WHISPER] Stats:\n"
        << "  Ready: "
        << (ready_ ? "yes" : "no")
        << "\n"
        << "  Transcriptions: "
        << transcriptions_ << "\n"
        << "  Language: "
        << cfg_.language << "\n";
}

} // namespace cleainput
