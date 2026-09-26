// The current user, for package os/user.
module;
#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <pwd.h>
    #include <unistd.h>
#endif
export module os.user;
import os.sys;

export int32_t userName(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    wchar_t w[257];
    DWORD n = 257;
    typedef BOOL (WINAPI *GetUserNameWFn)(LPWSTR, LPDWORD);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    GetUserNameWFn fn = advapi ? (GetUserNameWFn)GetProcAddress(advapi, "GetUserNameW") : nullptr;
    if (!fn || !fn(w, &n)) return Err::notFound;
    return narrowOut(w, (int32_t)n - 1, buf, max);
#else
    struct passwd pw, *res = nullptr;
    char tmp[4096];
    if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 || !res) return Err::notFound;
    return copyOut(res->pw_name, buf, max);
#endif
}

// userHome is $HOME (%USERPROFILE% on Windows), or the account's home
// where that is not set.
export int32_t userHome(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (n == 0) return mapError(GetLastError());
    wchar_t* v = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!v) return Err::noMemory;
    DWORD got = GetEnvironmentVariableW(L"USERPROFILE", v, n);
    int32_t rc = narrowOut(v, (int32_t)got, buf, max);
    free(v);
    return rc;
#else
    const char* h = getenv("HOME");
    if (h && *h) return copyOut(h, buf, max);
    struct passwd pw, *res = nullptr;
    char tmp[4096];
    if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 || !res) return Err::notFound;
    return copyOut(res->pw_dir, buf, max);
#endif
}
