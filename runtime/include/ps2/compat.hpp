#pragma once

// Small C++17 replacements for the C++20 facilities the runtime used:
//   std::span, std::bit_cast, std::countl_zero.
// Include this header instead of <bit>/<span> in code that has to build as
// C++17. The C++20 build keeps working with the same helpers.

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__has_include)
#if __has_include(<array>)
#include <array>
#endif
#endif

namespace ps2 {

template <typename T>
class span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using pointer = T *;
    using reference = T &;
    using iterator = T *;

    constexpr span() noexcept = default;
    constexpr span(pointer data, size_type size) noexcept : data_(data), size_(size) {}

    template <typename U, std::size_t N>
    constexpr span(U (&array)[N]) noexcept : data_(array), size_(N) {}

    template <typename U, std::size_t N>
    constexpr span(std::array<U, N> &array) noexcept : data_(array.data()), size_(array.size()) {}

    template <typename U, std::size_t N>
    constexpr span(const std::array<U, N> &array) noexcept : data_(array.data()), size_(array.size()) {}

    template <typename U, typename Allocator>
    span(std::vector<U, Allocator> &vector) noexcept : data_(vector.data()), size_(vector.size()) {}

    template <typename U, typename Allocator>
    span(const std::vector<U, Allocator> &vector) noexcept : data_(vector.data()), size_(vector.size()) {}

    constexpr pointer data() const noexcept { return data_; }
    constexpr size_type size() const noexcept { return size_; }
    constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr iterator begin() const noexcept { return data_; }
    constexpr iterator end() const noexcept { return data_ + size_; }
    constexpr reference operator[](size_type index) const noexcept { return data_[index]; }
    constexpr reference front() const noexcept { return data_[0]; }
    constexpr reference back() const noexcept { return data_[size_ - 1]; }

    constexpr span subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const noexcept {
        return span(data_ + offset, count == static_cast<size_type>(-1) ? size_ - offset : count);
    }

private:
    pointer data_ = nullptr;
    size_type size_ = 0;
};

template <typename To, typename From>
inline To bit_cast(const From &source) noexcept {
    static_assert(sizeof(To) == sizeof(From), "bit_cast requires equal sizes");
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
                  "bit_cast requires trivially copyable types");
    To target;
    std::memcpy(&target, &source, sizeof(To));
    return target;
}

inline int countl_zero(std::uint32_t value) noexcept {
    return value ? __builtin_clz(value) : 32;
}

inline int countl_zero(std::uint64_t value) noexcept {
    return value ? __builtin_clzll(value) : 64;
}

// On LP64 (Linux, Android arm64) unsigned long is uint64_t, and on ILP32 it
// is uint32_t: in both cases the overloads above already cover it. Keep this
// one only where unsigned long is a distinct type (Windows LLP64), otherwise
// it would be a redefinition.
template <typename T,
          std::enable_if_t<std::is_same_v<T, unsigned long> &&
                               !std::is_same_v<unsigned long, std::uint32_t> &&
                               !std::is_same_v<unsigned long, std::uint64_t>,
                           int> = 0>
inline int countl_zero(T value) noexcept {
    return sizeof(unsigned long) == 8 ? countl_zero(static_cast<std::uint64_t>(value))
                                      : countl_zero(static_cast<std::uint32_t>(value));
}

}  // namespace ps2
