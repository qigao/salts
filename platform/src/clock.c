#include <salts/clock.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <time.h>
#endif

#ifdef _WIN32
uint64_t salts_hrtime(void) {
  /* Benign race: worst case two threads initialize to the same frequency. */
  static volatile uint64_t freq = 0;
  uint64_t f = freq;
  LARGE_INTEGER counter;

  if (f == 0) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    f = (uint64_t)frequency.QuadPart;
    freq = f;
  }

  QueryPerformanceCounter(&counter);
  if (f == 0) return 0;

  uint64_t ticks = (uint64_t)counter.QuadPart;
  uint64_t whole = (ticks / f) * 1000000000ULL;
  uint64_t part = (ticks % f) * 1000000000ULL / f;
  return whole + part;
}

uint64_t salts_realtime_ms(void) {
  FILETIME ft;
  ULARGE_INTEGER value;

  GetSystemTimeAsFileTime(&ft);
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return (value.QuadPart - 116444736000000000ULL) / 10000ULL;
}
#else
uint64_t salts_hrtime(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t salts_realtime_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
#endif

uint64_t salts_monotonic_ms(void) { return salts_ns_to_ms(salts_hrtime()); }

uint64_t salts_uptime_ms(void) {
  /* Benign race: worst case two threads choose nearly identical start times. */
  static volatile uint64_t start_time = 0;
  uint64_t st = start_time;
  if (st == 0) {
    st = salts_hrtime();
    start_time = st;
  }
  return salts_ns_to_ms(salts_hrtime() - st);
}
