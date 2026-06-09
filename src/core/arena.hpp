#pragma once
#include <cstddef>
#include <cstdlib>
#include <cassert>

namespace cleainput {

class Arena {
public:
    explicit Arena(size_t size)
        : size_(size), used_(0) {
        base_ = static_cast<uint8_t*>(
            aligned_alloc(64, size));
        assert(base_ && "Arena alloc failed");
    }

    ~Arena() { free(base_); }

    template<typename T>
    T* alloc(size_t count = 1) {
        size_t bytes = sizeof(T) * count;
        bytes = (bytes + 63) & ~63ULL;
        assert(used_ + bytes <= size_);
        T* ptr = reinterpret_cast<T*>(base_ + used_);
        used_ += bytes;
        return ptr;
    }

    void reset() { used_ = 0; }
    size_t used() const { return used_; }
    size_t remaining() const { return size_ - used_; }

private:
    uint8_t* base_;
    size_t size_;
    size_t used_;

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
};

} // namespace cleainput
