#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "utils.h"

using Bit = bool;
using Natural = uint64_t;

// Set of natural numbers, implemented as a bitset.
class SetOfNaturals {
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

// A possibly infinite sequence of bits.
class BitSequence {
public:
  // Subclasses override this method to provide class specific functionality.
  //
  // Either returns a bit or a sentinel value (std::optional).
  virtual std::optional<Bit> Get(Natural) = 0;
  virtual ~BitSequence() {}
};

// This bit sequence contains a finite prefix of an infinite bit sequence.
//
// If the caller asks for bits beyond the prefix it was told about, it returns
// the sentinel.  It also keeps track of the indices that it returned sentinel
// for.
class LazyBitSequence : public BitSequence {
public:
  explicit LazyBitSequence(const std::vector<Bit> *values,
                           const SetOfNaturals *indices_present,
                           SetOfNaturals *unfulfilled_indices)
      : values_(*values), indices_present_(*indices_present),
        unfulfilled_indices_(unfulfilled_indices) {}
  virtual ~LazyBitSequence() override {}

  std::optional<Bit> Get(Natural idx) override {
    if (indices_present_.Contains(idx)) {
      return values_[idx];
    }

    unfulfilled_indices_->Insert(idx);
    return std::nullopt;
  }

private:
  const std::vector<bool> &values_;
  const SetOfNaturals &indices_present_;
  SetOfNaturals *unfulfilled_indices_;
};

// Used to check that we have only one active ForSome in a thread.
class OnlyOneActiveForSome {
public:
  OnlyOneActiveForSome() {
    if (find_is_active_) {
      printf("Multiple active ForSome frames on the same thread!\n");
      abort();
    }

    find_is_active_ = true;
  }

  ~OnlyOneActiveForSome() { find_is_active_ = false; }

private:
  static thread_local bool find_is_active_;
};

/*static*/ bool thread_local OnlyOneActiveForSome::find_is_active_ = false;

template <typename PredicateTy> Bit ForSome(PredicateTy predicate) {
  OnlyOneActiveForSome nfs;

  std::vector<bool> scratch;
  SetOfNaturals indices_of_bits_present;
  SetOfNaturals indices_of_bits_requested;
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

      LazyBitSequence lazy_bit_stream(&scratch, &indices_of_bits_present,
                                      &indices_of_bits_requested);

      std::optional<Bit> result = predicate(&lazy_bit_stream);
      if (result.has_value() && *result) {
        return true;
      }

      if (!result.has_value()) {
        // This is where we need the condition asserted by OnlyOneActiveForSome.
        //
        // We assume that if `predicate` has returned the sentinel value then it
        // must have run out of bits.  But that is not necessary if we allowed
        // nested ForSome calls -- it could have run out of bits in the
        // LazyBitSequence provided by an "outer" ForSome.
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
  auto inverse_pred = [=](BitSequence *c) -> std::optional<Bit> {
    ASSIGN_OR_RETURN(Bit, val, pred(c));
    return !val;
  };
  return !ForSome(inverse_pred);
}

// Can be used to map a single bit sequence into N bit sequences, each reading
// mapping bit `I` to bit `N*I+J` in the main sequence, with 0 <= `J` < N.
class StridedBitSequence : public BitSequence {
public:
  StridedBitSequence(BitSequence *source, int stride, int offset)
      : source_(source), stride_(stride), offset_(offset) {}

  std::optional<Bit> Get(Natural idx) override {
    return source_->Get(idx * stride_ + offset_);
  }

private:
  BitSequence *source_;
  int stride_;
  int offset_;
};

template <typename Predicate2Ty> Bit ForEvery2(Predicate2Ty pred) {
  return ForEvery([=](BitSequence *product) {
    StridedBitSequence a(product, /*stride=*/2, /*offset=*/0);
    StridedBitSequence b(product, /*stride=*/2, /*offset=*/1);
    return pred(&a, &b);
  });
}

template <typename T, typename PredicateTy>
Bit Equal(PredicateTy f_a, PredicateTy f_b) {
  auto check = [=](BitSequence *idx) -> std::optional<Bit> {
    ASSIGN_OR_RETURN(T, a, f_a(idx));
    ASSIGN_OR_RETURN(T, b, f_b(idx));
    return a == b;
  };
  return ForEvery(check);
}

template <typename PredicateNoOptionalTy>
Natural Least(PredicateNoOptionalTy fn) {
  Natural i = 0;
  while (!fn(i)) {
    i++;
  }
  return i;
}

std::optional<bool> Eq(Natural n, BitSequence *a, BitSequence *b) {
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
    return ForEvery2([=](BitSequence *a, BitSequence *b) -> std::optional<Bit> {
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

std::optional<Bit> FuncF(BitSequence *a) {
  ASSIGN_OR_RETURN(Bit, t0, a->Get(4));
  ASSIGN_OR_RETURN(Bit, t1, a->Get(t0 * 7));
  ASSIGN_OR_RETURN(Bit, t2, a->Get(7));
  return t0 * 7 + t1 * t2;
}

std::optional<Bit> FuncG(BitSequence *a) {
  ASSIGN_OR_RETURN(Bit, t0, a->Get(4));
  ASSIGN_OR_RETURN(Bit, t1, a->Get(7));
  ASSIGN_OR_RETURN(Bit, t2, a->Get(t0 + 11 * t1));
  return t2 * t0;
}

void TestA() {
  CREATE_TIMER();

  PRINT_BIT_EXPR(Equal<Bit>(FuncF, FuncF));
  PRINT_BIT_EXPR(Equal<Bit>(FuncG, FuncG));

  PRINT_BIT_EXPR(Equal<Bit>(FuncF, FuncG));
  PRINT_BIT_EXPR(Equal<Bit>(FuncG, FuncF));

  PRINT_NAT_EXPR(Modulus<Bit>(FuncF));
  PRINT_NAT_EXPR(Modulus<Bit>(FuncG));
}

int main() { TestA(); }
