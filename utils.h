#ifndef IMPOSSIBLE_PROGRAMS_UTILS_H
#define IMPOSSIBLE_PROGRAMS_UTILS_H

#define PRINT_EXPR_IMPL(expr, fmt_str, conversion)                             \
  do {                                                                         \
    auto __val = (expr);                                                       \
    printf("%s = " fmt_str "\n", #expr, conversion);                           \
  } while (false)

#define PRINT_BIT_EXPR(expr)                                                   \
  PRINT_EXPR_IMPL(expr, "%s", __val ? "true" : "false")

#define PRINT_NAT_EXPR(expr) PRINT_EXPR_IMPL(expr, "%llu", __val)

class Timer {
public:
  explicit Timer(const char *id) : id_(id) {
    start_ = std::chrono::high_resolution_clock::now();
  }

  ~Timer() {
    TimePointTy stop = std::chrono::high_resolution_clock::now();
    double us =
        std::chrono::duration_cast<std::chrono::microseconds>(stop - start_)
            .count();

    std::array<const char *, 3> unit = {"us", "ms", "s"};
    int unit_idx;
    for (unit_idx = 0; unit_idx < 3; unit_idx++) {
      if (us < std::pow(1000, unit_idx)) {
        break;
      }
    }
    if (unit_idx != 0) {
      unit_idx--;
    }

    printf("Time taken in %s: %0.3lf%s\n", id_, us / std::pow(1000, unit_idx),
           unit[unit_idx]);
  }

private:
  using TimePointTy =
      std::chrono::time_point<std::chrono::high_resolution_clock>;
  TimePointTy start_;
  const char *id_;
};

#define CREATE_TIMER() Timer __timer(__func__);

template <typename T> struct is_optional {
  static constexpr bool value = false;
};

template <typename T> struct is_optional<std::optional<T>> {
  static constexpr bool value = true;
};

// Macro that is a poor man's maybe monad.  If DoFoo() returns a value of type
// std::optional<T> then you can use this macro like this:
//
// S DoBar(T);
//
// std::optional<S> Bar() {
//   ASSIGN_OR_RETURN(T, var, DoFoo());
//   return DoBar(var);
// }
//
// Which is equivalent to:
//
// S DoBar(T);
//
// std::optional<S> Bar() {
//   T var;
//   std::optional<T> tmp = DoFoo();
//   if (!tmp.has_value()) {
//     return std::nullopt;
//   }
//   var = *tmp;
//   return DoBar(var);
// }
#define ASSIGN_OR_RETURN(type, var, expr)                                      \
  static_assert(is_optional<decltype(expr)>::value);                           \
  type var;                                                                    \
  do {                                                                         \
    auto __tmp = (expr);                                                       \
    if (!__tmp.has_value()) {                                                  \
      return std::nullopt;                                                     \
    }                                                                          \
    var = *__tmp;                                                              \
  } while (false)

#ifdef ENABLE_LOG
#define LOG(str, ...)                                                          \
  printf("[%s/%s:%d] " str "\n", __FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define LOG(str, ...) (void)0
#endif

// Used to check that we have only one active call to a function in a thread.
// Don't use this class directly, use ASSERT_ONLY_ONE_ACTIVE_CALL instead.
template <int *FuncId> class AssertOnlyOneActiveCall {
public:
  explicit AssertOnlyOneActiveCall(const char *function_name) {
    if (find_is_active_) {
      printf("Multiple active %s frames on the same thread!\n", function_name);
      abort();
    }

    find_is_active_ = true;
  }

  ~AssertOnlyOneActiveCall() { find_is_active_ = false; }

private:
  static thread_local bool find_is_active_;
};

template <int *FuncId>
/*static*/ bool thread_local AssertOnlyOneActiveCall<FuncId>::find_is_active_ =
    false;

#define ASSERT_ONLY_ONE_ACTIVE_CALL()                                          \
  static int __only_one_active_call_id;                                        \
  AssertOnlyOneActiveCall<&__only_one_active_call_id> __only_one_active_call(  \
      __func__);

#endif
