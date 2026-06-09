#include <iostream>
#include <memory>
#include "core/arena.hpp"
#include "core/tensor.hpp"
#include "core/thread_pool.hpp"

namespace ci = cleainput;

int main() {
    std::cout << "=========================\n";
    std::cout << "  Cleainput-AI v0.1.0   \n";
    std::cout << "=========================\n";

    // Initialize main memory arena — 512MB
    ci::Arena arena(512ULL * 1024 * 1024);
    std::cout << "[OK] Arena initialized: 512MB\n";

    // Initialize thread pool
    ci::ThreadPool pool;
    std::cout << "[OK] ThreadPool initialized: "
              << pool.size() << " threads\n";

    // Test tensor creation
    auto t = ci::Tensor::create(&arena, 128, 128);
    t.zero();
    std::cout << "[OK] Tensor created: 128x128\n";

    // Test SIMD dot product
    float a[8] = {1,2,3,4,5,6,7,8};
    float b[8] = {1,2,3,4,5,6,7,8};
    float result = ci::Tensor::dot(a, b, 8);
    std::cout << "[OK] SIMD dot product: "
              << result << "\n";

    std::cout << "\n[READY] All systems nominal\n";
    std::cout << "[READY] Cleainput-AI online\n";
    std::cout << "=========================\n";

    // TODO: Initialize voice pipeline
    // TODO: Initialize transformer
    // TODO: Initialize web crawler
    // TODO: Start REST server

    return 0;
}
