#include "transformer.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>

namespace cleainput {

Transformer::Transformer(
    const TransformerConfig& cfg,
    Arena* arena)
    : cfg_(cfg), arena_(arena) {

    // Token embeddings
    token_emb_ = Tensor::create(arena_,
        cfg_.vocab_size, cfg_.embed_dim);
    token_emb_.zero();

    // Initialize layers
    layers_.resize(cfg_.num_layers);
    for (auto& layer : layers_) {
        // Attention weights
        layer.attention.W_q = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.embed_dim);
        layer.attention.W_k = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.embed_dim);
        layer.attention.W_v = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.embed_dim);
        layer.attention.W_o = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.embed_dim);

        // FFN weights
        layer.ffn.W1 = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.ffn_dim);
        layer.ffn.W2 = Tensor::create(
            arena_, cfg_.ffn_dim,
            cfg_.embed_dim);
        layer.ffn.W3 = Tensor::create(
            arena_, cfg_.embed_dim,
            cfg_.ffn_dim);

        // KV cache
        layer.k_cache = Tensor::create(
            arena_, cfg_.context_len,
            cfg_.embed_dim);
        layer.v_cache = Tensor::create(
            arena_, cfg_.context_len,
            cfg_.embed_dim);

        // Norms
        layer.norm1.weight = Tensor::create(
            arena_, cfg_.embed_dim);
        layer.norm1.eps = cfg_.norm_eps;
        layer.norm2.weight = Tensor::create(
            arena_, cfg_.embed_dim);
        layer.norm2.eps = cfg_.norm_eps;

        // Zero everything
        layer.attention.W_q.zero();
        layer.attention.W_k.zero();
        layer.attention.W_v.zero();
        layer.attention.W_o.zero();
        layer.ffn.W1.zero();
        layer.ffn.W2.zero();
        layer.ffn.W3.zero();
        layer.k_cache.zero();
        layer.v_cache.zero();
    }

    // Output norm + projection
    output_norm_.weight = Tensor::create(
        arena_, cfg_.embed_dim);
    output_norm_.eps = cfg_.norm_eps;
    output_proj_ = Tensor::create(arena_,
        cfg_.embed_dim, cfg_.vocab_size);
    output_proj_.zero();

    std::cout << "[OK] Transformer initialized\n"
              << "     Layers:  "
              << cfg_.num_layers << "\n"
              << "     Heads:   "
              << cfg_.num_heads  << "\n"
              << "     Dim:     "
              << cfg_.embed_dim  << "\n"
              << "     Params:  "
              << param_count() / 1e6f
              << "M\n";
}

void RMSNorm::forward(Tensor& x) const {
    size_t n = x.shape[0];
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++)
        sum += x.data[i] * x.data[i];
    float rms = sqrtf(sum / n + eps);
    float scale = 1.0f / rms;
    for (size_t i = 0; i < n; i++)
        x.data[i] *= scale * weight.data[i];
}

void Transformer::softmax(
    float* x, size_t n) const {
    float max_val = x[0];
    for (size_t i = 1; i < n; i++)
        if (x[i] > max_val)
            max_val = x[i];
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    float inv = 1.0f / sum;
    for (size_t i = 0; i < n; i++)
        x[i] *= inv;
}

void Transformer::apply_rope(
    Tensor& q, Tensor& k,
    uint32_t pos) const {
    uint32_t head_dim = cfg_.head_dim();
    for (uint32_t i = 0;
         i < head_dim; i += 2) {
        float freq = 1.0f / powf(10000.0f,
            (float)i / head_dim);
        float angle = pos * freq;
        float cos_a = cosf(angle);
        float sin_a = sinf(angle);
        float q0 = q.data[i];
        float q1 = q.data[i+1];
        q.data[i]   = q0*cos_a - q1*sin_a;
        q.data[i+1] = q0*sin_a + q1*cos_a;
        float k0 = k.data[i];
        float k1 = k.data[i+1];
        k.data[i]   = k0*cos_a - k1*sin_a;
        k.data[i+1] = k0*sin_a + k1*cos_a;
    }
}

uint32_t Transformer::sample(
    const Tensor& logits,
    float temperature,
    float top_p) const {

    size_t vocab = cfg_.vocab_size;
    std::vector<float> probs(vocab);
    memcpy(probs.data(), logits.data,
           vocab * sizeof(float));

    // Apply temperature
    if (temperature > 0.0f) {
        float inv_temp = 1.0f / temperature;
        for (auto& p : probs)
            p *= inv_temp;
    }
    softmax(probs.data(), vocab);

    // Top-p sampling
    std::vector<std::pair<float,uint32_t>>
        sorted(vocab);
    for (uint32_t i = 0; i < vocab; i++)
        sorted[i] = {probs[i], i};
    std::sort(sorted.begin(), sorted.end(),
        std::greater<>());

    float cumsum = 0.0f;
    size_t cutoff = vocab;
    for (size_t i = 0; i < vocab; i++) {
        cumsum += sorted[i].first;
        if (cumsum >= top_p) {
            cutoff = i + 1;
            break;
        }
    }

    // Sample from top-p
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float>
        dist(0.0f, cumsum);
    float r = dist(gen);
    cumsum = 0.0f;
    for (size_t i = 0; i < cutoff; i++) {
        cumsum += sorted[i].first;
        if (r <= cumsum)
            return sorted[i].second;
    }
    return sorted[0].second;
}

size_t Transformer::param_count() const {
    size_t D = cfg_.embed_dim;
    size_t F = cfg_.ffn_dim;
    size_t V = cfg_.vocab_size;
    size_t L = cfg_.num_layers;
    // Embeddings + layers + output
    return V*D + L*(4*D*D + 3*D*F + 2*D)
           + D + D*V;
}

bool Transformer::load_weights(
    const std::string& path) {
    std::ifstream f(path,
        std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Cannot open: "
                  << path << "\n";
        return false;
    }
    // Read weights directly into tensors
    f.read((char*)token_emb_.data,
        token_emb_.numel()*sizeof(float));
    for (auto& layer : layers_) {
        f.read((char*)layer.attention.W_q.data,
            layer.attention.W_q.numel()
            *sizeof(float));
        f.read((char*)layer.attention.W_k.data,
            layer.attention.W_k.numel()
            *sizeof(float));
        f.read((char*)layer.attention.W_v.data,
            layer.attention.W_v.numel()
            *sizeof(float));
        f.read((char*)layer.attention.W_o.data,
            layer.attention.W_o.numel()
            *sizeof(float));
        f.read((char*)layer.ffn.W1.data,
            layer.ffn.W1.numel()
            *sizeof(float));
        f.read((char*)layer.ffn.W2.data,
            layer.ffn.W2.numel()
            *sizeof(float));
        f.read((char*)layer.ffn.W3.data,
            layer.ffn.W3.numel()
            *sizeof(float));
    }
    std::cout << "[OK] Weights loaded: "
              << path << "\n";
    return true;
}

std::string Transformer::generate(
    const std::string& prompt,
    uint32_t max_tokens,
    float temperature,
    float top_p) {

    // Will be implemented after
    // tokenizer is connected
    return "[generate not yet connected]";
}

} // namespace cleainput

