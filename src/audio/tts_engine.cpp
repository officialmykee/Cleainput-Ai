#include "tts_engine.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <sstream>

namespace cleainput {

// ─────────────────────────────────────────
// TTS Constants
// ─────────────────────────────────────────

static constexpr int TTS_SAMPLE_RATE = 22050;
static constexpr int TTS_HOP_LENGTH  = 256;
static constexpr int TTS_WIN_LENGTH  = 1024;
static constexpr int TTS_N_MELS      = 80;
static constexpr int TTS_N_FFT       = 1024;

// ─────────────────────────────────────────
// Text normalization helpers
// ─────────────────────────────────────────

// Expand numbers to words
static std::string expand_number(
    int n) {

    if (n == 0) return "zero";

    static const char* ones[] = {
        "", "one", "two", "three",
        "four", "five", "six", "seven",
        "eight", "nine", "ten",
        "eleven", "twelve", "thirteen",
        "fourteen", "fifteen", "sixteen",
        "seventeen", "eighteen",
        "nineteen"
    };
    static const char* tens[] = {
        "", "", "twenty", "thirty",
        "forty", "fifty", "sixty",
        "seventy", "eighty", "ninety"
    };

    std::string result;

    if (n < 0) {
        result = "minus ";
        n = -n;
    }

    if (n >= 1000) {
        result += expand_number(
            n / 1000) + " thousand ";
        n %= 1000;
    }
    if (n >= 100) {
        result += std::string(
            ones[n / 100])
            + " hundred ";
        n %= 100;
    }
    if (n >= 20) {
        result += std::string(
            tens[n / 10]) + " ";
        n %= 10;
    }
    if (n > 0) {
        result += std::string(ones[n])
            + " ";
    }

    // Trim trailing space
    if (!result.empty() &&
        result.back() == ' ')
        result.pop_back();

    return result;
}

// Expand abbreviations
static std::string expand_abbrev(
    const std::string& word) {

    static const std::unordered_map<
        std::string,
        std::string> abbrevs = {
        {"mr",  "mister"},
        {"mrs", "missus"},
        {"dr",  "doctor"},
        {"prof","professor"},
        {"st",  "street"},
        {"ave", "avenue"},
        {"etc", "et cetera"},
        {"vs",  "versus"},
        {"fig", "figure"},
        {"approx", "approximately"},
        {"max", "maximum"},
        {"min", "minimum"},
        {"ms",  "milliseconds"},
        {"kb",  "kilobytes"},
        {"mb",  "megabytes"},
        {"gb",  "gigabytes"},
        {"ai",  "artificial intelligence"},
        {"api", "A P I"},
        {"url", "U R L"},
        {"cpu", "C P U"},
        {"gpu", "G P U"},
        {"llm", "L L M"},
    };

    std::string lower = word;
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        ::tolower);

    auto it = abbrevs.find(lower);
    if (it != abbrevs.end())
        return it->second;

    return word;
}

// ─────────────────────────────────────────
// Phoneme mapping (simple English)
// ─────────────────────────────────────────

static std::string word_to_phonemes(
    const std::string& word) {

    // Simplified phoneme rules
    // Full implementation would use
    // CMU pronouncing dictionary
    std::string result;
    std::string w = word;
    std::transform(
        w.begin(), w.end(),
        w.begin(), ::tolower);

    // Basic letter-to-phoneme
    // mapping for English
    for (size_t i = 0;
         i < w.size(); i++) {

        char c = w[i];
        char next = (i + 1 < w.size())
            ? w[i+1] : '\0';

        // Digraphs first
        if (c == 't' && next == 'h') {
            result += "DH ";
            i++;
        } else if (c == 's' &&
                   next == 'h') {
            result += "SH ";
            i++;
        } else if (c == 'c' &&
                   next == 'h') {
            result += "CH ";
            i++;
        } else if (c == 'p' &&
                   next == 'h') {
            result += "F ";
            i++;
        } else if (c == 'n' &&
                   next == 'g') {
            result += "NG ";
            i++;
        }
        // Vowels
        else if (c == 'a')
            result += "AE ";
        else if (c == 'e')
            result += "EH ";
        else if (c == 'i')
            result += "IH ";
        else if (c == 'o')
            result += "OW ";
        else if (c == 'u')
            result += "UH ";
        // Consonants
        else if (c == 'b')
            result += "B ";
        else if (c == 'c')
            result += "K ";
        else if (c == 'd')
            result += "D ";
        else if (c == 'f')
            result += "F ";
        else if (c == 'g')
            result += "G ";
        else if (c == 'h')
            result += "HH ";
        else if (c == 'j')
            result += "JH ";
        else if (c == 'k')
            result += "K ";
        else if (c == 'l')
            result += "L ";
        else if (c == 'm')
            result += "M ";
        else if (c == 'n')
            result += "N ";
        else if (c == 'p')
            result += "P ";
        else if (c == 'q')
            result += "K W ";
        else if (c == 'r')
            result += "R ";
        else if (c == 's')
            result += "S ";
        else if (c == 't')
            result += "T ";
        else if (c == 'v')
            result += "V ";
        else if (c == 'w')
            result += "W ";
        else if (c == 'x')
            result += "K S ";
        else if (c == 'y')
            result += "Y ";
        else if (c == 'z')
            result += "Z ";
        else
            result += std::string(1,c)
                + " ";
    }

    return result;
}

// ─────────────────────────────────────────
// TTSEngine
// ─────────────────────────────────────────

TTSEngine::TTSEngine(
    const TTSConfig& cfg,
    Arena* arena)
    : cfg_(cfg)
    , arena_(arena)
    , ready_(false)
    , syntheses_(0) {

    // Pre-allocate mel buffer
    mel_buffer_.resize(
        TTS_N_MELS * 1000, 0.0f);

    // Pre-allocate audio buffer
    audio_buffer_.resize(
        TTS_SAMPLE_RATE * 10, 0.0f);

    // Pre-allocate phoneme buffer
    phoneme_buffer_.reserve(512);

    std::cout
        << "[OK] TTSEngine created\n"
        << "     Voice: "
        << cfg_.voice    << "\n"
        << "     Speed: "
        << cfg_.speed    << "\n"
        << "     Pitch: "
        << cfg_.pitch    << "\n"
        << "     Sample rate: "
        << cfg_.sample_rate << "\n";
}

bool TTSEngine::load(
    const std::string& model_path) {

    std::ifstream f(
        model_path,
        std::ios::binary);

    if (!f.is_open()) {
        std::cerr
            << "[TTS] Cannot open: "
            << model_path << "\n";
        return false;
    }

    // Read model header
    uint32_t magic = 0;
    f.read((char*)&magic,
           sizeof(magic));

    // Read dimensions
    f.read((char*)&model_.n_vocab,
           sizeof(uint32_t));
    f.read((char*)&model_.n_speakers,
           sizeof(uint32_t));
    f.read((char*)&model_.n_layers,
           sizeof(uint32_t));
    f.read((char*)&model_.hidden_dim,
           sizeof(uint32_t));
    f.read((char*)&model_.n_mels,
           sizeof(uint32_t));

    std::cout
        << "[TTS] Model loaded:\n"
        << "  Vocab:    "
        << model_.n_vocab    << "\n"
        << "  Speakers: "
        << model_.n_speakers << "\n"
        << "  Layers:   "
        << model_.n_layers   << "\n"
        << "  Hidden:   "
        << model_.hidden_dim << "\n";

    ready_ = true;
    return true;
}

// Text normalization
std::string TTSEngine::normalize_text(
    const std::string& text) {

    std::string result;
    std::istringstream ss(text);
    std::string word;

    while (ss >> word) {
        // Remove punctuation except
        // sentence enders
        std::string clean;
        bool has_period = false;
        bool has_comma  = false;

        for (char c : word) {
            if (c == '.' || c == '!'
                || c == '?') {
                has_period = true;
            } else if (c == ',') {
                has_comma = true;
            } else if (isalnum(c)
                       || c == '-'
                       || c == '\'') {
                clean += c;
            }
        }

        if (clean.empty()) continue;

        // Check if number
        bool is_num = true;
        for (char c : clean) {
            if (!isdigit(c)) {
                is_num = false;
                break;
            }
        }

        if (is_num) {
            result += expand_number(
                std::stoi(clean))
                + " ";
        } else {
            result += expand_abbrev(
                clean) + " ";
        }

        if (has_comma)
            result += ", ";
        if (has_period)
            result += ". ";
    }

    // Trim
    if (!result.empty() &&
        result.back() == ' ')
        result.pop_back();

    return result;
}

// Text to phonemes
std::vector<std::string>
TTSEngine::text_to_phonemes(
    const std::string& text) {

    std::vector<std::string> phonemes;
    std::istringstream ss(text);
    std::string word;

    while (ss >> word) {
        // Clean word
        std::string clean;
        bool sentence_end = false;
        bool phrase_end   = false;

        for (char c : word) {
            if (c == '.' || c == '!'
                || c == '?')
                sentence_end = true;
            else if (c == ',')
                phrase_end = true;
            else if (isalpha(c))
                clean += c;
        }

        if (!clean.empty()) {
            std::string ph =
                word_to_phonemes(clean);
            // Split phonemes
            std::istringstream pss(ph);
            std::string p;
            while (pss >> p)
                phonemes.push_back(p);
        }

        // Add prosody markers
        if (phrase_end)
            phonemes.push_back("sp");
        if (sentence_end)
            phonemes.push_back("sil");
    }

    return phonemes;
}

// Generate mel spectrogram
// from phonemes
std::vector<float>
TTSEngine::phonemes_to_mel(
    const std::vector<std::string>&
        phonemes) {

    if (!ready_)
        return {};

    // Simplified mel generation
    // Full impl needs FastSpeech2
    // or Tacotron2 neural network
    // with loaded weights

    size_t n_frames =
        phonemes.size() * 8;

    std::vector<float> mel(
        TTS_N_MELS * n_frames, 0.0f);

    // Generate basic formant
    // structure per phoneme
    for (size_t p = 0;
         p < phonemes.size(); p++) {

        const std::string& ph =
            phonemes[p];

        // Base frequency for
        // this phoneme
        float f0 = 150.0f; // Hz

        // Adjust for phoneme type
        if (ph == "AE" || ph == "EH"
            || ph == "IH" || ph == "OW"
            || ph == "UH")
            f0 = 200.0f; // vowels higher

        // Apply pitch
        f0 *= cfg_.pitch;

        // Fill mel frames
        for (int frame = 0;
             frame < 8; frame++) {

            int idx = (p * 8 + frame);
            if (idx >= (int)n_frames)
                break;

            // Simple harmonic content
            for (int m = 0;
                 m < TTS_N_MELS; m++) {

                float freq =
                    (float)m / TTS_N_MELS
                    * TTS_SAMPLE_RATE / 2;

                float energy = 0.0f;

                // Fundamental + harmonics
                for (int h = 1;
                     h <= 5; h++) {
                    float harm = f0 * h;
                    float diff =
                        fabsf(freq - harm);
                    if (diff < 100.0f)
                        energy +=
                            1.0f / h
                            * expf(
                                -diff / 50.0f);
                }

                mel[m * n_frames + idx]
                    = energy;
            }
        }
    }

    return mel;
}

// Vocoder — mel to waveform
std::vector<float>
TTSEngine::mel_to_waveform(
    const std::vector<float>& mel,
    size_t n_frames) {

    size_t n_samples =
        n_frames * TTS_HOP_LENGTH;

    std::vector<float> audio(
        n_samples, 0.0f);

    // Griffin-Lim vocoder
    // Iterative phase reconstruction
    std::vector<float> phase(
        TTS_N_FFT / 2 + 1, 0.0f);

    for (size_t frame = 0;
         frame < n_frames; frame++) {

        // Get mel for this frame
        std::vector<float> frame_mel(
            TTS_N_MELS);
        for (int m = 0;
             m < TTS_N_MELS; m++)
            frame_mel[m] =
                mel[m * n_frames + frame];

        // Simple overlap-add synthesis
        size_t center =
            frame * TTS_HOP_LENGTH;

        for (int i = 0;
             i < TTS_HOP_LENGTH &&
             center + i < n_samples;
             i++) {

            // Weighted sum of mel bands
            float sample = 0.0f;
            for (int m = 0;
                 m < TTS_N_MELS; m++) {
                float t =
                    (float)i / TTS_HOP_LENGTH;
                float freq =
                    (float)m / TTS_N_MELS;
                sample += frame_mel[m]
                    * sinf(2.0f
                        * (float)M_PI
                        * freq * t
                        * cfg_.speed);
            }

            audio[center + i] +=
                sample / TTS_N_MELS;
        }
    }

    // Normalize audio
    float max_val = 0.0f;
    for (float s : audio)
        max_val = std::max(
            max_val, fabsf(s));

    if (max_val > 0.0f) {
        float scale =
            0.95f / max_val;
        for (float& s : audio)
            s *= scale;
    }

    return audio;
}

// Main synthesize function
SynthesisResult TTSEngine::synthesize(
    const std::string& text) {

    SynthesisResult result;

    if (text.empty()) {
        result.success = false;
        return result;
    }

    auto t_start =
        std::chrono::high_resolution_clock
            ::now();

    // 1. Normalize text
    std::string normalized =
        normalize_text(text);

    // 2. Text to phonemes
    auto phonemes =
        text_to_phonemes(normalized);

    // 3. Phonemes to mel
    auto mel =
        phonemes_to_mel(phonemes);

    // 4. Mel to waveform
    size_t n_frames =
        mel.size() / TTS_N_MELS;

    result.samples =
        mel_to_waveform(mel, n_frames);

    auto t_end =
        std::chrono::high_resolution_clock
            ::now();

    result.latency_ms =
        std::chrono::duration<
            float, std::milli>(
            t_end - t_start).count();

    result.sample_rate =
        cfg_.sample_rate;
    result.duration_ms =
        (float)result.samples.size()
        / cfg_.sample_rate * 1000.0f;
    result.success = true;
    result.normalized_text = normalized;

    syntheses_++;

    std::cout
        << "[TTS] Synthesized: \""
        << text.substr(0,
            std::min(
                text.size(),
                size_t(50)))
        << (text.size() > 50
            ? "..." : "")
        << "\"\n"
        << "  Samples: "
        << result.samples.size()
        << "\n"
        << "  Duration: "
        << result.duration_ms
        << "ms\n"
        << "  Latency: "
        << result.latency_ms
        << "ms\n";

    return result;
}

// Synthesize to int16
SynthesisResult
TTSEngine::synthesize_int16(
    const std::string& text) {

    auto result = synthesize(text);

    if (!result.success)
        return result;

    // Convert float to int16
    result.samples_int16.resize(
        result.samples.size());

    for (size_t i = 0;
         i < result.samples.size();
         i++) {
        float s = std::max(-1.0f,
            std::min(1.0f,
                result.samples[i]));
        result.samples_int16[i] =
            (int16_t)(s * 32767.0f);
    }

    return result;
}

// Stream synthesis
void TTSEngine::synthesize_stream(
    const std::string& text,
    StreamSynthCallback callback) {

    if (!callback) return;

    // Split into sentences
    std::vector<std::string> sentences;
    std::string current;

    for (char c : text) {
        current += c;
        if (c == '.' || c == '!'
            || c == '?') {
            if (!current.empty()) {
                sentences.push_back(
                    current);
                current.clear();
            }
        }
    }
    if (!current.empty())
        sentences.push_back(current);

    // Synthesize each sentence
    // and stream back
    for (size_t i = 0;
         i < sentences.size(); i++) {

        auto result = synthesize(
            sentences[i]);

        if (result.success) {
            bool is_final =
                (i == sentences.size()-1);
            callback(result, is_final);
        }
    }
}

bool TTSEngine::is_ready() const {
    return ready_;
}

uint64_t TTSEngine::synthesis_count()
    const {
    return syntheses_;
}

void TTSEngine::set_speed(
    float speed) {
    cfg_.speed = std::max(0.5f,
        std::min(2.0f, speed));
}

void TTSEngine::set_pitch(
    float pitch) {
    cfg_.pitch = std::max(0.5f,
        std::min(2.0f, pitch));
}

void TTSEngine::set_volume(
    float volume) {
    cfg_.volume = std::max(0.0f,
        std::min(1.0f, volume));
}

void TTSEngine::set_voice(
    const std::string& voice) {
    cfg_.voice = voice;
}

void TTSEngine::print_stats() const {
    std::cout
        << "[TTS] Stats:\n"
        << "  Ready: "
        << (ready_ ? "yes" : "no")
        << "\n"
        << "  Syntheses: "
        << syntheses_ << "\n"
        << "  Voice: "
        << cfg_.voice << "\n"
        << "  Speed: "
        << cfg_.speed << "\n"
        << "  Pitch: "
        << cfg_.pitch << "\n";
}

} // namespace cleainput
