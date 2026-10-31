#include <stdint.h>
extern uintptr_t PerformCallCheckerProbe(uintptr_t *);
__declspec(dllexport) uintptr_t RunCallCheckerProbe(uintptr_t *arguments)
{
    return PerformCallCheckerProbe(arguments);
}
