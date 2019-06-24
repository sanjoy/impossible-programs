#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace {
template <typename T> struct is_optional {
  static constexpr bool value = false;
};

template <typename T> struct is_optional<std::optional<T>> {
  static constexpr bool value = true;
};

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

using Bit = bool;
using Natural = uint64_t;

class BitStream {
public:
  virtual std::optional<Bit> Get(Natural) = 0;
  virtual ~BitStream() {}
};

class StrictBitStream : public BitStream {
public:
  explicit StrictBitStream(std::vector<Bit> values)
      : values_(std::move(values)) {}
  std::optional<Bit> Get(Natural idx) override { return values_[idx]; }
  virtual ~StrictBitStream() override {}

private:
  std::vector<bool> values_;
};

class LazyBitStream : public BitStream {
public:
  explicit LazyBitStream(const std::vector<Bit> *values) : values_(*values) {}
  virtual ~LazyBitStream() override {}

  std::optional<Bit> Get(Natural idx) override {
    if (idx >= values_.size()) {
      max_index_requested_ = std::max(max_index_requested_, idx);
      return std::nullopt;
    }

    return values_[idx];
  }

  Natural max_index_requested() const { return max_index_requested_; }

private:
  Natural max_index_requested_ = 0;
  const std::vector<bool> &values_;
};

using Predicate = std::function<std::optional<Bit>(BitStream *)>;

void IntegerToBits(uint64_t integer, std::vector<bool> *bits) {
  for (int i = 0; i < bits->size(); i++) {
    (*bits)[i] = integer & (1ull << i);
  }
}

class OnlyOneActiveFind {
public:
  OnlyOneActiveFind() {
    if (find_is_active_) {
      abort();
    }

    find_is_active_ = true;
  }

  ~OnlyOneActiveFind() { find_is_active_ = false; }

private:
  static thread_local bool find_is_active_;
};

/*static*/ bool thread_local OnlyOneActiveFind::find_is_active_ = false;

std::unique_ptr<BitStream> Find(Predicate predicate) {
  OnlyOneActiveFind nfs;

  uint64_t current_modulus = 0;
  std::vector<bool> scratch;
  while (true) {
    scratch.clear();
    scratch.resize(current_modulus);
    bool current_modulus_too_small = false;
    for (uint64_t i = 0; i < 1ull << current_modulus; i++) {
      IntegerToBits(i, &scratch);

      LazyBitStream lazy_cantor(&scratch);

      std::optional<Bit> result = predicate(&lazy_cantor);
      if (result.has_value() && *result) {
        LOG("Returning at %llu", i);
        return std::make_unique<StrictBitStream>(std::move(scratch));
      }

      if (!result.has_value()) {
        LOG("Current modulus, %llu, too small, increasing to %llu",
            current_modulus, lazy_cantor.max_index_requested() + 1);
        current_modulus = lazy_cantor.max_index_requested() + 1;
        current_modulus_too_small = true;
        break;
      }
    }

    if (!current_modulus_too_small) {
      LOG("Not found, modulus = %llu", current_modulus);
      return std::make_unique<StrictBitStream>(std::move(scratch));
    } else {
      current_modulus++;
    }
  }
}

Bit ForSome(std::function<std::optional<Bit>(BitStream *)> pred) {
  std::unique_ptr<BitStream> idx = Find(pred);
  return *pred(idx.get());
}

Bit ForEvery(std::function<std::optional<Bit>(BitStream *)> pred) {
  auto inverse_pred = [=](BitStream *c) -> std::optional<Bit> {
    ASSIGN_OR_RETURN(Bit, val, pred(c));
    return !val;
  };
  return !ForSome(inverse_pred);
}

class StridedBitStream : public BitStream {
public:
  StridedBitStream(BitStream *source, int stride, int offset)
      : source_(source), stride_(stride), offset_(offset) {}

  std::optional<Bit> Get(Natural idx) override {
    return source_->Get(idx * stride_ + offset_);
  }

private:
  BitStream *source_;
  int stride_;
  int offset_;
};

Bit ForEvery2(
    std::function<std::optional<Bit>(BitStream *, BitStream *)> pred) {
  return ForEvery([=](BitStream *product) {
    StridedBitStream a(product, /*stride=*/2, /*offset=*/0);
    StridedBitStream b(product, /*stride=*/2, /*offset=*/1);
    return pred(&a, &b);
  });
}

template <typename T>
Bit Equal(std::function<std::optional<T>(BitStream *)> f_a,
          std::function<std::optional<T>(BitStream *)> f_b) {
  auto check = [=](BitStream *idx) -> std::optional<Bit> {
    ASSIGN_OR_RETURN(T, a, f_a(idx));
    ASSIGN_OR_RETURN(T, b, f_b(idx));
    return a == b;
  };
  return ForEvery(check);
}

std::optional<Bit> FuncF(BitStream *a) {
  ASSIGN_OR_RETURN(Bit, t0, a->Get(4));
  ASSIGN_OR_RETURN(Bit, t1, a->Get(t0 * 7));
  ASSIGN_OR_RETURN(Bit, t2, a->Get(7));
  return t0 * 7 + t1 * t2;
}

std::optional<Bit> FuncG(BitStream *a) {
  ASSIGN_OR_RETURN(Bit, t0, a->Get(4));
  ASSIGN_OR_RETURN(Bit, t1, a->Get(7));
  ASSIGN_OR_RETURN(Bit, t2, a->Get(t0 + 11 * t1));
  return t2 * t0;
}

Natural Least(std::function<bool(Natural)> fn) {
  Natural i = 0;
  while (!fn(i)) {
    i++;
  }
  return i;
}

std::optional<bool> Eq(Natural n, BitStream *a, BitStream *b) {
  for (Natural i = 0; i < n; i++) {
    ASSIGN_OR_RETURN(Bit, ai, a->Get(i));
    ASSIGN_OR_RETURN(Bit, bi, b->Get(i));
    if (ai != bi) {
      return false;
    }
  }

  return true;
}

template <typename T>
Natural Modulus(std::function<std::optional<T>(BitStream *)> fn) {
  auto is_modulus = [=](Natural n) {
    return ForEvery2([=](BitStream *a, BitStream *b) -> std::optional<Bit> {
      ASSIGN_OR_RETURN(bool, equal, Eq(n, a, b));
      if (!equal) {
        return true;
      }

      ASSIGN_OR_RETURN(T, fa, fn(a));
      ASSIGN_OR_RETURN(T, fb, fn(b));
      return fa == fb;
    });
  };
  return Least(is_modulus);
}

#define PRINT_EXPR_IMPL(expr, fmt_str, conversion)                             \
  do {                                                                         \
    auto __val = (expr);                                                       \
    printf("%s = " fmt_str "\n", #expr, conversion);                           \
  } while (false)

#define PRINT_BIT_EXPR(expr)                                                   \
  PRINT_EXPR_IMPL(expr, "%s", __val ? "true" : "false")

#define PRINT_NAT_EXPR(expr) PRINT_EXPR_IMPL(expr, "%llu", __val)

void TestA() {
  PRINT_BIT_EXPR(Equal<Bit>(FuncF, FuncF));
  PRINT_BIT_EXPR(Equal<Bit>(FuncG, FuncG));

  PRINT_BIT_EXPR(Equal<Bit>(FuncF, FuncG));
  PRINT_BIT_EXPR(Equal<Bit>(FuncG, FuncF));

  PRINT_NAT_EXPR(Modulus<Bit>(FuncF));
  PRINT_NAT_EXPR(Modulus<Bit>(FuncG));
}
} // namespace

int main() { TestA(); }
