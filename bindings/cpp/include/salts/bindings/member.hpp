#ifndef SALTS_BINDINGS_MEMBER_HPP
#define SALTS_BINDINGS_MEMBER_HPP

#include <salts/bindings/object.hpp>

#include <cmeta/invokable.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Salts {
namespace detail {

template <typename Return, typename... Args>
struct MemberValueSignature {
  static constexpr cmeta_sig value = CMETA_SIG_INVALID;
};

#define SALTS_MEMBER_SIGNATURE_U(in, ret)                                  \
  template <>                                                               \
  struct MemberValueSignature<CMETA_TYPE_CTYPE(ret),                        \
                              CMETA_TYPE_CTYPE(in)> {                        \
    static constexpr cmeta_sig value =                                      \
        CMETA_SIG_NAME(CMETA_U_ID(in, ret));                                \
  };

#define SALTS_MEMBER_SIGNATURE_B(a, b, ret)                                \
  template <>                                                               \
  struct MemberValueSignature<CMETA_TYPE_CTYPE(ret),                        \
                              CMETA_TYPE_CTYPE(a),                           \
                              CMETA_TYPE_CTYPE(b)> {                         \
    static constexpr cmeta_sig value =                                      \
        CMETA_SIG_NAME(CMETA_B_ID(a, b, ret));                              \
  };

CMETA_VALUE_SIGNATURES(SALTS_MEMBER_SIGNATURE_U, SALTS_MEMBER_SIGNATURE_B)

#undef SALTS_MEMBER_SIGNATURE_U
#undef SALTS_MEMBER_SIGNATURE_B

template <typename T>
inline constexpr unsigned char member_class_token = 0u;

template <typename Class>
constexpr const void *member_class_token_of() noexcept {
  return &member_class_token<std::remove_cv_t<Class>>;
}

using MemberBindFn = cmeta_status (*)(
    void *object, const cmeta_function_data_desc *data,
    cmeta_object_method_binding *out);

struct MemberMethodEntry {
  const cmeta_receiver_method *method;
  const cmeta_function_data_desc *data;
  MemberBindFn bind;
  const void *class_token;
  cmeta_sig signature;
};

inline bool member_signature_matches(
    cmeta_sig signature, const cmeta_function_desc *function) noexcept {
  cmeta_fn meta{};
  const cmeta_sig_desc *shape;
  std::size_t i;

  if (!cmeta_function_desc_valid(function) ||
      signature == CMETA_SIG_INVALID)
    return false;

  meta.sig = signature;
  shape = cmeta_fn_signature(meta);
  if (shape == nullptr || shape->protocol != CMETA_FN_PROTOCOL_VALUE ||
      shape->param_count != function->param_count ||
      shape->return_type == nullptr ||
      !cmeta_type_equal(shape->return_type, function->return_type))
    return false;

  for (i = 0u; i < function->param_count; ++i) {
    const cmeta_param_desc *param = cmeta_function_param(function, i);
    if (param == nullptr || shape->params[i] == nullptr ||
        !cmeta_type_equal(shape->params[i], param->type))
      return false;
  }

  return true;
}

template <auto Member, typename Return, typename Class, typename... Args>
struct MemberThunkBase {
  using return_type = std::remove_cv_t<Return>;
  using class_type = std::remove_cv_t<Class>;
  using argument_tuple =
      std::tuple<std::remove_cv_t<std::remove_reference_t<Args>>...>;

  static constexpr cmeta_sig signature =
      MemberValueSignature<
          return_type,
          std::remove_cv_t<std::remove_reference_t<Args>>...>::value;

  static_assert(signature != CMETA_SIG_INVALID,
                "C++ member signature is not admitted by the active "
                "CMeta value-signature policy");
  static_assert(!std::is_reference_v<Return>,
                "C++ member thunk does not admit reference returns");
  static_assert(std::is_trivially_copyable_v<return_type>,
                "C++ member thunk return must follow CMeta by-value "
                "trivial-copy semantics");
  static_assert((!std::is_reference_v<Args> && ...),
                "C++ member thunk does not admit reference parameters");
  static_assert(
      (std::is_trivially_copyable_v<
           std::remove_cv_t<std::remove_reference_t<Args>>> && ...),
      "C++ member thunk parameters must follow CMeta by-value "
      "trivial-copy semantics");
  static_assert(sizeof(void *) <= CMETA_CAPTURE_INLINE,
                "CMeta callable capture cannot hold one receiver pointer");

  template <std::size_t Index>
  static bool load_one(argument_tuple &values,
                       const void *const *args) noexcept {
    using value_type =
        std::tuple_element_t<Index, argument_tuple>;
    if (args == nullptr || args[Index] == nullptr)
      return false;
    std::memcpy(
        &std::get<Index>(values), args[Index], sizeof(value_type));
    return true;
  }

  template <std::size_t... Index>
  static bool load_all(argument_tuple &values,
                       const void *const *args,
                       std::index_sequence<Index...>) noexcept {
    return (load_one<Index>(values, args) && ...);
  }

  template <std::size_t... Index>
  static return_type call(
      class_type *receiver, argument_tuple &values,
      std::index_sequence<Index...>) {
    return (receiver->*Member)(std::get<Index>(values)...);
  }

  static bool invoke(
      const cmeta_callable *self, void *out,
      const void *const *args) noexcept {
    void *raw_receiver = nullptr;
    class_type *receiver;
    argument_tuple values{};

    if (self == nullptr || out == nullptr ||
        self->capture_size != sizeof(raw_receiver))
      return false;

    std::memcpy(
        &raw_receiver, self->capture.bytes, sizeof(raw_receiver));
    receiver = static_cast<class_type *>(raw_receiver);
    if (receiver == nullptr ||
        !load_all(
            values, args, std::index_sequence_for<Args...>{}))
      return false;

    const return_type result = call(
        receiver, values, std::index_sequence_for<Args...>{});
    std::memcpy(out, &result, sizeof(result));
    return true;
  }

  static cmeta_status bind(
      void *object, const cmeta_function_data_desc *data,
      cmeta_object_method_binding *out) noexcept {
    cmeta_callable callable{};

    if (object == nullptr || out == nullptr ||
        !cmeta_function_data_desc_valid(data))
      return CMETA_INVALID_ARGUMENT;

    *out = cmeta_object_method_binding{
        sizeof(cmeta_object_method_binding), nullptr, {}};

    callable.meta.sig = signature;
    callable.meta.effects = data->function->effects;
    callable.meta.properties = data->function->properties;
    callable.resolve = nullptr;
    callable.invoke = &invoke;
    callable.generate = nullptr;
    callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
    callable.capture_size = sizeof(object);
    std::memcpy(
        callable.capture.bytes, &object, sizeof(object));

    out->data = data;
    out->callable = callable;
    return CMETA_OK;
  }

  static MemberMethodEntry entry(
      const cmeta_receiver_method *method,
      const cmeta_function_data_desc *data) noexcept {
    return MemberMethodEntry{
        method, data, &bind, member_class_token_of<class_type>(),
        signature};
  }
};

template <auto Member, typename Signature = decltype(Member)>
struct MemberThunk {
  static_assert(
      sizeof(Signature) == 0,
      "Unsupported C++ member-function qualifier or signature");
};

template <auto Member, typename Return, typename Class, typename... Args>
struct MemberThunk<Member, Return (Class::*)(Args...)>
    : MemberThunkBase<Member, Return, Class, Args...> {};

template <auto Member, typename Return, typename Class, typename... Args>
struct MemberThunk<Member, Return (Class::*)(Args...) const>
    : MemberThunkBase<Member, Return, Class, Args...> {};

template <auto Member, typename Return, typename Class, typename... Args>
struct MemberThunk<Member, Return (Class::*)(Args...) noexcept>
    : MemberThunkBase<Member, Return, Class, Args...> {};

template <auto Member, typename Return, typename Class, typename... Args>
struct MemberThunk<Member, Return (Class::*)(Args...) const noexcept>
    : MemberThunkBase<Member, Return, Class, Args...> {};

inline bool member_entry_valid(
    const MemberMethodEntry &entry) noexcept {
  return entry.method != nullptr && entry.bind != nullptr &&
         entry.class_token != nullptr &&
         entry.signature != CMETA_SIG_INVALID &&
         cmeta_receiver_method_reflection_valid(entry.method) &&
         cmeta_function_data_desc_valid(entry.data) &&
         cmeta_receiver_method_projection_valid(
             entry.method, entry.data->function) &&
         member_signature_matches(
             entry.signature, entry.data->function);
}

}  // namespace detail

template <auto Member>
inline detail::MemberMethodEntry member_method(
    const cmeta_receiver_method *method,
    const cmeta_function_data_desc *data) noexcept {
  return detail::MemberThunk<Member>::entry(method, data);
}

template <std::size_t Count>
class MemberMethodProvider {
 public:
  static_assert(Count != 0u,
                "MemberMethodProvider requires at least one method");

  template <
      typename... Entry,
      std::enable_if_t<sizeof...(Entry) == Count, int> = 0>
  explicit MemberMethodProvider(
      const cmeta_receiver_method_set *methods,
      Entry &&...entry) noexcept
      : methods_(methods),
        entries_{
            {std::forward<Entry>(entry)...}},
        provider_{
            sizeof(cmeta_object_method_provider),
            methods,
            this,
            &bind} {}

  MemberMethodProvider(const MemberMethodProvider &) = delete;
  MemberMethodProvider &operator=(const MemberMethodProvider &) = delete;
  MemberMethodProvider(MemberMethodProvider &&) = delete;
  MemberMethodProvider &operator=(MemberMethodProvider &&) = delete;

  bool valid() const noexcept {
    if (!cmeta_receiver_method_set_valid(methods_) ||
        methods_->method_count != Count)
      return false;

    const void *class_token = entries_[0].class_token;
    if (class_token == nullptr)
      return false;

    for (std::size_t i = 0u; i < Count; ++i) {
      const detail::MemberMethodEntry &entry = entries_[i];
      if (entry.method != &methods_->methods[i] ||
          entry.class_token != class_token ||
          !detail::member_entry_valid(entry))
        return false;
    }

    return true;
  }

  template <typename Class>
  bool accepts() const noexcept {
    return valid() &&
           entries_[0].class_token ==
               detail::member_class_token_of<Class>();
  }

  const cmeta_object_method_provider *c_provider() const noexcept {
    return valid() ? &provider_ : nullptr;
  }

 private:
  static cmeta_status bind(
      void *context, void *object,
      const cmeta_receiver_method *method,
      cmeta_object_method_binding *out) noexcept {
    auto *self = static_cast<MemberMethodProvider *>(context);

    if (self == nullptr || object == nullptr ||
        method == nullptr || out == nullptr || !self->valid())
      return CMETA_INVALID_ARGUMENT;

    for (const auto &entry : self->entries_) {
      if (entry.method == method)
        return entry.bind(object, entry.data, out);
    }

    return CMETA_INVALID_ARGUMENT;
  }

  const cmeta_receiver_method_set *methods_;
  std::array<detail::MemberMethodEntry, Count> entries_;
  cmeta_object_method_provider provider_;
};

template <typename... Entry>
MemberMethodProvider(
    const cmeta_receiver_method_set *, Entry &&...)
    -> MemberMethodProvider<sizeof...(Entry)>;

}  // namespace Salts

#endif
