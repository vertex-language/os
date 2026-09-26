// The machine and operating system, for package os/host.
module;
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <errno.h>
    #include <unistd.h>
    #if defined(__APPLE__)
        #include <mach/mach.h>
        #include <sys/sysctl.h>
    #else
        #include <sys/sysinfo.h>
        #include <sys/system_properties.h>
    #endif
#endif
export module os.host;
import os.sys;

// The platform the program was built for.
export namespace OSCode {
    constexpr int32_t macOS = 1;
    constexpr int32_t windows = 2;
    constexpr int32_t linux = 3;
    constexpr int32_t android = 4;
}

export namespace ArchCode {
    constexpr int32_t aarch64 = 1;
    constexpr int32_t x86_64 = 2;
}

export int32_t osKind() noexcept {
#if defined(_WIN32)
    return OSCode::windows;
#elif defined(__APPLE__)
    return OSCode::macOS;
#elif defined(__ANDROID__)
    return OSCode::android;
#else
    return OSCode::linux;
#endif
}

export int32_t archKind() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return ArchCode::aarch64;
#else
    return ArchCode::x86_64;
#endif
}

export int32_t osVersion(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
    OSVERSIONINFOW v = {};
    v.dwOSVersionInfoSize = sizeof(v);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn fn = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    if (!fn || fn(&v) != 0) return Err::unsupported;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return copyOut(tmp, buf, max);
#elif defined(__APPLE__)
    char tmp[64];
    size_t n = sizeof(tmp);
    if (sysctlbyname("kern.osproductversion", tmp, &n, nullptr, 0) != 0) return mapError(errno);
    return copyOut(tmp, buf, max);
#else
    char tmp[PROP_VALUE_MAX];
    int n = __system_property_get("ro.build.version.release", tmp);
    if (n <= 0) return Err::unsupported;
    return copyOutN(tmp, (size_t)n, buf, max);
#endif
}

export int32_t hostName(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    wchar_t w[256];
    DWORD n = 256;
    if (!GetComputerNameExW(ComputerNameDnsHostname, w, &n)) return mapError(GetLastError());
    return narrowOut(w, (int32_t)n, buf, max);
#else
    char tmp[256];
    if (gethostname(tmp, sizeof(tmp)) != 0) return mapError(errno);
    tmp[sizeof(tmp) - 1] = 0;
    return copyOut(tmp, buf, max);
#endif
}

// cpuCounts writes three: logical, performance and efficiency processors.
export void cpuCounts(int32_t* out) noexcept {
#if defined(_WIN32)
    int32_t logical = (int32_t)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    out[0] = logical; out[1] = logical; out[2] = 0;
#elif defined(__APPLE__)
    int logical = 0, perf = 0, eff = 0;
    size_t n = sizeof(int);
    sysctlbyname("hw.logicalcpu", &logical, &n, nullptr, 0);
    int levels = 0;
    n = sizeof(int);
    sysctlbyname("hw.nperflevels", &levels, &n, nullptr, 0);
    if (levels >= 2) {
        n = sizeof(int);
        sysctlbyname("hw.perflevel0.logicalcpu", &perf, &n, nullptr, 0);
        n = sizeof(int);
        sysctlbyname("hw.perflevel1.logicalcpu", &eff, &n, nullptr, 0);
    } else {
        perf = logical;
    }
    out[0] = logical; out[1] = perf; out[2] = eff;
#else
    int32_t logical = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
    out[0] = logical; out[1] = logical; out[2] = 0;
#endif
}

// memoryBytes writes two: total and available bytes.
export void memoryBytes(int64_t* out) noexcept {
    out[0] = 0; out[1] = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX m = {};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
        out[0] = (int64_t)m.ullTotalPhys;
        out[1] = (int64_t)m.ullAvailPhys;
    }
#elif defined(__APPLE__)
    uint64_t total = 0;
    size_t n = sizeof(total);
    sysctlbyname("hw.memsize", &total, &n, nullptr, 0);
    out[0] = (int64_t)total;
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        // What the kernel can hand out without paging anything out: free
        // pages, and the inactive and purgeable ones it can reclaim.
        uint64_t pages = (uint64_t)vm.free_count + vm.inactive_count + vm.purgeable_count
                       - vm.speculative_count;
        out[1] = (int64_t)(pages * (uint64_t)vm_kernel_page_size);
    }
    mach_port_deallocate(mach_task_self(), host);
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out[0] = (int64_t)si.totalram * si.mem_unit;
        out[1] = (int64_t)si.freeram * si.mem_unit;
    }
    // MemAvailable counts the cache the kernel can drop, which freeram does not.
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            long long kb;
            if (sscanf(line, "MemAvailable: %lld kB", &kb) == 1) { out[1] = kb * 1024; break; }
        }
        fclose(f);
    }
#endif
}

export int64_t pageSize() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

// uptimeNanos is nanoseconds since boot.
export int64_t uptimeNanos() noexcept {
#if defined(_WIN32)
    return (int64_t)GetTickCount64() * 1000000;
#elif defined(__APPLE__)
    // The continuous clock counts time asleep too, which uptime does.
    return (int64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}
