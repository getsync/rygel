#include <inttypes.h>
#include <string.h>
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
#else
    #include <dirent.h>
    #include <fcntl.h>
    #include <fnmatch.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

#include "kutil.hh"

// ------------------------------------------------------------------------
// Memory / Allocator
// ------------------------------------------------------------------------

// This Allocator design should allow efficient and mostly-transparent use of memory
// arenas and simple pointer-bumping allocator. This will be implemented later, for
// now it's just a doubly linked list of malloc() memory blocks.

static Allocator default_allocator;

#define PTR_TO_BUCKET(Ptr) \
    ((AllocatorBucket *)((uint8_t *)(Ptr) - OffsetOf(AllocatorBucket, data)))

Allocator::~Allocator()
{
    if (LIKELY(this != &default_allocator)) {
        ReleaseAll();
    }
}

void Allocator::ReleaseAll(Allocator *alloc)
{
    if (!alloc) {
        alloc = &default_allocator;
    }
    alloc->ReleaseAll();
}

void *Allocator::Allocate(Allocator *alloc, size_t size, unsigned int flags)
{
    if (!alloc) {
        alloc = &default_allocator;
    }
    return alloc->Allocate(size, flags);
}

void Allocator::Resize(Allocator *alloc, void **ptr, size_t old_size, size_t new_size,
                       unsigned int flags)
{
    if (!alloc) {
        alloc = &default_allocator;
    }
    alloc->Resize(ptr, old_size, new_size, flags);
}

void Allocator::Release(Allocator *alloc, void *ptr, size_t size)
{
    if (!alloc) {
        alloc = &default_allocator;
    }
    alloc->Release(ptr, size);
}

void *Allocator::Allocate(size_t size, unsigned int flags)
{
    if (!size)
        return nullptr;

    AllocatorBucket *bucket = (AllocatorBucket *)malloc(sizeof(*bucket) + size);
    if (!bucket) {
        LogError("Failed to allocate %1 of memory", FmtMemSize(size));
        abort();
    }
    list.prev->next = &bucket->head;
    bucket->head.prev = list.prev;
    list.prev = &bucket->head;
    bucket->head.next = &list;

    if (flags & (int)Flag::Zero) {
        memset(bucket->data, 0, size);
    }

    return bucket->data;
}

void Allocator::Resize(void **ptr, size_t old_size, size_t new_size, unsigned int flags)
{
    if (!*ptr) {
        *ptr = Allocate(new_size, flags | (int)Flag::Resizable);
        return;
    }
    if (!new_size) {
        Release(*ptr, old_size);
        *ptr = nullptr;
        return;
    }

    AllocatorBucket *bucket = PTR_TO_BUCKET(*ptr);
    AllocatorBucket *new_bucket = (AllocatorBucket *)realloc(bucket, sizeof(*new_bucket) + new_size);
    if (!new_bucket) {
        LogError("Failed to resize %1 memory block to %2",
                 FmtMemSize(old_size), FmtMemSize(new_size));
        abort();
    }
    new_bucket->head.prev->next = &new_bucket->head;
    new_bucket->head.next->prev = &new_bucket->head;
    *ptr = new_bucket->data;

    if (flags & (int)Flag::Zero && new_size > old_size) {
        memset(new_bucket->data + old_size, 0, new_size - old_size);
    }
}

void Allocator::Release(void *ptr, size_t)
{
    if (!ptr)
        return;

    AllocatorBucket *bucket = PTR_TO_BUCKET(ptr);
    bucket->head.next->prev = bucket->head.prev;
    bucket->head.prev->next = bucket->head.next;
    free(bucket);
}

void Allocator::ReleaseAll()
{
    AllocatorList *head = list.next;
    while (head != &list) {
        AllocatorList *next = head->next;
        free(head);
        head = next;
    }
    list = { &list, &list };
}

// ------------------------------------------------------------------------
// Date
// ------------------------------------------------------------------------

Date Date::FromString(const char *date_str, bool strict)
{
    Date date = {};

GCC_PUSH_IGNORE(-Wformat-nonliteral)
    int parts[3];
    const auto TryFormat = [&](const char *format) {
        int end_offset;
        int parts_count = sscanf(date_str, format, &parts[0], &parts[1], &parts[2], &end_offset);
        return parts_count == 3 && !date_str[end_offset];
    };
    if (!TryFormat("%6d-%2u-%2u%n")) {
        if (!TryFormat("%6d/%2u/%2u%n")) {
            LogError("Malformed date string '%1'", date_str);
            return date;
        }
    }
GCC_POP_IGNORE()

    if (parts[2] >= 100 || parts[2] <= -100) {
        std::swap(parts[0], parts[2]);
    } else if (parts[0] < 100 && parts[0] > -100) {
        LogError("Ambiguous date string '%1'", date_str);
        return date;
    }
    if (parts[0] > UINT16_MAX || parts[1] > UINT8_MAX || parts[2] > UINT8_MAX) {
        LogError("Invalid date string '%1'", date_str);
        return date;
    }

    date.st.year = (int16_t)parts[0];
    date.st.month = (uint8_t)parts[1];
    date.st.day = (uint8_t)parts[2];
    if (strict && !date.IsValid()) {
        LogError("Invalid date string '%1'", date_str);
        date.value = 0;
    }

    return date;
}

Date Date::FromJulianDays(int days)
{
    DebugAssert(days >= 0);

    Date date;
    {
        // Algorithm from Richards, copied from Wikipedia:
        // https://en.wikipedia.org/w/index.php?title=Julian_day&oldid=792497863
        int f = days + 1401 + (((4 * days + 274277) / 146097) * 3) / 4 - 38;
        int e = 4 * f + 3;
        int g = e % 1461 / 4;
        int h = 5 * g + 2;
        date.st.day = h % 153 / 5 + 1;
        date.st.month = (h / 153 + 2) % 12 + 1;
        date.st.year = (e / 1461) - 4716 + (date.st.month < 3);
    }

    return date;
}

int Date::ToJulianDays() const
{
    DebugAssert(IsValid());

    int julian_days;
    {
        // Straight from the Web:
        // http://www.cs.utsa.edu/~cs1063/projects/Spring2011/Project1/jdn-explanation.html
        bool adjust = st.month < 3;
        int year = st.year + 4800 - adjust;
        int month = st.month + 12 * adjust - 3;
        julian_days = st.day + (153 * month + 2) / 5 + 365 * year - 32045 +
                      year / 4 - year / 100 + year / 400;
    }

    return julian_days;
}

Date &Date::operator++()
{
    DebugAssert(IsValid());

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
    DebugAssert(IsValid());

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

uint64_t start_time = GetMonotonicTime();

#ifdef _WIN32

uint64_t GetMonotonicTime()
{
    return GetTickCount64();
}

#else

uint64_t GetMonotonicTime()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        LogError("clock_gettime() failed: %1", strerror(errno));
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 10000000;
}

#endif

// ------------------------------------------------------------------------
// Strings
// ------------------------------------------------------------------------

char *MakeString(Allocator *alloc, ArrayRef<const char> bytes)
{
    char *str = (char *)Allocator::Allocate(alloc, bytes.len + 1);
    memcpy(str, bytes.ptr, bytes.len);
    str[bytes.len] = 0;
    return str;
}

char *DuplicateString(Allocator *alloc, const char *str, size_t max_len)
{
    size_t str_len = strlen(str);
    if (str_len > max_len) {
        str_len = max_len;
    }
    char *new_str = (char *)Allocator::Allocate(alloc, str_len + 1);
    memcpy(new_str, str, str_len);
    new_str[str_len] = 0;
    return new_str;
}

template <typename AppendFunc>
static inline void WriteUnsignedAsDecimal(uint64_t value, AppendFunc append)
{
    static const char literals[] = "0123456789";

    char buf[32];
    size_t len = sizeof(buf);
    do {
        uint64_t digit = value % 10;
        value /= 10;
        buf[--len] = literals[digit];
    } while (value);

    append(ArrayRef<const char>(buf + len, sizeof(buf) - len));
}

template <typename AppendFunc>
static inline void WriteUnsignedAsHex(uint64_t value, AppendFunc append)
{
    static const char literals[] = "0123456789ABCDEF";

    char buf[32];
    size_t len = sizeof(buf);
    do {
        uint64_t digit = value & 0xF;
        value >>= 4;
        buf[--len] = literals[digit];
    } while (value);

    append(ArrayRef<const char>(buf + len, sizeof(buf) - len));
}

template <typename AppendFunc>
static inline void WriteUnsignedAsBinary(uint64_t value, AppendFunc append)
{
    char buf[64];
    size_t msb = 64 - (size_t)CountLeadingZeros(value);
    for (size_t i = 0; i < msb; i++) {
        bool bit = (value >> (msb - i - 1)) & 0x1;
        buf[i] = bit ? '1' : '0';
    }

    append(ArrayRef<const char>(buf, msb));
}

template <typename AppendFunc>
static inline void WriteDouble(double value, int precision, AppendFunc append)
{
    char buf[256];
    // That's the lazy way to do it, it'll do for now
    int buf_len;
    if (precision >= 0) {
        buf_len = snprintf(buf, sizeof(buf), "%.*f", precision, value);
    } else {
        buf_len = snprintf(buf, sizeof(buf), "%g", value);
    }
    Assert(buf_len >= 0 && (size_t)buf_len <= sizeof(buf));

    append(MakeArrayRef(buf, (size_t)buf_len));
}

template <typename AppendFunc>
static inline void ProcessArg(const FmtArg &arg, AppendFunc append)
{
    for (int i = 0; i < arg.repeat; i++) {
        switch (arg.type) {
            case FmtArg::Type::StrRef: {
                append(arg.value.str_ref);
            } break;

            case FmtArg::Type::StrBuf: {
                append(MakeStrRef(arg.value.str_buf));
            } break;

            case FmtArg::Type::Char: {
                append(ArrayRef<const char>(&arg.value.ch, 1));
            } break;

            case FmtArg::Type::Bool: {
                if (arg.value.b) {
                    append(MakeStrRef("true"));
                } else {
                    append(MakeStrRef("false"));
                }
            } break;

            case FmtArg::Type::Integer: {
                if (arg.value.i < 0) {
                    append(MakeStrRef("-"));
                    WriteUnsignedAsDecimal((uint64_t)-arg.value.i, append);
                } else {
                    WriteUnsignedAsDecimal((uint64_t)arg.value.i, append);
                }
            } break;

            case FmtArg::Type::Unsigned: {
                WriteUnsignedAsDecimal(arg.value.u, append);
            } break;

            case FmtArg::Type::Double: {
                WriteDouble(arg.value.d.value, arg.value.d.precision, append);
            } break;

            case FmtArg::Type::Binary: {
                if (arg.value.u) {
                    append(MakeStrRef("0b"));
                    WriteUnsignedAsBinary(arg.value.u, append);
                } else {
                    append('0');
                }
            } break;

            case FmtArg::Type::Hexadecimal: {
                if (arg.value.u) {
                    append(MakeStrRef("0x"));
                    WriteUnsignedAsHex(arg.value.u, append);
                } else {
                    append('0');
                }
            } break;

            case FmtArg::Type::MemorySize: {
                if (arg.value.size > 1024 * 1024) {
                    double len_mib = (double)arg.value.size / (1024.0 * 1024.0);
                    WriteDouble(len_mib, 2, append);
                    append(MakeStrRef(" MiB"));
                } else if (arg.value.size > 1024) {
                    double len_kib = (double)arg.value.size / 1024.0;
                    WriteDouble(len_kib, 2, append);
                    append(MakeStrRef(" kiB"));
                } else {
                    WriteUnsignedAsDecimal(arg.value.size, append);
                    append(MakeStrRef(" B"));
                }
            } break;

            case FmtArg::Type::DiskSize: {
                if (arg.value.size > 1000 * 1000) {
                    double len_mb = (double)arg.value.size / (1000.0 * 1000.0);
                    WriteDouble(len_mb, 2, append);
                    append(MakeStrRef(" MB"));
                } else if (arg.value.size > 1000) {
                    double len_kb = (double)arg.value.size / 1000.0;
                    WriteDouble(len_kb, 2, append);
                    append(MakeStrRef(" kB"));
                } else {
                    WriteUnsignedAsDecimal(arg.value.size, append);
                    append(MakeStrRef(" B"));
                }
            } break;

            case FmtArg::Type::Date: {
                DebugAssert(arg.value.date.IsValid());
                int year = arg.value.date.st.year;
                if (year < 0) {
                    append(MakeStrRef("-"));
                    year = -year;
                }
                if (year < 10) {
                    append(MakeStrRef("000"));
                } else if (year < 100) {
                    append(MakeStrRef("00"));
                } else if (year < 1000) {
                    append(MakeStrRef("0"));
                }
                WriteUnsignedAsDecimal(year, append);
                append(MakeStrRef("-"));
                if (arg.value.date.st.month < 10) {
                    append(MakeStrRef("0"));
                }
                WriteUnsignedAsDecimal(arg.value.date.st.month, append);
                append(MakeStrRef("-"));
                if (arg.value.date.st.day < 10) {
                    append(MakeStrRef("0"));
                }
                WriteUnsignedAsDecimal(arg.value.date.st.day, append);
            } break;

            case FmtArg::Type::List: {
                if (arg.value.list.args.len) {
                    ProcessArg(arg.value.list.args[0], append);
                    ArrayRef<const char> separator = MakeStrRef(arg.value.list.separator);
                    for (size_t j = 1; j < arg.value.list.args.len; j++) {
                        append(separator);
                        ProcessArg(arg.value.list.args[j], append);
                    }
                }
            } break;
        }
    }
}

template <typename AppendFunc>
static inline void DoFormat(const char *fmt, ArrayRef<const FmtArg> args,
                            AppendFunc append)
{
#ifndef NDEBUG
    bool invalid_marker = false;
    uint32_t unused_arguments = (1 << args.len) - 1;
#endif

    const char *fmt_ptr = fmt;
    for (;;) {
        // Find the next marker (or the end of string) and write everything before it
        const char *marker_ptr = fmt_ptr;
        while (marker_ptr[0] && marker_ptr[0] != '%') {
            marker_ptr++;
        }
        append(ArrayRef<const char>(fmt_ptr, (size_t)(marker_ptr - fmt_ptr)));
        if (!marker_ptr[0])
            break;

        // Try to interpret this marker as a number
        size_t idx = 0;
        size_t idx_end = 1;
        for (;;) {
            // Unsigned cast makes the test below quicker, don't remove it or it'll break
            unsigned int digit = (unsigned int)marker_ptr[idx_end] - '0';
            if (digit > 9)
                break;
            idx = (idx * 10) + digit;
            idx_end++;
        }

        // That was indeed a number
        if (idx_end > 1) {
            idx--;
            if (idx < args.len) {
                ProcessArg<AppendFunc>(args[idx], append);
#ifndef NDEBUG
                unused_arguments &= ~(1 << idx);
            } else {
                invalid_marker = true;
#endif
            }
            fmt_ptr = marker_ptr + idx_end;
        } else if (marker_ptr[1] == '%') {
            append('%');
            fmt_ptr = marker_ptr + 2;
        } else if (marker_ptr[1] == '/') {
            append(*PATH_SEPARATORS);
            fmt_ptr = marker_ptr + 2;
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

size_t FmtString(ArrayRef<char> buf, const char *fmt, ArrayRef<const FmtArg> args)
{
    size_t real_len = 0;

    DoFormat(fmt, args, [&](ArrayRef<const char> fragment) {
        if (real_len < buf.len) {
            size_t copy_len = fragment.len;
            if (copy_len > buf.len - real_len) {
                copy_len = buf.len - real_len;
            }
            memcpy(buf.ptr + real_len, fragment.ptr, copy_len);
        }
        real_len += fragment.len;
    });
    if (real_len < buf.len) {
        buf[real_len] = 0;
    } else if (buf.len) {
        buf[buf.len - 1] = 0;
    }
    real_len++;

    return real_len;
}

char *FmtString(Allocator *alloc, const char *fmt, ArrayRef<const FmtArg> args)
{
    char *buf = (char *)Allocator::Allocate(alloc, FMT_STRING_BASE_CAPACITY,
                                            (int)Allocator::Flag::Resizable);
    size_t buf_len = 0;
    // Cheat a little bit to make room for the NUL byte
    size_t buf_capacity = FMT_STRING_BASE_CAPACITY - 1;

    DoFormat(fmt, args, [&](ArrayRef<const char> fragment) {
        // Same thing, use >= and <= to make sure we have enough place for the NUL byte
        if (fragment.len >= buf_capacity - buf_len) {
            size_t new_capacity = buf_capacity;
            do {
                new_capacity = (size_t)(new_capacity * FMT_STRING_GROWTH_FACTOR);
            } while (fragment.len <= new_capacity - buf_len);
            Allocator::Resize(alloc, (void **)&buf, buf_capacity, new_capacity);
            buf_capacity = new_capacity;
        }
        memcpy(buf + buf_len, fragment.ptr, fragment.len);
        buf_len += fragment.len;
    });
    buf[buf_len] = 0;

    return buf;
}

void FmtPrint(FILE *fp, const char *fmt, ArrayRef<const FmtArg> args)
{
    LocalArray<char, FMT_STRING_PRINT_BUFFER_SIZE> buf;
    DoFormat(fmt, args, [&](ArrayRef<const char> fragment) {
        if (fragment.len > CountOf(buf.data) - buf.len) {
            fwrite(buf.data, 1, buf.len, fp);
            buf.len = 0;
        }
        if (fragment.len >= CountOf(buf.data)) {
            fwrite(fragment.ptr, 1, fragment.len, fp);
        } else {
            memcpy(buf.data + buf.len, fragment.ptr, fragment.len);
            buf.len += fragment.len;
        }
    });
    fwrite(buf.data, 1, buf.len, fp);
}

// ------------------------------------------------------------------------
// Debug and errors
// ------------------------------------------------------------------------

static LocalArray<std::function<void(FILE *)>, 16> log_handlers;

static bool ConfigTerminalOutput()
{
    static bool init, output_is_terminal;

    if (!init) {
#ifdef _WIN32
        static HANDLE stdout_handle;
        static DWORD orig_console_mode;

        stdout_handle = (HANDLE)_get_osfhandle(_fileno(stdout));
        output_is_terminal = GetConsoleMode(stdout_handle, &orig_console_mode);
        if (output_is_terminal && !(orig_console_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            // Enable VT100 escape sequences, introduced in Windows 10
            DWORD new_mode = orig_console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            output_is_terminal = SetConsoleMode(stdout_handle, new_mode);
            atexit([]() {
                SetConsoleMode(stdout_handle, orig_console_mode);
            });
        }
#else
        int stdout_fd = fileno(stdout);
        output_is_terminal = isatty(stdout_fd);
#endif

        init = true;
    }

    return output_is_terminal;
}

void FmtLog(LogLevel level, const char *ctx, const char *fmt, ArrayRef<const FmtArg> args)
{
    FILE *fp = stderr;
    const char *end_marker = nullptr;

    if (ConfigTerminalOutput()) {
        switch (level)  {
            case LogLevel::Error: {
                fputs("\x1B[31m", fp);
                end_marker = "\x1B[0m";
            } break;
            case LogLevel::Info: break;
            case LogLevel::Debug: {
                fputs("\x1B[36m", fp);
                end_marker = "\x1B[0m";
            } break;
        }
    }

    {
        double time = (double)(GetMonotonicTime() - start_time) / 1000;
        size_t ctx_len = strlen(ctx);
        if (ctx_len > 20) {
            fprintf(fp, " ...%s [%8.3f]  ", ctx + ctx_len - 17, time);
        } else {
            fprintf(fp, "%21s [%8.3f]  ", ctx, time);
        }
    }
    if (log_handlers.len) {
        log_handlers[log_handlers.len - 1](fp);
    }
    FmtPrint(fp, fmt, args);
    if (end_marker) {
        fputs(end_marker, fp);
    }
    fputc('\n', fp);
}

void PushLogHandler(std::function<void(FILE *)> handler)
{
    log_handlers.Append(handler);
}

void PopLogHandler()
{
    DebugAssert(log_handlers.len > 0);
    log_handlers.RemoveLast();
}

// ------------------------------------------------------------------------
// System
// ------------------------------------------------------------------------

bool ReadFile(Allocator *alloc, const char *filename, size_t max_size,
              ArrayRef<uint8_t> *out_data)
{
    FILE *fp = fopen(filename, "rb" FOPEN_COMMON_FLAGS);
    if (!fp) {
        LogError("Cannot open '%1': %2", filename, strerror(errno));
        return false;
    }
    DEFER { fclose(fp); };

    ArrayRef<uint8_t> data;
    fseek(fp, 0, SEEK_END);
    data.len = ftell(fp);
    if (data.len > max_size) {
        LogError("File '%1' is too large (limit = %2)", filename, FmtDiskSize(max_size));
        return false;
    }
    fseek(fp, 0, SEEK_SET);

    data.ptr = (uint8_t *)Allocator::Allocate(alloc, data.len);
    DEFER_N(data_guard) { Allocator::Release(alloc, data.ptr, data.len); };
    if (fread(data.ptr, 1, data.len, fp) != data.len || ferror(fp)) {
        LogError("Error while reading file '%1'", filename);
        return false;
    }

    data_guard.disable();
    *out_data = data;
    return true;
}

#ifdef _WIN32

static char *Win32ErrorString(DWORD error_code = UINT32_MAX)
{
    if (error_code == UINT32_MAX) {
        error_code = GetLastError();
    }

    static thread_local char str_buf[256];
    DWORD fmt_ret = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                  str_buf, sizeof(str_buf), nullptr);
    if (fmt_ret) {
        char *str_end = str_buf + strlen(str_buf);
        // FormatMessage adds newlines, remove them
        while (str_end > str_buf && (str_end[-1] == '\n' || str_end[-1] == '\r'))
            str_end--;
        *str_end = 0;
    } else {
        strcpy(str_buf, "(unknown)");
    }

    return str_buf;
}

EnumStatus EnumerateDirectory(const char *dirname, const char *filter,
                              std::function<bool(const char *, const FileInfo &)> func)
{
    char find_filter[4096];
    if (!filter) {
        filter = "*";
    }
    if (snprintf(find_filter, sizeof(find_filter), "%s\\%s", dirname, filter)
            >= (int)sizeof(find_filter)) {
        LogError("Cannot enumerate directory '%1': Path too long", dirname);
        return EnumStatus::Error;
    }

    WIN32_FIND_DATA find_data;
    HANDLE handle = FindFirstFileEx(find_filter, FindExInfoBasic, &find_data,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        LogError("Cannot enumerate directory '%1': %2", dirname,
                 Win32ErrorString());
        return EnumStatus::Error;
    }
    DEFER { FindClose(handle); };

    do {
        FileInfo file_info;

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            file_info.type = FileType::Directory;
        } else {
            file_info.type = FileType::File;
        }

        if (!func(find_data.cFileName, file_info))
            return EnumStatus::Partial;
    } while (FindNextFile(handle, &find_data));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        LogError("Error while enumerating directory '%1': %2", dirname,
                 Win32ErrorString());
        return EnumStatus::Error;
    }

    return EnumStatus::Done;
}

#else

EnumStatus EnumerateDirectory(const char *dirname, const char *filter,
                              std::function<bool(const char *, const FileInfo &)> func)
{
    DIR *dirp = opendir(dirname);
    if (!dirp) {
        LogError("Cannot enumerate directory '%1': %2", dirname, strerror(errno));
        return EnumStatus::Error;
    }
    DEFER { closedir(dirp); };

    dirent *dent;
    while ((dent = readdir(dirp))) {
        if ((dent->d_name[0] == '.' && !dent->d_name[1]) ||
                (dent->d_name[0] == '.' && dent->d_name[1] == '.' && !dent->d_name[2]))
            continue;

        if (!fnmatch(filter, dent->d_name, FNM_PERIOD)) {
            FileInfo file_info;

#ifdef _DIRENT_HAVE_D_TYPE
            if (dent->d_type != DT_UNKNOWN && dent->d_type != DT_LNK) {
                switch (dent->d_type) {
                    case DT_DIR: { file_info.type = FileType::Directory; } break;
                    case DT_REG: { file_info.type = FileType::File; } break;
                    default: { file_info.type = FileType::Special; } break;
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
                    file_info.type = FileType::Directory;
                } else if (S_ISREG(sb.st_mode)) {
                    file_info.type = FileType::File;
                } else {
                    file_info.type = FileType::Special;
                }
            }

            if (!func(dent->d_name, file_info))
                return EnumStatus::Partial;
        }
    }

    if (errno) {
        LogError("Error while enumerating directory '%1': %2", dirname, strerror(errno));
        return EnumStatus::Error;
    }

    return EnumStatus::Done;
}

#endif

bool EnumerateDirectoryFiles(const char *dirname, const char *filter, Allocator &str_alloc,
                             HeapArray<const char *> *out_files, size_t max_files)
{
    Assert(max_files > 0);

    DEFER_NC(out_guard, len = out_files->len) { out_files->RemoveFrom(len); };

    EnumStatus status = EnumerateDirectory(dirname, filter,
                                           [&](const char *filename, const FileInfo &info) {
        if (info.type == FileType::File) {
            out_files->Append(Fmt(&str_alloc, "%1%/%2", dirname, filename));
        }
        return true;
    });
    if (status == EnumStatus::Error)
        return false;

    if (status == EnumStatus::Partial) {
        LogError("Partial enumeration of directory '%1'", dirname);
    }

    out_guard.disable();
    return true;
}

// ------------------------------------------------------------------------
// Option Parser
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

static void ReverseArgs(const char **args, size_t start, size_t end)
{
    for (size_t i = 0; i < (end - start) / 2; i++) {
        const char *tmp = args[start + i];
        args[start + i] = args[end - i - 1];
        args[end - i - 1] = tmp;
    }
}

static void RotateArgs(const char **args, size_t start, size_t mid, size_t end)
{
    if (start == mid || mid == end)
        return;

    ReverseArgs(args, start, mid);
    ReverseArgs(args, mid, end);
    ReverseArgs(args, start, end);
}

const char *OptionParser::ConsumeOption()
{
    size_t next_index;
    const char *opt;

    current_option = nullptr;
    current_value = nullptr;

    // Support aggregate short options, such as '-fbar'. Note that this can also be
    // parsed as the short option '-f' with value 'bar', if the user calls
    // ConsumeOptionValue() after getting '-f'.
    if (smallopt_offset) {
        opt = args[pos];
        smallopt_offset++;
        if (opt[smallopt_offset]) {
            buf[1] = opt[smallopt_offset];
            current_option = buf;
            return current_option;
        } else {
            smallopt_offset = 0;
            pos++;
        }
    }

    // Skip non-options, do the permutation once we reach an option or the last argument
    next_index = pos;
    while (next_index < limit && !IsOption(args[next_index])) {
        next_index++;
    }
    RotateArgs(args.ptr, pos, next_index, args.len);
    limit -= (next_index - pos);
    if (pos >= limit)
        return nullptr;
    opt = args[pos];

    if (IsLongOption(opt)) {
        const char *needle = strchr(opt, '=');
        if (needle) {
            // We can reorder args, but we don't want to change strings. So copy the
            // option up to '=' in our buffer. And store the part after '=' as the
            // current value.
            size_t len = (size_t)(needle - opt);
            if (len > sizeof(buf) - 1) {
                len = sizeof(buf) - 1;
            }
            memcpy(buf, opt, len);
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
        RotateArgs(args.ptr, pos + 1, limit, args.len);
        limit = pos;
        pos++;
    } else if (opt[2]) {
        // We either have aggregated short options or one short option with a value,
        // depending on whether or not the user calls ConsumeOptionValue().
        buf[0] = '-';
        buf[1] = opt[1];
        buf[2] = 0;
        current_option = buf;
        smallopt_offset = 1;
    } else {
        current_option = opt;
        pos++;
    }

    return current_option;
}

const char *OptionParser::ConsumeOptionValue()
{
    if (current_value)
        return current_value;

    const char *arg = args[pos];

    // Support '-fbar' where bar is the value, but only for the first short option
    // if it's an aggregate.
    if (smallopt_offset == 1 && arg[2]) {
        smallopt_offset = 0;
        current_value = arg + 2;
        pos++;
    // Support '-f bar' and '--foo bar', see ConsumeOption() for '--foo=bar'
    } else if (!smallopt_offset && pos < args.len && !IsOption(arg)) {
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

const char *OptionParser::RequireOptionValue(const char *usage_str)
{
    if (!ConsumeOptionValue()) {
        PrintLn(stderr, "Option '%1' needs an argument", current_option);
        if (usage_str) {
            PrintLn(stderr, "%1", usage_str);
        }
    }

    return current_value;
}
