// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#define __STDC_FORMAT_MACROS
#include <algorithm>
#include <atomic>
#include <errno.h>
#include <float.h>
#include <functional>
#include <inttypes.h>
#include <limits.h>
#include <limits>
#include <math.h>
#include <mutex>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <utility>
#ifdef _WIN32
    #include <intrin.h>
#endif
#ifdef _MSC_VER
    #define ENABLE_INTSAFE_SIGNED_FUNCTIONS
    #include <intsafe.h>
    #pragma intrinsic(_BitScanReverse)
    #ifdef _WIN64
        #pragma intrinsic(_BitScanReverse64)
    #endif
    #pragma intrinsic(__rdtsc)
#endif

// ------------------------------------------------------------------------
// Config
// ------------------------------------------------------------------------

#define DEBUG_ENV_PREFIX "RYGEL_"

#define DEFAULT_ALLOCATOR MallocAllocator

#define HEAPARRAY_BASE_CAPACITY 8
#define HEAPARRAY_GROWTH_FACTOR 1.5f

// Must be a power-of-two
#define HASHSET_BASE_CAPACITY 32
#define HASHSET_MAX_LOAD_FACTOR 0.4f

#define FMT_STRING_BASE_CAPACITY 128
#define FMT_STRING_GROWTH_FACTOR 1.5f
#define FMT_STRING_PRINT_BUFFER_SIZE 1024

#define THREAD_MAX_IDLE_TIME 10000

// ------------------------------------------------------------------------
// Utilities
// ------------------------------------------------------------------------

enum class Endianness {
    LittleEndian,
    BigEndian
};

#if defined(__x86_64__) || defined(_M_X64)
    #define ARCH_64
    #define ARCH_LITTLE_ENDIAN
    #define ARCH_ENDIANNESS (Endianness::LittleEndian)

    typedef int64_t Size;
    #define LEN_MAX INT64_MAX
#elif defined(__i386__) || defined(_M_IX86) || defined(__EMSCRIPTEN__)
    #define ARCH_32
    #define ARCH_LITTLE_ENDIAN
    #define ARCH_ENDIANNESS (Endianness::LittleEndian)

    typedef int32_t Size;
    #define LEN_MAX INT32_MAX
#else
    #error Machine architecture not supported
#endif

#define STRINGIFY_(a) #a
#define STRINGIFY(a) STRINGIFY_(a)
#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)
#define UNIQUE_ID(prefix) CONCAT(prefix, __LINE__)
#define FORCE_EXPAND(x) x

#if defined(__GNUC__)
    #define GCC_PRAGMA(Pragma) \
        _Pragma(STRINGIFY(Pragma))
    #define GCC_PUSH_IGNORE(Option) \
        GCC_PRAGMA(GCC diagnostic push) \
        GCC_PRAGMA(GCC diagnostic ignored STRINGIFY(Option))
    #define GCC_POP_IGNORE() \
        GCC_PRAGMA(GCC diagnostic pop)

    // thread_local has many bugs with MinGW (Windows):
    // - Destructors are run after the memory is freed
    // - It crashes when used in R packages
    #define THREAD_LOCAL __thread
    #define MAYBE_UNUSED __attribute__((unused))
    #define FORCE_INLINE __attribute__((always_inline)) inline
    #define LIKELY(Cond) __builtin_expect(!!(Cond), 1)
    #define UNLIKELY(Cond) __builtin_expect(!!(Cond), 0)
    #define RESTRICT __restrict__
    #define UNREACHABLE() __builtin_unreachable()

    #ifndef SCNd8
        #define SCNd8 "hhd"
    #endif
    #ifndef SCNi8
        #define SCNi8 "hhd"
    #endif
    #ifndef SCNu8
        #define SCNu8 "hhu"
    #endif
#elif defined(_MSC_VER)
    #define GCC_PRAGMA(Pragma)
    #define GCC_PUSH_IGNORE(Option)
    #define GCC_POP_IGNORE()

    #define THREAD_LOCAL thread_local
    #define MAYBE_UNUSED
    #define FORCE_INLINE __forceinline
    #define LIKELY(Cond) (Cond)
    #define UNLIKELY(Cond) (Cond)
    #define RESTRICT __restrict
    #define UNREACHABLE()
#else
    #error Compiler not supported
#endif

#define Assert(Cond) \
    do { \
        if (!LIKELY(Cond)) { \
            fputs("Assertion '" STRINGIFY(Cond) "' failed\n", stderr); \
            abort(); \
        } \
    } while (false)
#ifndef NDEBUG
    #define DebugAssert(Cond) Assert(Cond)
#else
    #define DebugAssert(Cond) \
        do { \
            if (!LIKELY(Cond)) { \
                UNREACHABLE(); \
            } \
        } while (false)
#endif
#define StaticAssert(Cond) \
    static_assert((Cond), STRINGIFY(Cond))

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT
#endif

constexpr uint16_t MakeUInt16(uint8_t high, uint8_t low)
    { return (uint16_t)(((uint16_t)high << 8) | low); }
constexpr uint32_t MakeUInt32(uint16_t high, uint16_t low) { return ((uint32_t)high << 16) | low; }
constexpr uint64_t MakeUInt64(uint32_t high, uint32_t low) { return ((uint64_t)high << 32) | low; }

constexpr Size Mebibytes(Size len) { return len * 1024 * 1024; }
constexpr Size Kibibytes(Size len) { return len * 1024; }
constexpr Size Megabytes(Size len) { return len * 1000 * 1000; }
constexpr Size Kilobytes(Size len) { return len * 1000; }

#define SIZE(Type) ((Size)sizeof(Type))
template <typename T, unsigned N>
char (&ComputeArraySize(T const (&)[N]))[N];
#define ARRAY_SIZE(Array) SIZE(ComputeArraySize(Array))
#define OFFSET_OF(Type, Member) ((Size)&(((Type *)nullptr)->Member))

static inline constexpr uint16_t ReverseBytes(uint16_t u)
{
    return (uint16_t)(((u & 0x00FF) << 8) |
                      ((u & 0xFF00) >> 8));
}

static inline constexpr uint32_t ReverseBytes(uint32_t u)
{
    return ((u & 0x000000FF) << 24) |
           ((u & 0x0000FF00) << 8)  |
           ((u & 0x00FF0000) >> 8)  |
           ((u & 0xFF000000) >> 24);
}

static inline constexpr uint64_t ReverseBytes(uint64_t u)
{
    return ((u & 0x00000000000000FF) << 56) |
           ((u & 0x000000000000FF00) << 40) |
           ((u & 0x0000000000FF0000) << 24) |
           ((u & 0x00000000FF000000) << 8)  |
           ((u & 0x000000FF00000000) >> 8)  |
           ((u & 0x0000FF0000000000) >> 24) |
           ((u & 0x00FF000000000000) >> 40) |
           ((u & 0xFF00000000000000) >> 56);
}

static inline constexpr int16_t ReverseBytes(int16_t i)
    { return (int16_t)ReverseBytes((uint16_t)i); }
static inline constexpr int32_t ReverseBytes(int32_t i)
    { return (int32_t)ReverseBytes((uint32_t)i); }
static inline constexpr int64_t ReverseBytes(int64_t i)
    { return (int64_t)ReverseBytes((uint64_t)i); }

#ifdef ARCH_LITTLE_ENDIAN
    template <typename T>
    constexpr T LittleEndian(T v) { return v; }

    template <typename T>
    constexpr T BigEndian(T v) { return ReverseBytes(v); }
#else
    template <typename T>
    constexpr T LittleEndian(T v) { return ReverseBytes(v); }

    template <typename T>
    constexpr T BigEndian(T v) { return v; }
#endif

static inline void SwapMemory(void *RESTRICT ptr1, void *RESTRICT ptr2, Size len)
{
    uint8_t *raw1 = (uint8_t *)ptr1, *raw2 = (uint8_t *)ptr2;
    for (Size i = 0; i < len; i++) {
        uint8_t tmp = raw1[i];
        raw1[i] = raw2[i];
        raw2[i] = tmp;
    }
}

#if defined(__GNUC__)
    static inline int CountLeadingZeros(uint32_t u)
    {
        if (!u)
            return 32;

        return __builtin_clz(u);
    }
    static inline int CountLeadingZeros(uint64_t u)
    {
        if (!u)
            return 64;

    #if UINT64_MAX == ULONG_MAX
        return __builtin_clzl(u);
    #elif UINT64_MAX == ULLONG_MAX
        return __builtin_clzll(u);
    #else
        #error Neither unsigned long nor unsigned long long is a 64-bit unsigned integer
    #endif
    }

    static inline int CountTrailingZeros(uint32_t u)
    {
        if (!u)
            return 32;

        return __builtin_ctz(u);
    }
    static inline int CountTrailingZeros(uint64_t u)
    {
        if (!u)
            return 64;

    #if UINT64_MAX == ULONG_MAX
        return __builtin_ctzl(u);
    #elif UINT64_MAX == ULLONG_MAX
        return __builtin_ctzll(u);
    #else
        #error Neither unsigned long nor unsigned long long is a 64-bit unsigned integer
    #endif
    }

    static inline int PopCount(uint32_t u)
    {
        return __builtin_popcount(u);
    }
    static inline int PopCount(uint64_t u)
    {
        return __builtin_popcountll(u);
    }
#elif defined(_MSC_VER)
    static inline int CountLeadingZeros(uint32_t u)
    {
        unsigned long leading_zero;
        if (_BitScanReverse(&leading_zero, u)) {
            return (int)(31 - leading_zero);
        } else {
            return 32;
        }
    }
    static inline int CountLeadingZeros(uint64_t u)
    {
        unsigned long leading_zero;
    #ifdef _WIN64
        if (_BitScanReverse64(&leading_zero, u)) {
            return (int)(63 - leading_zero);
        } else {
            return 64;
        }
    #else
        if (_BitScanReverse(&leading_zero, u >> 32)) {
            return (int)(31 - leading_zero);
        } else if (_BitScanReverse(&leading_zero, (uint32_t)u)) {
            return (int)(63 - leading_zero);
        } else {
            return 64;
        }
    #endif
    }

    static inline int CountTrailingZeros(uint32_t u)
    {
        unsigned long trailing_zero;
        if (_BitScanForward(&trailing_zero, u)) {
            return (int)trailing_zero;
        } else {
            return 32;
        }
    }
    static inline int CountTrailingZeros(uint64_t u)
    {
        unsigned long trailing_zero;
    #ifdef _WIN64
        if (_BitScanForward64(&trailing_zero, u)) {
            return (int)trailing_zero;
        } else {
            return 64;
        }
    #else
        if (_BitScanForward(&trailing_zero, (uint32_t)u)) {
            return trailing_zero;
        } else if (_BitScanForward(&trailing_zero, u >> 32)) {
            return 32 + trailing_zero;
        } else {
            return 64;
        }
    #endif
    }

    static inline int PopCount(uint32_t u)
    {
        return __popcnt(u);
    }
    static inline int PopCount(uint64_t u)
    {
    #ifdef _WIN64
        return (int)__popcnt64(u);
    #else
        return __popcnt(u >> 32) + __popcnt((uint32_t)u);
    #endif
    }
#else
    #error No implementation of CountLeadingZeros(), CountTrailingZeros() and PopCount() for this compiler / toolchain
#endif

template <typename T, typename = typename std::enable_if<std::is_enum<T>::value, T>>
typename std::underlying_type<T>::type MaskEnum(T value)
{
    auto mask = 1 << static_cast<typename std::underlying_type<T>::type>(value);
    return (typename std::underlying_type<T>::type)mask;
}

template <typename Fun>
class DeferGuard {
    Fun f;
    bool enabled;

public:
    DeferGuard() = delete;
    DeferGuard(Fun f_, bool enable = true) : f(std::move(f_)), enabled(enable) {}
    ~DeferGuard()
    {
        if (enabled) {
            f();
        }
    }

    // With C++17 we don't need any copy or move operator thanks to guaranteed
    // copy elision. I think. MSVC does not agree... yet?
#ifdef _MSC_VER
    DeferGuard(DeferGuard &&other)
        : f(std::move(other.f)), enabled(other.enabled)
    {
        other.enabled = false;
    }
#endif

    DeferGuard(const DeferGuard &) = delete;
    DeferGuard &operator=(DeferGuard &) = delete;

    void enable() { enabled = true; }
    void disable() { enabled = false; }
};

// Honestly, I don't understand all the details in there, this comes from Andrei Alexandrescu.
// https://channel9.msdn.com/Shows/Going+Deep/C-and-Beyond-2012-Andrei-Alexandrescu-Systematic-Error-Handling-in-C
struct DeferGuardHelper {
    bool enabled;
    DeferGuardHelper(bool enable = true) : enabled(enable) {}
};
template <typename Fun>
DeferGuard<Fun> operator+(DeferGuardHelper h, Fun &&f)
{
    return DeferGuard<Fun>(std::forward<Fun>(f), h.enabled);
}

// Write 'DEFER { code };' to do something at the end of the current scope, you
// can use DEFER_N(Name) if you need to disable the guard for some reason, and
// DEFER_NC(Name, Captures) if you need to capture values.
#define DEFER \
    auto UNIQUE_ID(defer) = DeferGuardHelper() + [&]()
#define DEFER_N(Name) \
    auto Name = DeferGuardHelper() + [&]()
#define DEFER_N_DISABLED(Name) \
    auto Name = DeferGuardHelper(false) + [&]()
#define DEFER_C(...) \
    auto UNIQUE_ID(defer) = DeferGuardHelper() + [&, __VA_ARGS__]()
#define DEFER_NC(Name, ...) \
    auto Name = DeferGuardHelper() + [&, __VA_ARGS__]()
#define DEFER_NC_DISABLED(Name, ...) \
    auto Name = DeferGuardHelper(false) + [&, __VA_ARGS__]()

#define INIT(Name) \
    class UNIQUE_ID(InitHelper) { \
    public: \
        UNIQUE_ID(InitHelper)(); \
    }; \
    static UNIQUE_ID(InitHelper) UNIQUE_ID(init); \
    UNIQUE_ID(InitHelper)::UNIQUE_ID(InitHelper)()

#define EXIT(Name) \
    class UNIQUE_ID(ExitHelper) { \
    public: \
        ~UNIQUE_ID(ExitHelper)(); \
    }; \
    static UNIQUE_ID(ExitHelper) UNIQUE_ID(exit); \
    UNIQUE_ID(ExitHelper)::~UNIQUE_ID(ExitHelper)()

template <typename T>
T MultiCmp()
{
    return 0;
}
template <typename T, typename... Args>
T MultiCmp(T cmp_value, Args... other_args)
{
    if (cmp_value) {
        return cmp_value;
    } else {
        return MultiCmp<T>(other_args...);
    }
}

// ------------------------------------------------------------------------
// Overflow Safety
// ------------------------------------------------------------------------

#if defined(__GNUC__)
    template <typename T> bool AddOverflow(T a, T b, T *rret)
        { return __builtin_add_overflow(a, b, rret); }
    template <typename T> bool SubOverflow(T a, T b, T *rret)
        { return __builtin_sub_overflow(a, b, rret); }
    template <typename T> bool MulOverflow(T a, T b, T *rret)
        { return __builtin_mul_overflow(a, b, rret); }
#elif defined(_MSC_VER)
    // Unfortunately intsafe.h functions change rret to an invalid value when overflow would
    // occur. We want to keep the old value in, like GCC instrinsics do. The good solution
    // would be to implement our own functions, but I'm too lazy to do it right now and test
    // it's working. That'll do for now.
    #define DEFINE_OVERFLOW_OPERATION(Type, Operation, Func) \
        static inline bool Operation##Overflow(Type a, Type b, Type *rret) \
        { \
            Type backup = *rret; \
            if (!Func(a, b, rret)) { \
                return false; \
            } else { \
                *rret = backup; \
                return true; \
            } \
        }
    #define OVERFLOW_OPERATIONS(Type, Prefix) \
        DEFINE_OVERFLOW_OPERATION(Type, Add, Prefix##Add) \
        DEFINE_OVERFLOW_OPERATION(Type, Sub, Prefix##Sub) \
        DEFINE_OVERFLOW_OPERATION(Type, Mul, Prefix##Mult)

    OVERFLOW_OPERATIONS(signed char, Int8)
    OVERFLOW_OPERATIONS(unsigned char, UInt8)
    OVERFLOW_OPERATIONS(short, Short)
    OVERFLOW_OPERATIONS(unsigned short, UShort)
    OVERFLOW_OPERATIONS(int, Int)
    OVERFLOW_OPERATIONS(unsigned int, UInt)
    OVERFLOW_OPERATIONS(long, Long)
    OVERFLOW_OPERATIONS(unsigned long, ULong)
    OVERFLOW_OPERATIONS(long long, LongLong)
    OVERFLOW_OPERATIONS(unsigned long long, ULongLong)

    #undef OVERFLOW_OPERATIONS
    #undef DEFINE_OVERFLOW_OPERATION
#else
    #error Overflow operations are not implemented for this compiler
#endif

// ------------------------------------------------------------------------
// Memory / Allocator
// ------------------------------------------------------------------------

class Allocator {
public:
    enum class Flag {
        Zero = 1,
        Resizable = 2
    };

    Allocator() = default;
    virtual ~Allocator() = default;
    Allocator(Allocator &) = delete;
    Allocator &operator=(const Allocator &) = delete;

    static void *Allocate(Allocator *alloc, Size size, unsigned int flags = 0);
    static void Resize(Allocator *alloc, void **ptr, Size old_size, Size new_size,
                       unsigned int flags = 0);
    static void Release(Allocator *alloc, void *ptr, Size size);

protected:
    virtual void *Allocate(Size size, unsigned int flags = 0) = 0;
    virtual void Resize(void **ptr, Size old_size, Size new_size, unsigned int flags = 0) = 0;
    virtual void Release(void *ptr, Size size) = 0;
};

class LinkedAllocator: public Allocator {
    struct Node {
        Node *prev;
        Node *next;
    };
    struct Bucket {
         // Keep head first or stuff will break
        Node head;
        alignas(8) uint8_t data[];
    };

    Allocator *allocator;
    // We want allocators to be memmovable, which means we can't use a circular linked list.
    // Even though it makes the code less nice.
    Node list = {};

public:
    LinkedAllocator(Allocator *alloc = nullptr) : allocator(alloc) {}
    ~LinkedAllocator() override { ReleaseAll(); }
    void ReleaseAll();

protected:
    void *Allocate(Size size, unsigned int flags = 0) override;
    void Resize(void **ptr, Size old_size, Size new_size, unsigned int flags = 0) override;
    void Release(void *ptr, Size size) override;
};

// ------------------------------------------------------------------------
// Collections
// ------------------------------------------------------------------------

template <typename T>
struct ArraySlice {
    Size offset;
    Size len;

    ArraySlice() = default;
    ArraySlice(Size offset, Size len) : offset(offset), len(len) {}
};

// I'd love to make Span default to { nullptr, 0 } but unfortunately that makes
// it a non-POD and prevents putting it in a union.
template <typename T>
struct Span {
    T *ptr;
    Size len;

    Span() = default;
    constexpr Span(T &value) : ptr(&value), len(1) {}
    constexpr Span(std::initializer_list<T> l) : ptr(l.begin()), len((Size)l.size()) {}
    constexpr Span(T *ptr_, Size len_) : ptr(ptr_), len(len_) {}
    template <Size N>
    constexpr Span(T (&arr)[N]) : ptr(arr), len(N) {}

    void Reset()
    {
        ptr = nullptr;
        len = 0;
    }

    T *begin() const { return ptr; }
    T *end() const { return ptr + len; }

    bool IsValid() const { return ptr; }

    T &operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < len);
        return ptr[idx];
    }

    operator Span<const T>() const { return Span<const T>(ptr, len); }

    Span Take(ArraySlice<T> slice) const
    {
        DebugAssert(slice.len >= 0 && slice.len <= len);
        DebugAssert(slice.offset >= 0 && slice.offset <= len - slice.len);

        Span<T> sub;
        sub.ptr = ptr + slice.offset;
        sub.len = slice.len;
        return sub;
    }
    Span Take(Size offset, Size len) const
        { return Take(ArraySlice<T>(offset, len)); }
};

// Use strlen() to build Span<const char> instead of the template-based
// array constructor.
template <>
struct Span<const char> {
    const char *ptr;
    Size len;

    Span() = default;
    constexpr Span(const char &ch) : ptr(&ch), len(1) {}
    constexpr Span(const char *ptr_, Size len_) : ptr(ptr_), len(len_) {}
    Span(const char *const &str) : ptr(str), len(str ? (Size)strlen(str) : 0) {}

    void Reset()
    {
        ptr = nullptr;
        len = 0;
    }

    const char *begin() const { return ptr; }
    const char *end() const { return ptr + len; }

    bool IsValid() const { return ptr; }

    char operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < len);
        return ptr[idx];
    }

    // The implementation comes later, after TestStr() is available
    bool operator==(Span<const char> other) const;
    bool operator==(const char *other) const;
    bool operator!=(Span<const char> other) const { return !(*this == other); }
    bool operator!=(const char *other) const { return !(*this == other); }

    Span Take(ArraySlice<const char> slice) const
    {
        DebugAssert(slice.len >= 0 && slice.len <= len);
        DebugAssert(slice.offset >= 0 && slice.offset <= len - slice.len);

        Span<const char> sub;
        sub.ptr = ptr + slice.offset;
        sub.len = slice.len;
        return sub;
    }
    Span Take(Size offset, Size len) const
        { return Take(ArraySlice<const char>(offset, len)); }
};

template <typename T>
static inline constexpr Span<T> MakeSpan(T *ptr, Size len)
{
    return Span<T>(ptr, len);
}
template <typename T, Size N>
static inline constexpr Span<T> MakeSpan(T (&arr)[N])
{
    return Span<T>(arr, N);
}

template <typename T, Size N>
class FixedArray {
public:
    T data[N];

    typedef T value_type;
    typedef T *iterator_type;

    FixedArray() = default;
    FixedArray(std::initializer_list<T> l)
    {
        DebugAssert(l.size() <= N);
        for (Size i = 0; i < l.size(); i++) {
            data[i] = l[i];
        }
    }

    operator Span<T>() { return Span<T>(data, N); }
    operator Span<const T>() const { return Span<const T>(data, N); }

    T *begin() { return data; }
    const T *begin() const { return data; }
    T *end() { return data + N; }
    const T *end() const { return data + N; }

    T &operator[](Size idx)
    {
        DebugAssert(idx >= 0 && idx < N);
        return data[idx];
    }
    const T &operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < N);
        return data[idx];
    }

    Span<T> Take(Size offset, Size len) const
        { return Span<T>(data, N).Take(offset, len); }
    Span<T> Take(ArraySlice<T> slice) const
        { return Span<T>(data, N).Take(slice); }
};

template <typename T, Size N>
class LocalArray {
public:
    T data[N];
    Size len = 0;

    typedef T value_type;
    typedef T *iterator_type;

    LocalArray() = default;
    LocalArray(std::initializer_list<T> l)
    {
        DebugAssert(l.size() <= N);
        for (const T &val: l) {
            Append(val);
        }
    }

    void Clear()
    {
        for (Size i = 0; i < len; i++) {
            data[i] = T();
        }
        len = 0;
    }

    operator Span<T>() { return Span<T>(data, len); }
    operator Span<const T>() const { return Span<const T>(data, len); }

    T *begin() { return data; }
    const T *begin() const { return data; }
    T *end() { return data + len; }
    const T *end() const { return data + len; }

    Size Available() const { return SIZE(data) - len; }

    T &operator[](Size idx)
    {
        DebugAssert(idx >= 0 && idx < len);
        return data[idx];
    }
    const T &operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < len);
        return data[idx];
    }

    T *AppendDefault(Size count = 1)
    {
        DebugAssert(len <= N - count);

        T *it = data + len;
        len += count;

        return it;
    }

    T *Append(const T &value)
    {
        DebugAssert(len < N);

        T *it = data + len;
        *it = value;
        len++;

        return it;
    }
    T *Append(Span<const T> values)
    {
        DebugAssert(values.len <= N - len);

        T *it = data + len;
        for (Size i = 0; i < values.len; i++) {
            data[len + i] = values[i];
        }
        len += values.len;

        return it;
    }

    void RemoveFrom(Size first)
    {
        DebugAssert(first >= 0 && first <= len);

        for (Size i = first; i < len; i++) {
            data[i] = T();
        }
        len = first;
    }
    void RemoveLast(Size count = 1)
    {
        DebugAssert(count >= 0 && count <= len);
        RemoveFrom(len - count);
    }

    Span<T> Take(Size offset, Size len) const
        { return Span<T>(data, this->len).Take(offset, len); }
    Span<T> Take(ArraySlice<T> slice) const
        { return Span<T>(data, this->len).Take(slice); }
};

template <typename T>
class HeapArray {
    // StaticAssert(std::is_trivially_copyable<T>::value);

public:
    T *ptr = nullptr;
    Size len = 0;
    Size capacity = 0;
    Allocator *allocator = nullptr;

    typedef T value_type;
    typedef T *iterator_type;

    HeapArray() = default;
    HeapArray(Allocator *alloc, Size min_capacity = 0) : allocator(alloc)
        { SetCapacity(min_capacity); }
    HeapArray(Size min_capacity) { Reserve(min_capacity); }
    HeapArray(std::initializer_list<T> l)
    {
        Reserve(l.size());
        for (const T &val: l) {
            Append(val);
        }
    }
    ~HeapArray() { Clear(); }

    HeapArray(HeapArray &&other) { *this = std::move(other); }
    HeapArray &operator=(HeapArray &&other)
    {
        Clear();
        memmove(this, &other, SIZE(other));
        memset(&other, 0, SIZE(other));
        return *this;
    }
    HeapArray(HeapArray &) = delete;
    HeapArray &operator=(const HeapArray &) = delete;

    void Clear(Size reserve_capacity = 0)
    {
        RemoveFrom(0);
        if (capacity > reserve_capacity) {
            SetCapacity(reserve_capacity);
        }
    }

    operator Span<T>() { return Span<T>(ptr, len); }
    operator Span<const T>() const { return Span<const T>(ptr, len); }

    T *begin() { return ptr; }
    const T *begin() const { return ptr; }
    T *end() { return ptr + len; }
    const T *end() const { return ptr + len; }

    Size Available() const { return capacity - len; }

    T &operator[](Size idx)
    {
        DebugAssert(idx >= 0 && idx < len);
        return ptr[idx];
    }
    const T &operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < len);
        return ptr[idx];
    }

    void SetCapacity(Size new_capacity)
    {
        DebugAssert(new_capacity >= 0);

        if (new_capacity == capacity)
            return;

        if (len > new_capacity) {
            for (Size i = new_capacity; i < len; i++) {
                ptr[i].~T();
            }
            len = new_capacity;
        }
        Allocator::Resize(allocator, (void **)&ptr,
                          capacity * SIZE(T), new_capacity * SIZE(T));
        capacity = new_capacity;
    }

    void Reserve(Size min_capacity)
    {
        if (min_capacity <= capacity)
            return;

        SetCapacity(min_capacity);
    }

    void Grow(Size reserve_capacity = 1)
    {
        if (reserve_capacity <= capacity - len)
            return;

        Size needed_capacity;
#ifndef NDEBUG
        DebugAssert(!AddOverflow(capacity, reserve_capacity, &needed_capacity));
#else
        needed_capacity = capacity + reserve_capacity;
#endif

        Size new_capacity;
        if (!capacity) {
            new_capacity = HEAPARRAY_BASE_CAPACITY;
        } else {
            new_capacity = capacity;
        }
        do {
            new_capacity = (Size)((float)new_capacity * HEAPARRAY_GROWTH_FACTOR);
        } while (new_capacity < needed_capacity);

        SetCapacity(new_capacity);
    }

    void Trim() { SetCapacity(len); }

    T *AppendDefault(Size count = 1)
    {
        Grow(count);

        T *first = ptr + len;
        for (Size i = 0; i < count; i++) {
            new (ptr + len) T;
            len++;
        }
        return first;
    }

    T *Append(const T &value)
    {
        Grow();

        T *first = ptr + len;
        if constexpr(!std::is_trivial<T>::value) {
            new (ptr + len) T;
        }
        ptr[len++] = value;
        return first;
    }
    T *Append(Span<const T> values)
    {
        Grow(values.len);

        T *first = ptr + len;
        for (const T &value: values) {
            if constexpr(!std::is_trivial<T>::value) {
                new (ptr + len) T;
            }
            ptr[len++] = value;
        }
        return first;
    }

    void RemoveFrom(Size first)
    {
        DebugAssert(first >= 0 && first <= len);

        if constexpr(!std::is_trivial<T>::value) {
            for (Size i = first; i < len; i++) {
                ptr[i].~T();
            }
        }
        len = first;
    }
    void RemoveLast(Size count = 1)
    {
        DebugAssert(count >= 0 && count <= len);
        RemoveFrom(len - count);
    }

    Span<T> Take(Size offset, Size len) const
        { return Span<T>(ptr, this->len).Take(offset, len); }
    Span<T> Take(ArraySlice<T> slice) const
        { return Span<T>(ptr, this->len).Take(slice); }

    Span<T> Leak()
    {
        Span<T> span = *this;

        ptr = nullptr;
        len = 0;
        capacity = 0;

        return span;
    }
};

template <typename T, Size BucketSize = 1024>
class DynamicQueue {
public:
    struct Bucket {
        T *values;
        LinkedAllocator allocator;
    };

    // TODO: Make the iterator faster (right now it's naive and goes through operator[])
    template <typename U>
    class Iterator {
    public:
        U *queue;
        Size idx;

        Iterator(U *queue, Size idx)
            : queue(queue), idx(idx) {}

        T *operator->() { return &(*queue)[idx]; }
        const T *operator->() const { return &(*queue)[idx]; }
        T &operator*() { return (*queue)[idx]; }
        const T &operator*() const { return (*queue)[idx]; }

        Iterator &operator++()
        {
            idx++;
            return *this;
        }
        Iterator operator++(int) const
        {
            Iterator ret = *this;
            ++(*this);
            return ret;
        }

        Iterator &operator--()
        {
            idx--;
            return *this;
        }
        Iterator operator--(int) const
        {
            Iterator ret = *this;
            --(*this);
            return ret;
        }

        bool operator==(const Iterator &other) const
            { return queue == other.queue && idx == other.idx; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }
    };

    HeapArray<Bucket *> buckets;
    Size offset = 0;
    Size len = 0;
    Allocator *bucket_allocator;

    typedef T value_type;
    typedef Iterator<DynamicQueue> iterator_type;

    DynamicQueue()
    {
        Bucket *first_bucket = *buckets.Append(CreateBucket());
        bucket_allocator = &first_bucket->allocator;
    }
    ~DynamicQueue()
    {
        for (Bucket *bucket: buckets) {
            DeleteBucket(bucket);
        }
    }

    DynamicQueue(DynamicQueue &&other) { *this = std::move(other); }
    DynamicQueue &operator=(DynamicQueue &&other)
    {
        Clear();
        memmove(this, &other, SIZE(other));
        memset(&other, 0, SIZE(other));
        return *this;
    }
    DynamicQueue(DynamicQueue &) = delete;
    DynamicQueue &operator=(const DynamicQueue &) = delete;

    void Clear()
    {
        for (Bucket *bucket: buckets) {
            DeleteBucket(bucket);
        }
        buckets.Clear();

        Bucket *first_bucket = *buckets.Append(CreateBucket());
        offset = 0;
        len = 0;
        bucket_allocator = &first_bucket->allocator;
    }

    iterator_type begin() { return iterator_type(this, 0); }
    Iterator<const DynamicQueue<T, BucketSize>> begin() const { return iterator_type(this, 0); }
    iterator_type end() { return iterator_type(this, len); }
    Iterator<const DynamicQueue<T, BucketSize>> end() const { return iterator_type(this, len); }

    const T &operator[](Size idx) const
    {
        DebugAssert(idx >= 0 && idx < len);

        idx += offset;
        Size bucket_idx = idx / BucketSize;
        Size bucket_offset = idx % BucketSize;

        return buckets[bucket_idx]->values[bucket_offset];
    }
    T &operator[](Size idx) { return (T &)(*(const DynamicQueue *)this)[idx]; }

    T *AppendDefault()
    {
        Size bucket_idx = (offset + len) / BucketSize;
        Size bucket_offset = (offset + len) % BucketSize;

        T *first = buckets[bucket_idx]->values + bucket_offset;
        if constexpr(!std::is_trivial<T>::value) {
            new (first) T;
        }

        len++;
        if (bucket_offset == BucketSize - 1) {
            Bucket *new_bucket = *buckets.Append(CreateBucket());
            bucket_allocator = &new_bucket->allocator;
        }

        return first;
    }

    T *Append(const T &value)
    {
        T *it = AppendDefault();
        *it = value;
        return it;
    }

    void RemoveFrom(Size from)
    {
        DebugAssert(from >= 0 && from <= len);

        if (from == len)
            return;
        if (!from) {
            Clear();
            return;
        }

        Size start_idx = offset + from;
        Size end_idx = offset + len;
        Size start_bucket_idx = start_idx / BucketSize;
        Size end_bucket_idx = end_idx / BucketSize;

        if constexpr(!std::is_trivial<T>::value) {
            iterator_type end_it = end();
            for (iterator_type it(this, from); it != end_it; ++it) {
                it->~T();
            }
        }

        for (Size i = start_bucket_idx + 1; i <= end_bucket_idx; i++) {
            DeleteBucket(buckets[i]);
        }
        buckets.RemoveFrom(start_bucket_idx + 1);
        if (start_idx % BucketSize == 0) {
            DeleteBucket(buckets[buckets.len - 1]);
            buckets[buckets.len - 1] = CreateBucket();
        }

        len = from;
        bucket_allocator = &buckets[buckets.len - 1]->allocator;
    }
    void RemoveLast(Size count = 1)
    {
        DebugAssert(count >= 0 && count <= len);
        RemoveFrom(len - count);
    }

    void RemoveFirst(Size count = 1)
    {
        DebugAssert(count >= 0 && count <= len);

        if (count == len) {
            Clear();
            return;
        }

        Size end_idx = offset + count;
        Size end_bucket_idx = end_idx / BucketSize;

        if constexpr(!std::is_trivial<T>::value) {
            iterator_type end_it(this, count);
            for (iterator_type it = begin(); it != end_it; ++it) {
                it->~T();
            }
        }

        if (end_bucket_idx) {
            for (Size i = 0; i < end_bucket_idx; i++) {
                DeleteBucket(buckets[i]);
            }
            memmove(&buckets[0], &buckets[end_bucket_idx],
                    (size_t)((buckets.len - end_bucket_idx) * SIZE(Bucket *)));
            buckets.RemoveLast(end_bucket_idx);
        }

        offset = (offset + count) % BucketSize;
        len -= count;
    }

private:
    Bucket *CreateBucket()
    {
        Bucket *new_bucket = (Bucket *)Allocator::Allocate(buckets.allocator, SIZE(Bucket));
        new (&new_bucket->allocator) LinkedAllocator;
        new_bucket->values = (T *)Allocator::Allocate(&new_bucket->allocator, BucketSize * SIZE(T));
        return new_bucket;
    }

    void DeleteBucket(Bucket *bucket)
    {
        bucket->allocator.~LinkedAllocator();
        Allocator::Release(buckets.allocator, bucket, SIZE(Bucket));
    }
};

template <Size N>
class Bitset {
public:
    template <typename T>
    class Iterator {
    public:
        T *bitset;
        Size offset;
        size_t bits = 0;
        int ctz;

        Iterator(T *bitset, Size offset)
            : bitset(bitset), offset(offset - 1)
        {
            operator++();
        }

        Size operator*() const
        {
            DebugAssert(offset <= ARRAY_SIZE(bitset->data));

            if (offset == ARRAY_SIZE(bitset->data))
                return -1;
            return offset * SIZE(size_t) * 8 + ctz;
        }

        Iterator &operator++()
        {
            DebugAssert(offset <= ARRAY_SIZE(bitset->data));

            while (!bits) {
                if (offset == ARRAY_SIZE(bitset->data) - 1)
                    return *this;
                bits = bitset->data[++offset];
            }

            ctz = CountTrailingZeros(bits);
            bits ^= (size_t)1 << ctz;

            return *this;
        }

        Iterator operator++(int) const
        {
            Iterator ret = *this;
            ++(*this);
            return ret;
        }

        bool operator==(const Iterator &other) const
            { return bitset == other.bitset && offset == other.offset; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }
    };

    static constexpr Size Bits = N;
    size_t data[(N + SIZE(size_t) - 1) / SIZE(size_t)] = {};

    typedef Size value_type;
    typedef Iterator<Bitset<N>> iterator_type;

    void Clear()
    {
        memset(data, 0, SIZE(data));
    }

    iterator_type begin() { return iterator_type(this, 0); }
    Iterator<const Bitset<N>> begin() const { return Iterator<const Bitset<N>>(this, 0); }
    iterator_type end() { return iterator_type(this, ARRAY_SIZE(data)); }
    Iterator<const Bitset<N>> end() const
        { return Iterator<const Bitset<N>>(this, ARRAY_SIZE(data)); }

    Size PopCount() const
    {
        Size count = 0;
        for (size_t bits: data) {
            count += PopCount(bits);
        }
        return count;
    }

    inline bool Test(Size idx) const
    {
        DebugAssert(idx >= 0 && idx < N);
        return data[idx / (SIZE(size_t) * 8)] & ((size_t)1 << (idx % (SIZE(size_t) * 8)));
    }
    inline void Set(Size idx, bool value = true)
    {
        DebugAssert(idx >= 0 && idx < N);
        if (value) {
            data[idx / (SIZE(size_t) * 8)] |= (size_t)1 << (idx % (SIZE(size_t) * 8));
        } else {
            data[idx / (SIZE(size_t) * 8)] &= ~((size_t)1 << (idx % (SIZE(size_t) * 8)));
        }
    }

    Bitset &operator&=(const Bitset &other)
    {
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            data[i] &= other.data[i];
        }
        return *this;
    }
    Bitset operator&(const Bitset &other)
    {
        Bitset ret;
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            ret.data[i] = data[i] & other.data[i];
        }
        return ret;
    }

    Bitset &operator|=(const Bitset &other)
    {
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            data[i] |= other.data[i];
        }
        return *this;
    }
    Bitset operator|(const Bitset &other)
    {
        Bitset ret;
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            ret.data[i] = data[i] | other.data[i];
        }
        return ret;
    }

    Bitset &operator^=(const Bitset &other)
    {
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            data[i] ^= other.data[i];
        }
        return *this;
    }
    Bitset operator^(const Bitset &other)
    {
        Bitset ret;
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            ret.data[i] = data[i] ^ other.data[i];
        }
        return ret;
    }

    Bitset &Flip()
    {
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            data[i] = ~data[i];
        }
        return *this;
    }
    Bitset operator~()
    {
        Bitset ret;
        for (Size i = 0; i < ARRAY_SIZE(data); i++) {
            ret.data[i] = ~data[i];
        }
        return ret;
    }

    // TODO: Shift operators
};

template <typename KeyType, typename ValueType,
          typename Handler = typename std::remove_pointer<ValueType>::type::HashHandler>
class HashTable {
public:
    ValueType *data = nullptr;
    Size count = 0;
    Size capacity = 0;
    Allocator *allocator = nullptr;

    HashTable() = default;
    ~HashTable()
    {
        if constexpr(std::is_trivial<ValueType>::value) {
            count = 0;
            Rehash(0);
        } else {
            Clear();
        }
    }

    HashTable(HashTable &&other) { *this = std::move(other); }
    HashTable &operator=(HashTable &&other)
    {
        Clear();
        memmove(this, &other, SIZE(other));
        memset(&other, 0, SIZE(other));
        return *this;
    }
    HashTable(HashTable &) = delete;
    HashTable &operator=(const HashTable &) = delete;

    void Clear()
    {
        for (Size i = 0; i < capacity; i++) {
            if (!Handler::IsEmpty(data[i])) {
                data[i].~ValueType();
            }
        }
        count = 0;

        Rehash(0);
    }

    ValueType *Find(const KeyType &key)
        { return (ValueType *)((const HashTable *)this)->Find(key); }
    const ValueType *Find(const KeyType &key) const
    {
        if (!capacity)
            return nullptr;

        uint64_t hash = Handler::HashKey(key);
        Size idx = HashToIndex(hash);
        return Find(&idx, key);
    }
    ValueType FindValue(const KeyType &key, const ValueType &default_value)
        { return (ValueType)((const HashTable *)this)->FindValue(key, default_value); }
    const ValueType FindValue(const KeyType &key, const ValueType &default_value) const
    {
        const ValueType *it = Find(key);
        return it ? *it : default_value;
    }

    std::pair<ValueType *, bool> AppendUninitialized(const KeyType &key)
    {
        return Insert(key);
    }
    std::pair<ValueType *, bool> Append(const ValueType &value)
    {
        const KeyType &key = Handler::GetKey(value);
        std::pair<ValueType *, bool> ret = Insert(key);
        if (ret.second) {
            *ret.first = value;
        }
        return ret;
    }

    ValueType *SetUninitialized(const KeyType &key)
    {
        return Insert(key, true).first;
    }
    ValueType *Set(const ValueType &value)
    {
        const KeyType &key = Handler::GetKey(value);
        ValueType *it = Insert(key).first;
        *it = value;
        return it;
    }

    void Remove(ValueType *it)
    {
        if (!it)
            return;
        DebugAssert(!Handler::IsEmpty(*it));

        it->~ValueType();

        Size idx = it - data;
        for (;;) {
            Size next_idx = (idx + 1) & (capacity - 1);
            if (Handler::IsEmpty(data[next_idx]) ||
                    KeyToIndex(Handler::GetKey(data[next_idx])) == next_idx) {
                new (&data[idx]) ValueType();
                break;
            } else {
                memmove(&data[idx], &data[next_idx], SIZE(*data));
            }
        }
    }
    void Remove(const KeyType &key) { Remove(Find(key)); }

private:
    ValueType *Find(Size *idx, const KeyType &key)
        { return (ValueType *)((const HashTable *)this)->Find(idx, key); }
    const ValueType *Find(Size *idx, const KeyType &key) const
    {
        while (!Handler::IsEmpty(data[*idx])) {
            const KeyType &it_key = Handler::GetKey(data[*idx]);
            if (Handler::CompareKeys(it_key, key))
                return &data[*idx];
            *idx = (*idx + 1) & (capacity - 1);
        }
        return nullptr;
    }

    std::pair<ValueType *, bool> Insert(const KeyType &key)
    {
        uint64_t hash = Handler::HashKey(key);

        if (capacity) {
            Size idx = HashToIndex(hash);
            ValueType *it = Find(&idx, key);
            if (!it) {
                if (count >= (Size)((float)capacity * HASHSET_MAX_LOAD_FACTOR)) {
                    Rehash(capacity << 1);
                    idx = HashToIndex(hash);
                    while (!Handler::IsEmpty(data[idx])) {
                        idx = (idx + 1) & (capacity - 1);
                    }
                }
                count++;
                return {&data[idx], true};
            } else {
                return {it, false};
            }
        } else {
            Rehash(HASHSET_BASE_CAPACITY);

            Size idx = HashToIndex(hash);
            count++;
            return {&data[idx], true};
        }
    }

    void Rehash(Size new_capacity)
    {
        if (new_capacity == capacity)
            return;
        DebugAssert(count <= new_capacity);

        ValueType *old_data = data;
        Size old_capacity = capacity;

        if (new_capacity) {
            // We need to zero memory for POD values, we could avoid it in other
            // cases but I'll wait for widespred constexpr if (C++17) support
            data = (ValueType *)Allocator::Allocate(allocator, new_capacity * SIZE(ValueType),
                                                    (int)Allocator::Flag::Zero);
            for (Size i = 0; i < new_capacity; i++) {
                new (&data[i]) ValueType;
            }
            capacity = new_capacity;

            for (Size i = 0; i < old_capacity; i++) {
                if (!Handler::IsEmpty(old_data[i])) {
                    Size new_idx = KeyToIndex(Handler::GetKey(old_data[i]));
                    while (!Handler::IsEmpty(data[new_idx])) {
                        new_idx = (new_idx + 1) & (capacity - 1);
                    }
                    memmove(&data[new_idx], &old_data[i], SIZE(*data));
                }
            }
        } else {
            data = nullptr;
            capacity = 0;
        }

        Allocator::Release(allocator, old_data, old_capacity * SIZE(ValueType));
    }

    Size HashToIndex(uint64_t hash) const
    {
        return (Size)(hash & (uint64_t)(capacity - 1));
    }
    Size KeyToIndex(const KeyType &key) const
    {
        uint64_t hash = Handler::HashKey(key);
        return HashToIndex(hash);
    }
};

// Stole this from Java's HashMap
static inline uint64_t DefaultHash(unsigned long long key)
{
    key ^= (key >> 20) ^ (key >> 12);
    key = key ^ (key >> 7) ^ (key >> 4);
    return key;
}
static inline uint64_t DefaultHash(char key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(unsigned char key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(short key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(unsigned short key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(int key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(unsigned int key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(long key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(unsigned long key) { return DefaultHash((unsigned long long)key); }
static inline uint64_t DefaultHash(long long key) { return DefaultHash((unsigned long long)key); }

// FNV-1a
static inline uint64_t DefaultHash(const char *key)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (Size i = 0; key[i]; i++) {
        hash ^= (uint64_t)key[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}
static inline uint64_t DefaultHash(Span<const char> key)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (char c: key) {
        hash ^= (uint64_t)c;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static inline bool DefaultCompare(char key1, char key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned char key1, unsigned char key2)
    { return key1 == key2; }
static inline bool DefaultCompare(short key1, short key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned short key1, unsigned short key2)
    { return key1 == key2; }
static inline bool DefaultCompare(int key1, int key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned int key1, unsigned int key2)
    { return key1 == key2; }
static inline bool DefaultCompare(long key1, long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned long key1, unsigned long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(long long key1, long long key2)
    { return key1 == key2; }
static inline bool DefaultCompare(unsigned long long key1, unsigned long long key2)
    { return key1 == key2; }

static inline bool DefaultCompare(const char *key1, const char *key2)
    { return !strcmp(key1, key2); }
static inline bool DefaultCompare(Span<const char> key1, const char *key2)
    { return key1 == key2; }
static inline bool DefaultCompare(Span<const char> key1, Span<const char> key2)
    { return key1 == key2; }

#define HASH_TABLE_HANDLER_EX_N(Name, ValueType, KeyMember, EmptyKey, HashFunc, CompareFunc) \
    class Name { \
    public: \
        static bool IsEmpty(const ValueType &value) \
            { return value.KeyMember == (EmptyKey); } \
        static bool IsEmpty(const ValueType *value) { return !value; } \
        static decltype(ValueType::KeyMember) GetKey(const ValueType &value) \
            { return value.KeyMember; } \
        static decltype(ValueType::KeyMember) GetKey(const ValueType *value) \
            { return value->KeyMember; } \
        static uint64_t HashKey(decltype(ValueType::KeyMember) key) \
            { return HashFunc(key); } \
        static bool CompareKeys(decltype(ValueType::KeyMember) key1, \
                                decltype(ValueType::KeyMember) key2) \
            { return CompareFunc((key1), (key2)); } \
    }
#define HASH_TABLE_HANDLER_EX(ValueType, KeyMember, EmptyKey, HashFunc, CompareFunc) \
    HASH_TABLE_HANDLER_EX_N(HashHandler, ValueType, KeyMember, EmptyKey, HashFunc, CompareFunc)
#define HASH_TABLE_HANDLER(ValueType, KeyMember) \
    HASH_TABLE_HANDLER_EX(ValueType, KeyMember, decltype(ValueType::KeyMember)(), DefaultHash, DefaultCompare)
#define HASH_TABLE_HANDLER_N(Name, ValueType, KeyMember) \
    HASH_TABLE_HANDLER_EX_N(Name, ValueType, KeyMember, decltype(ValueType::KeyMember)(), DefaultHash, DefaultCompare)

template <typename KeyType, typename ValueType>
class HashMap {
public:
    struct Bucket {
        KeyType key;
        ValueType value;

        HASH_TABLE_HANDLER(Bucket, key);
    };

    HashTable<KeyType, Bucket> table;

    void Clear() { table.Clear(); }

    std::pair<ValueType *, bool> Append(const KeyType &key, const ValueType &value)
    {
        std::pair<Bucket *, bool> ret = table.Append({key, value});
        return { &ret.first->value, ret.second };
    }
    std::pair<ValueType *, bool> AppendUninitialized(const KeyType &key)
    {
        std::pair<Bucket *, bool> ret = table.Append(key);
        if (ret.second) {
            ret.first->key = key;
        }
        return { &ret.first->value, ret.second };
    }

    ValueType *Set(const KeyType &key, const ValueType &value)
        { return &table.Set({key, value})->value; }
    ValueType *SetUninitialized(const KeyType &key)
    {
        Bucket *table_it = table.Set(key);
        table_it->key = key;
        return &table_it->value;
    }

    void Remove(ValueType *it)
    {
        if (!it)
            return;
        table.Remove((Bucket *)((uint8_t *)it - OFFSET_OF(Bucket, value)));
    }
    void Remove(const KeyType &key) { Remove(Find(key)); }

    ValueType *Find(const KeyType &key)
        { return (ValueType *)((const HashMap *)this)->Find(key); }
    const ValueType *Find(const KeyType &key) const
    {
        const Bucket *table_it = table.Find(key);
        return table_it ? &table_it->value : nullptr;
    }
    ValueType FindValue(const KeyType &key, const ValueType &default_value)
        { return (ValueType)((const HashMap *)this)->FindValue(key, default_value); }
    const ValueType FindValue(const KeyType &key, const ValueType &default_value) const
    {
        const ValueType *it = Find(key);
        return it ? *it : default_value;
    }
};

template <typename ValueType>
class HashSet {
public:
    struct Bucket {
        ValueType value;

        HASH_TABLE_HANDLER(Bucket, value);
    };

    HashTable<ValueType, Bucket> table;

    void Clear() { table.Clear(); }

    std::pair<ValueType *, bool> Append(const ValueType &value)
    {
        std::pair<Bucket *, bool> ret = table.Append({value});
        return { &ret.first->value, ret.second };
    }

    ValueType *Set(const ValueType &value)
        { return &table.Set({value})->value; }

    void Remove(ValueType *it)
    {
        if (!it)
            return;
        table.Remove((Bucket *)((uint8_t *)it - OFFSET_OF(Bucket, value)));
    }
    void Remove(const ValueType &value) { Remove(Find(value)); }

    ValueType *Find(const ValueType &value)
        { return (ValueType *)((const HashSet *)this)->Find(value); }
    const ValueType *Find(const ValueType &value) const
    {
        const Bucket *table_it = table.Find(value);
        return table_it ? &table_it->value : nullptr;
    }
    ValueType FindValue(const ValueType &value, const ValueType &default_value)
        { return (ValueType)((const HashSet *)this)->FindValue(value, default_value); }
    const ValueType FindValue(const ValueType &value, const ValueType &default_value) const
    {
        const ValueType *it = Find(value);
        return it ? *it : default_value;
    }
};

// ------------------------------------------------------------------------
// Date
// ------------------------------------------------------------------------

union Date {
    int32_t value;
    struct {
#ifdef ARCH_LITTLE_ENDIAN
        int8_t day;
        int8_t month;
        int16_t year;
#else
        int16_t year;
        int8_t month;
        int8_t day;
#endif
    } st;

    Date() = default;
    Date(int16_t year, int8_t month, int8_t day)
#ifdef ARCH_LITTLE_ENDIAN
        : st({day, month, year}) { DebugAssert(IsValid()); }
#else
        : st({year, month, day}) { DebugAssert(IsValid()); }
#endif

    static inline bool IsLeapYear(int16_t year)
    {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }
    static inline int8_t DaysInMonth(int16_t year, int8_t month)
    {
        static const int8_t DaysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return (int8_t)(DaysPerMonth[month - 1] + (month == 2 && IsLeapYear(year)));
    }

    static Date FromString(const char *date_str, bool strict = true);
    static Date FromJulianDays(int days);
    static Date FromCalendarDate(int days) { return Date::FromJulianDays(days + 2440588); }

    bool IsValid() const
    {
        if (st.year < -4712)
            return false;
        if (st.month < 1 || st.month > 12)
            return false;
        if (st.day < 1 || st.day > DaysInMonth(st.year, st.month))
            return false;

        return true;
    }

    bool operator==(Date other) const { return value == other.value; }
    bool operator!=(Date other) const { return value != other.value; }
    bool operator>(Date other) const { return value > other.value; }
    bool operator>=(Date other) const { return value >= other.value; }
    bool operator<(Date other) const { return value < other.value; }
    bool operator<=(Date other) const { return value <= other.value; }

    int ToJulianDays() const;
    int ToCalendarDate() const { return ToJulianDays() - 2440588; }

    int operator-(Date other) const
        { return ToJulianDays() - other.ToJulianDays(); }

    Date operator+(int days) const
    {
        if (days < 5 && days > -5) {
            Date date = *this;
            if (days > 0) {
                while (days--) {
                    ++date;
                }
            } else {
                while (days++) {
                    --date;
                }
            }
            return date;
        } else {
            return Date::FromJulianDays(ToJulianDays() + days);
        }
    }
    // That'll fail with INT_MAX days but that's far more days than can
    // be represented as a date anyway
    Date operator-(int days) const { return *this + (-days); }

    Date &operator+=(int days) { *this = *this + days; return *this; }
    Date &operator-=(int days) { *this = *this - days; return *this; }
    Date &operator++();
    Date operator++(int) { Date date = *this; ++(*this); return date; }
    Date &operator--();
    Date operator--(int) { Date date = *this; --(*this); return date; }
};
static inline uint64_t DefaultHash(Date date) { return DefaultHash(date.value); }
static inline bool DefaultCompare(Date date1, Date date2) { return date1 == date2; }

// ------------------------------------------------------------------------
// Time
// ------------------------------------------------------------------------

extern uint64_t g_start_time;

uint64_t GetMonotonicTime();

// ------------------------------------------------------------------------
// Strings
// ------------------------------------------------------------------------

Span<const char> MakeString(Allocator *alloc, Span<const char> str);
Span<const char> DuplicateString(Allocator *alloc, const char *str, Size max_len = -1);

static inline bool IsAsciiAlpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline bool IsAsciiDigit(char c)
{
    return (c >= '0' && c <= '9');
}
static inline bool IsAsciiAlphaOrDigit(char c)
{
    return IsAsciiAlpha(c) || IsAsciiDigit(c);
}

static inline char UpperAscii(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 32);
    } else {
        return c;
    }
}
static inline char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + 32);
    } else {
        return c;
    }
}

static inline bool TestStr(Span<const char> str1, Span<const char> str2)
{
    if (str1.len != str2.len)
        return false;
    for (Size i = 0; i < str1.len; i++) {
        if (str1[i] != str2[i])
            return false;
    }
    return true;
}
inline bool Span<const char>::operator==(Span<const char> other) const
    { return TestStr(*this, other); }
static inline bool TestStr(Span<const char> str1, const char *str2)
{
    Size i;
    for (i = 0; i < str1.len && str2[i]; i++) {
        if (str1[i] != str2[i])
            return false;
    }
    return (i == str1.len) && !str2[i];
}
inline bool Span<const char>::operator==(const char *other) const
    { return TestStr(*this, other); }
static inline bool TestStr(const char *str1, const char *str2)
    { return !strcmp(str1, str2); }

static inline int CmpStr(Span<const char> str1, Span<const char> str2)
{
    for (Size i = 0; i < str1.len && i < str2.len; i++) {
        if (str1[i] != str2[i])
            return str1[i] - str2[i];
    }
    if (str1.len < str2.len) {
        return -str2[str1.len];
    } else if (str1.len > str2.len) {
        return str1[str2.len];
    } else {
        return 0;
    }
}
static inline int CmpStr(Span<const char> str1, const char *str2)
{
    Size i;
    for (i = 0; i < str1.len && str2[i]; i++) {
        if (str1[i] != str2[i])
            return str1[i] - str2[i];
    }
    if (str1.len == i) {
        return -str2[i];
    } else {
        return str1[i];
    }
}
static inline int CmpStr(const char *str1, const char *str2)
    { return strcmp(str1, str2); }

static inline Span<const char> SplitStr(Span<const char> str, char split_char,
                                        Span<const char> *out_remainder = nullptr)
{
    Size part_len = 0;
    while (part_len < str.len) {
        if (str[part_len] == split_char) {
            if (out_remainder) {
                *out_remainder = str.Take(part_len + 1, str.len - part_len - 1);
            }
            return str.Take(0, part_len);
        }
        part_len++;
    }

    if (out_remainder) {
        *out_remainder = str.Take(str.len, 0);
    }
    return str;
}
static inline Span<const char> SplitStr(const char *str, char split_char,
                                        const char **out_remainder = nullptr)
{
    Size part_len = 0;
    while (str[part_len]) {
        if (str[part_len] == split_char) {
            if (out_remainder) {
                *out_remainder = str + part_len + 1;
            }
            return MakeSpan(str, part_len);
        }
        part_len++;
    }

    if (out_remainder) {
        *out_remainder = str + part_len;
    }
    return MakeSpan(str, part_len);
}

static inline Span<const char> SplitStrLine(Span<const char> str,
                                            Span<const char> *out_remainder = nullptr)
{
    Span<const char> part = SplitStr(str, '\n', out_remainder);
    if (part.len < str.len && part.len && part[part.len - 1] == '\r') {
        part.len--;
    }
    return part;
}
static inline Span<const char> SplitStrLine(const char *str,
                                            const char **out_remainder = nullptr)
{
    Span<const char> part = SplitStr(str, '\n', out_remainder);
    if (str[part.len] && part.len && part[part.len - 1] == '\r') {
        part.len--;
    }
    return part;
}

static inline Span<const char> SplitStrAny(Span<const char> str, const char *split_chars,
                                           Span<const char> *out_remainder = nullptr)
{
    char split_mask[256 / 8] = {};
    for (Size i = 0; split_chars[i]; i++) {
        split_mask[i / 8] |= (char)(1 << (split_chars[i] % 8));
    }

    Size part_len = 0;
    while (part_len < str.len) {
        if (split_mask[str[part_len] / 8] & (str[part_len] % 8)) {
            if (out_remainder) {
                *out_remainder = str.Take(part_len, str.len - part_len);
            }
            return str.Take(0, part_len);
        }
        part_len++;
    }

    if (out_remainder) {
        *out_remainder = str.Take(part_len, 0);
    }
    return str.Take(0, str.len);
}
static inline Span<const char> SplitStrAny(const char *str, const char *split_chars,
                                           const char **out_remainder = nullptr)
{
    char split_mask[256 / 8] = {};
    for (Size i = 0; split_chars[i]; i++) {
        split_mask[i / 8] |= (char)(1 << (split_chars[i] % 8));
    }

    Size part_len = 0;
    while (str[part_len]) {
        if (split_mask[str[part_len] / 8] & (str[part_len] % 8)) {
            if (out_remainder) {
                *out_remainder = str + part_len + 1;
            }
            return MakeSpan(str, part_len);
        }
        part_len++;
    }

    if (out_remainder) {
        *out_remainder = str + part_len;
    }
    return MakeSpan(str, part_len);
}

static inline Span<const char> TrimStr(Span<const char> str, const char *trim_chars = " \t\r\n")
{
    while (str.len && strchr(trim_chars, str[0])) {
        str.ptr++;
        str.len--;
    }
    while (str.len && strchr(trim_chars, str[str.len - 1])) {
        str.len--;
    }

    return str;
}

// ------------------------------------------------------------------------
// Format
// ------------------------------------------------------------------------

class FmtArg {
public:
    enum class Type {
        StrRef,
        StrBuf,
        Char,
        Bool,
        Integer,
        Unsigned,
        Double,
        Binary,
        Hexadecimal,
        MemorySize,
        DiskSize,
        Date,
        List
    };

    Type type;
    union {
        Span<const char> str_ref;
        char str_buf[12];
        char ch;
        bool b;
        int64_t i;
        uint64_t u;
        struct {
            double value;
            int precision;
        } d;
        const void *ptr;
        Size size;
        Date date;
        struct {
            Span<FmtArg> args;
            const char *separator;
        } list;
    } value;
    int repeat = 1;

    FmtArg() = default;
    FmtArg(const char *str) : type(Type::StrRef) { value.str_ref = str ? str : "(null)"; }
    FmtArg(Span<const char> str) : type(Type::StrRef) { value.str_ref = str; }
    FmtArg(char c) : type(Type::Char) { value.ch = c; }
    FmtArg(bool b) : type(Type::Bool) { value.b = b; }
    FmtArg(unsigned char u)  : type(Type::Unsigned) { value.u = u; }
    FmtArg(short i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned short u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(int i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned int u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(long i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned long u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(long long i) : type(Type::Integer) { value.i = i; }
    FmtArg(unsigned long long u) : type(Type::Unsigned) { value.u = u; }
    FmtArg(float f) : type(Type::Double) { value.d = { (double)f, -1 }; }
    FmtArg(double d) : type(Type::Double) { value.d = { d, -1 }; }
    FmtArg(const void *ptr) : type(Type::Hexadecimal) { value.u = (uint64_t)ptr; }
    FmtArg(const Date &date) : type(Type::Date) { value.date = date; }
    FmtArg(Span<FmtArg> args) : type(Type::List)
    {
        value.list.args = args;
        value.list.separator = ", ";
    }

    FmtArg &Repeat(int new_repeat) { repeat = new_repeat; return *this; }
};

static inline FmtArg FmtBin(uint64_t u)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Binary;
    arg.value.u = u;
    return arg;
}
static inline FmtArg FmtHex(uint64_t u)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Hexadecimal;
    arg.value.u = u;
    return arg;
}

static inline FmtArg FmtDouble(double d, int precision = -1)
{
    FmtArg arg;
    arg.type = FmtArg::Type::Double;
    arg.value.d.value = d;
    arg.value.d.precision = precision;
    return arg;
}

static inline FmtArg FmtMemSize(Size size)
{
    FmtArg arg;
    arg.type = FmtArg::Type::MemorySize;
    arg.value.size = size;
    return arg;
}
static inline FmtArg FmtDiskSize(Size size)
{
    FmtArg arg;
    arg.type = FmtArg::Type::DiskSize;
    arg.value.size = size;
    return arg;
}
static inline FmtArg FmtList(Span<FmtArg> args, const char *sep = ", ")
{
    FmtArg arg;
    arg.type = FmtArg::Type::List;
    arg.value.list.args = args;
    arg.value.list.separator = sep;
    return arg;
}

enum class LogLevel {
    Debug,
    Info,
    Error
};

Span<char> FmtFmt(Span<char> buf, const char *fmt, Span<const FmtArg> args);
Span<char> FmtFmt(Allocator *alloc, const char *fmt, Span<const FmtArg> args);
void PrintFmt(FILE *fp, const char *fmt, Span<const FmtArg> args);

// Print formatted strings to fixed-size buffer
static inline Span<char> Fmt(Span<char> buf, const char *fmt)
{
    return FmtFmt(buf, fmt, {});
}
template <typename... Args>
static inline Span<char> Fmt(Span<char> buf, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    return FmtFmt(buf, fmt, fmt_args);
}

// Print formatted strings to dynamic char array
static inline Span<char> Fmt(Allocator *alloc, const char *fmt)
{
    return FmtFmt(alloc, fmt, {});
}
template <typename... Args>
static inline Span<char> Fmt(Allocator *alloc, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    return FmtFmt(alloc, fmt, fmt_args);
}

// Print formatted strings to stdio FILE
static inline void Print(FILE *fp, const char *fmt)
{
    PrintFmt(fp, fmt, {});
}
template <typename... Args>
static inline void Print(FILE *fp, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    PrintFmt(fp, fmt, fmt_args);
}

// Print formatted strings to stdout
static inline void Print(const char *fmt)
{
    PrintFmt(stdout, fmt, {});
}
template <typename... Args>
static inline void Print(const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    PrintFmt(stdout, fmt, fmt_args);
}

// Variants of the Print() functions with terminating newline
template <typename... Args>
static inline void PrintLn(FILE *fp, const char *fmt, Args... args)
{
    Print(fp, fmt, args...);
    putc('\n', fp);
}
template <typename... Args>
static inline void PrintLn(const char *fmt, Args... args)
{
    Print(stdout, fmt, args...);
    putchar('\n');
}
static inline void PrintLn(FILE *fp = stdout)
{
    fputc('\n', fp);
}

// ------------------------------------------------------------------------
// Debug and errors
// ------------------------------------------------------------------------

extern bool enable_debug;

typedef void LogHandlerFunc(LogLevel level, const char *ctx,
                            const char *fmt, Span<const FmtArg> args);

void LogFmt(LogLevel level, const char *ctx, const char *fmt, Span<const FmtArg> args);

// Log text line to stderr with context, for the Log() macros below
static inline void Log(LogLevel level, const char *ctx, const char *fmt)
{
    LogFmt(level, ctx, fmt, {});
}
template <typename... Args>
static inline void Log(LogLevel level, const char *ctx, const char *fmt, Args... args)
{
    const FmtArg fmt_args[] = { FmtArg(args)... };
    LogFmt(level, ctx, fmt, fmt_args);
}

#define LOG_LOCATION (__FILE__ ":" STRINGIFY(__LINE__))
#define LogDebug(...) Log(LogLevel::Debug, LOG_LOCATION, __VA_ARGS__)
#define LogInfo(...) Log(LogLevel::Info, LOG_LOCATION, __VA_ARGS__)
#define LogError(...) Log(LogLevel::Error, LOG_LOCATION, __VA_ARGS__)

void DefaultLogHandler(LogLevel level, const char *ctx,
                       const char *fmt, Span<const FmtArg> args);

void StartConsoleLog(LogLevel level);
void EndConsoleLog();

void PushLogHandler(std::function<LogHandlerFunc> handler);
void PopLogHandler();

// ------------------------------------------------------------------------
// System
// ------------------------------------------------------------------------

enum class CompressionType {
    None,
    Zlib,
    Gzip
};

#ifdef _WIN32
    #define PATH_SEPARATORS "\\/"
    #define FOPEN_COMMON_FLAGS
#else
    #define PATH_SEPARATORS "/"
    #define FOPEN_COMMON_FLAGS "e"
#endif

CompressionType GetPathCompression(const char *filename);
Size GetPathExtension(const char *filename, Span<char> out_buf,
                      CompressionType *out_compression_type = nullptr);

enum class FileType {
    Directory,
    File,
    Unknown
};

struct FileInfo {
    FileType type;
};

enum class EnumStatus {
    Error,
    Partial,
    Done
};

bool TestPath(const char *path, FileType type = FileType::Unknown);

EnumStatus EnumerateDirectory(const char *dirname, const char *filter,
                              std::function<bool(const char *, const FileInfo &)> func);
bool EnumerateDirectoryFiles(const char *dirname, const char *filter, Allocator *str_alloc,
                             HeapArray<const char *> *out_files, Size max_files);

const char *GetApplicationExecutable(); // Can be NULL
const char *GetApplicationDirectory(); // Can be NULL

// ------------------------------------------------------------------------
// Tasks
// ------------------------------------------------------------------------

class Async {
    std::atomic_int success {1};
    std::atomic_int remaining_tasks {0};

public:
    Async();
    ~Async();

    Async(Async &) = delete;
    Async &operator=(const Async &) = delete;

    void AddTask(const std::function<bool()> &f);
    bool Sync();

private:
    void RunTask(struct Task *task);

    static void RunWorker(struct ThreadPool *thread_pool, struct WorkerThread *worker);
    static void StealAndRunTasks();
};

class AsyncHelper {
    Async async;

public:
    template <typename Fun>
    AsyncHelper(Fun f)
    {
        if constexpr(std::is_same<typename std::result_of<Fun()>::type, void>::value) {
            async.AddTask([=]() {
                f();
                return true;
            });
        } else {
            async.AddTask(std::function<bool()>(f));
        }
    }
    ~AsyncHelper() { Sync(); }

    AsyncHelper(const AsyncHelper &other) = delete;
    AsyncHelper &operator=(const AsyncHelper &) = delete;

    bool Sync() { return async.Sync(); }
};

// See DEFER for an explanation about the suffixes used here
#define ASYNC \
    AsyncHelper UNIQUE_ID(async) = [&]()
#define ASYNC_N(Name) \
    AsyncHelper Name = [&]()
#define ASYNC_C(...) \
    AsyncHelper UNIQUE_ID(async) = [&, __VA_ARGS__]()
#define ASYNC_NC(Name, ...) \
    AsyncHelper Name = [&, __VA_ARGS__]()

// ------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------

class StreamReader {
public:
    enum class SourceType {
        File,
        Memory
    };

    // NOTE: Should we use DuplicateString() for this? Same question in StreamWriter.
    const char *filename;

    struct {
        SourceType type;
        union {
            FILE *fp;
            struct {
                Span<const uint8_t> buf;
                Size pos;
            } memory;
        } u;
        bool owned = false;

        bool eof;
    } source;

    struct {
        CompressionType type = CompressionType::None;
        union {
            struct MinizInflateContext *miniz;
        } u;
    } compression;

    bool eof;
    bool error;

    StreamReader() { Close(); }
    StreamReader(Span<const uint8_t> buf, const char *filename = nullptr,
                 CompressionType compression_type = CompressionType::None)
        { Open(buf, filename, compression_type); }
    StreamReader(FILE *fp, const char *filename = nullptr,
                 CompressionType compression_type = CompressionType::None)
        { Open(fp, filename, compression_type); }
    StreamReader(const char *filename,
                 CompressionType compression_type = CompressionType::None)
        { Open(filename, compression_type); }
    ~StreamReader() { Close(); }

    bool Open(Span<const uint8_t> buf, const char *filename = nullptr,
              CompressionType compression_type = CompressionType::None);
    bool Open(FILE *fp, const char *filename = nullptr,
              CompressionType compression_type = CompressionType::None);
    bool Open(const char *filename, CompressionType compression_type = CompressionType::None);
    void Close();

    Size RemainingLen() const;

    Size Read(Size max_len, void *out_buf);
    Size ReadAll(Size max_len, HeapArray<uint8_t> *out_buf);

private:
    bool InitDecompressor(CompressionType type);
    void ReleaseResources();

    Size Deflate(Size max_len, void *out_buf);

    Size ReadRaw(Size max_len, void *out_buf);
};

static inline Size ReadFile(const char *filename, Size max_len, CompressionType compression_type,
                            HeapArray<uint8_t> *out_buf)
{
    StreamReader st(filename, compression_type);
    return st.ReadAll(max_len, out_buf);
}
static inline Size ReadFile(const char *filename, Size max_len, HeapArray<uint8_t> *out_buf)
{
    StreamReader st(filename);
    return st.ReadAll(max_len, out_buf);
}

class StreamWriter {
public:
    enum class DestinationType {
        File,
        Memory
    };

    const char *filename;

    struct {
        DestinationType type;
        union {
            HeapArray<uint8_t> *mem;
            FILE *fp;
        } u;
        bool owned = false;
    } dest;

    struct {
        CompressionType type = CompressionType::None;
        union {
            struct MinizDeflateContext *miniz;
        } u;
    } compression;

    bool open = false;
    bool error;

    StreamWriter() { Close(); }
    StreamWriter(HeapArray<uint8_t> *mem, const char *filename = nullptr,
                 CompressionType compression_type = CompressionType::None)
        { Open(mem, filename, compression_type); }
    StreamWriter(FILE *fp, const char *filename = nullptr,
                 CompressionType compression_type = CompressionType::None)
        { Open(fp, filename, compression_type); }
    StreamWriter(const char *filename,
                 CompressionType compression_type = CompressionType::None)
        { Open(filename, compression_type); }
    ~StreamWriter() { Close(); }

    bool Open(HeapArray<uint8_t> *mem, const char *filename = nullptr,
              CompressionType compression_type = CompressionType::None);
    bool Open(FILE *fp, const char *filename = nullptr,
              CompressionType compression_type = CompressionType::None);
    bool Open(const char *filename, CompressionType compression_type = CompressionType::None);
    bool Close();

    bool Write(Span<const uint8_t> buf);
    bool Write(const void *buf, Size len) { return Write(MakeSpan((const uint8_t *)buf, len)); }

private:
    bool InitCompressor(CompressionType type);
    void ReleaseResources();

    bool WriteRaw(Span<const uint8_t> buf);
};

// ------------------------------------------------------------------------
// Option Parser
// ------------------------------------------------------------------------

static inline bool TestOption(const char *opt, const char *test1, const char *test2 = nullptr)
{
    return TestStr(opt, test1) ||
           (test2 && TestStr(opt, test2));
}

class OptionParser {
    Size limit;
    Size smallopt_offset = 0;
    char buf[80];

public:
    Span<const char *> args;
    Size pos = 0;

    const char *current_option = nullptr;
    const char *current_value = nullptr;

    OptionParser(Span<const char *> args)
        : limit(args.len), args(args) {}
    OptionParser(int argc, char **argv)
        : limit(argc > 0 ? argc - 1 : 0),
          args(limit ? (const char **)(argv + 1) : nullptr, limit) {}

    const char *ConsumeOption();
    const char *ConsumeOptionValue();
    const char *ConsumeNonOption();
    void ConsumeNonOptions(HeapArray<const char *> *non_options);

    const char *RequireOptionValue(void (*usage_func)(FILE *fp) = nullptr);

    bool TestOption(const char *test1, const char *test2 = nullptr) const
        { return ::TestOption(current_option, test1, test2); }
};
