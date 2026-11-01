#include <stdint.h>
__declspec(dllexport) uintptr_t CaptureNativeArguments(uintptr_t *out, uintptr_t b, uintptr_t c,
                                                      uintptr_t d, uintptr_t e, uintptr_t f)
{
    out[0] = (uintptr_t)out;
    out[1] = b; out[2] = c; out[3] = d; out[4] = e; out[5] = f;
    return b ^ c ^ d ^ e ^ f;
}
