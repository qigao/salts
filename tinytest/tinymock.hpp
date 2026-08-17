#ifndef TINYMOCK_HPP
#define TINYMOCK_HPP

#define TINYTEST_NO_MAIN
#include "tinymock.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>


namespace tinymock {

  struct any_matcher_t {
    template <typename... Args> bool operator()(const Args &...) const { return true; }
  };

  constexpr any_matcher_t any{};

  struct cardinality {
    int min_calls = 1;
    int max_calls = 1; // -1 means unlimited
  };

  inline cardinality exactly(int n) { return cardinality{n, n}; }
  inline cardinality at_least(int n) { return cardinality{n, -1}; }
  inline cardinality between(int lo, int hi) { return cardinality{lo, hi}; }

  class sequence {
  public:
    std::size_t reserve() noexcept { return reserve_++; }
    std::size_t next() const noexcept { return expected_; }
    void advance() noexcept { ++expected_; }

  private:
    std::size_t reserve_ = 0;
    std::size_t expected_ = 0;
  };

  template <typename... Matchers> auto all_of(Matchers... matchers) {
    return [=](const auto &...args) { return (matchers(args...) && ...); };
  }

  template <typename... Matchers> auto any_of(Matchers... matchers) {
    return [=](const auto &...args) { return (matchers(args...) || ...); };
  }

  template <typename Matcher> auto not_(Matcher matcher) {
    return [=](const auto &...args) { return !matcher(args...); };
  }

  template <typename T> auto eq(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual == expected; };
  }

  template <typename T> auto ne(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual != expected; };
  }

  template <typename T> auto gt(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual > expected; };
  }

  template <typename T> auto lt(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual < expected; };
  }

  template <typename T> auto ge(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual >= expected; };
  }

  template <typename T> auto le(T expected) {
    return [expected = std::move(expected)](const auto &actual) { return actual <= expected; };
  }

  inline auto starts_with(std::string prefix) {
    return [prefix = std::move(prefix)](std::string_view actual) {
      return actual.rfind(prefix, 0) == 0;
    };
  }

  inline auto contains(std::string needle) {
    return [needle = std::move(needle)](std::string_view actual) {
      return actual.find(needle) != std::string_view::npos;
    };
  }

  inline auto is_null() {
    return [](const auto *actual) { return actual == nullptr; };
  }

  template <typename Signature> class function_mock;

  template <typename R, typename... Args> class function_mock<R(Args...)> {
  public:
    using decayed_tuple = std::tuple<std::decay_t<Args>...>;
    using matcher_type = std::function<bool(const std::decay_t<Args> &...)>;
    using action_type = std::function<R(std::decay_t<Args>...)>;

    class expectation_builder;

    enum class verify_mode { normal, naggy, strict };

    function_mock() = default;
    function_mock(const function_mock &) = delete;
    function_mock &operator=(const function_mock &) = delete;

    function_mock &strict() noexcept {
      mode_ = verify_mode::strict;
      return *this;
    }

    function_mock &naggy() noexcept {
      mode_ = verify_mode::naggy;
      return *this;
    }

    std::size_t unexpected_calls() const noexcept { return unexpected_calls_; }

    template <typename Matcher, typename Action>
    function_mock &on(Matcher matcher, Action action, int times = 1) {
      return on(std::move(matcher), std::move(action), exactly(times));
    }

    template <typename Matcher, typename Action>
    function_mock &on(Matcher matcher, Action action, cardinality calls) {
      expectation entry;
      entry.matcher = matcher_type(std::move(matcher));
      entry.actions.push_back(action_type(std::move(action)));
      entry.min_calls = calls.min_calls;
      entry.max_calls = calls.max_calls;
      expectations_.push_back(std::move(entry));
      return *this;
    }

    template <typename Matcher, typename Action>
    function_mock &on(Matcher matcher, Action action, sequence &seq,
                      cardinality calls = exactly(1)) {
      expectation entry;
      entry.matcher = matcher_type(std::move(matcher));
      entry.actions.push_back(action_type(std::move(action)));
      entry.min_calls = calls.min_calls;
      entry.max_calls = calls.max_calls;
      entry.seq = &seq;
      entry.seq_index = seq.reserve();
      expectations_.push_back(std::move(entry));
      return *this;
    }

    template <typename Matcher> expectation_builder expect(Matcher matcher) {
      return expectation_builder(*this, matcher_type(std::move(matcher)));
    }

    template <typename Action> function_mock &then_return(Action action, int times = 1) {
      return on([](const std::decay_t<Args> &...) { return true; }, std::move(action),
                exactly(times));
    }

    function_mock &then_throw(std::exception_ptr exception, int times = 1) {
      return on([](const std::decay_t<Args> &...) { return true; },
                [exception](std::decay_t<Args>...) -> R {
                  std::rethrow_exception(exception);
                  if constexpr (!std::is_void_v<R>) return R{};
                },
                exactly(times));
    }

    template <typename Action> function_mock &set_default_action(Action action) {
      default_action_ = action_type(std::move(action));
      return *this;
    }

    template <typename Action> function_mock &on_call(Action action) {
      return set_default_action(std::move(action));
    }

    R invoke(Args... args) {
      calls_.emplace_back(std::decay_t<Args>(args)...);

      for (expectation &entry : expectations_) {
        if (entry.max_calls >= 0 && entry.seen_calls >= entry.max_calls) continue;
        if (!entry.matcher || match(entry.matcher, args...)) {
          if (entry.seq && entry.seq_index != entry.seq->next())
            throw std::runtime_error("tinymock: call is out of sequence");
          if (entry.seq) entry.seq->advance();

          ++entry.seen_calls;
          return call_expectation(entry, args...);
        }
      }

      if (default_action_) return call_action(default_action_, args...);

      ++unexpected_calls_;
      if (mode_ == verify_mode::naggy) {
        if constexpr (std::is_void_v<R>) {
          return;
        } else if constexpr (std::is_default_constructible_v<R>) {
          return R{};
        } else {
          throw std::runtime_error("tinymock: naggy mode cannot default-construct return value");
        }
      }

      throw std::runtime_error("tinymock: unscripted function_mock call");
    }

    void verify() const {
      for (const expectation &entry : expectations_) {
        if (entry.seen_calls < entry.min_calls ||
            (entry.max_calls >= 0 && entry.seen_calls > entry.max_calls)) {
          throw std::runtime_error(
              "tinymock: expected calls in [" + std::to_string(entry.min_calls) + ", " +
              (entry.max_calls >= 0 ? std::to_string(entry.max_calls) : "inf") + "], got " +
              std::to_string(entry.seen_calls));
        }
      }
    }

    const std::vector<decayed_tuple> &calls() const noexcept { return calls_; }

  private:
    struct expectation {
      matcher_type matcher;
      std::vector<action_type> actions;
      action_type repeated_action;
      std::size_t action_index = 0;
      int min_calls = 1;
      int max_calls = 1;
      int seen_calls = 0;
      sequence *seq = nullptr;
      std::size_t seq_index = 0;
    };

    template <typename Matcher, typename... Actual>
    static bool match(Matcher &matcher, Actual &&...actual) {
      return matcher(std::forward<Actual>(actual)...);
    }

    template <typename Action, typename... Actual>
    static R call_action(Action &action, Actual &&...actual) {
      if constexpr (std::is_void_v<R>) {
        action(std::forward<Actual>(actual)...);
      } else {
        return action(std::forward<Actual>(actual)...);
      }
    }

    R call_expectation(expectation &entry, Args &...args) {
      if (entry.action_index < entry.actions.size()) {
        action_type &action = entry.actions[entry.action_index++];
        return call_action(action, args...);
      }
      if (entry.repeated_action) return call_action(entry.repeated_action, args...);
      throw std::runtime_error("tinymock: expectation has no action");
    }

    std::vector<expectation> expectations_;
    std::vector<decayed_tuple> calls_;
    action_type default_action_;
    verify_mode mode_ = verify_mode::normal;
    std::size_t unexpected_calls_ = 0;
  };

  template <typename R, typename... Args> class function_mock<R(Args...)>::expectation_builder {
  public:
    expectation_builder(function_mock &owner, typename function_mock::matcher_type matcher)
        : owner_(&owner), matcher_(std::move(matcher)) {}

    expectation_builder(const expectation_builder &) = delete;
    expectation_builder &operator=(const expectation_builder &) = delete;

    expectation_builder(expectation_builder &&other) noexcept
        : owner_(other.owner_), matcher_(std::move(other.matcher_)),
          actions_(std::move(other.actions_)), repeated_action_(std::move(other.repeated_action_)),
          min_calls_(other.min_calls_), max_calls_(other.max_calls_), seq_(other.seq_),
          seq_index_(other.seq_index_) {
      other.owner_ = nullptr;
    }

    ~expectation_builder() { commit(); }

    template <typename Action> expectation_builder &will_once(Action action) {
      actions_.push_back(typename function_mock::action_type(std::move(action)));
      return *this;
    }

    template <typename Action> expectation_builder &will_repeatedly(Action action) {
      repeated_action_ = typename function_mock::action_type(std::move(action));
      return *this;
    }

    expectation_builder &times(int n) {
      min_calls_ = n;
      max_calls_ = n;
      return *this;
    }

    expectation_builder &times(cardinality calls) {
      min_calls_ = calls.min_calls;
      max_calls_ = calls.max_calls;
      return *this;
    }

    expectation_builder &in_sequence(sequence &seq) {
      seq_ = &seq;
      seq_index_ = seq.reserve();
      return *this;
    }

  private:
    void commit() {
      if (owner_ == nullptr) return;
      typename function_mock::expectation entry;
      entry.matcher = std::move(matcher_);
      entry.actions = std::move(actions_);
      entry.repeated_action = std::move(repeated_action_);
      entry.min_calls = min_calls_;
      entry.max_calls = max_calls_;
      entry.seq = seq_;
      entry.seq_index = seq_index_;
      owner_->expectations_.push_back(std::move(entry));
      owner_ = nullptr;
    }

    function_mock *owner_;
    typename function_mock::matcher_type matcher_;
    std::vector<typename function_mock::action_type> actions_;
    typename function_mock::action_type repeated_action_;
    int min_calls_ = 1;
    int max_calls_ = 1;
    sequence *seq_ = nullptr;
    std::size_t seq_index_ = 0;
  };

} // namespace tinymock
#define TINYMOCK_CPP_MOCK_METHOD0(R, NAME)                                                         \
  ::tinymock::function_mock<R()> NAME##_mock;                                                      \
  R NAME() override { return NAME##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_NAMED(MEMBER, R, NAME)                                           \
  ::tinymock::function_mock<R()> MEMBER##_mock;                                                    \
  R NAME() override { return MEMBER##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_CONST(R, NAME)                                                   \
  mutable ::tinymock::function_mock<R()> NAME##_mock;                                              \
  R NAME() const override { return NAME##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_NAMED_CONST(MEMBER, R, NAME)                                     \
  mutable ::tinymock::function_mock<R()> MEMBER##_mock;                                            \
  R NAME() const override { return MEMBER##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_NOEXCEPT(R, NAME)                                                \
  ::tinymock::function_mock<R()> NAME##_mock;                                                      \
  R NAME() noexcept override { return NAME##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_NAMED_NOEXCEPT(MEMBER, R, NAME)                                  \
  ::tinymock::function_mock<R()> MEMBER##_mock;                                                    \
  R NAME() noexcept override { return MEMBER##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_CONST_NOEXCEPT(R, NAME)                                          \
  mutable ::tinymock::function_mock<R()> NAME##_mock;                                              \
  R NAME() const noexcept override { return NAME##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD0_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME)                            \
  mutable ::tinymock::function_mock<R()> MEMBER##_mock;                                            \
  R NAME() const noexcept override { return MEMBER##_mock.invoke(); }

#define TINYMOCK_CPP_MOCK_METHOD1(R, NAME, T0)                                                     \
  ::tinymock::function_mock<R(T0)> NAME##_mock;                                                    \
  R NAME(T0 a0) override { return NAME##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_NAMED(MEMBER, R, NAME, T0)                                       \
  ::tinymock::function_mock<R(T0)> MEMBER##_mock;                                                  \
  R NAME(T0 a0) override { return MEMBER##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_CONST(R, NAME, T0)                                               \
  mutable ::tinymock::function_mock<R(T0)> NAME##_mock;                                            \
  R NAME(T0 a0) const override { return NAME##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_NAMED_CONST(MEMBER, R, NAME, T0)                                 \
  mutable ::tinymock::function_mock<R(T0)> MEMBER##_mock;                                          \
  R NAME(T0 a0) const override { return MEMBER##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_NOEXCEPT(R, NAME, T0)                                            \
  ::tinymock::function_mock<R(T0)> NAME##_mock;                                                    \
  R NAME(T0 a0) noexcept override { return NAME##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_NAMED_NOEXCEPT(MEMBER, R, NAME, T0)                              \
  ::tinymock::function_mock<R(T0)> MEMBER##_mock;                                                  \
  R NAME(T0 a0) noexcept override { return MEMBER##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_CONST_NOEXCEPT(R, NAME, T0)                                      \
  mutable ::tinymock::function_mock<R(T0)> NAME##_mock;                                            \
  R NAME(T0 a0) const noexcept override { return NAME##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD1_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0)                        \
  mutable ::tinymock::function_mock<R(T0)> MEMBER##_mock;                                          \
  R NAME(T0 a0) const noexcept override { return MEMBER##_mock.invoke(std::forward<T0>(a0)); }

#define TINYMOCK_CPP_MOCK_METHOD2(R, NAME, T0, T1)                                                 \
  ::tinymock::function_mock<R(T0, T1)> NAME##_mock;                                                \
  R NAME(T0 a0, T1 a1) override {                                                                  \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_NAMED(MEMBER, R, NAME, T0, T1)                                   \
  ::tinymock::function_mock<R(T0, T1)> MEMBER##_mock;                                              \
  R NAME(T0 a0, T1 a1) override {                                                                  \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_CONST(R, NAME, T0, T1)                                           \
  mutable ::tinymock::function_mock<R(T0, T1)> NAME##_mock;                                        \
  R NAME(T0 a0, T1 a1) const override {                                                            \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_NAMED_CONST(MEMBER, R, NAME, T0, T1)                             \
  mutable ::tinymock::function_mock<R(T0, T1)> MEMBER##_mock;                                      \
  R NAME(T0 a0, T1 a1) const override {                                                            \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_NOEXCEPT(R, NAME, T0, T1)                                        \
  ::tinymock::function_mock<R(T0, T1)> NAME##_mock;                                                \
  R NAME(T0 a0, T1 a1) noexcept override {                                                         \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1)                          \
  ::tinymock::function_mock<R(T0, T1)> MEMBER##_mock;                                              \
  R NAME(T0 a0, T1 a1) noexcept override {                                                         \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_CONST_NOEXCEPT(R, NAME, T0, T1)                                  \
  mutable ::tinymock::function_mock<R(T0, T1)> NAME##_mock;                                        \
  R NAME(T0 a0, T1 a1) const noexcept override {                                                   \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD2_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1)                    \
  mutable ::tinymock::function_mock<R(T0, T1)> MEMBER##_mock;                                      \
  R NAME(T0 a0, T1 a1) const noexcept override {                                                   \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD3(R, NAME, T0, T1, T2)                                             \
  ::tinymock::function_mock<R(T0, T1, T2)> NAME##_mock;                                            \
  R NAME(T0 a0, T1 a1, T2 a2) override {                                                           \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_NAMED(MEMBER, R, NAME, T0, T1, T2)                               \
  ::tinymock::function_mock<R(T0, T1, T2)> MEMBER##_mock;                                          \
  R NAME(T0 a0, T1 a1, T2 a2) override {                                                           \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_CONST(R, NAME, T0, T1, T2)                                       \
  mutable ::tinymock::function_mock<R(T0, T1, T2)> NAME##_mock;                                    \
  R NAME(T0 a0, T1 a1, T2 a2) const override {                                                     \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2)                         \
  mutable ::tinymock::function_mock<R(T0, T1, T2)> MEMBER##_mock;                                  \
  R NAME(T0 a0, T1 a1, T2 a2) const override {                                                     \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_NOEXCEPT(R, NAME, T0, T1, T2)                                    \
  ::tinymock::function_mock<R(T0, T1, T2)> NAME##_mock;                                            \
  R NAME(T0 a0, T1 a1, T2 a2) noexcept override {                                                  \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2)                      \
  ::tinymock::function_mock<R(T0, T1, T2)> MEMBER##_mock;                                          \
  R NAME(T0 a0, T1 a1, T2 a2) noexcept override {                                                  \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_CONST_NOEXCEPT(R, NAME, T0, T1, T2)                              \
  mutable ::tinymock::function_mock<R(T0, T1, T2)> NAME##_mock;                                    \
  R NAME(T0 a0, T1 a1, T2 a2) const noexcept override {                                            \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD3_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2)                \
  mutable ::tinymock::function_mock<R(T0, T1, T2)> MEMBER##_mock;                                  \
  R NAME(T0 a0, T1 a1, T2 a2) const noexcept override {                                            \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD4(R, NAME, T0, T1, T2, T3)                                         \
  ::tinymock::function_mock<R(T0, T1, T2, T3)> NAME##_mock;                                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) override {                                                    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_NAMED(MEMBER, R, NAME, T0, T1, T2, T3)                           \
  ::tinymock::function_mock<R(T0, T1, T2, T3)> MEMBER##_mock;                                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) override {                                                    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_CONST(R, NAME, T0, T1, T2, T3)                                   \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3)> NAME##_mock;                                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) const override {                                              \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3)                     \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3)> MEMBER##_mock;                              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) const override {                                              \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_NOEXCEPT(R, NAME, T0, T1, T2, T3)                                \
  ::tinymock::function_mock<R(T0, T1, T2, T3)> NAME##_mock;                                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) noexcept override {                                           \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3)                  \
  ::tinymock::function_mock<R(T0, T1, T2, T3)> MEMBER##_mock;                                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) noexcept override {                                           \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3)                          \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3)> NAME##_mock;                                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) const noexcept override {                                     \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD4_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3)            \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3)> MEMBER##_mock;                              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3) const noexcept override {                                     \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD5(R, NAME, T0, T1, T2, T3, T4)                                     \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> NAME##_mock;                                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) override {                                             \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4)                       \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> MEMBER##_mock;                                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) override {                                             \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_CONST(R, NAME, T0, T1, T2, T3, T4)                               \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> NAME##_mock;                            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) const override {                                       \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4)                 \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> MEMBER##_mock;                          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) const override {                                       \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4)                            \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> NAME##_mock;                                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) noexcept override {                                    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4)              \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> MEMBER##_mock;                                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) noexcept override {                                    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4)                      \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> NAME##_mock;                            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) const noexcept override {                              \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD5_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4)        \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4)> MEMBER##_mock;                          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4) const noexcept override {                              \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD6(R, NAME, T0, T1, T2, T3, T4, T5)                                 \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> NAME##_mock;                                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) override {                                      \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5)                   \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> MEMBER##_mock;                              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) override {                                      \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_CONST(R, NAME, T0, T1, T2, T3, T4, T5)                           \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> NAME##_mock;                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) const override {                                \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5)             \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> MEMBER##_mock;                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) const override {                                \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5)                        \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> NAME##_mock;                                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) noexcept override {                             \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5)          \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> MEMBER##_mock;                              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) noexcept override {                             \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5)                  \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> NAME##_mock;                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) const noexcept override {                       \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD6_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5)    \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5)> MEMBER##_mock;                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) const noexcept override {                       \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD7(R, NAME, T0, T1, T2, T3, T4, T5, T6)                             \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> NAME##_mock;                            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) override {                               \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6)               \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> MEMBER##_mock;                          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) override {                               \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6)                       \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> NAME##_mock;                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) const override {                         \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6)         \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> MEMBER##_mock;                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) const override {                         \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6)                    \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> NAME##_mock;                            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) noexcept override {                      \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6)      \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> MEMBER##_mock;                          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) noexcept override {                      \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6)              \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> NAME##_mock;                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) const noexcept override {                \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD7_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,    \
                                                       T6)                                         \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6)> MEMBER##_mock;                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) const noexcept override {                \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD8(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)                         \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> NAME##_mock;                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) override {                        \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)           \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> MEMBER##_mock;                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) override {                        \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)                   \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> NAME##_mock;                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) const override {                  \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)     \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> MEMBER##_mock;              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) const override {                  \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)                \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> NAME##_mock;                        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) noexcept override {               \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)  \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> MEMBER##_mock;                      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) noexcept override {               \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7)          \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> NAME##_mock;                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) const noexcept override {         \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7));                         \
  }

#define TINYMOCK_CPP_MOCK_METHOD8_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,    \
                                                       T6, T7)                                     \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7)> MEMBER##_mock;              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) const noexcept override {         \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD9(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8)                     \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> NAME##_mock;                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) override {                 \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8)       \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> MEMBER##_mock;                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) override {                 \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8)               \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> NAME##_mock;            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) const override {           \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8) \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> MEMBER##_mock;          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) const override {           \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8)            \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> NAME##_mock;                    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) noexcept override {        \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7,  \
                                                 T8)                                               \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> MEMBER##_mock;                  \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) noexcept override {        \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8)      \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> NAME##_mock;            \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) const noexcept override {  \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8));   \
  }

#define TINYMOCK_CPP_MOCK_METHOD9_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,    \
                                                       T6, T7, T8)                                 \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8)> MEMBER##_mock;          \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) const noexcept override {  \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8)); \
  }

#define TINYMOCK_CPP_MOCK_METHOD10(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)                \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> NAME##_mock;                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) override {          \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)  \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> MEMBER##_mock;              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) override {          \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)          \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> NAME##_mock;        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) const override {    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7,    \
                                               T8, T9)                                             \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> MEMBER##_mock;      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) const override {    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)       \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> NAME##_mock;                \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) noexcept override { \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, \
                                                  T8, T9)                                          \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> MEMBER##_mock;              \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9) noexcept override { \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9) \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> NAME##_mock;        \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9)                     \
      const noexcept override {                                                                    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9));                                               \
  }

#define TINYMOCK_CPP_MOCK_METHOD10_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,   \
                                                        T6, T7, T8, T9)                            \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9)> MEMBER##_mock;      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9)                     \
      const noexcept override {                                                                    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD11(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)           \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> NAME##_mock;           \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10) override { \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9,  \
                                         T10)                                                      \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> MEMBER##_mock;         \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10) override { \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)     \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> NAME##_mock;   \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10)            \
      const override {                                                                             \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7,    \
                                               T8, T9, T10)                                        \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> MEMBER##_mock; \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10)            \
      const override {                                                                             \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)  \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> NAME##_mock;           \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9,                     \
         T10 a10) noexcept override {                                                              \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, \
                                                  T8, T9, T10)                                     \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> MEMBER##_mock;         \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9,                     \
         T10 a10) noexcept override {                                                              \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, \
                                                  T10)                                             \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> NAME##_mock;   \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10)            \
      const noexcept override {                                                                    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10));                       \
  }

#define TINYMOCK_CPP_MOCK_METHOD11_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,   \
                                                        T6, T7, T8, T9, T10)                       \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10)> MEMBER##_mock; \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10)            \
      const noexcept override {                                                                    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD12(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)      \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)> NAME##_mock;      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      override {                                                                                   \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9,  \
                                         T10, T11)                                                 \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)> MEMBER##_mock;    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      override {                                                                                   \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11));                                           \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10,     \
                                         T11)                                                      \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)>           \
      NAME##_mock;                                                                                 \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      const override {                                                                             \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7,    \
                                               T8, T9, T10, T11)                                   \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)>           \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      const override {                                                                             \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11));                                           \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10,  \
                                            T11)                                                   \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)> NAME##_mock;      \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10,            \
         T11 a11) noexcept override {                                                              \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, \
                                                  T8, T9, T10, T11)                                \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)> MEMBER##_mock;    \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10,            \
         T11 a11) noexcept override {                                                              \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11));                                           \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, \
                                                  T10, T11)                                        \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)>           \
      NAME##_mock;                                                                                 \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      const noexcept override {                                                                    \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11));                                             \
  }

#define TINYMOCK_CPP_MOCK_METHOD12_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,   \
                                                        T6, T7, T8, T9, T10, T11)                  \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11)>           \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11)   \
      const noexcept override {                                                                    \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11));                                           \
  }

#define TINYMOCK_CPP_MOCK_METHOD13(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12) \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)> NAME##_mock; \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) override {                                                                       \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11), std::forward<T12>(a12));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_NAMED(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9,  \
                                         T10, T11, T12)                                            \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>              \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) override {                                                                       \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11), std::forward<T12>(a12));                   \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_CONST(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10,     \
                                         T11, T12)                                                 \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>      \
      NAME##_mock;                                                                                 \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) const override {                                                                 \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11), std::forward<T12>(a12));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_NAMED_CONST(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7,    \
                                               T8, T9, T10, T11, T12)                              \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>      \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) const override {                                                                 \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11), std::forward<T12>(a12));                   \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10,  \
                                            T11, T12)                                              \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)> NAME##_mock; \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) noexcept override {                                                              \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11), std::forward<T12>(a12));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_NAMED_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, \
                                                  T8, T9, T10, T11, T12)                           \
  ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>              \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) noexcept override {                                                              \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11), std::forward<T12>(a12));                   \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_CONST_NOEXCEPT(R, NAME, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, \
                                                  T10, T11, T12)                                   \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>      \
      NAME##_mock;                                                                                 \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) const noexcept override {                                                        \
    return NAME##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),    \
                              std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),    \
                              std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),    \
                              std::forward<T9>(a9), std::forward<T10>(a10),                        \
                              std::forward<T11>(a11), std::forward<T12>(a12));                     \
  }

#define TINYMOCK_CPP_MOCK_METHOD13_NAMED_CONST_NOEXCEPT(MEMBER, R, NAME, T0, T1, T2, T3, T4, T5,   \
                                                        T6, T7, T8, T9, T10, T11, T12)             \
  mutable ::tinymock::function_mock<R(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12)>      \
      MEMBER##_mock;                                                                               \
  R NAME(T0 a0, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8, T9 a9, T10 a10, T11 a11,   \
         T12 a12) const noexcept override {                                                        \
    return MEMBER##_mock.invoke(std::forward<T0>(a0), std::forward<T1>(a1), std::forward<T2>(a2),  \
                                std::forward<T3>(a3), std::forward<T4>(a4), std::forward<T5>(a5),  \
                                std::forward<T6>(a6), std::forward<T7>(a7), std::forward<T8>(a8),  \
                                std::forward<T9>(a9), std::forward<T10>(a10),                      \
                                std::forward<T11>(a11), std::forward<T12>(a12));                   \
  }

#endif
