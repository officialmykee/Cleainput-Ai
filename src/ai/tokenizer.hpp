#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "core/arena.hpp"

namespace cleainput {

struct Token {
    uint32_t id;
    std::string text;
    float score;
};

class Tokenizer {
public:
    static constexpr uint32_t BOS_ID = 1; // begin of sentence
    static constexpr uint32_t EOS_ID = 2; // end of sentence
    static constexpr uint32_t UNK_ID = 0; // unknown token
    static constexpr uint32_t PAD_ID = 3; // padding

    Tokenizer() = default;

    // Encode text → token ids
    std::vector<uint32_t> encode(
        const std::string& text,
        bool add_bos = true,
        bool add_eos = false) const;

    // Decode token ids → text
    std::string decode(
        const std::vector<uint32_t>& ids,
        bool skip_special = true) const;

    // Vocabulary size
    size_t vocab_size() const {
        return vocab_.size();
    }

    // Load vocabulary from file
    bool load(const std::string& path);

    // Single token → text
    std::string id_to_text(uint32_t id) const;

    // Text → single token id
    uint32_t text_to_id(
        const std::string& text) const;

private:
    // Token vocabulary
    std::vector<Token> vocab_;

    // Fast lookup maps
    std::unordered_map<std::string,
        uint32_t> text_to_id_;
    std::unordered_map<uint32_t,
        std::string> id_to_text_;

    // BPE merge rules
    std::unordered_map<std::string,
        uint32_t> merges_;

    // Apply BPE to a single word
    std::vector<uint32_t> bpe(
        const std::string& word) const;

    // Split text into words
    std::vector<std::string> split_words(
        const std::string& text) const;

    // Byte fallback for unknown chars
    uint32_t byte_to_token(uint8_t byte) const;
};

} // namespace cleainput
