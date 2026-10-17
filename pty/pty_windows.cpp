// Windows pseudo-terminals are ConPTY (CreatePseudoConsole), which isn't
// written yet: every call is Err::unsupported.
module;
#include <stdint.h>
module os.pty;
import os.sys;

int32_t ptySpawn(const char*, const char*, int32_t, const char*, int32_t, const char*,
                 int32_t, int32_t, int64_t*) noexcept {
    return Err::unsupported;
}
int64_t ptyRead(int32_t, uint8_t*, int64_t) noexcept { return Err::unsupported; }
int64_t ptyWrite(int32_t, const uint8_t*, int64_t) noexcept { return Err::unsupported; }
int32_t ptyResize(int32_t, int32_t, int32_t, int32_t, int32_t) noexcept { return Err::unsupported; }
int32_t ptyForeground(int32_t, char*, int32_t) noexcept { return Err::unsupported; }
int32_t ptyForegroundDir(int32_t, char*, int32_t) noexcept { return Err::unsupported; }
int32_t ptyTryWait(int64_t, int32_t*, int32_t*) noexcept { return Err::unsupported; }
int32_t ptySignal(int64_t, int32_t) noexcept { return Err::unsupported; }
void ptyClose(int32_t) noexcept {}
