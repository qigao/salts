/* tinytest.hpp
 * C++ testing entry. Includes the C core from tinytest.h and adds the
 * C++-only container, string, generic, and exception assertions.
 */

#ifndef TINYTEST_HPP
#define TINYTEST_HPP

#include "tinytest.h"

/* --- C++ container assertions (only available in C++ mode) --- */
#ifdef __cplusplus
  #include <algorithm>
  #include <type_traits>
  #include <utility>
  #include <iterator>
  #include <sstream>
  #include <stdexcept>

namespace ttest_cpp__ {

  template <typename T, typename U> inline bool ttest_equal__(const T &a, const U &b) {
  #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal"
  #endif
    return a == b;
  #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
  #endif
  }

  template <typename T, typename U> inline bool ttest_not_equal__(const T &a, const U &b) {
  #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wfloat-equal"
  #endif
    return a != b;
  #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
  #endif
  }

  template <typename T, typename = void> struct is_container_like : std::false_type {};
  template <typename T>
  struct is_container_like<
      T, std::void_t<decltype(std::declval<const T &>().begin()), decltype(std::declval<const T &>().end()),
                     decltype(std::declval<const T &>().size())>> : std::true_type {};

  template <typename Container>
  std::string container_to_string(const Container &c, size_t max_items = 8) {
    std::ostringstream os;
    os << "[";
    size_t i = 0;
    for (auto it = c.begin(); it != c.end() && i < max_items; ++it, ++i) {
      if (i > 0) os << ", ";
      os << *it;
    }
    if (c.size() > max_items) os << ", ...(" << c.size() << " total)";
    os << "]";
    return os.str();
  }

  template <typename Container>
  bool containers_equal(const Container &actual, const Container &expected, size_t &fail_idx,
                        bool &size_mismatch) {
    size_mismatch = (actual.size() != expected.size());
    if (size_mismatch) return false;
    auto a = actual.begin();
    auto e = expected.begin();
    for (fail_idx = 0; a != actual.end(); ++a, ++e, ++fail_idx) {
      if (!(*a == *e)) return false;
    }
    return true;
  }

  template <typename Map>
  bool maps_equal(const Map &actual, const Map &expected, std::string &detail) {
    if (actual.size() != expected.size()) {
      std::ostringstream os;
      os << "size mismatch: expected " << expected.size() << " but got " << actual.size();
      detail = os.str();
      return false;
    }
    for (auto it = expected.begin(); it != expected.end(); ++it) {
      auto found = actual.find(it->first);
      if (found == actual.end()) {
        std::ostringstream os;
        os << "missing key: " << it->first;
        detail = os.str();
        return false;
      }
      if (!(found->second == it->second)) {
        std::ostringstream os;
        os << "value mismatch at key " << it->first << ": expected " << it->second << " but got "
           << found->second;
        detail = os.str();
        return false;
      }
    }
    return true;
  }

} /* namespace ttest_cpp__ */

  #define check_eq(actual, expected)                                                               \
    do {                                                                                           \
      size_t ttest_fi__ = 0;                                                                       \
      bool ttest_sm__ = false;                                                                     \
      if (!ttest_cpp__::containers_equal((actual), (expected), ttest_fi__, ttest_sm__)) {          \
        if (ttest_sm__) {                                                                          \
          std::string __a = ttest_cpp__::container_to_string((actual));                            \
          std::string __e = ttest_cpp__::container_to_string((expected));                          \
          TTEST_CHECK__(                                                                           \
              0,                                                                                   \
              "size mismatch: expected %zu elements but got %zu\n  expected: %s\n  actual:   %s",  \
              (expected).size(), (actual).size(), __e.c_str(), __a.c_str());                       \
        } else {                                                                                   \
          TTEST_CHECK__(0, "mismatch at index %zu", ttest_fi__);                                   \
        }                                                                                          \
      }                                                                                            \
    } while (0)
  #define check_eq_warn(actual, expected)                                                          \
    do {                                                                                           \
      size_t ttest_fi__ = 0;                                                                       \
      bool ttest_sm__ = false;                                                                     \
      if (!ttest_cpp__::containers_equal((actual), (expected), ttest_fi__, ttest_sm__)) {          \
        if (ttest_sm__) {                                                                          \
          std::string __a = ttest_cpp__::container_to_string((actual));                            \
          std::string __e = ttest_cpp__::container_to_string((expected));                          \
          TTEST_WARN__(                                                                            \
              0,                                                                                   \
              "size mismatch: expected %zu elements but got %zu\n  expected: %s\n  actual:   %s",  \
              (expected).size(), (actual).size(), __e.c_str(), __a.c_str());                       \
        } else {                                                                                   \
          TTEST_WARN__(0, "mismatch at index %zu", ttest_fi__);                                    \
        }                                                                                          \
      }                                                                                            \
    } while (0)

  #define check_map_eq(actual, expected)                                                           \
    do {                                                                                           \
      std::string ttest_detail__;                                                                  \
      if (!ttest_cpp__::maps_equal((actual), (expected), ttest_detail__)) {                        \
        TTEST_CHECK__(0, "%s", ttest_detail__.c_str());                                            \
      }                                                                                            \
    } while (0)
  #define check_map_eq_warn(actual, expected)                                                      \
    do {                                                                                           \
      std::string ttest_detail__;                                                                  \
      if (!ttest_cpp__::maps_equal((actual), (expected), ttest_detail__)) {                        \
        TTEST_WARN__(0, "%s", ttest_detail__.c_str());                                             \
      }                                                                                            \
    } while (0)

  /* Range assertion */
  #define check_in_range(actual, lower, upper)                                                     \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_lo_ref__ = (lower);                                                        \
      const auto &ttest_hi_ref__ = (upper);                                                        \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ >= ttest_lo_ref__ &&                                    \
                                 ttest_a_ref__ <= ttest_hi_ref__))) {                               \
        std::string ttest_a_str__ = ttest_cpp__::to_string_safe(ttest_a_ref__);                     \
        std::string ttest_lo_str__ = ttest_cpp__::to_string_safe(ttest_lo_ref__);                   \
        std::string ttest_hi_str__ = ttest_cpp__::to_string_safe(ttest_hi_ref__);                   \
        TTEST_CHECK__(0, "expected %s in range [%s, %s], got %s", ttest_lo_str__.c_str(),             \
                      ttest_hi_str__.c_str(), ttest_a_str__.c_str());                               \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_in_range_warn(actual, lower, upper)                                                \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_lo_ref__ = (lower);                                                        \
      const auto &ttest_hi_ref__ = (upper);                                                        \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ >= ttest_lo_ref__ &&                                   \
                                 ttest_a_ref__ <= ttest_hi_ref__))) {                               \
        std::string ttest_a_str__ = ttest_cpp__::to_string_safe(ttest_a_ref__);                     \
        std::string ttest_lo_str__ = ttest_cpp__::to_string_safe(ttest_lo_ref__);                   \
        std::string ttest_hi_str__ = ttest_cpp__::to_string_safe(ttest_hi_ref__);                   \
        TTEST_WARN__(0, "expected %s in range [%s, %s], got %s", ttest_lo_str__.c_str(),             \
                      ttest_hi_str__.c_str(), ttest_a_str__.c_str());                               \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_between(actual, lower, upper) check_in_range((actual), (lower), (upper))
  #define check_between_warn(actual, lower, upper) check_in_range_warn((actual), (lower), (upper))

  #define check_contains(container, value)                                                         \
    TTEST_CHECK__(std::find((container).begin(), (container).end(), (value)) != (container).end(), \
                  "container does not contain expected value")
  #define check_contains_warn(container, value)                                                    \
    TTEST_WARN__(std::find((container).begin(), (container).end(), (value)) != (container).end(),  \
                 "container does not contain expected value")

  #define check_not_contains(container, value)                                                     \
    TTEST_CHECK__(std::find((container).begin(), (container).end(), (value)) == (container).end(), \
                  "container contains unexpected value")
  #define check_not_contains_warn(container, value)                                                \
    TTEST_WARN__(std::find((container).begin(), (container).end(), (value)) == (container).end(),  \
                 "container contains unexpected value")

  #define check_size(container, expected_size)                                                     \
    TTEST_CHECK__((container).size() == TTEST_CAST(size_t, (expected_size)),                       \
                  "expected size %zu but got %zu", TTEST_CAST(size_t, (expected_size)),            \
                  TTEST_CAST(size_t, (container).size()))
  #define check_size_warn(container, expected_size)                                                \
    TTEST_WARN__((container).size() == TTEST_CAST(size_t, (expected_size)),                        \
                 "expected size %zu but got %zu", TTEST_CAST(size_t, (expected_size)),             \
                 TTEST_CAST(size_t, (container).size()))

  #define check_empty(container)                                                                   \
    TTEST_CHECK__((container).empty(), "expected empty but got %zu elements",                      \
                  TTEST_CAST(size_t, (container).size()))
  #define check_empty_warn(container)                                                              \
    TTEST_WARN__((container).empty(), "expected empty but got %zu elements",                       \
                 TTEST_CAST(size_t, (container).size()))

  #define check_not_empty(container)                                                               \
    TTEST_CHECK__(!(container).empty(), "expected non-empty container")
  #define check_not_empty_warn(container)                                                          \
    TTEST_WARN__(!(container).empty(), "expected non-empty container")

  #define check_map_has_key(map, key)                                                              \
    TTEST_CHECK__((map).find((key)) != (map).end(), "map does not contain expected key")
  #define check_map_has_key_warn(map, key)                                                         \
    TTEST_WARN__((map).find((key)) != (map).end(), "map does not contain expected key")

  #define check_map_not_has_key(map, key)                                                          \
    TTEST_CHECK__((map).find((key)) == (map).end(), "map contains unexpected key")
  #define check_map_not_has_key_warn(map, key)                                                     \
    TTEST_WARN__((map).find((key)) == (map).end(), "map contains unexpected key")

/* std::string assertions */
namespace ttest_cpp__ {

  template <typename T> std::string to_string_safe(const T &val) {
    std::ostringstream os;
    os << val;
    return os.str();
  }

  /* Safely convert to std::string for assertions, handling NULL pointers */
  inline std::string stringify_safe(const char *s) { return s ? s : "(null)"; }
  inline std::string stringify_safe(const std::string &s) { return s; }
  inline std::string stringify_safe(std::nullptr_t) { return "(null)"; }

} /* namespace ttest_cpp__ */

  #define check_string_eq(actual, expected)                                                        \
    do {                                                                                           \
      std::string __a = ttest_cpp__::stringify_safe(actual);                                       \
      std::string __e = ttest_cpp__::stringify_safe(expected);                                     \
      TTEST_CHECK__(__a == __e, "expected \"%s\" but got \"%s\"", __e.c_str(), __a.c_str());       \
    } while (0)
  #define check_string_eq_warn(actual, expected)                                                   \
    do {                                                                                           \
      std::string __a = ttest_cpp__::stringify_safe(actual);                                       \
      std::string __e = ttest_cpp__::stringify_safe(expected);                                     \
      TTEST_WARN__(__a == __e, "expected \"%s\" but got \"%s\"", __e.c_str(), __a.c_str());        \
    } while (0)

  #define check_string_ne(actual, expected)                                                        \
    do {                                                                                           \
      std::string __a = ttest_cpp__::stringify_safe(actual);                                       \
      std::string __e = ttest_cpp__::stringify_safe(expected);                                     \
      TTEST_CHECK__(__a != __e, "expected != \"%s\" but got \"%s\"", __e.c_str(), __a.c_str());    \
    } while (0)
  #define check_string_ne_warn(actual, expected)                                                   \
    do {                                                                                           \
      std::string __a = ttest_cpp__::stringify_safe(actual);                                       \
      std::string __e = ttest_cpp__::stringify_safe(expected);                                     \
      TTEST_WARN__(__a != __e, "expected != \"%s\" but got \"%s\"", __e.c_str(), __a.c_str());     \
    } while (0)

  #define check_string_contains(haystack, needle)                                                  \
    do {                                                                                           \
      std::string __h = ttest_cpp__::stringify_safe(haystack);                                     \
      std::string __n = ttest_cpp__::stringify_safe(needle);                                       \
      TTEST_CHECK__(__h.find(__n) != std::string::npos, "expected \"%s\" to contain \"%s\"",       \
                    __h.c_str(), __n.c_str());                                                     \
    } while (0)
  #define check_string_contains_warn(haystack, needle)                                             \
    do {                                                                                           \
      std::string __h = ttest_cpp__::stringify_safe(haystack);                                     \
      std::string __n = ttest_cpp__::stringify_safe(needle);                                       \
      TTEST_WARN__(__h.find(__n) != std::string::npos, "expected \"%s\" to contain \"%s\"",        \
                   __h.c_str(), __n.c_str());                                                      \
    } while (0)

  #define check_string_starts_with(str, prefix)                                                    \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      std::string __p = ttest_cpp__::stringify_safe(prefix);                                       \
      TTEST_CHECK__(__s.size() >= __p.size() && __s.compare(0, __p.size(), __p) == 0,              \
                    "expected \"%s\" to start with \"%s\"", __s.c_str(), __p.c_str());             \
    } while (0)
  #define check_string_starts_with_warn(str, prefix)                                               \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      std::string __p = ttest_cpp__::stringify_safe(prefix);                                       \
      TTEST_WARN__(__s.size() >= __p.size() && __s.compare(0, __p.size(), __p) == 0,               \
                   "expected \"%s\" to start with \"%s\"", __s.c_str(), __p.c_str());              \
    } while (0)

  #define check_string_ends_with(str, suffix)                                                      \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      std::string __x = ttest_cpp__::stringify_safe(suffix);                                       \
      TTEST_CHECK__(__s.size() >= __x.size() &&                                                    \
                        __s.compare(__s.size() - __x.size(), __x.size(), __x) == 0,                \
                    "expected \"%s\" to end with \"%s\"", __s.c_str(), __x.c_str());               \
    } while (0)
  #define check_string_ends_with_warn(str, suffix)                                                 \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      std::string __x = ttest_cpp__::stringify_safe(suffix);                                       \
      TTEST_WARN__(__s.size() >= __x.size() &&                                                     \
                       __s.compare(__s.size() - __x.size(), __x.size(), __x) == 0,                 \
                   "expected \"%s\" to end with \"%s\"", __s.c_str(), __x.c_str());                \
    } while (0)

  #define check_string_empty(str)                                                                  \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      TTEST_CHECK__(__s.empty(), "expected empty string but got \"%s\"", __s.c_str());             \
    } while (0)
  #define check_string_empty_warn(str)                                                             \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      TTEST_WARN__(__s.empty(), "expected empty string but got \"%s\"", __s.c_str());              \
    } while (0)

  #define check_string_not_empty(str)                                                              \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      TTEST_CHECK__(!__s.empty(), "expected non-empty string");                                    \
    } while (0)
  #define check_string_not_empty_warn(str)                                                         \
    do {                                                                                           \
      std::string __s = ttest_cpp__::stringify_safe(str);                                          \
      TTEST_WARN__(!__s.empty(), "expected non-empty string");                                     \
    } while (0)

  /* --- Template-based generic assertions --- */

  #define check_equal(actual, expected)                                                            \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if constexpr (ttest_cpp__::is_container_like<decltype(ttest_a_ref__)>::value &&                  \
                    ttest_cpp__::is_container_like<decltype(ttest_e_ref__)>::value) {                \
        check_eq(ttest_a_ref__, ttest_e_ref__);                                                    \
      } else if (!ttest_eval_bool__(!!(ttest_cpp__::ttest_equal__(ttest_a_ref__, ttest_e_ref__)))) { \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_CHECK__(0, "expected %s but got %s", __e.c_str(), __a.c_str());                      \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_equal_warn(actual, expected)                                                       \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if constexpr (ttest_cpp__::is_container_like<decltype(ttest_a_ref__)>::value &&                  \
                    ttest_cpp__::is_container_like<decltype(ttest_e_ref__)>::value) {                \
        check_eq_warn(ttest_a_ref__, ttest_e_ref__);                                               \
      } else if (!ttest_eval_bool__(!!(ttest_cpp__::ttest_equal__(ttest_a_ref__, ttest_e_ref__)))) { \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_WARN__(0, "expected %s but got %s", __e.c_str(), __a.c_str());                       \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

  #define check_not_equal(actual, expected)                                                        \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_cpp__::ttest_not_equal__(ttest_a_ref__, ttest_e_ref__)))) {  \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_CHECK__(0, "expected != %s but got %s", __e.c_str(), __a.c_str());                   \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_not_equal_warn(actual, expected)                                                   \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_cpp__::ttest_not_equal__(ttest_a_ref__, ttest_e_ref__)))) {  \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_WARN__(0, "expected != %s but got %s", __e.c_str(), __a.c_str());                    \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

  #define check_greater(actual, expected)                                                          \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ > ttest_e_ref__))) {                                 \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_CHECK__(0, "expected > %s but got %s", __e.c_str(), __a.c_str());                    \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_greater_warn(actual, expected)                                                     \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ > ttest_e_ref__))) {                                 \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_WARN__(0, "expected > %s but got %s", __e.c_str(), __a.c_str());                     \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

  #define check_less(actual, expected)                                                             \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ < ttest_e_ref__))) {                                 \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_CHECK__(0, "expected < %s but got %s", __e.c_str(), __a.c_str());                    \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)
  #define check_less_warn(actual, expected)                                                        \
    do {                                                                                           \
      const auto &ttest_a_ref__ = (actual);                                                        \
      const auto &ttest_e_ref__ = (expected);                                                      \
      if (!ttest_eval_bool__(!!(ttest_a_ref__ < ttest_e_ref__))) {                                 \
        std::string __a = ttest_cpp__::to_string_safe(ttest_a_ref__);                              \
        std::string __e = ttest_cpp__::to_string_safe(ttest_e_ref__);                              \
        TTEST_WARN__(0, "expected < %s but got %s", __e.c_str(), __a.c_str());                     \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

  /* --- Exception testing macros --- */

  /* check_throws(expr) — must throw any exception */
  #define check_throws(expr)                                                                       \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
      }                                                                                            \
      TTEST_CHECK__(ttest_threw__, "expected exception but none was thrown");                      \
    } while (0)

  /* check_throws_as(expr, ExType) — must throw specific type */
  #define check_throws_as(expr, ExType)                                                            \
    do {                                                                                           \
      bool ttest_threw_correct__ = false;                                                          \
      bool ttest_threw_other__ = false;                                                            \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const ExType &) {                                                                   \
        ttest_threw_correct__ = true;                                                              \
      } catch (...) {                                                                              \
        ttest_threw_other__ = true;                                                                \
      }                                                                                            \
      if (ttest_threw_other__) {                                                                   \
        TTEST_CHECK__(0, "expected " #ExType " but got a different exception");                    \
      } else {                                                                                     \
        TTEST_CHECK__(ttest_threw_correct__, "expected " #ExType " but no exception was thrown");  \
      }                                                                                            \
    } while (0)

  /* check_throws_with(expr, msg) — must throw with what() containing msg */
  #define check_throws_with(expr, msg)                                                             \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      std::string ttest_what__;                                                                    \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const std::exception &__e) {                                                        \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = __e.what();                                                                 \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = "(non-std exception)";                                                      \
      }                                                                                            \
      if (!ttest_threw__) {                                                                        \
        TTEST_CHECK__(0, "expected exception with message \"%s\" but none was thrown", (msg));     \
      } else {                                                                                     \
        TTEST_CHECK__(ttest_what__.find(msg) != std::string::npos,                                 \
                      "expected exception message containing \"%s\" but got \"%s\"", (msg),        \
                      ttest_what__.c_str());                                                       \
      }                                                                                            \
    } while (0)

  /* check_nothrow(expr) — must not throw */
  #define check_nothrow(expr)                                                                      \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      std::string ttest_what__;                                                                    \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const std::exception &__e) {                                                        \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = __e.what();                                                                 \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = "(non-std exception)";                                                      \
      }                                                                                            \
      if (ttest_threw__) {                                                                         \
        TTEST_CHECK__(0, "expected no exception but got: %s", ttest_what__.c_str());               \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

  /* Non-fatal versions */
  #define check_throws_warn(expr)                                                                  \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
      }                                                                                            \
      TTEST_WARN__(ttest_threw__, "expected exception but none was thrown");                       \
    } while (0)

  #define check_throws_as_warn(expr, ExType)                                                       \
    do {                                                                                           \
      bool ttest_threw_correct__ = false;                                                          \
      bool ttest_threw_other__ = false;                                                            \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const ExType &) {                                                                   \
        ttest_threw_correct__ = true;                                                              \
      } catch (...) {                                                                              \
        ttest_threw_other__ = true;                                                                \
      }                                                                                            \
      if (ttest_threw_other__) {                                                                   \
        TTEST_WARN__(0, "expected " #ExType " but got a different exception");                     \
      } else {                                                                                     \
        TTEST_WARN__(ttest_threw_correct__, "expected " #ExType " but no exception was thrown");   \
      }                                                                                            \
    } while (0)

  #define check_throws_with_warn(expr, msg)                                                        \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      std::string ttest_what__;                                                                    \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const std::exception &__e) {                                                        \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = __e.what();                                                                 \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = "(non-std exception)";                                                      \
      }                                                                                            \
      if (!ttest_threw__) {                                                                        \
        TTEST_WARN__(0, "expected exception with message \"%s\" but none was thrown", (msg));      \
      } else {                                                                                     \
        TTEST_WARN__(ttest_what__.find(msg) != std::string::npos,                                  \
                     "expected exception message containing \"%s\" but got \"%s\"", (msg),         \
                     ttest_what__.c_str());                                                        \
      }                                                                                            \
    } while (0)

  #define check_nothrow_warn(expr)                                                                 \
    do {                                                                                           \
      bool ttest_threw__ = false;                                                                  \
      std::string ttest_what__;                                                                    \
      try {                                                                                        \
        (void)(expr);                                                                              \
      } catch (const ttest_fail_exception__ &) {                                                   \
        throw;                                                                                     \
      } catch (const std::exception &__e) {                                                        \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = __e.what();                                                                 \
      } catch (...) {                                                                              \
        ttest_threw__ = true;                                                                      \
        ttest_what__ = "(non-std exception)";                                                      \
      }                                                                                            \
      if (ttest_threw__) {                                                                         \
        TTEST_WARN__(0, "expected no exception but got: %s", ttest_what__.c_str());                \
      } else {                                                                                     \
        ++ttest_active_config__->assertion_count;                                                  \
      }                                                                                            \
    } while (0)

#endif /* __cplusplus */

#endif /* TINYTEST_HPP */
