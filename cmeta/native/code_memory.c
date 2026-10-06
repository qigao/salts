#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
  #define _DEFAULT_SOURCE 1
#endif
#include "code_memory.h"
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <errno.h>
  #include <sys/mman.h>
  #include <unistd.h>
#endif

static cmeta_status memory_error(cmeta_native_thunk *thunk) {
#ifdef _WIN32
  thunk->platform_error = GetLastError();
  if (thunk->platform_error == ERROR_NOT_ENOUGH_MEMORY ||
      thunk->platform_error == ERROR_OUTOFMEMORY)
    return CMETA_OUT_OF_MEMORY;
#else
  thunk->platform_error = (unsigned long)errno;
  if (thunk->platform_error == ENOMEM) return CMETA_OUT_OF_MEMORY;
#endif
  return CMETA_CALLBACK_ERROR;
}

cmeta_status cmeta_native_memory_create(size_t budget, cmeta_native_thunk *thunk) {
  size_t page_size;
  void *memory;
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  /* Charge the reservation granularity as well as committed code bytes. */
  page_size = info.dwAllocationGranularity;
#else
  const long page = sysconf(_SC_PAGESIZE);
  if (page <= 0) return memory_error(thunk);
  page_size = (size_t)page;
#endif
  if (page_size > budget) return CMETA_CAPACITY_EXCEEDED;
#ifdef _WIN32
  memory = VirtualAlloc(NULL, page_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (memory == NULL) return memory_error(thunk);
#else
  memory = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memory == MAP_FAILED) return memory_error(thunk);
#endif
  thunk->allocation = memory;
  thunk->allocation_size = page_size;
  thunk->state = CMETA_NATIVE_WRITABLE;
  thunk->platform_error = 0;
  return CMETA_OK;
}

cmeta_status cmeta_native_memory_write(cmeta_native_thunk *thunk) {
#ifdef _WIN32
  DWORD previous;
  if (!VirtualProtect(thunk->allocation, thunk->allocation_size, PAGE_READWRITE, &previous))
    return memory_error(thunk);
#else
  if (mprotect(thunk->allocation, thunk->allocation_size, PROT_READ | PROT_WRITE) != 0)
    return memory_error(thunk);
#endif
  thunk->state = CMETA_NATIVE_WRITABLE;
  return CMETA_OK;
}

cmeta_status cmeta_native_memory_publish(cmeta_native_thunk *thunk) {
  thunk->state = CMETA_NATIVE_UNPUBLISHED;
#ifdef _WIN32
  DWORD previous;
  if (!VirtualProtect(thunk->allocation, thunk->allocation_size, PAGE_EXECUTE_READ, &previous))
    return memory_error(thunk);
  if (!FlushInstructionCache(GetCurrentProcess(), thunk->allocation, thunk->allocation_size))
    return memory_error(thunk);
#else
  if (mprotect(thunk->allocation, thunk->allocation_size, PROT_READ | PROT_EXEC) != 0)
    return memory_error(thunk);
  __builtin___clear_cache((char *)thunk->allocation,
                          (char *)thunk->allocation + thunk->allocation_size);
#endif
  thunk->state = CMETA_NATIVE_READY;
  thunk->platform_error = 0;
  return CMETA_OK;
}

cmeta_status cmeta_native_memory_destroy(cmeta_native_thunk *thunk) {
#ifdef _WIN32
  if (!VirtualFree(thunk->allocation, 0, MEM_RELEASE)) return memory_error(thunk);
#else
  if (munmap(thunk->allocation, thunk->allocation_size) != 0) return memory_error(thunk);
#endif
  return CMETA_OK;
}
