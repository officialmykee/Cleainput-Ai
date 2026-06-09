#include "tokenizer.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <regex>

namespace cleainput {

std::vector<uint32_t> Tokenizer::encode(
    const std::string& text,
    bool add_bos,
    bool add_eos) const {

    std::vector<uint32_t> ids;

    if (add_bos)
        ids.push_back(BOS_ID);

    // Split into words first
    auto words = split_words(text);

    // BPE encode each word
    for (const auto& word : words) {
        auto word_ids = bpe(word);
        ids.insert(ids.end(),
            word_ids.begin(),
            word_ids.end());
    }

    if (add_eos)
        ids.push_back(EOS_ID);

    return ids;
}

std::string Tokenizer::decode(
    const std::vector<uint32_t>& ids,
    bool skip_special) const {

    std::string result;
    for (auto id : ids) {
        // Skip special tokens if requested
        if (skip_special && (
            id == BOS_ID ||
            id == EOS_ID ||
            id == PAD_ID))
            continue;

        auto it = id_to_text_.find(id);
        if (it != id_to_text_.end()) {
            std::string token = it->second;
            // Replace special space marker
            for (size_t i = 0;
                 i < token.size(); i++) {
                if (token[i] == '\xc4' &&
                    i+1 < token.size() &&
                    token[i+1] == '\xa0') {
                    result += ' ';
                    i++;
                } else {
                    result += token[i];
                }
            }
        } else {
            result += "[UNK]";
        }
    }
    return result;
}

std::vector<std::string>
Tokenizer::split_words(
    const std::string& text) const {

    std::vector<std::string> words;
    std::string current;

    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == ' ') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            // Add space marker to next word
            current += "\xc4\xa0";
        } else {
            current += c;
        }
    }
    if (!current.empty())
        words.push_back(current);

    return words;
}

std::vector<uint32_t> Tokenizer::bpe(
    const std::string& word) const {

    if (word.empty())
        return {};

    // Check if whole word is in vocab
    auto it = text_to_id_.find(word);
    if (it != text_to_id_.end())
        return {it->second};

    // Start with individual bytes
    std::vector<std::string> symbols;
    for (unsigned char c : word) {
        symbols.push_back(
            std::string(1, (char)c));
    }

    // Apply BPE merges
    while (symbols.size() > 1) {
        std::string best_pair;
        uint32_t best_rank = UINT32_MAX;
        size_t best_idx = 0;

        for (size_t i = 0;
             i + 1 < symbols.size(); i++) {
            std::string pair =
                symbols[i] + symbols[i+1];
            auto mit = merges_.find(pair);
            if (mit != merges_.end() &&
                mit->second < best_rank) {
                best_rank = mit->second;
                best_pair = pair;
                best_idx = i;
            }
        }

        if (best_rank == UINT32_MAX)
            break;

        // Merge best pair
        symbols[best_idx] = best_pair;
        symbols.erase(
            symbols.begin() + best_idx + 1);
    }

    // Convert symbols to ids
    std::vector<uint32_t> ids;
    for (const auto& sym : symbols) {
        auto sit = text_to_id_.find(sym);
        if (sit != text_to_id_.end()) {
            ids.push_back(sit->second);
        } else {
            // Byte fallback
            for (unsigned char c : sym)
                ids.push_back(
                    byte_to_token(c));
        }
    }
    return ids;
}

bool Tokenizer::load(
    const std::string& path) {

    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open: "
                  << path << "\n";
        return false;
    }

    std::string line;
    uint32_t id = 0;

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        Token tok;
        tok.id   = id++;
        tok.text = line;
        tok.score = 0.0f;
        vocab_.push_back(tok);
        text_to_id_[tok.text] = tok.id;
        id_to_text_[tok.id]   = tok.text;
    }

    std::cout << "[OK] Tokenizer loaded: "
              << vocab_.size()
              << " tokens\n";
    return true;
}

uint32_t Tokenizer::byte_to_token(
    uint8_t byte) const {
    // Map raw byte to token id
    // Bytes 0-255 map to first 256 tokens
    return static_cast<uint32_t>(byte);
}

std::string Tokenizer::id_to_text(
    uint32_t id) const {
    auto it = id_to_text_.find(id);
    if (it != id_to_text_.end())
        return it->second;
    return "[UNK]";
}

uint32_t Tokenizer::text_to_id(
    const std::string& text) const {
    auto it = text_to_id_.find(text);
    if (it != text_to_id_.end())
        return it->second;
    return UNK_ID;
}

} // namespace cleainput
