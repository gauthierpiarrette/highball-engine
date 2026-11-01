#include <windows.h>
#include <stdint.h>
#include <stdio.h>

extern uintptr_t CallAligned(void *, uintptr_t *);
extern uintptr_t CallMisaligned(void *, uintptr_t *);
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    HMODULE dll = LoadLibraryA(argv[1]);
    void *fn = dll ? (void *)GetProcAddress(dll, "CaptureNativeArguments") : NULL;
    if (!fn) { printf("FAIL helper unavailable error=%lu\n", GetLastError()); return 2; }
    unsigned pass = 0, fail = 0;
    for (unsigned mode = 0; mode < 2; ++mode) {
        uintptr_t out[6] = {0}, want[] = {(uintptr_t)out,0x22334455,0x33445566,0x44556677,0x55667711,0x66771122};
        uintptr_t result = mode ? CallMisaligned(fn, out) : CallAligned(fn, out);
        for (unsigned i = 0; i < 6; ++i) {
            int ok = out[i] == want[i];
            if (ok) ++pass; else ++fail;
            printf("%s %s argument%u actual=%llx expected=%llx\n",ok ? "PASS" : "FAIL",mode ? "misaligned" : "aligned",i+1,(unsigned long long)out[i],(unsigned long long)want[i]);
        }
        uintptr_t expected = want[1]^want[2]^want[3]^want[4]^want[5];
        if (result == expected) ++pass; else ++fail;
        printf("%s %s return actual=%llx expected=%llx\n",result==expected ? "PASS" : "FAIL",mode ? "misaligned" : "aligned",(unsigned long long)result,(unsigned long long)expected);
    }
    printf("SUMMARY native-stack-args pass=%u fail=%u\n",pass,fail);
    return fail ? 1 : 0;
}
