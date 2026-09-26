// Starting, waiting for and signalling children on Windows: see
// process.cpp.
module;
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
module os.process;
import os.sys;

// Wide is a UTF-16 copy of a UTF-8 string, freed when it goes.
struct Wide {
    wchar_t* p = nullptr;
    explicit Wide(const char* s) : p(widen(s)) {}
    ~Wide() { free(p); }
    Wide(const Wide&) = delete;
    Wide& operator=(const Wide&) = delete;
};

// quoteArg appends one argument to a command line the way
// CommandLineToArgvW and the C runtime read it back: quoted where it has
// a space, a tab or a quote in it (or is empty), with backslashes doubled
// only where a quote follows them.
void quoteArg(wchar_t*& out, const wchar_t* a) {
    bool quote = *a == 0 || wcspbrk(a, L" \t\"") != nullptr;
    if (!quote) {
        while (*a) *out++ = *a++;
        return;
    }
    *out++ = L'"';
    for (;;) {
        size_t slashes = 0;
        while (*a == L'\\') { a++; slashes++; }
        if (*a == 0) {
            for (size_t i = 0; i < slashes * 2; i++) *out++ = L'\\';
            break;
        }
        if (*a == L'"') {
            for (size_t i = 0; i < slashes * 2 + 1; i++) *out++ = L'\\';
            *out++ = *a++;
            continue;
        }
        for (size_t i = 0; i < slashes; i++) *out++ = L'\\';
        *out++ = *a++;
    }
    *out++ = L'"';
}

// The children this process started, by pid, with their handles: a pid
// is what crosses the boundary, and the handle is what Windows waits on.
struct ChildHandle { DWORD pid; HANDLE h; };
ChildHandle children[256];
SRWLOCK childrenLock = SRWLOCK_INIT;

HANDLE handleOf(int64_t pid) {
    AcquireSRWLockShared(&childrenLock);
    HANDLE h = nullptr;
    for (auto& c : children) {
        if (c.h && c.pid == (DWORD)pid) { h = c.h; break; }
    }
    ReleaseSRWLockShared(&childrenLock);
    return h;
}

int32_t spawn(const char* path,
                  const char* args, int32_t argCount,
                  const char* env, int32_t envCount,
                  const char* dir,
                  const int32_t* modes, const char* filesBlob,
                  int64_t* out) noexcept {
    for (int i = 0; i < 5; i++) out[i] = -1;
    const char* files[3];
    files[0] = filesBlob;
    files[1] = files[0] + strlen(files[0]) + 1;
    files[2] = files[1] + strlen(files[1]) + 1;

    // The command line: every argument quoted as the child's C runtime
    // will split it again.
    size_t cap = 1;
    const char* p = args;
    for (int32_t i = 0; i < argCount; i++) { size_t n = strlen(p); cap += n * 2 + 3; p += n + 1; }
    wchar_t* cmd = (wchar_t*)calloc(cap, sizeof(wchar_t));
    if (!cmd) return Err::noMemory;
    wchar_t* w = cmd;
    p = args;
    for (int32_t i = 0; i < argCount; i++) {
        Wide a(p);
        if (i > 0) *w++ = L' ';
        quoteArg(w, a.p ? a.p : L"");
        p += strlen(p) + 1;
    }
    *w = 0;

    // The environment block: NAME=value\0 ... \0, in UTF-16.
    wchar_t* envBlock = nullptr;
    if (envCount >= 0) {
        size_t total = 2;
        const char* e = env;
        for (int32_t i = 0; i < envCount; i++) { size_t n = strlen(e); total += n + 1; e += n + 1; }
        envBlock = (wchar_t*)calloc(total, sizeof(wchar_t));
        if (!envBlock) { free(cmd); return Err::noMemory; }
        wchar_t* q = envBlock;
        e = env;
        for (int32_t i = 0; i < envCount; i++) {
            Wide v(e);
            if (v.p) { size_t n = wcslen(v.p); memcpy(q, v.p, n * sizeof(wchar_t)); q += n + 1; }
            e += strlen(e) + 1;
        }
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE childEnd[3] = { nullptr, nullptr, nullptr };
    HANDLE parentEnd[3] = { nullptr, nullptr, nullptr };
    DWORD stdIds[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    int32_t rc = Err::ok;
    for (int i = 0; i < 3 && rc == Err::ok; i++) {
        switch (modes[i]) {
        case StdioMode::pipe: {
            HANDLE r, wr;
            if (!CreatePipe(&r, &wr, &sa, 0)) { rc = mapError(GetLastError()); break; }
            childEnd[i] = i == 0 ? r : wr;
            parentEnd[i] = i == 0 ? wr : r;
            SetHandleInformation(parentEnd[i], HANDLE_FLAG_INHERIT, 0);
            break;
        }
        case StdioMode::discard:
        case StdioMode::file: {
            Wide name(modes[i] == StdioMode::discard ? "NUL" : files[i]);
            DWORD access = i == 0 ? GENERIC_READ : GENERIC_WRITE;
            DWORD disp = i == 0 ? OPEN_EXISTING : CREATE_ALWAYS;
            HANDLE h = CreateFileW(name.p, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { rc = mapError(GetLastError()); break; }
            childEnd[i] = h;
            break;
        }
        default: {
            HANDLE h = GetStdHandle(stdIds[i]);
            HANDLE dup = nullptr;
            if (h && h != INVALID_HANDLE_VALUE) {
                DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE,
                                DUPLICATE_SAME_ACCESS);
            }
            childEnd[i] = dup;
            break;
        }
        }
    }

    PROCESS_INFORMATION pi = {};
    if (rc == Err::ok) {
        // Only the three handles are inherited, whatever else is open and
        // inheritable in this process.
        HANDLE list[3];
        DWORD listCount = 0;
        for (int i = 0; i < 3; i++) if (childEnd[i]) list[listCount++] = childEnd[i];

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = childEnd[0];
        si.StartupInfo.hStdOutput = childEnd[1];
        si.StartupInfo.hStdError = childEnd[2];
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
        if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &size) &&
            UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      list, listCount * sizeof(HANDLE), nullptr, nullptr)) {
            si.lpAttributeList = attrs;
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
        Wide app(path), cwd(dir);
        if (!CreateProcessW(app.p, cmd, nullptr, nullptr, TRUE, flags, envBlock,
                            dir ? cwd.p : nullptr, &si.StartupInfo, &pi)) {
            rc = mapError(GetLastError());
        }
        if (si.lpAttributeList) DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }

    for (int i = 0; i < 3; i++) if (childEnd[i]) CloseHandle(childEnd[i]);
    free(cmd);
    free(envBlock);
    if (rc != Err::ok) {
        for (int i = 0; i < 3; i++) if (parentEnd[i]) CloseHandle(parentEnd[i]);
        return rc;
    }

    CloseHandle(pi.hThread);
    AcquireSRWLockExclusive(&childrenLock);
    bool kept = false;
    for (auto& c : children) {
        if (!c.h) { c.pid = pi.dwProcessId; c.h = pi.hProcess; kept = true; break; }
    }
    ReleaseSRWLockExclusive(&childrenLock);
    if (!kept) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        for (int i = 0; i < 3; i++) if (parentEnd[i]) CloseHandle(parentEnd[i]);
        return Err::noMemory;
    }

    out[0] = pi.dwProcessId;
    for (int i = 0; i < 3; i++) {
        if (parentEnd[i]) {
            out[i + 1] = _open_osfhandle((intptr_t)parentEnd[i], i == 0 ? _O_WRONLY : _O_RDONLY);
        }
    }
    out[4] = -1;
    return Err::ok;
}

int32_t tryWait(int64_t pid, int32_t exitFd, int32_t block,
                     int32_t* kind, int32_t* code) noexcept {
    (void)exitFd;
    HANDLE h = handleOf(pid);
    if (!h) return Err::notFound;
    DWORD r = WaitForSingleObject(h, block ? INFINITE : 0);
    if (r == WAIT_TIMEOUT) return 0;
    if (r != WAIT_OBJECT_0) return mapError(GetLastError());
    DWORD status = 0;
    GetExitCodeProcess(h, &status);
    *kind = ExitKind::exited;
    *code = (int32_t)status;
    return 1;
}

int32_t sendSignal(int64_t pid, int32_t kind, int32_t posix) noexcept {
    if (posix != 0) return Err::unsupported;
    switch (kind) {
    case Sig::interrupt:
    case Sig::terminate:
        // The child is its own process group, so this reaches it alone.
        return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid)
            ? Err::ok : mapError(GetLastError());
    case Sig::kill: {
        HANDLE h = handleOf(pid);
        if (!h) return Err::notFound;
        return TerminateProcess(h, 1) ? Err::ok : mapError(GetLastError());
    }
    default:
        return Err::unsupported;
    }
}

void releaseChild(int64_t pid, int32_t exitFd) noexcept {
    (void)exitFd;
    AcquireSRWLockExclusive(&childrenLock);
    for (auto& c : children) {
        if (c.h && c.pid == (DWORD)pid) { CloseHandle(c.h); c.h = nullptr; break; }
    }
    ReleaseSRWLockExclusive(&childrenLock);
}
