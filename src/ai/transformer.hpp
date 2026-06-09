#pragma once
#include "core/arena.hpp"
#include "core/tensor.hpp"
#include "core/thread_pool.hpp"
#include "tokenizer.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

namespace cleainput {

// Model hyperparameters
struct TransformerConfig {
    uint32_t vocab_size   = 32000;
    uint32_t context_len  = 2048;
    uint32_t embed_dim    = 512;
    uint32_t num_heads    = 8;
    uint32_t num_layers   = 6;
    uint32_t ffn_dim      = 2048;
    float    dropout      = 0.1f;
    float    norm_eps     = 1e-5f;

    // Derived
    uint32_t head_dim() const {
        return embed_dim / num_heads;
    }
};

// Single attention head
struct AttentionHead {
    Tensor W_q; // query weights
    Tensor W_k; // key weights
    Tensor W_v; // value weights
    Tensor W_o; // output weights
};

// Feed forward network
struct FFN {
    Tensor W1; // up projection
    Tensor W2; // down projection
    Tensor W3; // gate (SwiGLU)

    // SwiGLU activation — better than ReLU
    static float swiglu(float x) {
        return x * (1.0f /
            (1.0f + expf(-x)));
    }
};

// RMS Layer normalization
struct RMSNorm {
    Tensor weight;
    float  eps;

    void forward(Tensor& x) const;
};

// Single transformer layer
struct TransformerLayer {
    AttentionHead attention;
    FFN           ffn;
    RMSNorm       norm1;
    RMSNorm       norm2;

    // KV cache for fast inference
    Tensor k_cache;
    Tensor v_cache;
    uint32_t cache_len = 0;

    void forward(
        Tensor& x,
        uint32_t pos,
        Arena* arena) const;
};

// Full transformer model
class Transformer {
public:
    explicit Transformer(
        const TransformerConfig& cfg,
        Arena* arena);

    // Forward pass — returns logits
    Tensor forward(
        const std::vector<uint32_t>& tokens,
        uint32_t start_pos = 0);

    // Generate text
    std::string generate(
        const std::string& prompt,
        uint32_t max_tokens = 256,
        float temperature  = 0.7f,
        float top_p        = 0.9f);

    // Load weights from file
    bool load_weights(
        const std::string& path);

    // Get config
    const TransformerConfig& config()
        const { return cfg_; }

    size_t param_count() const;

private:
    TransformerConfig cfg_;
    Arena*            arena_;
    ThreadPool*       pool_;

    // Token embeddings
    Tensor token_emb_;

    // Transformer layers
    std::vector<TransformerLayer> layers_;

    // Final norm + output projection
    RMSNorm output_norm_;
    Tensor  output_proj_;

    // Positional encoding (RoPE)
    void apply_rope(
        Tensor& q,
        Tensor& k,
        uint32_t pos) const;

    // Softmax
    void softmax(
        float* x,
        size_t n) const;

    // Sample next token
    uint32_t sample(
        const Tensor& logits,
        float temperature,
        float top_p) const;

    // Attention forward
    void attention_forward(
        Tensor& x,
        TransformerLayer& layer,
        uint32_t pos,
        Arena* scratch) const;

    // FFN forward
    void ffn_forward(
        Tensor& x,
        const FFN& ffn,
        Arena* scratch) const;
};

} // namespace cleainput
