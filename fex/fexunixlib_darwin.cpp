// Highball spike 2026-10-04: Darwin build of FEX Windows unix helper (Source/Windows/UnixLib/FEXUnixLib.cpp, MIT).
// Same function table and argument structs; prctl-based pieces replaced by their macOS equivalents.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_traps.h>

enum class FEXUnixLibFunctions : uint32_t { SetHardwareTSOControl, SetKernelUnalignedAtomicControl, Madvise, SetVMAName, GetSHMStatsVMA, DeleteSHMStatsFile, MapFile, GetPID, MAX };
struct FEXUnixLib_SetHardwareTSOControlArgs { bool Enable; };
struct FEXUnixLib_SetKernelUnalignedAtomicControl { uint64_t Flags; };
struct FEXUnixLib_Madvise { const void* Addr; size_t Size; int32_t Advise; uint32_t pad; };
struct FEXUnixLib_SetVMAName { const void* Addr; size_t Size; const char* Name; };
struct FEXUnixLib_GetSHMStatsVMA { void* SHMBase; uint32_t MapSize; uint32_t MaxSize; };
struct FEXUnixLib_MapFile { int32_t FD; uint64_t MapSize; void* Result; };
struct FEXUnixLib_GetPID { uint32_t Result; };

using NTSTATUS = int32_t;
using unixlib_entry_t = NTSTATUS (*)(void*);
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_NOT_SUPPORTED = 0xC00000BB;
constexpr NTSTATUS STATUS_INTERNAL_ERROR = 0xC00000E5;

static void trace(const char* what, long a, long b) { if (getenv("FEXUNIX_TRACE")) fprintf(stderr, "fexunixlib: %s %lx %lx\n", what, a, b); }
static NTSTATUS TSOControl(void* _Args) {
  trace("TSOControl", ((const FEXUnixLib_SetHardwareTSOControlArgs*)_Args)->Enable, 0);
  auto Args = reinterpret_cast<const FEXUnixLib_SetHardwareTSOControlArgs*>(_Args);
  kern_return_t kr = thread_set_x86_64_compat(Args->Enable ? 1 : 0);   /* per thread, needs the cross-architecture entitlement */
  return kr == KERN_SUCCESS ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}
static NTSTATUS UnalignedAtomicControl(void* a) { trace("UnalignedAtomic", ((const FEXUnixLib_SetKernelUnalignedAtomicControl*)a)->Flags, 0); return STATUS_NOT_SUPPORTED; }
static NTSTATUS Madvise(void* _Args) {
  auto Args = reinterpret_cast<const FEXUnixLib_Madvise*>(_Args);
  int advise;
  trace("Madvise", (long)Args->Addr, Args->Advise);
  switch (Args->Advise) {
    case 4: advise = MADV_DONTNEED; break;
    case 8: advise = MADV_FREE; break;
    case 14: case 15: return STATUS_SUCCESS;
    default: if (Args->Advise >= 0 && Args->Advise <= 3) advise = Args->Advise; else return STATUS_SUCCESS;
  }
  return madvise(const_cast<void*>(Args->Addr), Args->Size, advise) == 0 ? STATUS_SUCCESS : STATUS_INTERNAL_ERROR;
}
static NTSTATUS SetVMAName(void* a) { trace("SetVMAName", (long)((const FEXUnixLib_SetVMAName*)a)->Addr, ((const FEXUnixLib_SetVMAName*)a)->Size); return STATUS_SUCCESS; }
static std::string StatsPath() { const char* t = getenv("TMPDIR"); return std::string(t ? t : "/tmp/") + "fex-" + std::to_string(getpid()) + "-stats"; }
static NTSTATUS GetSHMStatsVMA(void* _Args) {
  auto Args = reinterpret_cast<FEXUnixLib_GetSHMStatsVMA*>(_Args);
  trace("GetSHMStatsVMA", (long)Args->SHMBase, Args->MapSize);
  int fd = open(StatsPath().c_str(), O_CREAT | O_RDWR, 0600);
  if (fd == -1) return STATUS_INTERNAL_ERROR;
  if (ftruncate(fd, Args->MapSize) == -1) { close(fd); return STATUS_INTERNAL_ERROR; }
  if (!Args->SHMBase) {
    void* base = mmap(nullptr, Args->MaxSize, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) { close(fd); return STATUS_INTERNAL_ERROR; }
    Args->SHMBase = base;
  }
  void* shared = mmap(Args->SHMBase, Args->MapSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  close(fd);
  return shared == MAP_FAILED ? STATUS_INTERNAL_ERROR : STATUS_SUCCESS;
}
static NTSTATUS DeleteSHMStatsFile(void*) { unlink(StatsPath().c_str()); return STATUS_SUCCESS; }
static NTSTATUS MapFile(void* _Args) {
  auto Args = reinterpret_cast<FEXUnixLib_MapFile*>(_Args);
  trace("MapFile", Args->FD, Args->MapSize);
  Args->Result = mmap(nullptr, Args->MapSize, PROT_READ, MAP_SHARED | MAP_NORESERVE, Args->FD, 0);
  close(Args->FD);
  if (Args->Result == MAP_FAILED) { Args->Result = nullptr; return STATUS_INTERNAL_ERROR; }
  return STATUS_SUCCESS;
}
static NTSTATUS GetPID(void* _Args) { trace("GetPID", 0, 0); reinterpret_cast<FEXUnixLib_GetPID*>(_Args)->Result = getpid(); return STATUS_SUCCESS; }

extern "C" __attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_funcs[] = {
  TSOControl, UnalignedAtomicControl, Madvise, SetVMAName, GetSHMStatsVMA, DeleteSHMStatsFile, MapFile, GetPID,
};
static_assert(sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) == static_cast<uint32_t>(FEXUnixLibFunctions::MAX));
