#define PACKAGE "libmicrohttpd"
#define PACKAGE_BUGREPORT "libmicrohttpd@gnu.org"
#define PACKAGE_NAME "GNU Libmicrohttpd"
#define PACKAGE_STRING "GNU Libmicrohttpd 0.9.68"
#define PACKAGE_TARNAME "libmicrohttpd"
#define PACKAGE_URL "http://www.gnu.org/software/libmicrohttpd/"
#define PACKAGE_VERSION "0.9.68"
#define VERSION "0.9.68"

#if defined(__MINGW32__)
    #define BAUTH_SUPPORT 1
    #define DAUTH_SUPPORT 1
    #define HAVE_ASSERT 1
    #define HAVE_C_VARARRAYS 1
    #define HAVE_CALLOC 1
    #define HAVE_CLOCK_GETTIME 1
    #define HAVE_DECL_SOCK_NONBLOCK 0
    #define HAVE_ERRNO_H 1
    #define HAVE_FCNTL_H 1
    #define HAVE_FSEEKO 1
    #define HAVE_GETTIMEOFDAY 1
    #define HAVE_INET6 1
    #define HAVE_INTTYPES_H 1
    #define HAVE_LIMITS_H 1
    #define HAVE_LOCALE_H 1
    #define HAVE_LSEEK64 1
    #define HAVE_MATH_H 1
    #define HAVE_MEMORY_H 1
    #define HAVE_MESSAGES 1
    #define HAVE_NANOSLEEP 1
    #define HAVE_POSTPROCESSOR 1
    #define HAVE_PTHREAD_H 1
    #define HAVE_PTHREAD_PRIO_INHERIT 1
    #define HAVE_RAND 1
    #define HAVE_REAL_BOOL 1
    #define HAVE_SEARCH_H 1
    #define HAVE_SNPRINTF 1
    #define HAVE_STDBOOL_H 1
    #define HAVE_STDDEF_H 1
    #define HAVE_STDINT_H 1
    #define HAVE_STDIO_H 1
    #define HAVE_STDLIB_H 1
    #define HAVE_STRINGS_H 1
    #define HAVE_STRING_H 1
    #define HAVE_SYS_PARAM_H 1
    #define HAVE_SYS_STAT_H 1
    #define HAVE_SYS_TIME_H 1
    #define HAVE_SYS_TYPES_H 1
    #define HAVE_TIME_H 1
    #define HAVE_TSEARCH 1
    #define HAVE_UNISTD_H 1
    #define HAVE_USLEEP 1
    #define HAVE_W32_GMTIME_S 1
    #define HAVE_WINSOCK2_H 1
    #define HAVE_WS2TCPIP_H 1
    #define INLINE_FUNC 1
    #define LT_OBJDIR ".libs/"
    #define MHD_HAVE___BUILTIN_BSWAP32 1
    #define MHD_HAVE___BUILTIN_BSWAP64 1
    #define MHD_NO_THREAD_NAMES 1
    #define MHD_USE_W32_THREADS 1
    #define MINGW 1
    #define STDC_HEADERS 1
    #define UPGRADE_SUPPORT 1
    #define WINDOWS 1
    #define _FILE_OFFSET_BITS 64
    #define _GNU_SOURCE 1
    #define _MHD_EXTERN __attribute__((visibility("default"))) __declspec(dllexport) extern
    #define _MHD_ITC_SOCKETPAIR 1
    #define _MHD_static_inline static inline __attribute__((always_inline))
    #define _XOPEN_SOURCE 1
    #define _XOPEN_SOURCE_EXTENDED 1
#elif defined(_MSC_VER)
    #define WINDOWS 1
    #define MSVC 1
    #define INLINE_FUNC 1
    #define _MHD_static_inline static __forceinline
    #define BAUTH_SUPPORT 1
    #define DAUTH_SUPPORT 1
    #define HAVE_POSTPROCESSOR 1
    #define HAVE_MESSAGES 1
    #define UPGRADE_SUPPORT 1
    #define HAVE_INET6 1
    #define _MHD_ITC_SOCKETPAIR 1
    #define MHD_USE_W32_THREADS 1
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0501
    #endif /* _WIN32_WINNT */
    #if _WIN32_WINNT >= 0x0600
        #define HAVE_POLL 1
    #endif /* _WIN32_WINNT >= 0x0600 */
    #define HAVE_WINSOCK2_H 1
    #define HAVE_WS2TCPIP_H 1
    #define HAVE___LSEEKI64 1
    #define HAVE_W32_GMTIME_S 1
    #define HAVE_ASSERT 1
    #define HAVE_CALLOC 1
    #define HAVE_SNPRINTF 1
    #define HAVE_INTTYPES_H 1
    #define HAVE_ERRNO_H 1
    #define HAVE_FCNTL_H 1
    #define HAVE_INTTYPES_H 1
    #define HAVE_LIMITS_H 1
    #define HAVE_LOCALE_H 1
    #define HAVE_MATH_H 1
    #define HAVE_MEMORY_H 1
    #define HAVE_STDINT_H 1
    #define HAVE_STDIO_H 1
    #define HAVE_STDLIB_H 1
    #define HAVE_STRINGS_H 1
    #define HAVE_STRING_H 1
    #define HAVE_SYS_STAT_H 1
    #define HAVE_SYS_TYPES_H 1
    #define HAVE_TIME_H 1
    #define HAVE_STDDEF_H 1
    #define HAVE_STDBOOL_H 1
    #define _GNU_SOURCE  1
    #define STDC_HEADERS 1
    #define __STDC_NO_VLA__ 1
#elif defined(__APPLE__)
    #define BAUTH_SUPPORT 1
    #define DAUTH_SUPPORT 1
    #define HAVE_ARPA_INET_H 1
    #define HAVE_ASSERT 1
    #define HAVE_C_VARARRAYS 1
    #define HAVE_CALLOC 1
    #define HAVE_CLOCK_GETTIME 1
    #define HAVE_CLOCK_GET_TIME 1
    #define HAVE_DARWIN_SENDFILE 1
    #define HAVE_DECL_GETSOCKNAME 1
    #define HAVE_DECL_SOCK_NONBLOCK 0
    #define HAVE_DLFCN_H 1
    #define HAVE_ERRNO_H 1
    #define HAVE_FCNTL_H 1
    #define HAVE_FORK 1
    #define HAVE_FSEEKO 1
    #define HAVE_GETSOCKNAME 1
    #define HAVE_GETTIMEOFDAY 1
    #define HAVE_GMTIME_R 1
    #define HAVE_INET6 1
    #define HAVE_INTTYPES_H 1
    #define HAVE_LIBCURL 1
    #define HAVE_LIMITS_H 1
    #define HAVE_LOCALE_H 1
    #define HAVE_MACHINE_ENDIAN_H 1
    #define HAVE_MACHINE_PARAM_H 1
    #define HAVE_MATH_H 1
    #define HAVE_MEMMEM 1
    #define HAVE_MEMORY_H 1
    #define HAVE_MESSAGES 1
    #define HAVE_NANOSLEEP 1
    #define HAVE_NETDB_H 1
    #define HAVE_NETINET_IN_H 1
    #define HAVE_NETINET_IP_H 1
    #define HAVE_NETINET_TCP_H 1
    #define HAVE_NET_IF_H 1
    #define HAVE_POLL 1
    #define HAVE_POLL_H 1
    #define HAVE_POSTPROCESSOR 1
    #define HAVE_PREAD 1
    #define HAVE_PTHREAD_H 1
    #define HAVE_PTHREAD_PRIO_INHERIT 1
    #define HAVE_PTHREAD_SETNAME_NP_DARWIN 1
    #define HAVE_RAND 1
    #define HAVE_RANDOM 1
    #define HAVE_REAL_BOOL 1
    #define HAVE_SEARCH_H 1
    #define HAVE_SENDMSG 1
    #define HAVE_SNPRINTF 1
    #define HAVE_SOCKADDR_IN_SIN_LEN 1
    #define HAVE_STDBOOL_H 1
    #define HAVE_STDDEF_H 1
    #define HAVE_STDINT_H 1
    #define HAVE_STDIO_H 1
    #define HAVE_STDLIB_H 1
    #define HAVE_STRINGS_H 1
    #define HAVE_STRING_H 1
    #define HAVE_SYS_IOCTL_H 1
    #define HAVE_SYS_MMAN_H 1
    #define HAVE_SYS_MSG_H 1
    #define HAVE_SYS_PARAM_H 1
    #define HAVE_SYS_SELECT_H 1
    #define HAVE_SYS_SOCKET_H 1
    #define HAVE_SYS_STAT_H 1
    #define HAVE_SYS_TIME_H 1
    #define HAVE_SYS_TYPES_H 1
    #define HAVE_TIME_H 1
    #define HAVE_TSEARCH 1
    #define HAVE_UNISTD_H 1
    #define HAVE_USLEEP 1
    #define HAVE_WAITPID 1
    #define HAVE_WRITEV 1
    #define INLINE_FUNC 1
    #define OSX 1
    #define LT_OBJDIR ".libs/"
    #define MHD_HAVE___BUILTIN_BSWAP32 1
    #define MHD_HAVE___BUILTIN_BSWAP64 1
    #define MHD_USE_GETSOCKNAME 1
    #define MHD_USE_POSIX_THREADS 1
    #define STDC_HEADERS 1
    #define UPGRADE_SUPPORT 1
    #define _DARWIN_C_SOURCE 1
    #ifndef _DARWIN_USE_64_BIT_INODE
    # define _DARWIN_USE_64_BIT_INODE 1
    #endif
    #define _GNU_SOURCE 1
    #define _MHD_EXTERN __attribute__((visibility("default"))) extern
    #define _MHD_ITC_PIPE 1
    #define _MHD_static_inline static inline __attribute__((always_inline))
#elif defined(__linux__)
    #define BAUTH_SUPPORT 1
    #define DAUTH_SUPPORT 1
    #define EPOLL_SUPPORT 1
    #define HAVE_ACCEPT4 1
    #define HAVE_ARPA_INET_H 1
    #define HAVE_ASSERT 1
    #define HAVE_C_VARARRAYS 1
    #define HAVE_CALLOC 1
    #define HAVE_CLOCK_GETTIME 1
    #define HAVE_DECL_SOCK_NONBLOCK 1
    #define HAVE_DLFCN_H 1
    #define HAVE_ENDIAN_H 1
    #define HAVE_EPOLL_CREATE1 1
    #define HAVE_ERRNO_H 1
    #define HAVE_FCNTL_H 1
    #define HAVE_FORK 1
    #define HAVE_FSEEKO 1
    #define HAVE_GETTIMEOFDAY 1
    #define HAVE_GMTIME_R 1
    #define HAVE_INET6 1
    #define HAVE_INTTYPES_H 1
    #define HAVE_LIMITS_H 1
    #define HAVE_LISTEN_SHUTDOWN 1
    #define HAVE_LOCALE_H 1
    #define HAVE_LSEEK64 1
    #define HAVE_MATH_H 1
    #define HAVE_MEMMEM 1
    #define HAVE_MEMORY_H 1
    #define HAVE_MESSAGES 1
    #define HAVE_MSG_MORE 1
    #define HAVE_NANOSLEEP 1
    #define HAVE_NETDB_H 1
    #define HAVE_NETINET_IN_H 1
    #define HAVE_NETINET_IP_H 1
    #define HAVE_NETINET_TCP_H 1
    #define HAVE_NET_IF_H 1
    #define HAVE_POLL 1
    #define HAVE_POLL_H 1
    #define HAVE_POSTPROCESSOR 1
    #define HAVE_PREAD 1
    #define HAVE_PREAD64 1
    #define HAVE_PTHREAD_H 1
    #define HAVE_PTHREAD_PRIO_INHERIT 1
    #define HAVE_PTHREAD_SETNAME_NP_GNU 1
    #define HAVE_RAND 1
    #define HAVE_RANDOM 1
    #define HAVE_REAL_BOOL 1
    #define HAVE_SEARCH_H 1
    #define HAVE_SENDMSG 1
    #define HAVE_SENDFILE64 1
    #define HAVE_SNPRINTF 1
    #define HAVE_SOCK_NONBLOCK 1
    #define HAVE_STDBOOL_H 1
    #define HAVE_STDDEF_H 1
    #define HAVE_STDINT_H 1
    #define HAVE_STDIO_H 1
    #define HAVE_STDLIB_H 1
    #define HAVE_STRINGS_H 1
    #define HAVE_STRING_H 1
    #define HAVE_SYS_EVENTFD_H 1
    #define HAVE_SYS_IOCTL_H 1
    #define HAVE_SYS_MMAN_H 1
    #define HAVE_SYS_MSG_H 1
    #define HAVE_SYS_PARAM_H 1
    #define HAVE_SYS_SELECT_H 1
    #define HAVE_SYS_SOCKET_H 1
    #define HAVE_SYS_STAT_H 1
    #define HAVE_SYS_TIME_H 1
    #define HAVE_SYS_TYPES_H 1
    #define HAVE_SYSCONF 1
    #define HAVE_TIME_H 1
    #define HAVE_TSEARCH 1
    #define HAVE_UNISTD_H 1
    #define HAVE_USLEEP 1
    #define HAVE_WAITPID 1
    #define HAVE_WRITEV 1
    #define INLINE_FUNC 1
    #define LINUX 1
    #define LT_OBJDIR ".libs/"
    #define MHD_HAVE___BUILTIN_BSWAP32 1
    #define MHD_HAVE___BUILTIN_BSWAP64 1
    #define MHD_USE_POSIX_THREADS 1
    #define STDC_HEADERS 1
    #define UPGRADE_SUPPORT 1
    #define _GNU_SOURCE 1
    #define _MHD_EXTERN __attribute__((visibility("default"))) extern
    #define _MHD_ITC_EVENTFD 1
    #define _MHD_static_inline static inline __attribute__((always_inline))
    #define _XOPEN_SOURCE 700
#else
    #error "Custom MHD_config.h not suited for this platform"
#endif
