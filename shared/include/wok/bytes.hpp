#pragma once
// Little-endian byte serialization helpers.

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace wok {

class ByteWriter {
public:
    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) { le(v); }
    void u32(uint32_t v) { le(v); }
    void u64(uint64_t v) { le(v); }
    void f32(float v) { u32(std::bit_cast<uint32_t>(v)); }
    void raw(const void* p, size_t n) {
        auto b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    template <class T>
    void le(T v) {
        for (size_t i = 0; i < sizeof(T); ++i) buf_.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    std::vector<uint8_t> buf_;
};

// Reads never go out of bounds: on overflow the reader enters a failed state,
// returns zeros, and ok() becomes false.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}
    uint8_t u8() { return le<uint8_t>(); }
    uint16_t u16() { return le<uint16_t>(); }
    uint32_t u32() { return le<uint32_t>(); }
    uint64_t u64() { return le<uint64_t>(); }
    float f32() { return std::bit_cast<float>(u32()); }
    void raw(void* out, size_t n) {
        if (!need(n)) {
            std::memset(out, 0, n);
            return;
        }
        std::memcpy(out, data_.data() + pos_, n);
        pos_ += n;
    }
    bool ok() const { return ok_; }
    bool atEnd() const { return pos_ == data_.size(); }

private:
    bool need(size_t n) {
        if (!ok_ || data_.size() - pos_ < n) {
            ok_ = false;
            return false;
        }
        return true;
    }
    template <class T>
    T le() {
        if (!need(sizeof(T))) return T{};
        T v{};
        for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<T>(static_cast<T>(data_[pos_ + i]) << (8 * i));
        pos_ += sizeof(T);
        return v;
    }
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace wok
