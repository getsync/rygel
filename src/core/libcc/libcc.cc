// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "libcc.hh"
#if !defined(LIBCC_NO_MINIZ) && __has_include("../../../vendor/miniz/miniz.h")
    #define MINIZ_NO_STDIO
    #define MINIZ_NO_TIME
    #define MINIZ_NO_ARCHIVE_APIS
    #define MINIZ_NO_ARCHIVE_WRITING_APIS
    #define MINIZ_NO_ZLIB_APIS
    #define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    #define MINIZ_NO_MALLOC
    #include "../../../vendor/miniz/miniz.h"
#endif
#include "../../../vendor/grisu-exact/grisu_exact.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <fcntl.h>
    #include <io.h>
    #include <direct.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <dlfcn.h>
    #include <dirent.h>
    #include <fcntl.h>
    #include <fnmatch.h>
    #include <poll.h>
    #include <signal.h>
    #include <spawn.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <termios.h>
    #include <time.h>
    #include <unistd.h>

    extern char **environ;
#endif
#ifdef __APPLE__
    #include <mach-o/dyld.h>

    #define off64_t off_t
    #define fseeko64 fseeko
    #define ftello64 ftello
#endif
#include <chrono>
#include <thread>

namespace RG {

// ------------------------------------------------------------------------
// Utility
// ------------------------------------------------------------------------

#ifndef FELIX
const char *FelixVersion = "(unknown version)";
#endif

extern "C" void AssertMessage(const char *filename, int line, const char *cond)
{
    fprintf(stderr, "%s:%d: Assertion '%s' failed\n", filename, line, cond);
}

// ------------------------------------------------------------------------
// Memory / Allocator
// ------------------------------------------------------------------------

// This Allocator design should allow efficient and mostly-transparent use of memory
// arenas and simple pointer-bumping allocator. This will be implemented later, for
// now it's just a doubly linked list of malloc() memory blocks.

class MallocAllocator: public Allocator {
protected:
    void *Allocate(Size size, unsigned int flags = 0) override
    {
        void *ptr = malloc((size_t)size);
        if (RG_UNLIKELY(!ptr)) {
            LogError("Failed to allocate %1 of memory", FmtMemSize(size));
            abort();
        }
        if (flags & (int)Allocator::Flag::Zero) {
            memset(ptr, 0, (size_t)size);
        }
        return ptr;
    }

    void Resize(void **ptr, Size old_size, Size new_size, unsigned int flags = 0) override
    {
        if (!new_size) {
            Release(*ptr, old_size);
            *ptr = nullptr;
        } else {
            void *new_ptr = realloc(*ptr, (size_t)new_size);
            if (RG_UNLIKELY(new_size && !new_ptr)) {
                LogError("Failed to resize %1 memory block to %2",
                         FmtMemSize(old_size), FmtMemSize(new_size));
                abort();
            }
            if ((flags & (int)Allocator::Flag::Zero) && new_size > old_size) {
                memset((uint8_t *)new_ptr + old_size, 0, (size_t)(new_size - old_size));
            }
            *ptr = new_ptr;
        }
    }

    void Release(void *ptr, Size) override
    {
        free(ptr);
    }
};

Allocator *GetDefaultAllocator()
{
    static Allocator *default_allocator = new RG_DEFAULT_ALLOCATOR;
    return default_allocator;
}

LinkedAllocator& LinkedAllocator::operator=(LinkedAllocator &&other)
{
    ReleaseAll();
    list = other.list;
    other.list = {};

    return *this;
}

void LinkedAllocator::ReleaseAll()
{
    Node *head = list.next;
    while (head) {
        Node *next = head->next;
        Allocator::Release(allocator, head, -1);
        head = next;
    }
    list = {};
}

void *LinkedAllocator::Allocate(Size size, unsigned int flags)
{
    Bucket *bucket = (Bucket *)Allocator::Allocate(allocator, RG_SIZE(*bucket) + size, flags);

    if (list.prev) {
        list.prev->next = &bucket->head;
        bucket->head.prev = list.prev;
        bucket->head.next = nullptr;
        list.prev = &bucket->head;
    } else {
        list.prev = &bucket->head;
        list.next = &bucket->head;
        bucket->head.prev = nullptr;
        bucket->head.next = nullptr;
    }

    return bucket->data;
}

void LinkedAllocator::Resize(void **ptr, Size old_size, Size new_size, unsigned int flags)
{
    if (!*ptr) {
        *ptr = Allocate(new_size, flags);
    } else if (!new_size) {
        Release(*ptr, old_size);
        *ptr = nullptr;
    } else {
        Bucket *bucket = PointerToBucket(*ptr);
        Allocator::Resize(allocator, (void **)&bucket, RG_SIZE(*bucket) + old_size,
                          RG_SIZE(*bucket) + new_size, flags);

        if (bucket->head.next) {
            bucket->head.next->prev = &bucket->head;
        } else {
            list.prev = &bucket->head;
        }
        if (bucket->head.prev) {
            bucket->head.prev->next = &bucket->head;
        } else {
            list.next = &bucket->head;
        }

        *ptr = bucket->data;
    }
}

void LinkedAllocator::Release(void *ptr, Size size)
{
    if (ptr) {
        Bucket *bucket = PointerToBucket(ptr);

        if (bucket->head.next) {
            bucket->head.next->prev = bucket->head.prev;
        } else {
            list.prev = bucket->head.prev;
        }
        if (bucket->head.prev) {
            bucket->head.prev->next = bucket->head.next;
        } else {
            list.next = bucket->head.next;
        }

        Allocator::Release(allocator, bucket, size);
    }
}

void *BlockAllocatorBase::Allocate(Size size, unsigned int flags)
{
    RG_ASSERT(size >= 0);

    LinkedAllocator *alloc = GetAllocator();

    // Keep alignement requirements
    Size aligned_size = AlignSizeValue(size);

    if (AllocateSeparately(aligned_size)) {
        uint8_t *ptr = (uint8_t *)Allocator::Allocate(alloc, size, flags);
        return ptr;
    } else {
        if (!current_bucket || (current_bucket->used + aligned_size) > block_size) {
            current_bucket = (Bucket *)Allocator::Allocate(alloc, RG_SIZE(Bucket) + block_size,
                                                           flags & ~(int)Allocator::Flag::Zero);
            current_bucket->used = 0;
        }

        uint8_t *ptr = current_bucket->data + current_bucket->used;
        current_bucket->used += aligned_size;

        if (flags & (int)Allocator::Flag::Zero) {
            memset(ptr, 0, size);
        }

        last_alloc = ptr;
        return ptr;
    }
}

void BlockAllocatorBase::Resize(void **ptr, Size old_size, Size new_size, unsigned int flags)
{
    RG_ASSERT(old_size >= 0);
    RG_ASSERT(new_size >= 0);

    if (!new_size) {
        Release(*ptr, old_size);
    } else {
        if (!*ptr) {
            old_size = 0;
        }

        Size aligned_old_size = AlignSizeValue(old_size);
        Size aligned_new_size = AlignSizeValue(new_size);
        Size aligned_delta = aligned_new_size - aligned_old_size;

        // Try fast path
        if (*ptr && *ptr == last_alloc && (current_bucket->used + aligned_delta) <= block_size &&
                !AllocateSeparately(aligned_new_size)) {
            current_bucket->used += aligned_delta;

            if ((flags & (int)Allocator::Flag::Zero) && new_size > old_size) {
                memset(ptr + old_size, 0, new_size - old_size);
            }
        } else if (AllocateSeparately(aligned_old_size)) {
            LinkedAllocator *alloc = GetAllocator();
            Allocator::Resize(alloc, ptr, old_size, new_size, flags);
        } else {
            void *new_ptr = Allocate(new_size, flags & ~(int)Allocator::Flag::Zero);
            if (new_size > old_size) {
                memcpy(new_ptr, *ptr, old_size);

                if (flags & (int)Allocator::Flag::Zero) {
                    memset(ptr + old_size, 0, new_size - old_size);
                }
            } else {
                memcpy(new_ptr, *ptr, new_size);
            }

            *ptr = new_ptr;
        }
    }
}

void BlockAllocatorBase::Release(void *ptr, Size size)
{
    RG_ASSERT(size >= 0);

    if (ptr) {
        LinkedAllocator *alloc = GetAllocator();

        Size aligned_size = AlignSizeValue(size);

        if (ptr == last_alloc) {
            current_bucket->used -= aligned_size;
            if (!current_bucket->used) {
                Allocator::Release(alloc, current_bucket, RG_SIZE(Bucket) + block_size);
                current_bucket = nullptr;
            }
            last_alloc = nullptr;
        } else if (AllocateSeparately(aligned_size)) {
            Allocator::Release(alloc, ptr, size);
        }
    }
}

void BlockAllocatorBase::CopyFrom(BlockAllocatorBase *other)
{
    block_size = other->block_size;
    current_bucket = other->current_bucket;
    last_alloc = other->last_alloc;
}

void BlockAllocatorBase::ForgetCurrentBlock()
{
    current_bucket = nullptr;
    last_alloc = nullptr;
}

BlockAllocator& BlockAllocator::operator=(BlockAllocator &&other)
{
    allocator.operator=(std::move(other.allocator));
    CopyFrom(&other);

    return *this;
}

void BlockAllocator::ReleaseAll()
{
    ForgetCurrentBlock();
    allocator.ReleaseAll();
}

IndirectBlockAllocator& IndirectBlockAllocator::operator=(IndirectBlockAllocator &&other)
{
    allocator->operator=(std::move(*other.allocator));
    CopyFrom(&other);

    return *this;
}

void IndirectBlockAllocator::ReleaseAll()
{
    allocator->ReleaseAll();
}

// ------------------------------------------------------------------------
// Date
// ------------------------------------------------------------------------

// XXX: Rewrite the ugly parsing part
Date Date::FromString(Span<const char> date_str, int flags, Span<const char> *out_remaining)
{
    Date date;

    int parts[3] = {};
    int lengths[3] = {};
    Size offset = 0;
    for (int i = 0; i < 3; i++) {
        int mult = 1;
        while (offset < date_str.len) {
            char c = date_str[offset];
            int digit = c - '0';
            if ((unsigned int)digit < 10) {
                parts[i] = (parts[i] * 10) + digit;
                if (RG_UNLIKELY(++lengths[i] > 5))
                    goto malformed;
            } else if (!lengths[i] && c == '-' && mult == 1 && i != 1) {
                mult = -1;
            } else if (RG_UNLIKELY(i == 2 && !(flags & (int)ParseFlag::End) && c != '/' && c != '-')) {
                break;
            } else if (RG_UNLIKELY(!lengths[i] || (c != '/' && c != '-'))) {
                goto malformed;
            } else {
                offset++;
                break;
            }
            offset++;
        }
        parts[i] *= mult;
    }
    if ((flags & (int)ParseFlag::End) && offset < date_str.len)
        goto malformed;

    if (RG_UNLIKELY((unsigned int)lengths[1] > 2))
        goto malformed;
    if (RG_UNLIKELY((lengths[0] > 2) == (lengths[2] > 2))) {
        if (flags & (int)ParseFlag::Log) {
            LogError("Ambiguous date string '%1'", date_str);
        }
        return {};
    } else if (lengths[2] > 2) {
        std::swap(parts[0], parts[2]);
    }
    if (RG_UNLIKELY(parts[0] < -INT16_MAX || parts[0] > INT16_MAX || (unsigned int)parts[2] > 99))
        goto malformed;

    date.st.year = (int16_t)parts[0];
    date.st.month = (int8_t)parts[1];
    date.st.day = (int8_t)parts[2];
    if ((flags & (int)ParseFlag::Validate) && !date.IsValid()) {
        if (flags & (int)ParseFlag::Log) {
            LogError("Invalid date string '%1'", date_str);
        }
        return {};
    }

    if (out_remaining) {
        *out_remaining = date_str.Take(offset, date_str.len - offset);
    }
    return date;

malformed:
    if (flags & (int)ParseFlag::Log) {
        LogError("Malformed date string '%1'", date_str);
    }
    return {};
}

Date Date::FromJulianDays(int days)
{
    RG_ASSERT(days >= 0);

    // Algorithm from Richards, copied from Wikipedia:
    // https://en.wikipedia.org/w/index.php?title=Julian_day&oldid=792497863

    Date date;
    {
        int f = days + 1401 + (((4 * days + 274277) / 146097) * 3) / 4 - 38;
        int e = 4 * f + 3;
        int g = e % 1461 / 4;
        int h = 5 * g + 2;
        date.st.day = (int8_t)(h % 153 / 5 + 1);
        date.st.month = (int8_t)((h / 153 + 2) % 12 + 1);
        date.st.year = (int16_t)((e / 1461) - 4716 + (date.st.month < 3));
    }

    return date;
}

int Date::ToJulianDays() const
{
    RG_ASSERT(IsValid());

    // Straight from the Web:
    // http://www.cs.utsa.edu/~cs1063/projects/Spring2011/Project1/jdn-explanation.html

    int julian_days;
    {
        bool adjust = st.month < 3;
        int year = st.year + 4800 - adjust;
        int month = st.month + 12 * adjust - 3;

        julian_days = st.day + (153 * month + 2) / 5 + 365 * year - 32045 +
                      year / 4 - year / 100 + year / 400;
    }

    return julian_days;
}

int Date::GetWeekDay() const
{
    RG_ASSERT(IsValid());

    // Zeller's congruence:
    // https://en.wikipedia.org/wiki/Zeller%27s_congruence

    int week_day;
    {
        int year = st.year;
        int month = st.month;
        if (month < 3) {
            year--;
            month += 12;
        }

        int century = year / 100;
        year %= 100;

        week_day = (st.day + (13 * (month + 1) / 5) + year + year / 4 + century / 4 + 5 * century + 5) % 7;
    }

    return week_day;
}

Date &Date::operator++()
{
    RG_ASSERT(IsValid());

    if (st.day < DaysInMonth(st.year, st.month)) {
        st.day++;
    } else if (st.month < 12) {
        st.month++;
        st.day = 1;
    } else {
        st.year++;
        st.month = 1;
        st.day = 1;
    }

    return *this;
}

Date &Date::operator--()
{
    RG_ASSERT(IsValid());

    if (st.day > 1) {
        st.day--;
    } else if (st.month > 1) {
        st.month--;
        st.day = DaysInMonth(st.year, st.month);
    } else {
        st.year--;
        st.month = 12;
        st.day = DaysInMonth(st.year, st.month);
    }

    return *this;
}

// ------------------------------------------------------------------------
// Time
// ------------------------------------------------------------------------

#ifdef _WIN32
static int64_t FileTimeToUnixTime(FILETIME ft)
{
    int64_t time = ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return time / 10000000 - 11644473600ll;
}
#endif

int64_t GetUnixTime()
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    return FileTimeToUnixTime(ft);
#elif defined(__EMSCRIPTEN__)
    return (int64_t)emscripten_get_now();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        LogError("clock_gettime() failed: %1", strerror(errno));
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

int64_t GetMonotonicTime()
{
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#elif defined(__EMSCRIPTEN__)
    return (int64_t)emscripten_get_now();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        LogError("clock_gettime() failed: %1", strerror(errno));
        return 0;
    }

    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

// ------------------------------------------------------------------------
// Strings
// ------------------------------------------------------------------------

Span<char> DuplicateString(Span<const char> str, Allocator *alloc)
{
    char *new_str = (char *)Allocator::Allocate(alloc, str.len + 1);
    memcpy(new_str, str.ptr, (size_t)str.len);
    new_str[str.len] = 0;
    return MakeSpan(new_str, str.len);
}

// ------------------------------------------------------------------------
// Format
// ------------------------------------------------------------------------

static const char DigitPairs[201] = "00010203040506070809101112131415161718192021222324"
                                    "25262728293031323334353637383940414243444546474849"
                                    "50515253545556575859606162636465666768697071727374"
                                    "75767778798081828384858687888990919293949596979899";

static Span<char> FormatUnsignedToDecimal(uint64_t value, char out_buf[32])
{
    Size offset = 32;
    {
        int pair_idx;
        do {
            pair_idx = (int)((value % 100) * 2);
            value /= 100;
            offset -= 2;
            memcpy(out_buf + offset, DigitPairs + pair_idx, 2);
        } while (value);
        offset += (pair_idx < 20);
    }

    return MakeSpan(out_buf + offset, 32 - offset);
}

static Span<char> FormatUnsignedToHex(uint64_t value, char out_buf[32])
{
    static const char literals[] = "0123456789ABCDEF";

    Size offset = 32;
    do {
        uint64_t digit = value & 0xF;
        value >>= 4;
        out_buf[--offset] = literals[digit];
    } while (value);

    return MakeSpan(out_buf + offset, 32 - offset);
}

static Span<char> FormatUnsignedToBinary(uint64_t value, char out_buf[64])
{
    Size msb = 64 - (Size)CountLeadingZeros(value);
    if (!msb) {
        msb = 1;
    }

    for (Size i = 0; i < msb; i++) {
        bool bit = (value >> (msb - i - 1)) & 0x1;
        out_buf[i] = bit ? '1' : '0';
    }

    return MakeSpan(out_buf, msb);
}

static Size FakeFloatPrecision(Span<char> buf, int K, int min_prec, int max_prec, int *out_K)
{
    if (-K < min_prec) {
        int delta = min_prec + K;
        memset(buf.end(), '0', delta);

        *out_K -= delta;
        return buf.len + delta;
    } else if (-K > max_prec) {
        int truncate = (int)buf.len + K + max_prec;

        if (buf[truncate] >= '5') {
            int i = truncate;
            while (i > (int)buf.len + K) {
                if (buf[i - 1] == '9') {
                    buf[--i] = '0';
                } else {
                    buf[i - 1]++;
                    break;
                }
            }

            truncate = std::max(i, (int)buf.len + K + min_prec);
        }

        *out_K -= (int)(truncate - buf.len);
        return truncate;
    } else {
        return buf.len;
    }
}

static Span<char> PrettifyFloat(Span<char> buf, int K, int min_prec, int max_prec)
{
    // Apply precision settings after conversion
    buf.len = FakeFloatPrecision(buf, K, min_prec, max_prec, &K);

    int KK = (int)buf.len + K;

    if (K >= 0) {
        // 1234e7 -> 12340000000

        memset(buf.end(), '0', (size_t)K);
        buf.len += K;
    } else if (KK > 0) {
        // 1234e-2 -> 12.34

        memmove(buf.ptr + KK + 1, buf.ptr + KK, (size_t)(buf.len - KK));
        buf.ptr[KK] = '.';
        buf.len++;
    } else {
        // 1234e-6 -> 0.001234

        int offset = 2 - KK;
        memmove(buf.ptr + offset, buf.ptr, (size_t)buf.len);
        memset(buf.ptr, '0', (size_t)offset);
        buf.ptr[1] = '.';
        buf.len += offset;
    }

    return buf;
}

static Span<char> ExponentiateFloat(Span<char> buf, int K, int min_prec, int max_prec)
{
    // Apply precision settings after conversion
    buf.len = FakeFloatPrecision(buf, (int)(1 - buf.len), min_prec, max_prec, &K);

    int exponent = (int)buf.len + K - 1;

    if (buf.len > 1) {
        memmove(buf.ptr + 2, buf.ptr + 1, (size_t)(buf.len - 1));
        buf.ptr[1] = '.';
        buf.ptr[buf.len + 1] = 'e';
        buf.len += 2;
    } else {
        buf.ptr[1] = 'e';
        buf.len = 2;
    }

    if (exponent > 0) {
        buf.ptr[buf.len++] = '+';
    } else {
        buf.ptr[buf.len++] = '-';
        exponent = -exponent;
    }

    if (exponent >= 100) {
        buf.ptr[buf.len++] = (char)('0' + exponent / 100);
        exponent %= 100;

        int pair_idx = (int)(exponent * 2);
        memcpy(buf.end(), DigitPairs + pair_idx, 2);
        buf.len += 2;
    } else if (exponent >= 10) {
        int pair_idx = (int)(exponent * 2);
        memcpy(buf.end(), DigitPairs + pair_idx, 2);
        buf.len += 2;
    } else {
        buf.ptr[buf.len++] = (char)('0' + exponent);
    }

    return buf;
}

// NaN and Inf are handled by caller
template <typename T>
Span<const char> FormatFloatingPoint(T value, int min_prec, int max_prec, char out_buf[32])
{
    auto v = jkj::grisu_exact(value);

    Span<char> buf = FormatUnsignedToDecimal(v.significand, out_buf);
    int KK = (int)buf.len + v.exponent;

    if (!v.significand) {
        buf.ptr[0] = '0';

        if (min_prec) {
            buf.ptr[1] = '.';
            memset(buf.ptr + 2, '0', min_prec);
            buf.len = 2 + min_prec;
        } else {
            buf.len = 1;
        }

        return buf;
    } else if (KK > -6 && KK <= 21) {
        return PrettifyFloat(buf, v.exponent, min_prec, max_prec);
    } else {
        return ExponentiateFloat(buf, v.exponent, min_prec, max_prec);
    }
}

template <typename AppendFunc>
static inline void ProcessArg(const FmtArg &arg, AppendFunc append)
{
    for (int i = 0; i < arg.repeat; i++) {
        LocalArray<char, 512> out_buf;
        char num_buf[128];
        Span<const char> out;

        Size pad_len = arg.pad_len;

        switch (arg.type) {
            case FmtType::Str1: { out = arg.u.str1; } break;
            case FmtType::Str2: { out = arg.u.str2; } break;
            case FmtType::Buffer: { out = arg.u.buf; } break;
            case FmtType::Char: { out = MakeSpan(&arg.u.ch, 1); } break;

            case FmtType::Bool: {
                if (arg.u.b) {
                    out = "true";
                } else {
                    out = "false";
                }
            } break;

            case FmtType::Integer: {
                if (arg.u.i < 0) {
                    if (arg.pad_len < 0 && arg.pad_char == '0') {
                        append('-');
                        pad_len++;
                    } else {
                        out_buf.Append('-');
                    }

                    out_buf.Append(FormatUnsignedToDecimal((uint64_t)-arg.u.i, num_buf));
                    out = out_buf;
                } else {
                    out = FormatUnsignedToDecimal((uint64_t)arg.u.i, num_buf);
                }
            } break;
            case FmtType::Unsigned: {
                out = FormatUnsignedToDecimal(arg.u.u, num_buf);
            } break;
            case FmtType::Float: {
                static const uint32_t ExponentMask = 0x7f800000u;
                static const uint32_t MantissaMask = 0x007fffffu;
                static const uint32_t SignMask = 0x80000000u;

                union { float f; uint32_t u32; } u;
                u.f = arg.u.f.value;

                if ((u.u32 & ExponentMask) == ExponentMask) {
                    uint32_t mantissa = u.u32 & MantissaMask;

                    if (mantissa) {
                        out = "NaN";
                    } else {
                        out = (u.u32 & SignMask) ? "-Inf" : "Inf";
                    }
                } else {
                    if (u.u32 & SignMask) {
                        if (arg.pad_len < 0 && arg.pad_char == '0') {
                            append('-');
                            pad_len++;
                        } else {
                            out_buf.Append('-');
                        }

                        out_buf.Append(FormatFloatingPoint(-u.f, arg.u.f.min_prec, arg.u.f.max_prec, num_buf));
                        out = out_buf;
                    } else {
                        out = FormatFloatingPoint(u.f, arg.u.f.min_prec, arg.u.f.max_prec, num_buf);
                    }
                }
            } break;
            case FmtType::Double: {
                static const uint64_t ExponentMask = 0x7FF0000000000000ull;
                static const uint64_t MantissaMask = 0x000FFFFFFFFFFFFFull;
                static const uint64_t SignMask = 0x8000000000000000ull;

                union { double d; uint64_t u64; } u;
                u.d = arg.u.d.value;

                if ((u.u64 & ExponentMask) == ExponentMask) {
                    uint64_t mantissa = u.u64 & MantissaMask;

                    if (mantissa) {
                        out = "NaN";
                    } else {
                        out = (u.u64 & SignMask) ? "-Inf" : "Inf";
                    }
                } else {
                    if (u.u64 & SignMask) {
                        if (arg.pad_len < 0 && arg.pad_char == '0') {
                            append('-');
                            pad_len++;
                        } else {
                            out_buf.Append('-');
                        }

                        out_buf.Append(FormatFloatingPoint(-u.d, arg.u.d.min_prec, arg.u.d.max_prec, num_buf));
                        out = out_buf;
                    } else {
                        out = FormatFloatingPoint(u.d, arg.u.d.min_prec, arg.u.d.max_prec, num_buf);
                    }
                }
            } break;
            case FmtType::Binary: {
                out = FormatUnsignedToBinary(arg.u.u, num_buf);
            } break;
            case FmtType::Hexadecimal: {
                out = FormatUnsignedToHex(arg.u.u, num_buf);
            } break;

            case FmtType::MemorySize: {
                size_t size_unsigned;
                if (arg.u.size < 0) {
                    size_unsigned = (size_t)-arg.u.size;
                    if (arg.pad_len < 0 && arg.pad_char == '0') {
                        append('-');
                        pad_len++;
                    } else {
                        out_buf.Append('-');
                    }
                } else {
                    size_unsigned = (size_t)arg.u.size;
                }
                if (size_unsigned > 1024 * 1024) {
                    double size_mib = (double)size_unsigned / (1024.0 * 1024.0);
                    out_buf.Append(FormatFloatingPoint(size_mib, 2, 2, num_buf));
                    out_buf.Append(" MiB");
                } else if (size_unsigned > 1024) {
                    double size_kib = (double)size_unsigned / 1024.0;
                    out_buf.Append(FormatFloatingPoint(size_kib, 2, 2, num_buf));
                    out_buf.Append(" kiB");
                } else {
                    out_buf.Append(FormatUnsignedToDecimal(size_unsigned, num_buf));
                    out_buf.Append(" B");
                }
                out = out_buf;
            } break;
            case FmtType::DiskSize: {
                size_t size_unsigned;
                if (arg.u.size < 0) {
                    size_unsigned = (size_t)-arg.u.size;
                    if (arg.pad_len < 0 && arg.pad_char == '0') {
                        append('-');
                        pad_len++;
                    } else {
                        out_buf.Append('-');
                    }
                } else {
                    size_unsigned = (size_t)arg.u.size;
                }
                if (size_unsigned > 1000 * 1000) {
                    double size_mib = (double)size_unsigned / (1000.0 * 1000.0);
                    out_buf.Append(FormatFloatingPoint(size_mib, 2, 2, num_buf));
                    out_buf.Append(" MB");
                } else if (size_unsigned > 1000) {
                    double size_kib = (double)size_unsigned / 1000.0;
                    out_buf.Append(FormatFloatingPoint(size_kib, 2, 2, num_buf));
                    out_buf.Append(" kB");
                } else {
                    out_buf.Append(FormatUnsignedToDecimal(size_unsigned, num_buf));
                    out_buf.Append(" B");
                }
                out = out_buf;
            } break;

            case FmtType::Date: {
                RG_ASSERT(!arg.u.date.value || arg.u.date.IsValid());

                int year = arg.u.date.st.year;
                if (year < 0) {
                    out_buf.Append('-');
                    year = -year;
                }
                if (year < 10) {
                    out_buf.Append("000");
                } else if (year < 100) {
                    out_buf.Append("00");
                } else if (year < 1000) {
                    out_buf.Append('0');
                }
                out_buf.Append(FormatUnsignedToDecimal((uint64_t)year, num_buf));
                out_buf.Append('-');
                if (arg.u.date.st.month < 10) {
                    out_buf.Append('0');
                }
                out_buf.Append(FormatUnsignedToDecimal((uint64_t)arg.u.date.st.month, num_buf));
                out_buf.Append('-');
                if (arg.u.date.st.day < 10) {
                    out_buf.Append('0');
                }
                out_buf.Append(FormatUnsignedToDecimal((uint64_t)arg.u.date.st.day, num_buf));
                out = out_buf;
            } break;

            case FmtType::Span: {
                FmtArg arg2;
                arg2.type = arg.u.span.type;
                arg2.repeat = arg.repeat;
                arg2.pad_len = arg.pad_len;
                arg2.pad_char = arg.pad_char;

                const uint8_t *ptr = (const uint8_t *)arg.u.span.ptr;
                for (Size j = 0; j < arg.u.span.len; j++) {
                    switch (arg.u.span.type) {
                        case FmtType::Str1: { arg2.u.str1 = *(const char **)ptr; } break;
                        case FmtType::Str2: { arg2.u.str2 = *(const Span<const char> *)ptr; } break;
                        case FmtType::Buffer: { RG_UNREACHABLE(); } break;
                        case FmtType::Char: { arg2.u.ch = *(const char *)ptr; } break;
                        case FmtType::Bool: { arg2.u.b = *(const bool *)ptr; } break;
                        case FmtType::Integer:
                        case FmtType::Unsigned:
                        case FmtType::Binary:
                        case FmtType::Hexadecimal: {
                            switch (arg.u.span.type_len) {
                                case 8: { arg2.u.u = *(const uint64_t *)ptr; } break;
                                case 4: { arg2.u.u = *(const uint32_t *)ptr; } break;
                                case 2: { arg2.u.u = *(const uint16_t *)ptr; } break;
                                case 1: { arg2.u.u = *(const uint8_t *)ptr; } break;
                                default: { RG_UNREACHABLE(); } break;
                            }
                        } break;
                        case FmtType::Float: {
                            arg2.u.f.value = *(const float *)ptr;
                            arg2.u.d.min_prec = 0;
                            arg2.u.d.max_prec = INT_MAX;
                        } break;
                        case FmtType::Double: {
                            arg2.u.d.value = *(const double *)ptr;
                            arg2.u.d.min_prec = 0;
                            arg2.u.d.max_prec = INT_MAX;
                        } break;
                        case FmtType::MemorySize: { arg2.u.size = *(const Size *)ptr; } break;
                        case FmtType::DiskSize: { arg2.u.size = *(const Size *)ptr; } break;
                        case FmtType::Date: { arg2.u.date = *(const Date *)ptr; } break;
                        case FmtType::Span: { RG_UNREACHABLE(); } break;
                    }
                    ptr += arg.u.span.type_len;

                    if (j) {
                        append(arg.u.span.separator);
                    }
                    ProcessArg(arg2, append);
                }

                out = {};
                pad_len = 0;
            } break;
        }

        if (pad_len < 0) {
            pad_len = (-pad_len) - out.len;
            for (Size i = 0; i < pad_len; i++) {
                append(arg.pad_char);
            }
            append(out);
        } else if (pad_len > 0) {
            append(out);
            pad_len -= out.len;
            for (Size i = 0; i < pad_len; i++) {
                append(arg.pad_char);
            }
        } else {
            append(out);
        }
    }
}

template <typename AppendFunc>
static inline Size ProcessAnsiSpecifier(const char *spec, bool vt100, AppendFunc append)
{
    Size idx = 0;

    char buf[32] = "\x1B[";
    bool valid = true;

    // Foreground color
    switch (spec[++idx]) {
        case 'd': { strcat(buf, "30"); } break;
        case 'r': { strcat(buf, "31"); } break;
        case 'g': { strcat(buf, "32"); } break;
        case 'y': { strcat(buf, "33"); } break;
        case 'b': { strcat(buf, "34"); } break;
        case 'm': { strcat(buf, "35"); } break;
        case 'c': { strcat(buf, "36"); } break;
        case 'w': { strcat(buf, "37"); } break;
        case 'D': { strcat(buf, "90"); } break;
        case 'R': { strcat(buf, "91"); } break;
        case 'G': { strcat(buf, "92"); } break;
        case 'Y': { strcat(buf, "93"); } break;
        case 'B': { strcat(buf, "94"); } break;
        case 'M': { strcat(buf, "95"); } break;
        case 'C': { strcat(buf, "96"); } break;
        case 'W': { strcat(buf, "97"); } break;
        case '.': { strcat(buf, "39"); } break;
        case '0': {
            strcat(buf, "0");
            goto end;
        } break;
        case 0: {
            valid = false;
            goto end;
        } break;
        default: { valid = false; } break;
    }

    // Background color
    switch (spec[++idx]) {
        case 'd': { strcat(buf, ";40"); } break;
        case 'r': { strcat(buf, ";41"); } break;
        case 'g': { strcat(buf, ";42"); } break;
        case 'y': { strcat(buf, ";43"); } break;
        case 'b': { strcat(buf, ";44"); } break;
        case 'm': { strcat(buf, ";45"); } break;
        case 'c': { strcat(buf, ";46"); } break;
        case 'w': { strcat(buf, ";47"); } break;
        case 'D': { strcat(buf, ";100"); } break;
        case 'R': { strcat(buf, ";101"); } break;
        case 'G': { strcat(buf, ";102"); } break;
        case 'Y': { strcat(buf, ";103"); } break;
        case 'B': { strcat(buf, ";104"); } break;
        case 'M': { strcat(buf, ";105"); } break;
        case 'C': { strcat(buf, ";106"); } break;
        case 'W': { strcat(buf, ";107"); } break;
        case '.': { strcat(buf, ";49"); } break;
        case 0: {
            valid = false;
            goto end;
        } break;
        default: { valid = false; } break;
    }

    // Bold/dim/underline/invert
    switch (spec[++idx]) {
        case '+': { strcat(buf, ";1"); } break;
        case '-': { strcat(buf, ";2"); } break;
        case '_': { strcat(buf, ";4"); } break;
        case '^': { strcat(buf, ";7"); } break;
        case '.': {} break;
        case 0: {
            valid = false;
            goto end;
        } break;
        default: { valid = false; } break;
    }

end:
    if (!valid) {
#ifndef NDEBUG
        LogDebug("Format string contains invalid ANSI specifier");
#endif
        return idx;
    }

    if (vt100) {
        strcat(buf, "m");
        append(buf);
    }

    return idx;
}

template <typename AppendFunc>
static inline void DoFormat(const char *fmt, Span<const FmtArg> args, bool vt100, AppendFunc append)
{
#ifndef NDEBUG
    bool invalid_marker = false;
    uint32_t unused_arguments = ((uint32_t)1 << args.len) - 1;
#endif

    const char *fmt_ptr = fmt;
    for (;;) {
        // Find the next marker (or the end of string) and write everything before it
        const char *marker_ptr = fmt_ptr;
        while (marker_ptr[0] && marker_ptr[0] != '%') {
            marker_ptr++;
        }
        append(MakeSpan(fmt_ptr, (Size)(marker_ptr - fmt_ptr)));
        if (!marker_ptr[0])
            break;

        // Try to interpret this marker as a number
        Size idx = 0;
        Size idx_end = 1;
        for (;;) {
            // Unsigned cast makes the test below quicker, don't remove it or it'll break
            unsigned int digit = (unsigned int)marker_ptr[idx_end] - '0';
            if (digit > 9)
                break;
            idx = (Size)(idx * 10) + (Size)digit;
            idx_end++;
        }

        // That was indeed a number
        if (idx_end > 1) {
            idx--;
            if (idx < args.len) {
                ProcessArg<AppendFunc>(args[idx], append);
#ifndef NDEBUG
                unused_arguments &= ~((uint32_t)1 << idx);
            } else {
                invalid_marker = true;
#endif
            }
            fmt_ptr = marker_ptr + idx_end;
        } else if (marker_ptr[1] == '%') {
            append('%');
            fmt_ptr = marker_ptr + 2;
        } else if (marker_ptr[1] == '/') {
            append(*RG_PATH_SEPARATORS);
            fmt_ptr = marker_ptr + 2;
        } else if (marker_ptr[1] == '!') {
            fmt_ptr = marker_ptr + 2 + ProcessAnsiSpecifier(marker_ptr + 1, vt100, append);
        } else if (marker_ptr[1]) {
            append(marker_ptr[0]);
            fmt_ptr = marker_ptr + 1;
#ifndef NDEBUG
            invalid_marker = true;
#endif
        } else {
#ifndef NDEBUG
            invalid_marker = true;
#endif
            break;
        }
    }

#ifndef NDEBUG
    if (invalid_marker && unused_arguments) {
        fprintf(stderr, "\nLog format string '%s' has invalid markers and unused arguments\n", fmt);
    } else if (unused_arguments) {
        fprintf(stderr, "\nLog format string '%s' has unused arguments\n", fmt);
    } else if (invalid_marker) {
        fprintf(stderr, "\nLog format string '%s' has invalid markers\n", fmt);
    }
#endif
}

static inline bool FormatBufferWithVt100()
{
    // In most cases, when fmt contains color tags, this is because the caller is
    // trying to make a string to show to the user. In this case, we want to generate
    // VT-100 escape sequences if the standard output is a terminal, because the
    // string will probably end up there.

    static bool use_vt100 = FileIsVt100(stdout) && FileIsVt100(stderr);
    return use_vt100;
}

Span<char> FmtFmt(const char *fmt, Span<const FmtArg> args, Span<char> out_buf)
{
    RG_ASSERT(out_buf.len >= 0);

    if (!out_buf.len)
        return {};
    out_buf.len--;

    Size available_len = out_buf.len;

    DoFormat(fmt, args, FormatBufferWithVt100(), [&](Span<const char> frag) {
        Size copy_len = std::min(frag.len, available_len);

        memcpy(out_buf.end() - available_len, frag.ptr, (size_t)copy_len);
        available_len -= copy_len;
    });

    out_buf.len -= available_len;
    out_buf.ptr[out_buf.len] = 0;

    return out_buf;
}

Span<char> FmtFmt(const char *fmt, Span<const FmtArg> args, HeapArray<char> *out_buf)
{
    Size start_len = out_buf->len;

    out_buf->Grow(RG_FMT_STRING_BASE_CAPACITY);
    DoFormat(fmt, args, FormatBufferWithVt100(), [&](Span<const char> frag) {
        out_buf->Grow(frag.len + 1);
        memcpy(out_buf->end(), frag.ptr, (size_t)frag.len);
        out_buf->len += frag.len;
    });
    out_buf->ptr[out_buf->len] = 0;

    return out_buf->Take(start_len, out_buf->len - start_len);
}

Span<char> FmtFmt(const char *fmt, Span<const FmtArg> args, Allocator *alloc)
{
    HeapArray<char> buf(alloc);
    FmtFmt(fmt, args, &buf);
    return buf.TrimAndLeak(1);
}

void PrintFmt(const char *fmt, Span<const FmtArg> args, StreamWriter *st)
{
    LocalArray<char, RG_FMT_STRING_PRINT_BUFFER_SIZE> buf;
    DoFormat(fmt, args, st->IsVt100(), [&](Span<const char> frag) {
        if (frag.len > RG_LEN(buf.data) - buf.len) {
            st->Write(buf);
            buf.len = 0;
        }
        if (frag.len >= RG_LEN(buf.data)) {
            st->Write(frag);
        } else {
            memcpy(buf.data + buf.len, frag.ptr, (size_t)frag.len);
            buf.len += frag.len;
        }
    });
    st->Write(buf);
}

static void WriteStdComplete(Span<const char> buf, FILE *fp)
{
    while (buf.len) {
        Size write_len = (Size)fwrite(buf.ptr, 1, (size_t)buf.len, fp);
        if (RG_UNLIKELY(!write_len))
            break;
        buf = buf.Take(write_len, buf.len - write_len);
    }
}

void PrintFmt(const char *fmt, Span<const FmtArg> args, FILE *fp)
{
    LocalArray<char, RG_FMT_STRING_PRINT_BUFFER_SIZE> buf;
    DoFormat(fmt, args, FileIsVt100(fp), [&](Span<const char> frag) {
        if (frag.len > RG_LEN(buf.data) - buf.len) {
            WriteStdComplete(buf, fp);
            buf.len = 0;
        }
        if (frag.len >= RG_LEN(buf.data)) {
            WriteStdComplete(frag, fp);
        } else {
            memcpy(buf.data + buf.len, frag.ptr, (size_t)frag.len);
            buf.len += frag.len;
        }
    });
    WriteStdComplete(buf, fp);
}

void PrintLnFmt(const char *fmt, Span<const FmtArg> args, StreamWriter *st)
{
    PrintFmt(fmt, args, st);
    st->Write('\n');
}
void PrintLnFmt(const char *fmt, Span<const FmtArg> args, FILE *fp)
{
    PrintFmt(fmt, args, fp);
    fputc('\n', fp);
}

// ------------------------------------------------------------------------
// Debug and errors
// ------------------------------------------------------------------------

static int64_t start_time = GetMonotonicTime();

static std::function<LogFunc> log_handler = DefaultLogHandler;

// NOTE: LocalArray does not work with __thread, and thread_local is broken on MinGW
// when destructors are involved. So heap allocation it is, at least for now.
static RG_THREAD_LOCAL std::function<LogFilterFunc> *log_filters[16];
static RG_THREAD_LOCAL Size log_filters_len;

bool GetDebugFlag(const char *name)
{
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        try {
            var debug_env_name = UTF8ToString($0);
            return (process.env[debug_env_name] !== undefined && process.env[debug_env_name] != 0);
        } catch (error) {
            return 0;
        }
    }, name);
#else
    const char *debug = getenv(name);

    if (!debug || TestStr(debug, "0")) {
        return false;
    } else if (TestStr(debug, "1")) {
        return true;
    } else {
        LogError("%1 should contain value '0' or '1'", name);
        return true;
    }
#endif
}

static void RunLogFilter(Size idx, LogLevel level, const char *ctx, const char *msg)
{
    const std::function<LogFilterFunc> &func = *log_filters[idx];

    func(level, ctx, msg, [&](LogLevel level, const char *ctx, const char *msg) {
        if (idx > 0) {
            RunLogFilter(idx - 1, level, ctx, msg);
        } else {
            log_handler(level, ctx, msg);
        }
    });
}

void LogFmt(LogLevel level, const char *ctx, const char *fmt, Span<const FmtArg> args)
{
    static bool init = false;
    static bool log_times;

    if (!init) {
        // Do this first... GetDebugFlag() might log an error or something, in which
        // case we don't want to recurse forever and crash!
        init = true;

        log_times = GetDebugFlag("LOG_TIMES");
    }

    char ctx_buf[512];
    if (log_times) {
        double time = (double)(GetMonotonicTime() - start_time) / 1000;
        Fmt(ctx_buf, "[%1] %2", FmtDouble(time, 3).Pad(-8), ctx ? ctx : "");

        ctx = ctx_buf;
    }

    char msg_buf[2048];
    {
        Size len = FmtFmt(fmt, args, msg_buf).len;
        if (len == RG_SIZE(msg_buf) - 1) {
            strcpy(msg_buf + RG_SIZE(msg_buf) - 32, "... [truncated]");
        }
    }

    if (log_filters_len) {
        RunLogFilter(log_filters_len - 1, level, ctx, msg_buf);
    } else {
        log_handler(level, ctx, msg_buf);
    }
}

void SetLogHandler(const std::function<LogFunc> &func)
{
    log_handler = func;
}

void DefaultLogHandler(LogLevel level, const char *ctx, const char *msg)
{
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    switch (level)  {
        case LogLevel::Debug:
        case LogLevel::Info: { PrintLn(stderr, "%!D..%1%2%!0%3", ctx ? ctx : "", ctx ? ": " : "", msg); } break;
        case LogLevel::Error: { PrintLn(stderr, "%!R..%1%2%!0%3", ctx ? ctx : "", ctx ? ": " : "", msg); } break;
    }

    fflush(stderr);
}

void PushLogFilter(const std::function<LogFilterFunc> &func)
{
    RG_ASSERT(log_filters_len < RG_LEN(log_filters));
    log_filters[log_filters_len++] = new std::function<LogFilterFunc>(func);
}

void PopLogFilter()
{
    RG_ASSERT(log_filters_len > 0);
    delete log_filters[--log_filters_len];
}

// ------------------------------------------------------------------------
// System
// ------------------------------------------------------------------------

#ifdef _WIN32

Size ConvertUtf8ToWin32Wide(Span<const char> str, Span<wchar_t> out_str_w)
{
    RG_ASSERT(out_str_w.len >= 2);

    int len = MultiByteToWideChar(CP_UTF8, 0, str.ptr, (int)str.len, out_str_w.ptr, (int)out_str_w.len - 1);
    if (!len) {
        switch (GetLastError()) {
            case ERROR_INSUFFICIENT_BUFFER: { LogError("String '%1' is too large", str); } break;
            case ERROR_NO_UNICODE_TRANSLATION: { LogError("String '%1' is not valid UTF-8", str); } break;
            default: { LogError("MultiByteToWideChar() failed: %1", GetWin32ErrorString()); } break;
        }
        return -1;
    }

    // MultiByteToWideChar() does not NUL terminate when passed in explicit string length
    out_str_w.ptr[len] = 0;

    return (Size)len;
}

static Size ConvertWin32WideToUtf8(LPCWSTR str_w, Span<char> out_str)
{
    RG_ASSERT(out_str.len >= 1);

    int len = WideCharToMultiByte(CP_UTF8, 0, str_w, -1, out_str.ptr, (int)out_str.len, nullptr, nullptr);
    if (!len) {
        // This function is mainly used for strings returned by Win32, errors should
        // be rare so there is no need for fancy messages.
        LogError("WideCharToMultiByte() failed: %1", GetWin32ErrorString());
        return -1;
    }

    return (Size)len;
}

char *GetWin32ErrorString(uint32_t error_code)
{
    static RG_THREAD_LOCAL char str_buf[512];

    if (error_code == UINT32_MAX) {
        error_code = GetLastError();
    }

    wchar_t buf_w[256];
    if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        buf_w, RG_SIZE(buf_w), nullptr))
        goto fail;

    if (!WideCharToMultiByte(CP_UTF8, 0, buf_w, -1, str_buf, RG_SIZE(str_buf), nullptr, nullptr))
        goto fail;

    // Truncate newlines
    {
        char *str_end = str_buf + strlen(str_buf);
        while (str_end > str_buf && (str_end[-1] == '\n' || str_end[-1] == '\r'))
            str_end--;
        *str_end = 0;
    }

    return str_buf;

fail:
    sprintf(str_buf, "Win32 error 0x%x", error_code);
    return str_buf;
}

static FileType FileAttributesToType(uint32_t attr)
{
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        return FileType::Directory;
    } else if (attr & FILE_ATTRIBUTE_DEVICE) {
        return FileType::Unknown;
    } else {
        return FileType::File;
    }
}

bool StatFile(const char *filename, bool error_if_missing, FileInfo *out_info)
{
    wchar_t filename_w[4096];
    if (ConvertUtf8ToWin32Wide(filename, filename_w) < 0)
        return false;

    HANDLE h = CreateFileW(filename_w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (error_if_missing ||
                (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)) {
            LogError("Cannot stat file '%1': %2", filename, GetWin32ErrorString(err));
        }
        return false;
    }
    RG_DEFER { CloseHandle(h); };

    BY_HANDLE_FILE_INFORMATION attr;
    if (!GetFileInformationByHandle(h, &attr)) {
        LogError("Cannot stat file '%1': %2", filename, GetWin32ErrorString());
        return false;
    }

    out_info->type = FileAttributesToType(attr.dwFileAttributes);
    out_info->size = ((uint64_t)attr.nFileSizeHigh << 32) | attr.nFileSizeLow;
    out_info->modification_time = FileTimeToUnixTime(attr.ftLastWriteTime);

    return true;
}

bool RenameFile(const char *src_filename, const char *dest_filename, bool)
{
    wchar_t src_filename_w[4096];
    wchar_t dest_filename_w[4096];
    if (ConvertUtf8ToWin32Wide(src_filename, src_filename_w) < 0)
        return false;
    if (ConvertUtf8ToWin32Wide(dest_filename, dest_filename_w) < 0)
        return false;

    if (!MoveFileExW(src_filename_w, dest_filename_w, MOVEFILE_REPLACE_EXISTING)) {
        LogError("Failed to rename file '%1' to '%2': %3", src_filename, dest_filename, GetWin32ErrorString());
        return false;
    }

    return true;
}

EnumStatus EnumerateDirectory(const char *dirname, const char *filter, Size max_files,
                              FunctionRef<bool(const char *, FileType)> func)
{
    if (filter) {
        RG_ASSERT(!strpbrk(filter, RG_PATH_SEPARATORS));
    } else {
        filter = "*";
    }

    wchar_t find_filter_w[4096];
    {
        char find_filter[4096];
        if (snprintf(find_filter, RG_SIZE(find_filter), "%s\\%s", dirname, filter) >= RG_SIZE(find_filter)) {
            LogError("Cannot enumerate directory '%1': Path too long", dirname);
            return EnumStatus::Error;
        }

        if (ConvertUtf8ToWin32Wide(find_filter, find_filter_w) < 0)
            return EnumStatus::Error;
    }

    WIN32_FIND_DATAW find_data;
    HANDLE handle = FindFirstFileExW(find_filter_w, FindExInfoBasic, &find_data,
                                     FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            // Erase the filter part from the buffer, we are about to exit anyway.
            // And no, I don't want to include wchar.h
            Size len = 0;
            while (find_filter_w[len++]);
            while (len > 0 && find_filter_w[--len] != L'\\');
            find_filter_w[len] = 0;

            DWORD attrib = GetFileAttributesW(find_filter_w);
            if (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY))
                return EnumStatus::Done;
        }

        LogError("Cannot enumerate directory '%1': %2", dirname,
                 GetWin32ErrorString());
        return EnumStatus::Error;
    }
    RG_DEFER { FindClose(handle); };

    Size count = 0;
    do {
        if ((find_data.cFileName[0] == '.' && !find_data.cFileName[1]) ||
                (find_data.cFileName[0] == '.' && find_data.cFileName[1] == '.' && !find_data.cFileName[2]))
            continue;

        if (RG_UNLIKELY(count++ >= max_files && max_files >= 0)) {
            LogError("Partial enumation of directory '%1'", dirname);
            return EnumStatus::Partial;
        }

        char filename[512];
        if (ConvertWin32WideToUtf8(find_data.cFileName, filename) < 0)
            return EnumStatus::Error;

        FileType file_type = FileAttributesToType(find_data.dwFileAttributes);

        if (!func(filename, file_type))
            return EnumStatus::Partial;
    } while (FindNextFileW(handle, &find_data));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        LogError("Error while enumerating directory '%1': %2", dirname,
                 GetWin32ErrorString());
        return EnumStatus::Error;
    }

    return EnumStatus::Done;
}

#else

bool StatFile(const char *filename, bool error_if_missing, FileInfo *out_info)
{
    struct stat sb;
    if (stat(filename, &sb) < 0) {
        if (error_if_missing || errno != ENOENT) {
            LogError("Cannot stat '%1': %2", filename, strerror(errno));
        }
        return false;
    }

    if (S_ISDIR(sb.st_mode)) {
        out_info->type = FileType::Directory;
    } else if (S_ISREG(sb.st_mode)) {
        out_info->type = FileType::File;
    } else {
        out_info->type = FileType::Unknown;
    }

    out_info->size = (int64_t)sb.st_size;
#if defined(__linux__)
    out_info->modification_time = (int64_t)sb.st_mtim.tv_sec * 1000 +
                                  (int64_t)sb.st_mtim.tv_nsec / 1000000;
#elif defined(__APPLE__)
    out_info->modification_time = (int64_t)sb.st_mtimespec.tv_sec * 1000 +
                                  (int64_t)sb.st_mtimespec.tv_nsec / 1000000;
#else
    out_info->modification_time = (int64_t)sb.st_mtime * 1000;
#endif

    return true;
}

static bool SyncFileDirectory(const char *filename)
{
    Span<const char> directory = GetPathDirectory(filename);

    char directory0[4096];
    if (directory.len >= RG_SIZE(directory0)) {
        LogError("Failed to sync directory '%1': path too long", directory);
        return false;
    }
    memcpy(directory0, directory.ptr, directory.len);
    directory0[directory.len] = 0;

    int dirfd = open(directory0, O_RDONLY | O_CLOEXEC);
    if (dirfd < 0) {
        LogError("Failed to sync directory '%1': %2", directory, strerror(errno));
        return false;
    }
    RG_DEFER { close(dirfd); };

    if (fsync(dirfd) < 0) {
        LogError("Failed to sync directory '%1': %2", directory, strerror(errno));
        return false;
    }

    return true;
}

bool RenameFile(const char *src_filename, const char *dest_filename, bool sync)
{
    // Rename the file
    if (rename(src_filename, dest_filename) < 0) {
        LogError("Failed to rename '%1' to '%2': %3", src_filename, dest_filename, strerror(errno));
        return false;
    }

    // Not much we can do if fsync fails (I think), so ignore errors.
    // Hope for the best: that's the spirit behind the POSIX filesystem API (...).
    if (sync) {
        SyncFileDirectory(src_filename);
        SyncFileDirectory(dest_filename);
    }

    return true;
}

EnumStatus EnumerateDirectory(const char *dirname, const char *filter, Size max_files,
                              FunctionRef<bool(const char *, FileType)> func)
{
    DIR *dirp = opendir(dirname);
    if (!dirp) {
        LogError("Cannot enumerate directory '%1': %2", dirname, strerror(errno));
        return EnumStatus::Error;
    }
    RG_DEFER { closedir(dirp); };

    // Avoid random failure in empty directories
    errno = 0;

    Size count = 0;
    dirent *dent;
    while ((dent = readdir(dirp))) {
        if ((dent->d_name[0] == '.' && !dent->d_name[1]) ||
                (dent->d_name[0] == '.' && dent->d_name[1] == '.' && !dent->d_name[2]))
            continue;

        if (!filter || !fnmatch(filter, dent->d_name, FNM_PERIOD)) {
            if (RG_UNLIKELY(count++ >= max_files && max_files >= 0)) {
                LogError("Partial enumation of directory '%1'", dirname);
                return EnumStatus::Partial;
            }

            FileType file_type;
#ifdef _DIRENT_HAVE_D_TYPE
            if (dent->d_type != DT_UNKNOWN && dent->d_type != DT_LNK) {
                switch (dent->d_type) {
                    case DT_DIR: { file_type = FileType::Directory; } break;
                    case DT_REG: { file_type = FileType::File; } break;
                    default: { file_type = FileType::Unknown; } break;
                }
            } else
#endif
            {
                struct stat sb;
                if (fstatat(dirfd(dirp), dent->d_name, &sb, 0) < 0) {
                    LogError("Ignoring file '%1' in '%2' (stat failed)",
                             dent->d_name, dirname);
                    continue;
                }
                if (S_ISDIR(sb.st_mode)) {
                    file_type = FileType::Directory;
                } else if (S_ISREG(sb.st_mode)) {
                    file_type = FileType::File;
                } else {
                    file_type = FileType::Unknown;
                }
            }

            if (!func(dent->d_name, file_type))
                return EnumStatus::Partial;
        }

        errno = 0;
    }

    if (errno) {
        LogError("Error while enumerating directory '%1': %2", dirname, strerror(errno));
        return EnumStatus::Error;
    }

    return EnumStatus::Done;
}

#endif

bool EnumerateFiles(const char *dirname, const char *filter, Size max_depth, Size max_files,
                    Allocator *str_alloc, HeapArray<const char *> *out_files)
{
    RG_DEFER_NC(out_guard, len = out_files->len) { out_files->RemoveFrom(len); };

    EnumStatus status = EnumerateDirectory(dirname, filter, max_files,
                                           [&](const char *filename, FileType file_type) {
        switch (file_type) {
            case FileType::Directory: {
                if (max_depth) {
                    const char *sub_directory = Fmt(str_alloc, "%1%/%2", dirname, filename).ptr;
                    return EnumerateFiles(sub_directory, filter, std::max((Size)-1, max_depth - 1),
                                          max_files, str_alloc, out_files);
                }
            } break;
            case FileType::File: {
                out_files->Append(Fmt(str_alloc, "%1%/%2", dirname, filename).ptr);
            } break;

            case FileType::Unknown: {} break;
        }

        return true;
    });
    if (status == EnumStatus::Error)
        return false;

    out_guard.Disable();
    return true;
}

bool IsDirectoryEmpty(const char *dirname)
{
    EnumStatus status = EnumerateDirectory(dirname, nullptr, -1, [](const char *, FileType) { return false; });
    return status == EnumStatus::Done;
}

bool TestFile(const char *filename, FileType type)
{
    FileInfo file_info;
    if (!StatFile(filename, false, &file_info))
        return false;

    if (type != FileType::Unknown && type != file_info.type) {
        switch (type) {
            case FileType::Directory: { LogError("Path '%1' is not a directory", filename); } break;
            case FileType::File: { LogError("Path '%1' is not a file", filename); } break;
            case FileType::Unknown: { RG_UNREACHABLE(); } break;
        }

        return false;
    }

    return true;
}

static Size MatchPathItem(const char *path, const char *spec)
{
    Size i = 0;

    while (spec[i] && spec[i] != '*') {
        switch (spec[i]) {
            case '?': {
                if (!path[i] || IsPathSeparator(path[i]))
                    return -1;
            } break;

#ifdef _WIN32
            case '\\':
            case '/': {
                if (!IsPathSeparator(path[i]))
                    return -1;
            } break;

            default: {
                // XXX: Use proper Unicode/locale case-folding? Or is this enough?
                if (LowerAscii(path[i]) != LowerAscii(spec[i]))
                    return -1;
            } break;
#else
            default: {
                if (path[i] != spec[i])
                    return -1;
            } break;
#endif
        }

        i++;
    }

    return i;
}

bool MatchPathName(const char *path, const char *spec)
{
    // Match head
    {
        Size match_len = MatchPathItem(path, spec);

        if (match_len < 0) {
            return false;
        } else {
            // Fast path (no wildcard)
            if (!spec[match_len])
                return !path[match_len];

            path += match_len;
            spec += match_len;
        }
    }

    // Find tail
    const char *tail = strrchr(spec, '*') + 1;

    // Match remaining items
    while (spec[0] == '*') {
        bool superstar = (spec[1] == '*');
        while (spec[0] == '*') {
            spec++;
        }

        for (;;) {
            Size match_len = MatchPathItem(path, spec);

            // We need to be greedy for the last wildcard, or we may not reach the tail
            if (match_len < 0 || (spec == tail && path[match_len])) {
                if (!path[0])
                    return false;
                if (!superstar && IsPathSeparator(path[0]))
                    return false;
                path++;
            } else {
                path += match_len;
                spec += match_len;

                break;
            }
        }
    }

    return true;
}

bool MatchPathSpec(const char *path, const char *spec)
{
    Span<const char> path2 = path;

    do {
        const char *it = SplitStrReverseAny(path2, RG_PATH_SEPARATORS, &path2).ptr;

        if (MatchPathName(it, spec))
            return true;
    } while (path2.len);

    return false;
}

bool FindExecutableInPath(const char *name, Allocator *alloc, const char **out_path)
{
    // XXX: Non-Unicode friendly on Win32
    Span<const char> env = getenv("PATH");

    while (env.len) {
        Span<const char> path = SplitStr(env, RG_PATH_DELIMITER, &env);

        LocalArray<char, 4096> buf;
        buf.len = Fmt(buf.data, "%1%/%2", path, name).len;

#ifdef _WIN32
        static const Span<const char> extensions[] = {".com", ".exe", ".bat", ".cmd"};

        for (Span<const char> ext: extensions) {
            if (RG_LIKELY(ext.len < buf.Available() - 1)) {
                memcpy(buf.end(), ext.ptr, ext.len + 1);

                if (TestFile(buf.data)) {
                    if (out_path) {
                        *out_path = DuplicateString(buf.data, alloc).ptr;
                    }
                    return true;
                }
            }
        }
#else
        if (RG_LIKELY(buf.len < RG_SIZE(buf.data) - 1) && TestFile(buf.data)) {
            if (out_path) {
                *out_path = DuplicateString(buf.data, alloc).ptr;
            }
            return true;
        }
#endif
    }

    return false;
}

bool SetWorkingDirectory(const char *directory)
{
#ifdef _WIN32
    wchar_t directory_w[4096];
    if (ConvertUtf8ToWin32Wide(directory, directory_w) < 0)
        return false;

    if (!SetCurrentDirectoryW(directory_w)) {
        LogError("Failed to set current directory to '%1': %2", directory, GetWin32ErrorString());
        return false;
    }
#else
    if (chdir(directory) < 0) {
        LogError("Failed to set current directory to '%1': %2", directory, strerror(errno));
        return false;
    }
#endif

    return true;
}

const char *GetWorkingDirectory()
{
    static RG_THREAD_LOCAL char buf[4096];

#ifdef _WIN32
    wchar_t buf_w[RG_SIZE(buf)];
    DWORD ret = GetCurrentDirectoryW(RG_SIZE(buf_w), buf_w);
    RG_ASSERT(ret && ret <= RG_SIZE(buf_w));

    Size str_len = ConvertWin32WideToUtf8(buf_w, buf);
    RG_ASSERT(str_len >= 0);
#else
    const char *ptr = getcwd(buf, RG_SIZE(buf));
    RG_ASSERT(ptr);
#endif

    return buf;
}

#ifdef __EMSCRIPTEN__
static bool running_in_node;

RG_INIT(MountHostFilesystem)
{
    running_in_node = EM_ASM_INT({
        try {
            var path = require('path');
            if (process.platform == 'win32') {
                FS.mkdir('/host');
                for (var c = 'a'.charCodeAt(0); c <= 'z'.charCodeAt(0); c++) {
                    var disk_path = String.fromCharCode(c) + ':';
                    var mount_point = '/host/' + String.fromCharCode(c);
                    FS.mkdir(mount_point);
                    try {
                        FS.mount(NODEFS, { root: disk_path }, mount_point);
                    } catch(error) {
                        FS.rmdir(mount_point);
                    }
                }

                var real_app_dir = path.dirname(process.mainModule.filename);
                var app_dir = '/host/' + real_app_dir[0].toLowerCase() +
                              real_app_dir.substr(2).replace(/\\\\\\\\/g, '/');
            } else {
                FS.mkdir('/host');
                FS.mount(NODEFS, { root: '/' }, '/host');

                var app_dir = '/host' + path.dirname(process.mainModule.filename);
            }
        } catch (error) {
            // Running in browser (maybe)
            return 0;
        }

        FS.mkdir('/work');
        FS.mount(NODEFS, { root: '.' }, '/work');
        FS.symlink(app_dir, '/app');

        return 1;
    });

    if (running_in_node) {
        chdir("/work");
    }
}
#endif

const char *GetApplicationExecutable()
{
#if defined(_WIN32)
    static char executable_path[4096];

    if (!executable_path[0]) {
        wchar_t path_w[RG_SIZE(executable_path)];
        Size path_len = (Size)GetModuleFileNameW(nullptr, path_w, RG_SIZE(path_w));
        RG_ASSERT(path_len && path_len < RG_SIZE(path_w));

        Size str_len = ConvertWin32WideToUtf8(path_w, executable_path);
        RG_ASSERT(str_len >= 0);
    }

    return executable_path;
#elif defined(__APPLE__)
    static char executable_path[4096];

    if (!executable_path[0]) {
        uint32_t buffer_size = RG_SIZE(executable_path);
        int ret = _NSGetExecutablePath(executable_path, &buffer_size);
        RG_ASSERT(!ret);

        char *path_buf = realpath(executable_path, nullptr);
        RG_ASSERT(path_buf);
        RG_ASSERT(strlen(path_buf) < RG_SIZE(executable_path));

        strncpy(executable_path, path_buf, RG_SIZE(executable_path) - 1);
        free(path_buf);
    }

    return executable_path;
#elif defined(__linux__)
    static char executable_path[4096];

    if (!executable_path[0]) {
        char *path_buf = realpath("/proc/self/exe", nullptr);
        RG_ASSERT(path_buf);
        RG_ASSERT(strlen(path_buf) < RG_SIZE(executable_path));

        strncpy(executable_path, path_buf, RG_SIZE(executable_path) - 1);
        free(path_buf);
    }

    return executable_path;
#elif defined(__EMSCRIPTEN__)
    return nullptr;
#else
    #error GetApplicationExecutable() not implemented for this platform
#endif
}

const char *GetApplicationDirectory()
{
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    static char executable_dir[4096];

    if (!executable_dir[0]) {
        const char *executable_path = GetApplicationExecutable();
        Size dir_len = (Size)strlen(executable_path);
        while (dir_len && !IsPathSeparator(executable_path[--dir_len]));
        memcpy(executable_dir, executable_path, (size_t)dir_len);
        executable_dir[dir_len] = 0;
    }

    return executable_dir;
#elif defined(__EMSCRIPTEN__)
    return running_in_node ? "/app" : nullptr;
#else
    #error GetApplicationDirectory() not implemented for this platform
#endif
}

Span<const char> GetPathDirectory(Span<const char> filename)
{
    Span<const char> directory;
    SplitStrReverseAny(filename, RG_PATH_SEPARATORS, &directory);

    return directory.len ? directory : ".";
}

// Names starting with a dot are not considered to be an extension (POSIX hidden files)
Span<const char> GetPathExtension(Span<const char> filename, CompressionType *out_compression_type)
{
    filename = SplitStrReverseAny(filename, RG_PATH_SEPARATORS);

    Span<const char> extension = {};
    const auto consume_next_extension = [&]() {
        extension = SplitStrReverse(filename, '.', &filename);
        if (extension.ptr > filename.ptr) {
            extension.ptr--;
            extension.len++;
        } else {
            extension = {};
        }
    };

    consume_next_extension();
    if (out_compression_type) {
        if (TestStr(extension, ".gz")) {
            *out_compression_type = CompressionType::Gzip;
            consume_next_extension();
        } else {
            *out_compression_type = CompressionType::None;
        }
    }

    return extension;
}

CompressionType GetPathCompression(Span<const char> filename)
{
    CompressionType compression_type;
    GetPathExtension(filename, &compression_type);
    return compression_type;
}

Span<char> NormalizePath(Span<const char> path, Span<const char> root_directory, Allocator *alloc)
{
    if (!path.len && !root_directory.len)
        return Fmt(alloc, "");

    HeapArray<char> buf(alloc);
    Size parts_count = 0;

    const auto append_normalized_path = [&](Span<const char> path) {
        if (!buf.len && PathIsAbsolute(path)) {
            Span<const char> prefix = SplitStrAny(path, RG_PATH_SEPARATORS, &path);
            buf.Append(prefix);
            buf.Append(*RG_PATH_SEPARATORS);
        }

        while (path.len) {
            Span<const char> part = SplitStrAny(path, RG_PATH_SEPARATORS, &path);

            if (part == "..") {
                if (parts_count) {
                    while (--buf.len && !IsPathSeparator(buf.ptr[buf.len - 1]));
                    parts_count--;
                } else {
                    buf.Append("..");
                    buf.Append(*RG_PATH_SEPARATORS);
                }
            } else if (part == ".") {
                // Skip
            } else if (part.len) {
                buf.Append(part);
                buf.Append(*RG_PATH_SEPARATORS);
                parts_count++;
            }
        }
    };

    if (root_directory.len && !PathIsAbsolute(path)) {
        append_normalized_path(root_directory);
    }
    append_normalized_path(path);

    if (!buf.len) {
        buf.Append('.');
    } else if (buf.len == 1 && IsPathSeparator(buf[0])) {
        // Root '/', keep as-is
    } else {
        // Strip last separator
        buf.len--;
    }

    // NUL terminator
    buf.Trim(1);
    buf.ptr[buf.len] = 0;

    return buf.Leak();
}

bool PathIsAbsolute(const char *path)
{
#ifdef _WIN32
    if (IsAsciiAlpha(path[0]) && path[1] == ':')
        return true;
#endif

    return IsPathSeparator(path[0]);
}
bool PathIsAbsolute(Span<const char> path)
{
#ifdef _WIN32
    if (path.len >= 2 && IsAsciiAlpha(path[0]) && path[1] == ':')
        return true;
#endif

    return path.len && IsPathSeparator(path[0]);
}

bool PathContainsDotDot(const char *path)
{
    const char *ptr = path;

    while ((ptr = strstr(ptr, ".."))) {
        if ((ptr == path || IsPathSeparator(ptr[-1])) && (IsPathSeparator(ptr[2]) || !ptr[2]))
            return true;
        ptr += 2;
    }

    return false;
}

#ifdef _WIN32

FILE *OpenFile(const char *filename, OpenFileMode mode)
{
    wchar_t filename_w[4096];
    if (ConvertUtf8ToWin32Wide(filename, filename_w) < 0)
        return nullptr;

    const wchar_t *mode_w = nullptr;
    switch (mode) {
        case OpenFileMode::Read: { mode_w = L"rbcN"; } break;
        case OpenFileMode::Write: { mode_w = L"wbcN"; } break;
        case OpenFileMode::Append: { mode_w = L"abcN"; } break;
    }
    RG_ASSERT(mode_w);

    FILE *fp = _wfopen(filename_w, mode_w);
    if (!fp) {
        LogError("Cannot open '%1': %2", filename, strerror(errno));
    }

    return fp;
}

bool FileIsVt100(FILE *fp)
{
    static RG_THREAD_LOCAL FILE *cache_fp;
    static RG_THREAD_LOCAL bool cache_vt100;

    // Fast path, for repeated calls (such as Print in a loop)
    if (fp == cache_fp)
        return cache_vt100;

    if (fp == stdout || fp == stderr) {
        HANDLE h = (HANDLE)_get_osfhandle(_fileno(fp));

        DWORD console_mode;
        if (GetConsoleMode(h, &console_mode)) {
            static bool enable_emulation = [&]() {
                bool emulation = console_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING;

                if (!emulation) {
                    // Enable VT100 escape sequences, introduced in Windows 10
                    DWORD new_mode = console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                    emulation = SetConsoleMode(h, new_mode);

                    if (emulation) {
                        static HANDLE exit_handle = h;
                        static DWORD exit_mode = console_mode;

                        atexit([]() { SetConsoleMode(exit_handle, exit_mode); });
                    } else {
                        // Try ConEmu ANSI support for Windows < 10
                        const char *conemuansi_str = getenv("ConEmuANSI");
                        emulation = conemuansi_str && TestStr(conemuansi_str, "ON");
                    }
                }

                return emulation;
            }();

            cache_vt100 = enable_emulation;
        } else {
            cache_vt100 = false;
        }
    } else {
        cache_vt100 = false;
    }

    cache_fp = fp;
    return cache_vt100;
}

bool MakeDirectory(const char *directory, bool error_if_exists)
{
    wchar_t directory_w[4096];
    if (ConvertUtf8ToWin32Wide(directory, directory_w) < 0)
        return false;

    if (!CreateDirectoryW(directory_w, nullptr)) {
        DWORD err = GetLastError();

        if (err != ERROR_ALREADY_EXISTS || error_if_exists) {
            LogError("Cannot create directory '%1': %2", directory, GetWin32ErrorString(err));
            return false;
        }
    }

    return true;
}

bool MakeDirectoryRec(Span<const char> directory)
{
    LocalArray<wchar_t, 4096> buf_w;
    buf_w.len = ConvertUtf8ToWin32Wide(directory, buf_w.data);
    if (buf_w.len < 0)
        return false;

    // Simple case: directory already exists or only last level was missing
    if (!CreateDirectoryW(buf_w.data, nullptr)) {
        DWORD err = GetLastError();

        if (err == ERROR_ALREADY_EXISTS) {
            return true;
        } else if (err != ERROR_PATH_NOT_FOUND) {
            LogError("Cannot create directory '%1': %2", directory, strerror(errno));
            return false;
        }
    }

    for (Size offset = 1, parts = 0; offset <= buf_w.len; offset++) {
        if (!buf_w.data[offset] || buf_w[offset] == L'\\' || buf_w[offset] == L'/') {
            buf_w.data[offset] = 0;
            parts++;

            if (!CreateDirectoryW(buf_w.data, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                Size offset8 = 0;
                while (offset8 < directory.len) {
                    parts -= IsPathSeparator(directory[offset8]);
                    if (!parts)
                        break;
                    offset8++;
                }

                LogError("Cannot create directory '%1': %2",
                         directory.Take(0, offset8), GetWin32ErrorString());
                return false;
            }

            buf_w.data[offset] = L'\\';
        }
    }

    return true;
}

bool UnlinkDirectory(const char *directory, bool error_if_missing)
{
    wchar_t directory_w[4096];
    if (ConvertUtf8ToWin32Wide(directory, directory_w) < 0)
        return false;

    if (!RemoveDirectoryW(directory_w) &&
            (GetLastError() != ERROR_FILE_NOT_FOUND || error_if_missing)) {
        LogError("Failed to remove directory '%1': %2", directory, GetWin32ErrorString());
        return false;
    }

    return true;
}

bool UnlinkFile(const char *filename, bool error_if_missing)
{
    wchar_t filename_w[4096];
    if (ConvertUtf8ToWin32Wide(filename, filename_w) < 0)
        return false;

    if (!DeleteFileW(filename_w) &&
            (GetLastError() != ERROR_FILE_NOT_FOUND || error_if_missing)) {
        LogError("Failed to remove file '%1': %2", filename, GetWin32ErrorString());
        return false;
    }

    return true;
}

#else

FILE *OpenFile(const char *filename, OpenFileMode mode)
{
    const char *mode_str = nullptr;
    switch (mode) {
        case OpenFileMode::Read: { mode_str = "rbe"; } break;
        case OpenFileMode::Write: { mode_str = "wbe"; } break;
        case OpenFileMode::Append: { mode_str = "abe"; } break;
    }
    RG_ASSERT(mode_str);

    FILE *fp = fopen(filename, mode_str);
    if (!fp) {
        LogError("Cannot open '%1': %2", filename, strerror(errno));
    }

    return fp;
}

bool FileIsVt100(FILE *fp)
{
    static RG_THREAD_LOCAL FILE *cache_fp;
    static RG_THREAD_LOCAL bool cache_vt100;

    // Fast path, for repeated calls (such as Print in a loop)
    if (fp == cache_fp)
        return cache_vt100;

    cache_fp = fp;
    cache_vt100 = isatty(fileno(fp));

    return cache_vt100;
}

bool MakeDirectory(const char *directory, bool error_if_exists)
{
    if (mkdir(directory, 0755) < 0 && (errno != EEXIST || error_if_exists)) {
        LogError("Cannot create directory '%1': %2", directory, strerror(errno));
        return false;
    }

    return true;
}

bool MakeDirectoryRec(Span<const char> directory)
{
    char buf[4096];
    if (RG_UNLIKELY(directory.len >= RG_SIZE(buf))) {
        LogError("Path '%1' is too large", directory);
        return false;
    }
    memcpy(buf, directory.ptr, directory.len);
    buf[directory.len] = 0;

    // Simple case: directory already exists or only last level was missing
    if (mkdir(buf, 0755) < 0) {
        if (errno == EEXIST) {
            return true;
        } else if (errno != ENOENT) {
            LogError("Cannot create directory '%1': %2", buf, strerror(errno));
            return false;
        }
    }

    for (Size offset = 1; offset <= directory.len; offset++) {
        if (!buf[offset] || IsPathSeparator(buf[offset])) {
            buf[offset] = 0;

            if (mkdir(buf, 0755) < 0 && errno != EEXIST) {
                LogError("Cannot create directory '%1': %2", buf, strerror(errno));
                return false;
            }

            buf[offset] = *RG_PATH_SEPARATORS;
        }
    }

    return true;
}

bool UnlinkDirectory(const char *directory, bool error_if_missing)
{
    if (rmdir(directory) < 0 && (errno != ENOENT || error_if_missing)) {
        LogError("Failed to remove directory '%1': %2", directory, strerror(errno));
        return false;
    }

    return true;
}

bool UnlinkFile(const char *filename, bool error_if_missing)
{
    if (unlink(filename) < 0 && (errno != ENOENT || error_if_missing)) {
        LogError("Failed to remove file '%1': %2", filename, strerror(errno));
        return false;
    }

    return true;
}

#endif

bool EnsureDirectoryExists(const char *filename)
{
    Span<const char> directory;
    SplitStrReverseAny(filename, RG_PATH_SEPARATORS, &directory);

    return MakeDirectoryRec(directory);
}

#ifdef _WIN32

static HANDLE console_ctrl_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
static bool ignore_ctrl_event = false;

static BOOL CALLBACK ConsoleCtrlHandler(DWORD)
{
    SetEvent(console_ctrl_event);
    return (BOOL)ignore_ctrl_event;
}

static bool InitConsoleCtrlHandler()
{
    static std::once_flag flag;

    static bool success;
    std::call_once(flag, []() { success = SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE); });

    if (!success) {
        LogError("SetConsoleCtrlHandler() failed: %1", GetWin32ErrorString());
    }
    return success;
}

static void CloseHandleSafe(HANDLE *handle_ptr)
{
    if (*handle_ptr && *handle_ptr != INVALID_HANDLE_VALUE) {
        CloseHandle(*handle_ptr);
    }

    *handle_ptr = nullptr;
}

static bool CreateOverlappedPipe(bool overlap0, bool overlap1, HANDLE *out_h0, HANDLE *out_h1)
{
    static LONG pipe_idx;

    HANDLE handles[2] = {};
    RG_DEFER_N(handle_guard) {
        CloseHandleSafe(&handles[0]);
        CloseHandleSafe(&handles[1]);
    };

    char pipe_name[128];
    do {
        Fmt(pipe_name, "\\\\.\\Pipe\\libcc.%1.%2",
            GetCurrentProcessId(), InterlockedIncrement(&pipe_idx));

        handles[0] = CreateNamedPipeA(pipe_name,
                                      PIPE_ACCESS_INBOUND | (overlap0 ? FILE_FLAG_OVERLAPPED : 0),
                                      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 8192, 8192, 0, nullptr);
        if (!handles[0] && GetLastError() != ERROR_ACCESS_DENIED) {
            LogError("Failed to create pipe: %1", GetWin32ErrorString());
            return false;
        }
    } while (!handles[0]);

    handles[1] = CreateFileA(pipe_name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | (overlap1 ? FILE_FLAG_OVERLAPPED : 0), nullptr);
    if (handles[1] == INVALID_HANDLE_VALUE) {
        LogError("Failed to create pipe: %1", GetWin32ErrorString());
        return false;
    }

    handle_guard.Disable();
    *out_h0 = handles[0];
    *out_h1 = handles[1];
    return true;
}

bool ExecuteCommandLine(const char *cmd_line, Span<const uint8_t> in_buf,
                        FunctionRef<void(Span<uint8_t> buf)> out_func, int *out_code)
{
    STARTUPINFOW startup_info = {};

    // Convert command line
    Span<wchar_t> cmd_line_w;
    cmd_line_w.len = 4 * strlen(cmd_line) + 2;
    cmd_line_w.ptr = (wchar_t *)Allocator::Allocate(nullptr, cmd_line_w.len);
    RG_DEFER { Allocator::Release(nullptr, cmd_line_w.ptr, cmd_line_w.len); };
    if (ConvertUtf8ToWin32Wide(cmd_line, cmd_line_w) < 0)
        return false;

    // Detect CTRL+C and CTRL+BREAK events
    if (!InitConsoleCtrlHandler())
        return false;

    // Neither GenerateConsoleCtrlEvent() or TerminateProcess() manage to fully kill Clang on
    // Windows (in highly-parallel builds) after CTRL-C, with occasionnal suspended child
    // processes remaining alive. Furthermore, processes killed by GenerateConsoleCtrlEvent()
    // can trigger "MessageBox" errors, unless SetErrorMode() is used.
    //
    // TerminateJobObject() is a bit brutal, but it takes care of these issues. If job creation
    // or assignement fails, we will try TerminateProcess() instead.
    HANDLE job_handle = CreateJobObject(nullptr, nullptr);
    RG_DEFER { CloseHandleSafe(&job_handle); };

    // Create read and write pipes
    HANDLE in_pipe[2] = {};
    HANDLE out_pipe[2] = {};
    RG_DEFER {
        CloseHandleSafe(&in_pipe[0]);
        CloseHandleSafe(&in_pipe[1]);
        CloseHandleSafe(&out_pipe[0]);
        CloseHandleSafe(&out_pipe[1]);
    };
    if (!CreateOverlappedPipe(false, true, &in_pipe[0], &in_pipe[1]) ||
            !CreateOverlappedPipe(true, false, &out_pipe[0], &out_pipe[1]))
        return false;

    // Start process
    HANDLE process_handle;
    {
        RG_DEFER {
            CloseHandleSafe(&startup_info.hStdInput);
            CloseHandleSafe(&startup_info.hStdOutput);
            CloseHandleSafe(&startup_info.hStdError);
        };
        if (!DuplicateHandle(GetCurrentProcess(), in_pipe[0], GetCurrentProcess(),
                             &startup_info.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS) ||
            !DuplicateHandle(GetCurrentProcess(), out_pipe[1], GetCurrentProcess(),
                             &startup_info.hStdOutput, 0, TRUE, DUPLICATE_SAME_ACCESS) ||
            !DuplicateHandle(GetCurrentProcess(), out_pipe[1], GetCurrentProcess(),
                             &startup_info.hStdError, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            LogError("Failed to duplicate handle: %1", GetWin32ErrorString());
            return false;
        }
        startup_info.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION process_info = {};
        if (!CreateProcessW(nullptr, cmd_line_w.ptr, nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP,
                            nullptr, nullptr, &startup_info, &process_info)) {
            LogError("Failed to start process: %1", GetWin32ErrorString());
            return false;
        }
        if (!AssignProcessToJobObject(job_handle, process_info.hProcess)) {
            CloseHandleSafe(&job_handle);
        }

        process_handle = process_info.hProcess;
        CloseHandleSafe(&process_info.hThread);

        CloseHandleSafe(&in_pipe[0]);
        CloseHandleSafe(&out_pipe[1]);
    }
    RG_DEFER { CloseHandleSafe(&process_handle); };

    // Read and write standard process streams
    {
        HANDLE events[3] = {
            CreateEvent(nullptr, TRUE, FALSE, nullptr),
            CreateEvent(nullptr, TRUE, TRUE, nullptr),
            console_ctrl_event
        };
        RG_DEFER {
            CloseHandleSafe(&events[0]);
            CloseHandleSafe(&events[1]);
        };
        if (!events[0] || !events[1]) {
            LogError("Failed to create event HANDLE: %1", GetWin32ErrorString());
            return false;
        }

        DWORD write_len = 0;
        OVERLAPPED write_ov = {};
        write_ov.hEvent = events[0];

        if (in_buf.len) {
            if (::WriteFile(in_pipe[1], in_buf.ptr, (DWORD)in_buf.len, &write_len, &write_ov)) {
                SetEvent(events[0]);
            } else if (GetLastError() == ERROR_IO_PENDING) {
                // Go on!
            } else if (GetLastError() == ERROR_BROKEN_PIPE) {
                CancelIo(in_pipe[1]);
                SetEvent(events[0]);
            } else {
                LogError("Failed to write process input: %1", GetWin32ErrorString());
                CancelIo(in_pipe[1]);
                SetEvent(events[0]);
            }
        } else {
            CloseHandleSafe(&in_pipe[1]);
        }

        uint8_t read_buf[1024];
        DWORD read_len = 0;
        bool read_pending = false;
        OVERLAPPED read_ov = {};
        read_ov.hEvent = events[1];

        do {
            DWORD ret = WaitForMultipleObjects(RG_LEN(events), events, FALSE, INFINITE);

            if (ret == WAIT_OBJECT_0) {
                CloseHandleSafe(&in_pipe[1]);
                ResetEvent(events[0]);
            } else if (ret == WAIT_OBJECT_0 + 1) {
                if (read_pending) {
                    if (GetOverlappedResult(out_pipe[0], &read_ov, &read_len, TRUE)) {
                        out_func(MakeSpan(read_buf, read_len));
                        read_pending = false;
                    } else if (GetLastError() == ERROR_BROKEN_PIPE) {
                        CancelIo(out_pipe[0]);
                        break;
                    } else {
                        LogError("Failed to read process output: %1", GetWin32ErrorString());
                        CancelIo(out_pipe[0]);
                        break;
                    }
                }

                while (::ReadFile(out_pipe[0], read_buf, RG_SIZE(read_buf), &read_len, &read_ov)) {
                    out_func(MakeSpan(read_buf, read_len));
                }

                if (GetLastError() == ERROR_IO_PENDING) {
                    read_pending = true;
                } else if (GetLastError() == ERROR_BROKEN_PIPE) {
                    CancelIo(out_pipe[0]);
                    break;
                } else {
                    LogError("Failed to read process output: %1", GetWin32ErrorString());
                    CancelIo(out_pipe[0]);
                    break;
                }
            } else if (ret == WAIT_OBJECT_0 + 2) {
                break;
            } else {
                // Not sure how this could happen, but who knows?
                LogError("Read/write for process failed: %1", GetWin32ErrorString());
                break;
            }
        } while (in_pipe[1] || out_pipe[0]);

        CloseHandleSafe(&out_pipe[0]);
        CloseHandleSafe(&in_pipe[1]);
    }

    // Wait for process exit
    DWORD exit_code;
    {
        HANDLE events[2] = {
            process_handle,
            console_ctrl_event
        };

        if (WaitForMultipleObjects(RG_LEN(events), events, FALSE, INFINITE) == WAIT_FAILED) {
            LogError("WaitForMultipleObjects() failed: %1", GetWin32ErrorString());
            return false;
        }
    }

    // Get exit code
    if (WaitForSingleObject(console_ctrl_event, 0) == WAIT_OBJECT_0) {
        if (job_handle) {
            TerminateJobObject(job_handle, STATUS_CONTROL_C_EXIT);
        } else {
            TerminateProcess(process_handle, STATUS_CONTROL_C_EXIT);
        }
        exit_code = STATUS_CONTROL_C_EXIT;
    } else if (!GetExitCodeProcess(process_handle, &exit_code)) {
        LogError("GetExitCodeProcess() failed: %1", GetWin32ErrorString());
        return false;
    }

    // Mimic POSIX SIGINT
    if (exit_code == STATUS_CONTROL_C_EXIT) {
        exit_code = 130;
    }

    *out_code = (int)exit_code;
    return true;
}

#else

static bool CreatePipe(int pfd[2])
{
#ifdef __APPLE__
    if (pipe(pfd) < 0) {
        LogError("Failed to create pipe: %1", strerror(errno));
        return false;
    }

    if (fcntl(pfd[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(pfd[1], F_SETFD, FD_CLOEXEC) < 0) {
        LogError("Failed to set FD_CLOEXEC on pipe: %1", strerror(errno));
        return false;
    }

    return true;
#else
    if (pipe2(pfd, O_CLOEXEC) < 0)  {
        LogError("Failed to create pipe: %1", strerror(errno));
        return false;
    }

    return true;
#endif
}

static void CloseDescriptorSafe(int *fd_ptr)
{
    if (*fd_ptr >= 0) {
        close(*fd_ptr);
    }

    *fd_ptr = -1;
}

bool ExecuteCommandLine(const char *cmd_line, Span<const uint8_t> in_buf,
                        FunctionRef<void(Span<uint8_t> buf)> out_func, int *out_code)
{
    // Create read and write pipes
    int in_pfd[2] = {-1, -1};
    int out_pfd[2] = {-1, -1};
    RG_DEFER {
        CloseDescriptorSafe(&in_pfd[0]);
        CloseDescriptorSafe(&in_pfd[1]);
        CloseDescriptorSafe(&out_pfd[0]);
        CloseDescriptorSafe(&out_pfd[1]);
    };
    if (!CreatePipe(in_pfd) || !CreatePipe(out_pfd))
        return false;
    if (fcntl(in_pfd[1], F_SETFL, O_NONBLOCK) < 0 || fcntl(out_pfd[0], F_SETFL, O_NONBLOCK) < 0) {
        LogError("Failed to set O_NONBLOCK on pipe: %1", strerror(errno));
        return false;
    }

    // Start process
    pid_t pid;
    {
        posix_spawn_file_actions_t file_actions;
        if ((errno = posix_spawn_file_actions_init(&file_actions))) {
            LogError("Failed to set up standard process descriptors: %1", strerror(errno));
            return false;
        }
        RG_DEFER { posix_spawn_file_actions_destroy(&file_actions); };

        if ((errno = posix_spawn_file_actions_adddup2(&file_actions, in_pfd[0], STDIN_FILENO)) ||
                (errno = posix_spawn_file_actions_adddup2(&file_actions, out_pfd[1], STDOUT_FILENO)) ||
                (errno = posix_spawn_file_actions_adddup2(&file_actions, out_pfd[1], STDERR_FILENO))) {
            LogError("Failed to set up standard process descriptors: %1", strerror(errno));
            return false;
        }

        const char *argv[] = {"sh", "-c", cmd_line, nullptr};
        if ((errno = posix_spawn(&pid, "/bin/sh", &file_actions, nullptr,
                                 const_cast<char **>(argv), environ))) {
            LogError("Failed to start process: %1", strerror(errno));
            return false;
        }

        CloseDescriptorSafe(&in_pfd[0]);
        CloseDescriptorSafe(&out_pfd[1]);
    }

    // Read and write standard process streams
    do {
        struct pollfd pfds[2] = {{-1}, {-1}};
        nfds_t pfds_count = 0;
        if (in_pfd[1] >= 0) {
            pfds[pfds_count++] = {in_pfd[1], POLLOUT};
        }
        if (out_pfd[0] >= 0) {
            pfds[pfds_count++] = {out_pfd[0], POLLIN};
        }

        if (RG_POSIX_RESTART_EINTR(poll(pfds, pfds_count, -1)) < 0) {
            LogError("Failed to read process output: %1", strerror(errno));
            break;
        }

        unsigned int in_revents;
        unsigned int out_revents;
        if (pfds[0].fd == in_pfd[1]) {
            in_revents = pfds[0].revents;
            out_revents = pfds[1].revents;
        } else {
            in_revents = 0;
            out_revents = pfds[0].revents;
        }

        // Try to write
        if (in_revents & POLLERR) {
            LogError("Failed to poll process input");
            CloseDescriptorSafe(&in_pfd[1]);
        } else if (in_revents & POLLOUT) {
            if (in_buf.len) {
                ssize_t write_len = write(in_pfd[1], in_buf.ptr, (size_t)in_buf.len);

                if (write_len > 0) {
                    in_buf.ptr += write_len;
                    in_buf.len -= (Size)write_len;
                } else if (!write_len) {
                    CloseDescriptorSafe(&in_pfd[1]);
                } else {
                    LogError("Failed to write process input: %1", strerror(errno));
                    CloseDescriptorSafe(&in_pfd[1]);
                }
            } else {
                CloseDescriptorSafe(&in_pfd[1]);
            }
        }

        // Try to read
        if (out_revents & POLLERR) {
            LogError("Failed to poll process output");
            break;
        } else if (out_revents & POLLIN) {
            uint8_t read_buf[1024];
            ssize_t read_len = read(out_pfd[0], read_buf, RG_SIZE(read_buf));

            if (read_len > 0) {
                out_func(MakeSpan(read_buf, read_len));
            } else if (!read_len) {
                // Does this happen? Should trigger POLLHUP instead, but who knows
                break;
            } else {
                LogError("Failed to read process output: %1", strerror(errno));
                break;
            }
        } else if (out_revents & POLLHUP) {
            break;
        }
    } while (in_pfd[1] >= 0 || out_pfd[0] >= 0);

    // Done reading and writing
    CloseDescriptorSafe(&in_pfd[1]);
    CloseDescriptorSafe(&out_pfd[0]);

    // Wait for process exit
    int status;
    if (RG_POSIX_RESTART_EINTR(waitpid(pid, &status, 0)) < 0) {
        LogError("Failed to wait for process exit: %1", strerror(errno));
        return false;
    }

    if (WIFSIGNALED(status)) {
        *out_code = 128 + WTERMSIG(status);
    } else if (WIFEXITED(status)) {
        *out_code = WEXITSTATUS(status);
    } else {
        *out_code = -1;
    }
    return true;
}

#endif

bool ExecuteCommandLine(const char *cmd_line, Span<const uint8_t> in_buf, Size max_len,
                        HeapArray<uint8_t> *out_buf, int *out_code)
{
    Size start_len = out_buf->len;
    RG_DEFER_N(out_guard) { out_buf->RemoveFrom(start_len); };

    // Don't f*ck up the log
    bool warned = false;

    bool success = ExecuteCommandLine(cmd_line, in_buf, [&](Span<uint8_t> buf) {
        if (max_len < 0 || out_buf->len - start_len <= max_len - buf.len) {
            out_buf->Append(buf);
        } else if (!warned) {
            LogError("Truncated output");
            warned = true;
        }
    }, out_code);
    if (!success)
        return false;

    out_guard.Disable();
    return true;
}

#ifdef _WIN32

void WaitForDelay(int64_t delay)
{
    RG_ASSERT(delay >= 0);
    RG_ASSERT(delay < 1000ll * INT32_MAX);

    while (delay) {
        DWORD delay32 = (DWORD)std::min(delay, (int64_t)UINT32_MAX);
        delay -= delay32;

        Sleep(delay32);
    }
}

bool WaitForInterruption(int64_t delay)
{
    ignore_ctrl_event = InitConsoleCtrlHandler();
    RG_ASSERT(ignore_ctrl_event);

    if (delay >= 0) {
        do {
            DWORD delay32 = (DWORD)std::min(delay, (int64_t)UINT32_MAX);
            delay -= delay32;

            if (WaitForSingleObject(console_ctrl_event, delay32) == WAIT_OBJECT_0)
                return true;
        } while (delay);

        return false;
    } else {
        return WaitForSingleObject(console_ctrl_event, INFINITE) == WAIT_OBJECT_0;
    }
}

#else

void WaitForDelay(int64_t delay)
{
    RG_ASSERT(delay >= 0);
    RG_ASSERT(delay < 1000ll * INT32_MAX);

    struct timespec ts;
    ts.tv_sec = (int)(delay / 1000);
    ts.tv_nsec = (int)((delay % 1000) * 1000000);

    struct timespec rem;
    while (nanosleep(&ts, &rem) < 0) {
        RG_ASSERT(errno == EINTR);
        ts = rem;
    }
}

bool WaitForInterruption(int64_t delay)
{
    static volatile bool run = true;

    signal(SIGINT, [](int) { run = false; });
    signal(SIGTERM, [](int) { run = false; });
    signal(SIGHUP, [](int) { run = false; });

    if (delay >= 0) {
        struct timespec ts;
        ts.tv_sec = (int)(delay / 1000);
        ts.tv_nsec = (int)((delay % 1000) * 1000000);

        struct timespec rem;
        while (run && nanosleep(&ts, &rem) < 0) {
            RG_ASSERT(errno == EINTR);
            ts = rem;
        }
    } else {
        while (run) {
            pause();
        }
    }

    return !run;
}

#endif

int GetCoreCount()
{
#ifdef __EMSCRIPTEN__
    return 1;
#else
    static int cores;

    if (!cores) {
        const char *env = getenv("OVERRIDE_CORES");

        if (env) {
            char *end_ptr;
            long value = strtol(env, &end_ptr, 10);
            if (end_ptr > env && !end_ptr[0] && value > 0) {
                cores = (int)value;
            } else {
                LogError("OVERRIDE_CORES must be positive number (ignored)");
            }
        } else {
            cores = (int)std::thread::hardware_concurrency();
        }

        RG_ASSERT(cores > 0);
    }

    return cores;
#endif
}

// ------------------------------------------------------------------------
// Tasks
// ------------------------------------------------------------------------

struct Task {
    Async *async;
    std::function<bool()> func;
};

struct TaskQueue {
    std::mutex queue_mutex;
    BucketArray<Task> tasks;
};

class AsyncPool {
    RG_DELETE_COPY(AsyncPool)

    std::mutex pool_mutex;
    std::condition_variable pending_cv;
    std::condition_variable sync_cv;

    // Manipulate with pool_mutex locked
    int refcount = 0;

    int async_count = 0;
    HeapArray<bool> workers_state;

    HeapArray<TaskQueue> queues;
    int next_queue_idx = 0;
    std::atomic_int pending_tasks {0};

public:
    AsyncPool(int threads, bool leak);

    void RegisterAsync();
    void UnregisterAsync();

    void AddTask(Async *async, const std::function<bool()> &func);

    void RunWorker(int worker_idx);
    void SyncOn(Async *async);

    void RunTasks(int queue_idx);
    void RunTask(Task *task);
};

// thread_local breaks down on MinGW when destructors are involved, work
// around this with heap allocation.
static RG_THREAD_LOCAL AsyncPool *async_default_pool = nullptr;
static RG_THREAD_LOCAL AsyncPool *async_running_pool = nullptr;
static RG_THREAD_LOCAL int async_running_worker_idx;
static RG_THREAD_LOCAL bool async_running_task = false;

Async::Async(int workers)
{
    if (workers >= 0) {
        if (workers > RG_ASYNC_MAX_WORKERS) {
            LogError("Async cannot use more than %1 workers", RG_ASYNC_MAX_WORKERS);
            workers = RG_ASYNC_MAX_WORKERS;
        }

        pool = new AsyncPool(workers, false);
    } else if (async_running_pool) {
        pool = async_running_pool;
    } else {
        if (!async_default_pool) {
            // NOTE: We're leaking one AsyncPool each time a non-worker thread uses Async()
            // for the first time. That's only one leak in most cases, when the main thread
            // is the only non-worker thread using Async, but still. Something to keep in mind.

            workers = std::min(GetCoreCount() - 1, RG_ASYNC_MAX_WORKERS);
            async_default_pool = new AsyncPool(workers, true);
        }

        pool = async_default_pool;
    }

    pool->RegisterAsync();
}

Async::~Async()
{
    RG_ASSERT(!remaining_tasks);
    pool->UnregisterAsync();
}

void Async::Run(const std::function<bool()> &func)
{
    pool->AddTask(this, func);
}

bool Async::Sync()
{
    pool->SyncOn(this);
    return success;
}

bool Async::IsTaskRunning()
{
    return async_running_task;
}

int Async::GetWorkerIdx()
{
    return async_running_worker_idx;
}

AsyncPool::AsyncPool(int workers, bool leak)
{
    // The first queue is for the main thread, whereas workers_state[0] is
    // not used but it's easier to index it the same way.
    workers_state.AppendDefault(workers + 1);
    queues.AppendDefault(workers + 1);

    refcount = leak;
}

void AsyncPool::RegisterAsync()
{
    std::lock_guard<std::mutex> lock_pool(pool_mutex);

    if (!async_count++) {
        for (int i = 1; i < workers_state.len; i++) {
            if (!workers_state[i]) {
                std::thread(&AsyncPool::RunWorker, this, i).detach();

                refcount++;
                workers_state[i] = true;
            }
        }
    }
}

void AsyncPool::UnregisterAsync()
{
    std::lock_guard<std::mutex> lock_pool(pool_mutex);
    async_count--;
}

void AsyncPool::AddTask(Async *async, const std::function<bool()> &func)
{
    if (async_running_pool != async->pool) {
        for (;;) {
            TaskQueue *queue = &queues[next_queue_idx];

            if (--next_queue_idx < 0) {
                next_queue_idx = (int)workers_state.len - 1;
            }

            std::unique_lock<std::mutex> lock_queue(queue->queue_mutex, std::try_to_lock);
            if (lock_queue.owns_lock()) {
                queue->tasks.Append({async, func});
                break;
            }
        }
    } else {
        TaskQueue *queue = &queues[async_running_worker_idx];

        std::lock_guard<std::mutex> lock_queue(queue->queue_mutex);
        queue->tasks.Append({async, func});
    }

    async->remaining_tasks++;

    // Wake up workers and syncing threads (extra help)
    if (!pending_tasks++) {
        std::lock_guard<std::mutex> lock_pool(pool_mutex);

        pending_cv.notify_all();
        sync_cv.notify_all();
    }
}

void AsyncPool::RunWorker(int worker_idx)
{
    async_running_pool = this;
    async_running_worker_idx = worker_idx;

    std::unique_lock<std::mutex> lock_pool(pool_mutex);

    while (async_count) {
        lock_pool.unlock();
        RunTasks(worker_idx);
        lock_pool.lock();

        std::chrono::duration<int, std::milli> duration(RG_ASYNC_MAX_IDLE_TIME); // Thanks C++
        pending_cv.wait_for(lock_pool, duration, [&]() { return !!pending_tasks; });
    }

    workers_state[worker_idx] = false;
    if (!--refcount) {
        lock_pool.unlock();
        delete this;
    }
}

void AsyncPool::SyncOn(Async *async)
{
    RG_DEFER_C(pool = async_running_pool,
               worker_idx = async_running_worker_idx) {
        async_running_pool = pool;
        async_running_worker_idx = worker_idx;
    };

    async_running_pool = this;
    async_running_worker_idx = 0;

    while (async->remaining_tasks) {
        RunTasks(async_running_worker_idx);

        std::unique_lock<std::mutex> lock_sync(pool_mutex);
        sync_cv.wait(lock_sync, [&]() { return pending_tasks || !async->remaining_tasks; });
    }
}

void AsyncPool::RunTasks(int queue_idx)
{
    // The '12' factor is pretty arbitrary, don't try to find meaning there
    for (int i = 0; i < workers_state.len * 12; i++) {
        TaskQueue *queue = &queues[queue_idx];

        std::unique_lock<std::mutex> lock_queue(queue->queue_mutex, std::try_to_lock);
        if (lock_queue.owns_lock() && queue->tasks.len) {
            Task task = std::move(queue->tasks[0]);
            queue->tasks.RemoveFirst();

            lock_queue.unlock();
            RunTask(&task);
        }

        queue_idx++;
        if (queue_idx >= queues.len) {
            queue_idx = 0;
        }
    }
}

void AsyncPool::RunTask(Task *task)
{
    Async *async = task->async;

    RG_DEFER_C(running = async_running_task) { async_running_task = running; };
    async_running_task = true;

    pending_tasks--;
    if (async->success && !task->func()) {
        async->success = false;
    }

    if (!--async->remaining_tasks) {
        std::lock_guard<std::mutex> lock_sync(pool_mutex);
        sync_cv.notify_all();
    }
}

// ------------------------------------------------------------------------
// Streams
// ------------------------------------------------------------------------

StreamReader stdin_st(stdin, "<stdin>");
StreamWriter stdout_st(stdout, "<stdout>");
StreamWriter stderr_st(stderr, "<stderr>");

#ifdef MZ_VERSION
struct MinizInflateContext {
    tinfl_decompressor inflator;
    bool done;

    uint8_t in[256 * 1024];
    uint8_t *in_ptr;
    Size in_len;

    uint8_t out[256 * 1024];
    uint8_t *out_ptr;
    Size out_len;

    // Gzip support
    bool header_done;
    uint32_t crc32;
    Size uncompressed_size;
};
RG_STATIC_ASSERT(RG_SIZE(MinizInflateContext::out) >= TINFL_LZ_DICT_SIZE);
#endif

bool StreamReader::Open(Span<const uint8_t> buf, const char *filename,
                        CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    this->filename = filename ? filename : "<memory>";

    source.type = SourceType::Memory;
    source.u.memory.buf = buf;
    source.u.memory.pos = 0;

    if (!InitDecompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamReader::Open(FILE *fp, const char *filename, CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    RG_ASSERT(fp);
    RG_ASSERT(filename);
    this->filename = filename;

    source.type = SourceType::File;
    source.u.file.fp = fp;
    source.u.file.owned = false;

    if (!InitDecompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamReader::Open(const char *filename, CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    RG_ASSERT(filename);
    this->filename = filename;

    source.type = SourceType::File;
    source.u.file.fp = OpenFile(filename, OpenFileMode::Read);
    if (!source.u.file.fp)
        return false;
    source.u.file.owned = true;

    if (!InitDecompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamReader::Open(const std::function<Size(Span<uint8_t>)> &func, const char *filename,
                        CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    this->filename = filename ? filename : "<closure>";

    source.type = SourceType::Function;
    new (&source.u.func) std::function<Size(Span<uint8_t>)>(func);

    if (!InitDecompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamReader::Close()
{
    ReleaseResources();

    bool ret = !error;

    filename = nullptr;
    source.eof = false;
    raw_len = -1;
    read = 0;
    raw_read = 0;
    error = false;
    eof = false;

    return ret;
}

Size StreamReader::Read(Span<uint8_t> out_buf)
{
    if (RG_UNLIKELY(error))
        return -1;

    Size read_len = 0;
    switch (compression.type) {
        case CompressionType::None: {
            read_len = ReadRaw(out_buf.len, out_buf.ptr);
            eof = source.eof;
        } break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
            read_len = Inflate(out_buf.len, out_buf.ptr);
        } break;
    }

    if (RG_LIKELY(read_len >= 0)) {
        read += read_len;
    }
    return read_len;
}

Size StreamReader::ReadAll(Size max_len, HeapArray<uint8_t> *out_buf)
{
    if (error)
        return -1;

    if (compression.type == CompressionType::None && ComputeStreamLen() >= 0) {
        if (raw_len > max_len) {
            LogError("File '%1' is too large (limit = %2)", filename, FmtDiskSize(max_len));
            return -1;
        }

        // Add one trailing byte to avoid reallocation for users who append a NUL character
        out_buf->Grow(raw_len + 1);
        Size read_len = Read(raw_len, out_buf->end());
        if (read_len < 0)
            return -1;
        out_buf->len += read_len;

        return read_len;
    } else {
        RG_DEFER_NC(buf_guard, buf_len = out_buf->len) { out_buf->RemoveFrom(buf_len); };

        Size read_len, total_len = 0;
        out_buf->Grow(Megabytes(1));
        while ((read_len = Read(out_buf->Available(), out_buf->end())) > 0) {
            if (RG_UNLIKELY(read_len > max_len - total_len)) {
                LogError("File '%1' is too large (limit = %2)", filename, FmtDiskSize(max_len));
                return -1;
            }
            total_len += read_len;

            out_buf->len += read_len;
            out_buf->Grow(Megabytes(1));
        }
        if (error)
            return -1;

        buf_guard.Disable();
        return total_len;
    }
}

Size StreamReader::ComputeStreamLen()
{
    if (raw_read || raw_len >= 0)
        return raw_len;

    switch (source.type) {
        case SourceType::Memory: {
            raw_len = source.u.memory.buf.len;
        } break;

        case SourceType::File: {
#ifdef _WIN32
            int64_t pos = _ftelli64(source.u.file.fp);
            RG_DEFER { _fseeki64(source.u.file.fp, pos, SEEK_SET); };
            if (_fseeki64(source.u.file.fp, 0, SEEK_END) < 0)
                return -1;
            int64_t len = _ftelli64(source.u.file.fp);
#else
            off64_t pos = ftello64(source.u.file.fp);
            RG_DEFER { fseeko64(source.u.file.fp, pos, SEEK_SET); };
            if (fseeko64(source.u.file.fp, 0, SEEK_END) < 0)
                return -1;
            off64_t len = ftello64(source.u.file.fp);
#endif
            if (len > RG_SIZE_MAX) {
                static bool warned = false;
                if (!warned) {
                    LogError("Files bigger than %1 are not well supported", FmtMemSize(RG_SIZE_MAX));
                    warned = true;
                }
                len = RG_SIZE_MAX;
            }
            raw_len = (Size)len;
        } break;

        case SourceType::Function: {
            return -1;
        } break;
    }

    return raw_len;
}

bool StreamReader::InitDecompressor(CompressionType type)
{
    switch (type) {
        case CompressionType::None: {} break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
#ifdef MZ_VERSION
            compression.u.miniz =
                (MinizInflateContext *)Allocator::Allocate(nullptr, RG_SIZE(MinizInflateContext),
                                                           (int)Allocator::Flag::Zero);
            tinfl_init(&compression.u.miniz->inflator);
            compression.u.miniz->crc32 = MZ_CRC32_INIT;
#else
            LogError("Deflate compression not available for '%1'", filename);
            error = true;
            return false;
#endif
        } break;
    }
    compression.type = type;

    return true;
}

void StreamReader::ReleaseResources()
{
    switch (compression.type) {
        case CompressionType::None: {} break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
#ifdef MZ_VERSION
            Allocator::Release(nullptr, compression.u.miniz, RG_SIZE(*compression.u.miniz));
#endif
        } break;
    }
    compression.type = CompressionType::None;

    switch (source.type) {
        case SourceType::Memory: {
            source.u.memory = {};
        } break;

        case SourceType::File: {
            if (source.u.file.owned && source.u.file.fp) {
                fclose(source.u.file.fp);
            }

            source.u.file.fp = nullptr;
            source.u.file.owned = false;
        } break;

        case SourceType::Function: {
            source.u.func.~function();
        } break;
    }
    source.type = SourceType::Memory;
}

Size StreamReader::Inflate(Size max_len, void *out_buf)
{
#ifdef MZ_VERSION
    MinizInflateContext *ctx = compression.u.miniz;

    // Gzip header is not directly supported by miniz. Currently this
    // will fail if the header is longer than 4096 bytes, which is
    // probably quite rare.
    if (compression.type == CompressionType::Gzip && !ctx->header_done) {
        uint8_t header[4096];
        Size header_len;

        header_len = ReadRaw(RG_SIZE(header), header);
        if (header_len < 0) {
            return -1;
        } else if (header_len < 10 || header[0] != 0x1F || header[1] != 0x8B) {
            LogError("File '%1' does not look like a Gzip stream", filename);
            error = true;
            return -1;
        }

        Size header_offset = 10;
        if (header[3] & 0x4) { // FEXTRA
            if (header_len - header_offset < 2)
                goto truncated_error;
            uint16_t extra_len = (uint16_t)((header[11] << 8) | header[10]);
            if (extra_len > header_len - header_offset)
                goto truncated_error;
            header_offset += extra_len;
        }
        if (header[3] & 0x8) { // FNAME
            uint8_t *end_ptr = (uint8_t *)memchr(header + header_offset, '\0',
                                                 (size_t)(header_len - header_offset));
            if (!end_ptr)
                goto truncated_error;
            header_offset = end_ptr - header + 1;
        }
        if (header[3] & 0x10) { // FCOMMENT
            uint8_t *end_ptr = (uint8_t *)memchr(header + header_offset, '\0',
                                                 (size_t)(header_len - header_offset));
            if (!end_ptr)
                goto truncated_error;
            header_offset = end_ptr - header + 1;
        }
        if (header[3] & 0x2) { // FHCRC
            if (header_len - header_offset < 2)
                goto truncated_error;
            uint16_t crc16 = (uint16_t)(header[1] << 8 | header[0]);
            if ((mz_crc32(MZ_CRC32_INIT, header, (size_t)header_offset) & 0xFFFF) == crc16) {
                LogError("Failed header CRC16 check in '%s'", filename);
                error = true;
                return -1;
            }
            header_offset += 2;
        }

        // Put back remaining data in the buffer
        memcpy(ctx->in, header + header_offset, (size_t)(header_len - header_offset));
        ctx->in_ptr = ctx->in;
        ctx->in_len = header_len - header_offset;

        ctx->header_done = true;
    }

    // Inflate (with miniz)
    {
        Size read_len = 0;
        for (;;) {
            if (max_len < ctx->out_len) {
                memcpy(out_buf, ctx->out_ptr, (size_t)max_len);
                read_len += max_len;
                ctx->out_ptr += max_len;
                ctx->out_len -= max_len;

                return read_len;
            } else {
                memcpy(out_buf, ctx->out_ptr, (size_t)ctx->out_len);
                read_len += ctx->out_len;
                out_buf = (uint8_t *)out_buf + ctx->out_len;
                max_len -= ctx->out_len;
                ctx->out_ptr = ctx->out;
                ctx->out_len = 0;

                if (ctx->done) {
                    eof = true;
                    return read_len;
                }
            }

            while (ctx->out_len < RG_SIZE(ctx->out)) {
                if (!ctx->in_len) {
                    ctx->in_ptr = ctx->in;
                    ctx->in_len = ReadRaw(RG_SIZE(ctx->in), ctx->in);
                    if (ctx->in_len < 0)
                        return read_len ? read_len : ctx->in_len;
                }

                size_t in_arg = (size_t)ctx->in_len;
                size_t out_arg = (size_t)(RG_SIZE(ctx->out) - ctx->out_len);
                uint32_t flags = (uint32_t)
                    ((compression.type == CompressionType::Zlib ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0) |
                     (source.eof ? 0 : TINFL_FLAG_HAS_MORE_INPUT));

                tinfl_status status = tinfl_decompress(&ctx->inflator, ctx->in_ptr, &in_arg,
                                                       ctx->out, ctx->out + ctx->out_len,
                                                       &out_arg, flags);

                if (compression.type == CompressionType::Gzip) {
                    ctx->crc32 = (uint32_t)mz_crc32(ctx->crc32, ctx->out + ctx->out_len, out_arg);
                    ctx->uncompressed_size += (Size)out_arg;
                }

                ctx->in_ptr += (Size)in_arg;
                ctx->in_len -= (Size)in_arg;
                ctx->out_len += (Size)out_arg;

                if (status == TINFL_STATUS_DONE) {
                    // Gzip footer (CRC and size check)
                    if (compression.type == CompressionType::Gzip) {
                        uint32_t footer[2];
                        RG_STATIC_ASSERT(RG_SIZE(footer) == 8);

                        if (ctx->in_len < RG_SIZE(footer)) {
                            memcpy(footer, ctx->in_ptr, (size_t)ctx->in_len);

                            Size missing_len = RG_SIZE(footer) - ctx->in_len;
                            if (ReadRaw(missing_len, footer + ctx->in_len) < missing_len) {
                                if (error) {
                                    return -1;
                                } else {
                                    goto truncated_error;
                                }
                            }
                        } else {
                            memcpy(footer, ctx->in_ptr, RG_SIZE(footer));
                        }
                        footer[0] = LittleEndian(footer[0]);
                        footer[1] = LittleEndian(footer[1]);

                        if (ctx->crc32 != footer[0] ||
                                (uint32_t)ctx->uncompressed_size != footer[1]) {
                            LogError("Failed CRC32 or size check in GZip stream '%1'", filename);
                            error = true;
                            return -1;
                        }
                    }

                    ctx->done = true;
                    break;
                } else if (status < TINFL_STATUS_DONE) {
                    LogError("Failed to decompress '%1' (Deflate)", filename);
                    error = true;
                    return -1;
                }
            }
        }
    }

truncated_error:
    LogError("Truncated Gzip header in '%1'", filename);
    error = true;
    return -1;
#else
    RG_UNREACHABLE();
#endif
}

Size StreamReader::ReadRaw(Size max_len, void *out_buf)
{
    ComputeStreamLen();

    Size read_len = 0;
    switch (source.type) {
        case SourceType::Memory: {
            read_len = source.u.memory.buf.len - source.u.memory.pos;
            if (read_len > max_len) {
                read_len = max_len;
            }
            memcpy(out_buf, source.u.memory.buf.ptr + source.u.memory.pos, (size_t)read_len);
            source.u.memory.pos += read_len;
            source.eof |= (source.u.memory.pos >= source.u.memory.buf.len);
        } break;

        case SourceType::File: {
restart:
            read_len = (Size)fread(out_buf, 1, (size_t)max_len, source.u.file.fp);
            if (ferror(source.u.file.fp)) {
                if (errno == EINTR)
                    goto restart;

                LogError("Error while reading file '%1': %2", filename, strerror(errno));
                error = true;
                return -1;
            }
            source.eof |= (bool)feof(source.u.file.fp);
        } break;

        case SourceType::Function: {
            read_len = source.u.func(MakeSpan((uint8_t *)out_buf, max_len));
            if (read_len < 0) {
                error = true;
                return -1;
            }
            source.eof |= (read_len == 0);
        } break;
    }

    raw_read += read_len;
    return read_len;
}

// XXX: Maximum line length
bool LineReader::Next(Span<char> *out_line)
{
    if (RG_UNLIKELY(error || eof))
        return false;

    for (;;) {
        if (!view.len) {
            buf.Grow(RG_LINE_READER_STEP_SIZE + 1);

            Size read_len = st->Read(RG_LINE_READER_STEP_SIZE, buf.end());
            if (read_len < 0) {
                error = true;
                return false;
            }
            buf.len += read_len;
            eof = !read_len;

            view = buf;
        }

        line = SplitStrLine(view, &view);
        if (view.len || eof) {
            line.ptr[line.len] = 0;
            line_number++;
            *out_line = line;
            return true;
        }

        buf.len = view.ptr - line.ptr;
        memmove(buf.ptr, line.ptr, (size_t)buf.len);
    }
}

void LineReader::PushLogFilter()
{
    RG::PushLogFilter([this](LogLevel level, const char *, const char *msg, FunctionRef<LogFunc> func) {
        char ctx_buf[1024];
        Fmt(ctx_buf, "%1(%2): ", st->GetFileName(), line_number);

        func(level, ctx_buf, msg);
    });
}

#ifdef MZ_VERSION
struct MinizDeflateContext {
    tdefl_compressor deflator;

    // Gzip support
    uint32_t crc32;
    Size uncompressed_size;

    // Used to buffer small writes
    LocalArray<uint8_t, 1024> buf;
};
#endif

bool StreamWriter::Open(HeapArray<uint8_t> *mem, const char *filename,
                        CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    this->filename = filename ? filename : "<memory>";

    dest.type = DestinationType::Memory;
    dest.u.memory = mem;
    dest.vt100 = false;

    if (!InitCompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamWriter::Open(FILE *fp, const char *filename, CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    RG_ASSERT(fp);
    RG_ASSERT(filename);
    this->filename = filename;

    dest.type = DestinationType::File;
    dest.u.file.fp = fp;
    dest.u.file.owned = false;
    dest.vt100 = FileIsVt100(fp);

    if (!InitCompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamWriter::Open(const char *filename, CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    RG_ASSERT(filename);
    this->filename = filename;

    dest.type = DestinationType::File;
    dest.u.file.fp = OpenFile(filename, OpenFileMode::Write);
    if (!dest.u.file.fp)
        return false;
    dest.u.file.owned = true;
    dest.vt100 = FileIsVt100(dest.u.file.fp);

    if (!InitCompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamWriter::Open(const std::function<bool(Span<const uint8_t>)> &func, const char *filename,
                        CompressionType compression_type)
{
    RG_ASSERT(!this->filename);

    RG_DEFER_N(error_guard) {
        ReleaseResources();
        error = true;
    };

    this->filename = filename ? filename : "<closure>";

    dest.type = DestinationType::Function;
    new (&dest.u.func) std::function<bool(Span<const uint8_t>)>(func);
    dest.vt100 = false;

    if (!InitCompressor(compression_type))
        return false;

    error_guard.Disable();
    return true;
}

bool StreamWriter::Close()
{
    RG_DEFER {
        ReleaseResources();

        filename = nullptr;
        error = false;
    };

    if (IsValid()) {
        switch (compression.type) {
            case CompressionType::None: {} break;

            case CompressionType::Gzip:
            case CompressionType::Zlib: {
#ifdef MZ_VERSION
                MinizDeflateContext *ctx = compression.u.miniz;

                if (ctx->buf.len && !Deflate(ctx->buf))
                    return false;

                tdefl_status status = tdefl_compress_buffer(&ctx->deflator, nullptr, 0, TDEFL_FINISH);
                if (status != TDEFL_STATUS_DONE) {
                    if (status != TDEFL_STATUS_PUT_BUF_FAILED) {
                        LogError("Failed to end Deflate stream for '%1", filename);
                    }
                    return false;
                }

                if (compression.type == CompressionType::Gzip) {
                    uint32_t gzip_footer[] = {
                        LittleEndian(ctx->crc32),
                        LittleEndian((uint32_t)ctx->uncompressed_size)
                    };

                    if (!WriteRaw(MakeSpan((uint8_t *)gzip_footer, RG_SIZE(gzip_footer))))
                        return false;
                }
#endif
            } break;
        }

        switch (dest.type) {
            case DestinationType::Memory: {} break;

            case DestinationType::File: {
#if defined(_WIN32)
                if (fflush(dest.u.file.fp) != 0) {
#elif defined(__APPLE__)
                if ((fflush(dest.u.file.fp) != 0 || fsync(fileno(dest.u.file.fp)) < 0) &&
                        errno != EINVAL && errno != ENOTSUP) {
#else
                if ((fflush(dest.u.file.fp) != 0 || fsync(fileno(dest.u.file.fp)) < 0) && errno != EINVAL) {
#endif
                    LogError("Failed to finalize writing to '%1': %2", filename, strerror(errno));
                    return false;
                }
            } break;

            case DestinationType::Function: {
                if (!dest.u.func({}))
                    return false;
            } break;
        }
    }

    return IsValid();
}

bool StreamWriter::Write(Span<const uint8_t> buf)
{
    if (RG_UNLIKELY(error))
        return false;

    switch (compression.type) {
        case CompressionType::None: {
            return WriteRaw(buf);
        } break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
#ifdef MZ_VERSION
            MinizDeflateContext *ctx = compression.u.miniz;

            if (ctx->buf.len) {
                Size copy_len = std::min(buf.len, ctx->buf.Available());

                memcpy(ctx->buf.end(), buf.ptr, copy_len);
                ctx->buf.len += copy_len;
                buf.ptr += copy_len;
                buf.len -= copy_len;
            }

            if (buf.len) {
                if (ctx->buf.len && !Deflate(ctx->buf))
                    return false;
                ctx->buf.Clear();

                if (buf.len >= RG_SIZE(ctx->buf.data) / 2) {
                    if (!Deflate(buf))
                        return false;
                } else {
                    memcpy(ctx->buf.data, buf.ptr, buf.len);
                    ctx->buf.len = buf.len;
                }
            }

            return true;
#endif
        } break;
    }

    RG_UNREACHABLE();
}

bool StreamWriter::InitCompressor(CompressionType type)
{
    switch (type) {
        case CompressionType::None: {} break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
#ifdef MZ_VERSION
            compression.u.miniz =
                (MinizDeflateContext *)Allocator::Allocate(nullptr, RG_SIZE(MinizDeflateContext),
                                                           (int)Allocator::Flag::Zero);
            compression.u.miniz->crc32 = MZ_CRC32_INIT;

            int flags = 32 | (type == CompressionType::Zlib ? TDEFL_WRITE_ZLIB_HEADER : 0);

            tdefl_status status = tdefl_init(&compression.u.miniz->deflator,
                                             [](const void *buf, int len, void *udata) {
                StreamWriter *st = (StreamWriter *)udata;
                return (int)st->WriteRaw(MakeSpan((uint8_t *)buf, len));
            }, this, flags);
            if (status != TDEFL_STATUS_OKAY) {
                LogError("Failed to initialize Deflate compression for '%1'", filename);
                error = true;
                return false;
            }

            if (type == CompressionType::Gzip) {
                static uint8_t gzip_header[] = {
                    0x1F, 0x8B, // Fixed bytes
                    8, // Deflate
                    0, // FLG
                    0, 0, 0, 0, // MTIME
                    0, // XFL
                    0 // OS
                };

                if (!WriteRaw(gzip_header))
                    return false;
            }
#else
            LogError("Deflate compression not available for '%1'", filename);
            error = true;
            return false;
#endif
        } break;
    }
    compression.type = type;

    return true;
}

void StreamWriter::ReleaseResources()
{
    switch (compression.type) {
        case CompressionType::None: {} break;

        case CompressionType::Gzip:
        case CompressionType::Zlib: {
#ifdef MZ_VERSION
            Allocator::Release(nullptr, compression.u.miniz, RG_SIZE(*compression.u.miniz));
#endif
        } break;
    }
    compression.type = CompressionType::None;

    switch (dest.type) {
        case DestinationType::Memory: {
            dest.u.memory = nullptr;
        } break;

        case DestinationType::File: {
            if (dest.u.file.owned && dest.u.file.fp) {
                fclose(dest.u.file.fp);
            }

            dest.u.file.fp = nullptr;
            dest.u.file.owned = false;
        } break;

        case DestinationType::Function: {
            dest.u.func.~function();
        } break;
    }
    dest.type = DestinationType::Memory;
}

#ifdef MZ_VERSION
bool StreamWriter::Deflate(Span<const uint8_t> buf)
{
    MinizDeflateContext *ctx = compression.u.miniz;

    if (compression.type == CompressionType::Gzip) {
        ctx->crc32 = (uint32_t)mz_crc32(ctx->crc32, buf.ptr, (size_t)buf.len);
        ctx->uncompressed_size += buf.len;
    }

    tdefl_status status = tdefl_compress_buffer(&ctx->deflator, buf.ptr, (size_t)buf.len, TDEFL_NO_FLUSH);
    if (status < TDEFL_STATUS_OKAY) {
        if (status != TDEFL_STATUS_PUT_BUF_FAILED) {
            LogError("Failed to deflate stream to '%1'", filename);
        }

        error = true;
        return false;
    }

    return true;
}
#endif

bool StreamWriter::WriteRaw(Span<const uint8_t> buf)
{
    switch (dest.type) {
        case DestinationType::Memory: {
            // dest.u.memory->Append(buf) would work but it's probably slower
            dest.u.memory->Grow(buf.len);
            memcpy(dest.u.memory->ptr + dest.u.memory->len, buf.ptr, (size_t)buf.len);
            dest.u.memory->len += buf.len;

            return true;
        } break;

        case DestinationType::File: {
            while (buf.len) {
                size_t write_len = fwrite(buf.ptr, 1, (size_t)buf.len, dest.u.file.fp);

                if (ferror(dest.u.file.fp)) {
                    if (errno == EINTR) {
                        clearerr(dest.u.file.fp);
                    } else {
                        LogError("Failed to write to '%1': %2", filename, strerror(errno));
                        error = true;
                        return false;
                    }
                }

                buf.ptr += write_len;
                buf.len -= write_len;
            }

            return true;
        } break;

        case DestinationType::Function: {
            // Empty writes are used to "close" the file
            if (buf.len) {
                bool ret = dest.u.func(buf);

                error |= !ret;
                return ret;
            }
        } break;
    }

    RG_UNREACHABLE();
}

bool SpliceStream(StreamReader *reader, Size max_len, StreamWriter *writer)
{
    if (!reader->IsValid())
        return false;

    Size total_len = 0;
    while (!reader->IsEOF()) {
        LocalArray<uint8_t, 16384> buf;
        buf.len = reader->Read(buf.data);
        if (buf.len < 0)
            return false;

        if (RG_UNLIKELY(max_len >= 0 && buf.len > max_len - total_len)) {
            LogError("File '%1' is too large (limit = %2)", reader->GetFileName(), FmtDiskSize(max_len));
            return false;
        }
        total_len += buf.len;

        if (!writer->Write(buf))
            return false;
    }

    return true;
}

// ------------------------------------------------------------------------
// INI
// ------------------------------------------------------------------------

static inline bool IsAsciiIdChar(char c)
{
    return IsAsciiAlphaOrDigit(c) || c == '_' || c == '-' || c == '.' || c == ' ';
}

IniParser::LineType IniParser::FindNextLine(IniProperty *out_prop)
{
    if (RG_UNLIKELY(error))
        return LineType::Exit;

    RG_DEFER_N(error_guard) { error = true; };

    Span<char> line;
    while (reader.Next(&line)) {
        line = TrimStr(line);

        if (!line.len || line[0] == ';' || line[0] == '#') {
            // Ignore this line (empty or comment)
        } else if (line[0] == '[') {
            if (line.len < 2 || line[line.len - 1] != ']') {
                LogError("Malformed section line");
                return LineType::Exit;
            }

            Span<const char> section = TrimStr(line.Take(1, line.len - 2));
            if (!section.len) {
                LogError("Empty section name");
                return LineType::Exit;
            }
            if (!std::all_of(section.begin(), section.end(), IsAsciiIdChar)) {
                LogError("Section names can only contain alphanumeric characters, '_', '-', '.' or ' '");
                return LineType::Exit;
            }

            current_section.RemoveFrom(0);
            current_section.Append(section);

            error_guard.Disable();
            return LineType::Section;
        } else {
            Span<char> value;
            Span<const char> key = TrimStr(SplitStr(line, '=', &value));
            if (!key.len || key.end() == line.end()) {
                LogError("Malformed key=value pair");
                return LineType::Exit;
            }
            if (!std::all_of(key.begin(), key.end(), IsAsciiIdChar)) {
                LogError("Key names can only contain alphanumeric characters, '_', '-' or '.'");
                return LineType::Exit;
            }
            value = TrimStr(value);
            *value.end() = 0;

            out_prop->section = current_section;
            out_prop->key = key;
            out_prop->value = value;

            error_guard.Disable();
            return LineType::KeyValue;
        }
    }
    if (!reader.IsValid())
        return LineType::Exit;

    eof = true;

    error_guard.Disable();
    return LineType::Exit;
}

bool IniParser::Next(IniProperty *out_prop)
{
    LineType type;
    while ((type = FindNextLine(out_prop)) == LineType::Section);
    return type == LineType::KeyValue;
}

bool IniParser::NextInSection(IniProperty *out_prop)
{
    LineType type = FindNextLine(out_prop);
    return type == LineType::KeyValue;
}

bool IniParser::ParseBoolValue(Span<const char> value, bool *out_value)
{
    if (value == "1" || value == "On" || value == "Y") {
        *out_value = true;
        return true;
    } else if (value == "0" || value == "Off" || value == "N") {
        *out_value = false;
        return true;
    } else {
        LogError("Invalid boolean value '%1'", value);
        return false;
    }
}

// ------------------------------------------------------------------------
// Assets
// ------------------------------------------------------------------------

AssetLoadStatus AssetSet::LoadFromLibrary(const char *filename, const char *var_name)
{
    const Span<const AssetInfo> *lib_assets = nullptr;

    // Check library time
    {
        FileInfo file_info;
        if (!StatFile(filename, &file_info))
            return AssetLoadStatus::Error;

        if (last_time == file_info.modification_time)
            return AssetLoadStatus::Unchanged;
        last_time = file_info.modification_time;
    }

#ifdef _WIN32
    wchar_t filename_w[4096];
    if (ConvertUtf8ToWin32Wide(filename, filename_w) < 0)
        return AssetLoadStatus::Error;

    HMODULE h = LoadLibraryW(filename_w);
    if (!h) {
        LogError("Cannot load library '%1'", filename);
        return AssetLoadStatus::Error;
    }
    RG_DEFER { FreeLibrary(h); };

    lib_assets = (const Span<const AssetInfo> *)GetProcAddress(h, var_name);
#else
    void *h = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        LogError("Cannot load library '%1': %2", filename, dlerror());
        return AssetLoadStatus::Error;
    }
    RG_DEFER { dlclose(h); };

    lib_assets = (const Span<const AssetInfo> *)dlsym(h, var_name);
#endif
    if (!lib_assets) {
        LogError("Cannot find symbol '%1' in library '%2'", var_name, filename);
        return AssetLoadStatus::Error;
    }

    assets.Clear();
    alloc.ReleaseAll();
    for (const AssetInfo &asset: *lib_assets) {
        AssetInfo asset_copy;

        asset_copy.name = DuplicateString(asset.name, &alloc).ptr;
        uint8_t *data_ptr = (uint8_t *)Allocator::Allocate(&alloc, asset.data.len);
        memcpy(data_ptr, asset.data.ptr, (size_t)asset.data.len);
        asset_copy.data = {data_ptr, asset.data.len};
        asset_copy.compression_type = asset.compression_type;
        asset_copy.source_map = DuplicateString(asset.source_map, &alloc).ptr;

        assets.Append(asset_copy);
    }

    return AssetLoadStatus::Loaded;
}

// This won't win any beauty or speed contest (especially when writing
// a compressed stream) but whatever.
Span<const uint8_t> PatchAssetVariables(const AssetInfo &asset, Allocator *alloc,
                                        FunctionRef<bool(const char *, StreamWriter *)> func)
{
    HeapArray<uint8_t> buf(alloc);

    StreamReader reader(asset.data, nullptr, asset.compression_type);
    StreamWriter writer(&buf, nullptr, asset.compression_type);

    char c;
    while (reader.Read(1, &c) == 1) {
        if (c == '{') {
            char name[33] = {};
            Size name_len = reader.Read(1, &name[0]);
            RG_ASSERT(name_len >= 0);

            bool valid = false;
            if (IsAsciiAlpha(name[0]) || name[0] == '_') {
                do {
                    Size read_len = reader.Read(1, &name[name_len]);
                    RG_ASSERT(read_len >= 0);

                    if (name[name_len] == '}') {
                        name[name_len] = 0;
                        valid = func(name, &writer);
                        name[name_len++] = '}';

                        break;
                    } else if (!IsAsciiAlphaOrDigit(name[name_len]) && name[name_len] != '_') {
                        name_len++;
                        break;
                    }
                } while (++name_len < RG_SIZE(name));
            }

            if (!valid) {
                writer.Write('{');
                writer.Write(name, name_len);
            }
        } else {
            writer.Write(c);
        }
    }
    RG_ASSERT(reader.IsValid());

    bool success = writer.Close();
    RG_ASSERT(success);

    return buf.Leak();
}

// ------------------------------------------------------------------------
// Option parser
// ------------------------------------------------------------------------

static inline bool IsOption(const char *arg)
{
    return arg[0] == '-' && arg[1];
}

static inline bool IsLongOption(const char *arg)
{
    return arg[0] == '-' && arg[1] == '-' && arg[2];
}

static inline bool IsDashDash(const char *arg)
{
    return arg[0] == '-' && arg[1] == '-' && !arg[2];
}

const char *OptionParser::Next()
{
    current_option = nullptr;
    current_value = nullptr;

    // Support aggregate short options, such as '-fbar'. Note that this can also be
    // parsed as the short option '-f' with value 'bar', if the user calls
    // ConsumeOptionValue() after getting '-f'.
    if (smallopt_offset) {
        const char *opt = args[pos];

        buf[1] = opt[smallopt_offset];
        current_option = buf;

        if (!opt[++smallopt_offset]) {
            smallopt_offset = 0;
            pos++;
        }

        return current_option;
    }

    // Skip non-options, do the permutation once we reach an option or the last argument
    Size next_index = pos;
    while (next_index < limit && !IsOption(args[next_index])) {
        next_index++;
    }
    if (flags & (int)Flag::SkipNonOptions) {
        pos = next_index;
    } else {
        std::rotate(args.ptr + pos, args.ptr + next_index, args.end());
        limit -= (next_index - pos);
    }
    if (pos >= limit)
        return nullptr;

    const char *opt = args[pos];

    if (IsLongOption(opt)) {
        const char *needle = strchr(opt, '=');
        if (needle) {
            // We can reorder args, but we don't want to change strings. So copy the
            // option up to '=' in our buffer. And store the part after '=' as the
            // current value.
            Size len = needle - opt;
            if (len > RG_SIZE(buf) - 1) {
                len = RG_SIZE(buf) - 1;
            }
            memcpy(buf, opt, (size_t)len);
            buf[len] = 0;
            current_option = buf;
            current_value = needle + 1;
        } else {
            current_option = opt;
        }
        pos++;
    } else if (IsDashDash(opt)) {
        // We may have previously moved non-options to the end of args. For example,
        // at this point 'a b c -- d e' is reordered to '-- d e a b c'. Fix it.
        std::rotate(args.ptr + pos + 1, args.ptr + limit, args.end());
        limit = pos;
        pos++;
    } else if (opt[2]) {
        // We either have aggregated short options or one short option with a value,
        // depending on whether or not the user calls ConsumeOptionValue().
        buf[0] = '-';
        buf[1] = opt[1];
        buf[2] = 0;
        current_option = buf;
        smallopt_offset = opt[2] ? 2 : 0;

        // The main point of SkipNonOptions is to be able to parse arguments in
        // multiple passes. This does not work well with ambiguous short options
        // (such as -oOption, which can be interpeted as multiple one-char options
        // or one -o option with a value), so force the value interpretation.
        if (flags & (int)Flag::SkipNonOptions) {
            ConsumeValue();
        }
    } else {
        current_option = opt;
        pos++;
    }

    return current_option;
}

bool OptionParser::Test(const char *test1, const char *test2, OptionType type)
{
    RG_ASSERT(test1 && IsOption(test1));
    RG_ASSERT(!test2 || IsOption(test2));

    if (TestStr(test1, current_option) || (test2 && TestStr(test2, current_option))) {
        switch (type) {
            case OptionType::NoValue: {
                if (current_value) {
                    LogError("Option '%1' does not support values", current_option);
                    return false;
                }
            } break;
            case OptionType::Value: {
                if (!ConsumeValue()) {
                    LogError("Option '%1' requires a value", current_option);
                    return false;
                }
            } break;
            case OptionType::OptionalValue: {
                ConsumeValue();
            } break;
        }

        return true;
    } else {
        return false;
    }
}

const char *OptionParser::ConsumeValue()
{
    if (current_value)
        return current_value;

    // Support '-fbar' where bar is the value, but only for the first short option
    // if it's an aggregate.
    if (smallopt_offset == 2 && args[pos][2]) {
        smallopt_offset = 0;
        current_value = args[pos] + 2;
        pos++;
    // Support '-f bar' and '--foo bar', see ConsumeOption() for '--foo=bar'
    } else if (current_option != buf && pos < limit && !IsOption(args[pos])) {
        current_value = args[pos];
        pos++;
    }

    return current_value;
}

const char *OptionParser::ConsumeNonOption()
{
    if (pos == args.len)
        return nullptr;
    // Beyond limit there are only non-options, the limit is moved when we move non-options
    // to the end or upon encouteering a double dash '--'.
    if (pos < limit && IsOption(args[pos]))
        return nullptr;

    return args[pos++];
}

void OptionParser::ConsumeNonOptions(HeapArray<const char *> *non_options)
{
    const char *non_option;
    while ((non_option = ConsumeNonOption())) {
        non_options->Append(non_option);
    }
}

// ------------------------------------------------------------------------
// Console prompter (simplified readline)
// ------------------------------------------------------------------------

static bool input_is_raw;
#ifdef _WIN32
static HANDLE stdin_handle;
static DWORD input_orig_mode;
#else
static struct termios input_orig_tio;
#endif

ConsolePrompter::ConsolePrompter()
{
    entries.AppendDefault();
}

static bool EnableRawMode()
{
    static bool init_atexit = false;

    if (!input_is_raw) {
#ifdef _WIN32
        stdin_handle = (HANDLE)_get_osfhandle(_fileno(stdin));

        if (GetConsoleMode(stdin_handle, &input_orig_mode)) {
            input_is_raw = SetConsoleMode(stdin_handle, ENABLE_WINDOW_INPUT);

            if (input_is_raw && !init_atexit) {
                atexit([]() { SetConsoleMode(stdin_handle, input_orig_mode); });
                init_atexit = true;
            }
        }
#else
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &input_orig_tio) >= 0) {
            struct termios raw = input_orig_tio;

            raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            raw.c_oflag &= ~(OPOST);
            raw.c_cflag |= (CS8);
            raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;

            input_is_raw = (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) >= 0);

            if (input_is_raw && !init_atexit) {
                atexit([]() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &input_orig_tio); });
                init_atexit = true;
            }
        }
#endif
    }

    return input_is_raw;
}

static void DisableRawMode()
{
    if (input_is_raw) {
#ifdef _WIN32
        input_is_raw = !SetConsoleMode(stdin_handle, input_orig_mode);
#else
        input_is_raw = !(tcsetattr(STDIN_FILENO, TCSAFLUSH, &input_orig_tio) >= 0);
#endif
    }
}

bool ConsolePrompter::Read()
{
#ifndef _WIN32
    struct sigaction old_sa;
    {
        struct sigaction sa;

        sa.sa_handler = [](int) {};
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction(SIGWINCH, &sa, &old_sa);
    }
    RG_DEFER { sigaction(SIGWINCH, &old_sa, nullptr); };
#endif

    if (!FileIsVt100(stdout) || !EnableRawMode()) {
        int c;
        while ((c = fgetc(stdin)) != EOF) {
            if (c == '\n') {
                str.Append('\n');
                return true;
            } else if (c >= 32 || c == '\t') {
                str.Append(c);
            }
        }

        if (ferror(stdin)) {
            LogError("Failed to read from standard input: %1", strerror(errno));
            return false;
        }

        // EOF
        return false;
    }
    RG_DEFER {
        Print("%!0");
        DisableRawMode();
    };

    // Don't overwrite current line
    fflush(stdout);
    if (GetCursorPosition().x > 0) {
        fputs("\r\n", stdout);
    }

    str_offset = str.len;
    Prompt();

    int32_t uc;
    while ((uc = ReadChar()) >= 0) {
        // Fix display if terminal is resized
        if (GetConsoleSize().x != columns) {
            Prompt();
        }

        switch (uc) {
            case 0x1B: {
                LocalArray<char, 16> buf;

                const auto match_escape = [&](const char *seq) {
                    RG_ASSERT(strlen(seq) < RG_SIZE(buf.data));

                    for (Size i = 0; seq[i]; i++) {
                        if (i >= buf.len) {
                            uc = ReadChar();

                            if (uc >= 128) {
                                // Got some kind of non-ASCII character, make sure nothing else matches
                                buf.Append(0);
                                return false;
                            }

                            buf.Append((char)uc);
                        }
                        if (buf[i] != seq[i])
                            return false;
                    }

                    return true;
                };

                if (match_escape("[1;5D")) { // Ctrl-Left
                    str_offset = FindBackward(str_offset, " \t\r\n");
                    Prompt();
                } else if (match_escape("[1;5C")) { // Ctrl-Right
                    str_offset = FindForward(str_offset, " \t\r\n");
                    Prompt();
                } else if (match_escape("[3~")) { // Delete
                    if (str_offset < str.len) {
                        Delete(str_offset, SkipForward(str_offset, 1));
                        Prompt();
                    }
                } else if (match_escape("\x7F")) { // Alt-Backspace
                    Delete(FindBackward(str_offset, " \t\r\n"), str_offset);
                    Prompt();
                } else if (match_escape("d")) { // Alt-D
                    Delete(str_offset, FindForward(str_offset, " \t\r\n"));
                    Prompt();
                } else if (match_escape("[A")) { // Up
                    fake_input = "\x10";
                } else if (match_escape("[B")) { // Down
                    fake_input = "\x0E";
                } else if (match_escape("[D")) { // Left
                    fake_input = "\x02";
                } else if (match_escape("[C")) { // Right
                    fake_input = "\x06";
                } else if (match_escape("[H")) { // Home
                    fake_input = "\x01";
                } else if (match_escape("[F")) { // End
                    fake_input = "\x05";
                }
            } break;

            case 0x2: { // Left
                if (str_offset > 0) {
                    str_offset = SkipBackward(str_offset, 1);
                    Prompt();
                }
            } break;
            case 0x6: { // Right
                if (str_offset < str.len) {
                    str_offset = SkipForward(str_offset, 1);
                    Prompt();
                }
            } break;
            case 0xE: { // Down
                Span<const char> remain = str.Take(str_offset, str.len - str_offset);
                SplitStr(remain, '\n', &remain);

                if (remain.len) {
                    Span<const char> line = SplitStr(remain, '\n', &remain);

                    Size line_offset = std::min(line.len, (Size)x - prompt_columns);
                    str_offset = std::min(line.ptr - str.ptr + line_offset, str.len);

                    Prompt();
                } else if (entry_idx < entries.len - 1) {
                    ChangeEntry(entry_idx + 1);
                    Prompt();
                }
            } break;
            case 0x10: { // Up
                Span<const char> remain = str.Take(0, str_offset);
                SplitStrReverse(remain, '\n', &remain);

                if (remain.len) {
                    Span<const char> line = SplitStrReverse(remain, '\n', &remain);

                    Size line_offset = std::min(line.len, (Size)x - prompt_columns);
                    str_offset = std::min(line.ptr - str.ptr + line_offset, str.len);

                    Prompt();
                } else if (entry_idx > 0) {
                    ChangeEntry(entry_idx - 1);
                    Prompt();
                }
            } break;

            case 0x1: { // Home
                str_offset = FindBackward(str_offset, "\n");
                Prompt();
            } break;
            case 0x5: { // End
                str_offset = FindForward(str_offset, "\n");
                Prompt();
            } break;

            case 0x8:
            case 0x7F: { // Backspace
                if (str.len) {
                    Delete(SkipBackward(str_offset, 1), str_offset);
                    Prompt();
                }
            } break;
            case 0x3: { // Ctrl-C
                if (str.len) {
                    str.RemoveFrom(0);
                    str_offset = 0;
                    entry_idx = entries.len - 1;
                    entries[entry_idx].RemoveFrom(0);

                    Prompt();
                } else {
                    fputs("\r\n", stdout);
                    fflush(stdout);
                    return false;
                }
            } break;
            case 0x4: { // Ctrl-D
                if (str.len) {
                    Delete(str_offset, SkipForward(str_offset, 1));
                    Prompt();
                } else {
                    return false;
                }
            } break;
            case 0x14: { // Ctrl-T
                Size middle = SkipBackward(str_offset, 1);
                Size start = SkipBackward(middle, 1);

                if (start < middle) {
                    std::rotate(str.ptr + start, str.ptr + middle, str.ptr + str_offset);
                    Prompt();
                }
            } break;
            case 0xB: { // Ctrl-K
                Delete(str_offset, FindForward(str_offset, "\n"));
                Prompt();
            } break;
            case 0x15: { // Ctrl-U
                Delete(FindBackward(str_offset, "\n"), str_offset);
                Prompt();
            } break;
            case 0xC: { // Ctrl-L
                fputs("\x1B[2J\x1B[999A", stdout);
                Prompt();
            } break;

            case '\r':
            case '\n': {
                if (rows > y) {
                    fprintf(stdout, "\x1B[%dB", rows - y);
                }
                fputs("\r\n", stdout);
                fflush(stdout);
                y = rows + 1;

                return true;
            } break;

            default: {
                LocalArray<char, 16> frag;
                if (uc == '\t') {
                    frag.Append("    ");
                } else if (uc >= 32) {
                    frag.len = EncodeUtf8(uc, frag.data);
                } else {
                    continue;
                }

                str.Grow(frag.len);
                memmove(str.ptr + str_offset + frag.len, str.ptr + str_offset, (size_t)(str.len - str_offset));
                memcpy(str.ptr + str_offset, frag.data, (size_t)frag.len);
                str.len += frag.len;
                str_offset += frag.len;

                if (!mask && str_offset == str.len && uc < 128 && x + frag.len < columns) {
                    fwrite(frag.data, 1, (size_t)frag.len, stdout);
                    fflush(stdout);
                    x += (int)frag.len;
                } else {
                    Prompt();
                }
            } break;
        }
    }

    return true;
}

void ConsolePrompter::Commit()
{
    str.len = TrimStrRight((Span<const char>)str, "\r\n").len;

    if (str.len) {
        std::swap(str, entries[entries.len - 1]);
        entries.AppendDefault();
    }
    entry_idx = entries.len - 1;
    str.RemoveFrom(0);
    str_offset = 0;

    rows = 0;
    rows_with_extra = 0;
    x = 0;
    y = 0;
}

void ConsolePrompter::ChangeEntry(Size new_idx)
{
    if (str.len) {
        std::swap(str, entries[entry_idx]);
    }

    str.RemoveFrom(0);
    str.Append(entries[new_idx]);
    str_offset = str.len;
    entry_idx = new_idx;
}

Size ConsolePrompter::SkipForward(Size offset, Size count)
{
    if (offset < str.len) {
        offset++;

        while (offset < str.len && (((str[offset] & 0xC0) == 0x80) || --count)) {
            offset++;
        }
    }

    return offset;
}

Size ConsolePrompter::SkipBackward(Size offset, Size count)
{
    if (offset > 0) {
        offset--;

        while (offset > 0 && (((str[offset] & 0xC0) == 0x80) || --count)) {
            offset--;
        }
    }

    return offset;
}

Size ConsolePrompter::FindForward(Size offset, const char *chars)
{
    while (offset < str.len && strchr(chars, str[offset])) {
        offset++;
    }
    while (offset < str.len && !strchr(chars, str[offset])) {
        offset++;
    }

    return offset;
}

Size ConsolePrompter::FindBackward(Size offset, const char *chars)
{
    if (offset > 0) {
        offset--;

        while (offset > 0 && strchr(chars, str[offset])) {
            offset--;
        }
        while (offset > 0 && !strchr(chars, str[offset - 1])) {
            offset--;
        }
    }

    return offset;
}

void ConsolePrompter::Delete(Size start, Size end)
{
    RG_ASSERT(start >= 0);
    RG_ASSERT(end >= start && end <= str.len);

    memmove(str.ptr + start, str.ptr + end, str.len - end);
    str.len -= end - start;

    if (str_offset > end) {
        str_offset -= end - start;
    } else if (str_offset > start) {
        str_offset = start;
    }
}

void ConsolePrompter::Prompt()
{
    columns = GetConsoleSize().x;
    rows = 0;

    int mask_columns = mask ? ComputeWidth(mask) : 0;
    prompt_columns = ComputeWidth(prompt);

    // Hide cursor during refresh
    fprintf(stdout, "\x1B[?25l");
    if (y) {
        fprintf(stdout, "\x1B[%dA", y);
    }

    // Output prompt(s) and string lines
    {
        Size i = 0;
        int x2 = prompt_columns;

        Print("\r%!0%1%!..+", prompt);

        for (;;) {
            if (i == str_offset) {
                x = x2;
                y = rows;
            }
            if (i >= str.len)
                break;

            Size bytes = std::min((Size)CountUtf8Bytes(str[i]), str.len - i);
            int width = mask ? mask_columns : ComputeWidth(str.Take(i, bytes));

            if (x2 + width >= columns || str[i] == '\n') {
                FmtArg prefix = FmtArg(str[i] == '\n' ? '.' : ' ').Repeat(prompt_columns - 1);
                Print(stdout, "\x1B[0K\r\n%!D.+%1%!0 ", prefix);

                x2 = prompt_columns;
                rows++;
            }
            if (width > 0) {
                if (mask) {
                    fputs(mask, stdout);
                } else {
                    fwrite(str.ptr + i, 1, (size_t)bytes, stdout);
                }
            }

            x2 += width;
            i += bytes;
        }
        fputs("\x1B[0K", stdout);
    }

    // Clear remaining rows
    for (int i = rows; i < rows_with_extra; i++) {
        fprintf(stdout, "\r\n\x1B[0K");
    }
    rows_with_extra = std::max(rows_with_extra, rows);

    // Fix up cursor and show it
    if (rows_with_extra > y) {
        fprintf(stdout, "\x1B[%dA", rows_with_extra - y);
    }
    fprintf(stdout, "\r\x1B[%dC", x);
    fprintf(stdout, "\x1B[?25h");

    fflush(stdout);
}

Vec2<int> ConsolePrompter::GetConsoleSize()
{
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stdout));

    CONSOLE_SCREEN_BUFFER_INFO screen;
    if (GetConsoleScreenBufferInfo(h, &screen))
        return {screen.dwSize.X, screen.dwSize.Y};
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) >= 0 && ws.ws_col)
        return {ws.ws_col, ws.ws_row};
#endif

    // Give up!
    return {80, 24};
}

Vec2<int> ConsolePrompter::GetCursorPosition()
{
#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stdout));

    CONSOLE_SCREEN_BUFFER_INFO screen;
    if (!GetConsoleScreenBufferInfo(h, &screen))
        return {};

    return {screen.dwCursorPosition.X, screen.dwCursorPosition.Y};
#else
    fputs("\x1B[6n", stdout);
    fflush(stdout);

    LocalArray<char, 64> buf;
    while (buf.Available() > 1 && read(STDIN_FILENO, buf.end(), 1) == 1 && buf.data[buf.len] != 'R') {
        buf.len++;
    }
    buf.Append(0);

    int v = 1, h = 1;
    sscanf(buf.data, "\x1B[%d;%d", &v, &h);

    return {h - 1, v - 1};
#endif
}

int32_t ConsolePrompter::ReadChar()
{
    if (fake_input[0]) {
        int c = fake_input[0];
        fake_input++;
        return c;
    }

#ifdef _WIN32
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stdin));

    for (;;) {
        INPUT_RECORD ev;
        DWORD ev_len;
        if (!ReadConsoleInputW(h, &ev, 1, &ev_len))
            return -1;
        if (!ev_len)
            return -1;

        if (ev.EventType == KEY_EVENT && ev.Event.KeyEvent.bKeyDown) {
            bool ctrl = ev.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
            bool alt = ev.Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);

            if (ctrl && !alt) {
                switch (ev.Event.KeyEvent.wVirtualKeyCode) {
                    case 'A': { return 0x1; } break;
                    case 'B': { return 0x2; } break;
                    case 'C': { return 0x3; } break;
                    case 'D': { return 0x4; } break;
                    case 'E': { return 0x5; } break;
                    case 'F': { return 0x6; } break;
                    case 'H': { return 0x8; } break;
                    case 'K': { return 0xB; } break;
                    case 'L': { return 0xC; } break;
                    case 'N': { return 0xE; } break;
                    case 'P': { return 0x10; } break;
                    case 'T': { return 0x14; } break;
                    case 'U': { return 0x15; } break;
                    case VK_LEFT: {
                        fake_input = "[1;5D";
                        return 0x1B;
                    } break;
                    case VK_RIGHT: {
                        fake_input = "[1;5C";
                        return 0x1B;
                    } break;
                }
            } else {
                if (alt) {
                    switch (ev.Event.KeyEvent.wVirtualKeyCode) {
                        case VK_BACK: {
                            fake_input = "\x7F";
                            return 0x1B;
                        } break;
                        case 'D': {
                            fake_input = "d";
                            return 0x1B;
                        } break;
                    }
                }

                switch (ev.Event.KeyEvent.wVirtualKeyCode) {
                    case VK_UP: { return 0x10; } break;
                    case VK_DOWN: { return 0xE; } break;
                    case VK_LEFT: { return 0x2; } break;
                    case VK_RIGHT: { return 0x6; } break;
                    case VK_HOME: { return 0x1; } break;
                    case VK_END: { return 0x5; } break;
                    case VK_RETURN: { return '\r'; } break;
                    case VK_BACK: { return 0x8; } break;
                    case VK_DELETE: {
                        fake_input = "[3~";
                        return 0x1B;
                    } break;

                    default: {
                        uint32_t uc = ev.Event.KeyEvent.uChar.UnicodeChar;

                        if ((uc - 0xD800u) < 0x800u) {
                            if ((uc & 0xFC00u) == 0xD800u) {
                                surrogate_buf = uc;
                                return 0;
                            } else if (surrogate_buf && (uc & 0xFC00) == 0xDC00) {
                                uc = (surrogate_buf << 10) + uc - 0x35FDC00;
                            } else {
                                // Yeah something is up. Give up on this character.
                                surrogate_buf = 0;
                                return 0;
                            }
                        }

                        return (int32_t)uc;
                    } break;
                }
            }
        } else if (ev.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            return 0;
        }
    }
#else
    int32_t uc = fgetc(stdin);
    if (uc < 0)
        goto eof;

    if (uc >= 128) {
        Size bytes = CountUtf8Bytes((char)uc);

        LocalArray<char, 4> buf;
        buf.Append((char)uc);
        buf.len += fread(buf.end(), 1, bytes - 1, stdin);
        if (buf.len < 1)
            goto eof;

        if (buf.len != bytes)
            return 0;
        if (DecodeUtf8(buf, 0, &uc) != bytes)
            return 0;
    }

    return uc;

eof:
    if (ferror(stdin)) {
        if (errno == EINTR) {
            // Could be SIGWINCH, give the user a chance to deal with it
            return 0;
        } else {
            LogError("Failed to read from standard input: %1", strerror(errno));
            return -1;
        }
    } else {
        // EOF
        return -1;
    }
#endif
}

int ConsolePrompter::ComputeWidth(Span<const char> str)
{
    int width = 0;

    for (char c: str) {
        // XXX: For now we assume all codepoints take a single column,
        // we need to use something like wcwidth() instead.
        width += ((uint8_t)c >= 32 && !((c & 0xC0) == 0x80));
    }

    return width;
}

const char *Prompt(const char *prompt, const char *mask, Allocator *alloc)
{
    ConsolePrompter prompter;
    prompter.prompt = prompt;
    prompter.mask = mask;

    if (!prompter.Read())
        return nullptr;

    const char *str = DuplicateString(prompter.str, alloc).ptr;
    return str;
}

}
