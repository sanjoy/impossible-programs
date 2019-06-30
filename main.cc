#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
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

class BitSet {
public:
  void Clear() { rep_.clear(); }

  void Insert(Natural idx) {
    if (idx >= rep_.size()) {
      rep_.resize(idx + 1, false);
    }
    size_ += !rep_[idx];
    rep_[idx] = true;
  }

  bool Contains(Natural idx) const { return idx < rep_.size() && rep_[idx]; }

  template <typename FnTy> void ForEach(FnTy func) {
    for (Natural i = 0, e = rep_.size(); i < e; i++) {
      if (rep_[i]) {
        func(i);
      }
    }
  }

  int64_t size() const { return size_; }

private:
  int64_t size_ = 0;
  std::vector<bool> rep_;
};

class BitStream {
public:
  virtual std::optional<Bit> Get(Natural) = 0;
  virtual ~BitStream() {}
};

class StrictBitStream : public BitStream {
public:
  explicit StrictBitStream(std::vector<Bit> values) : rep_(std::move(values)) {}
  std::optional<Bit> Get(Natural idx) override { return rep_[idx]; }
  virtual ~StrictBitStream() override {}

private:
  std::vector<bool> rep_;
};

class LazyBitStream : public BitStream {
public:
  explicit LazyBitStream(const std::vector<Bit> *values,
                         const BitSet *indices_present,
                         BitSet *indices_requested)
      : values_(*values), indices_present_(*indices_present),
        indices_requested_(indices_requested) {}
  virtual ~LazyBitStream() override {}

  std::optional<Bit> Get(Natural idx) override {
    if (indices_present_.Contains(idx)) {
      return values_[idx];
    }

    indices_requested_->Insert(idx);
    return std::nullopt;
  }

private:
  const std::vector<bool> &values_;
  const BitSet &indices_present_;
  BitSet *indices_requested_;
};

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

template <typename PredicateTy> Bit ForSome(PredicateTy predicate) {
  OnlyOneActiveFind nfs;

  std::vector<bool> scratch;
  BitSet indices_of_bits_present;
  BitSet indices_of_bits_requested;
  while (true) {
    bool current_modulus_too_small = false;
    LOG("Entering inner loop with indices_of_bits_present.size() = %lld",
        indices_of_bits_present.size());
    std::vector<int> indices_of_bits_present_vect;
    indices_of_bits_present.ForEach(
        [&](Natural n) { indices_of_bits_present_vect.push_back(n); });
    scratch.assign(scratch.size(), false);
    for (uint64_t i = 0, e = 1ull << (1 + indices_of_bits_present.size());
         i < e; i++) {
      for (int idx : indices_of_bits_present_vect) {
        if (!scratch[idx]) {
          scratch[idx] = true;
          break;
        } else {
          scratch[idx] = false;
        }
      }

#ifdef ENABLE_LOG
      bool enable_verbose_log = false;
      if (enable_verbose_log) {
        std::string scratch_str;
        for (bool b : scratch) {
          scratch_str += b ? "1 " : "0 ";
          ;
        }
        LOG("Scratch = %s", scratch_str.c_str());
      }
#endif

      LazyBitStream lazy_bit_stream(&scratch, &indices_of_bits_present,
                                    &indices_of_bits_requested);

      std::optional<Bit> result = predicate(&lazy_bit_stream);
      if (result.has_value() && *result) {
        return true;
      }

      if (!result.has_value()) {
        Natural new_scratch_size = scratch.size();
        indices_of_bits_requested.ForEach([&](Natural requested_index) {
          LOG("New index requested: %llu", requested_index);
          indices_of_bits_present.Insert(requested_index);
          new_scratch_size = std::max(new_scratch_size, requested_index + 1);
        });
        scratch.resize(new_scratch_size);
        current_modulus_too_small = true;
        indices_of_bits_requested.Clear();
        break;
      }
    }

    if (!current_modulus_too_small) {
#ifdef ENABLE_LOG
      std::string indices_of_bits_present_str;
      indices_of_bits_present.ForEach([&](Natural idx) {
        indices_of_bits_present_str += std::to_string(idx);
        indices_of_bits_present_str += " ";
      });
      LOG("Tried all possibilities with %s",
          indices_of_bits_present_str.c_str());
#endif
      return false;
    }
  }
}

template <typename PredicateTy> Bit ForEvery(PredicateTy pred) {
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

template <typename Predicate2Ty> Bit ForEvery2(Predicate2Ty pred) {
  return ForEvery([=](BitStream *product) {
    StridedBitStream a(product, /*stride=*/2, /*offset=*/0);
    StridedBitStream b(product, /*stride=*/2, /*offset=*/1);
    return pred(&a, &b);
  });
}

template <typename T, typename PredicateTy>
Bit Equal(PredicateTy f_a, PredicateTy f_b) {
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

template <typename PredicateNoOptionalTy>
Natural Least(PredicateNoOptionalTy fn) {
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

template <typename T, typename PredicateTy> Natural Modulus(PredicateTy fn) {
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
