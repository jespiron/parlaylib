#ifndef PARLAY_INTERNAL_DELAYED_COMMON_H
#define PARLAY_INTERNAL_DELAYED_COMMON_H

#include <cstddef>

#include <algorithm>
#include <iterator>
#include <functional>
#include <utility>
#include <type_traits>

#include "../../parallel.h"
#include "../../range.h"
#include "../../sequence.h"

namespace parlay {
namespace internal {
namespace delayed {

//  --------------------- Useful concept traits -----------------------

// Defines the member value true if, given a Range type Range_, and a unary operator
// UnaryOperator_ to be applied to each element of the range, the operator can be
// applied to the range even when it is const-qualified
//
// This trait is used to selectively disable const overloads of member functions
// (e.g., begin and end) when it would be ill-formed to attempt to transform
// the range when it is const.
template<typename Range_, typename UnaryOperator_, typename = std::void_t<>>
struct is_range_const_transformable : std::false_type {};

template<typename Range_, typename UnaryOperator_>
struct is_range_const_transformable<Range_, UnaryOperator_, std::void_t<
  std::enable_if_t< is_range_v<std::add_const_t<std::remove_reference_t<Range_>>> >,
  std::enable_if_t< std::is_invocable_v<const UnaryOperator_&, range_reference_type_t<std::add_const_t<std::remove_reference_t<Range_>>>> >
>> : std::true_type {};

// True if, given a Range type Range_, and a unary operator UnaryOperator_ to be
// applied to each element of the range, the operator can be applied to the range
// even when it is const-qualified
template<typename UV, typename UO>
static inline constexpr bool is_range_const_transformable_v = is_range_const_transformable<UV,UO>::value;



// ----------------------------------------------------------------------------
//            Block-iterable interface for random-access ranges
// ----------------------------------------------------------------------------

// Default block size to use for block-iterable sequences
static constexpr size_t block_size = 2000;

inline constexpr size_t num_blocks_from_size(size_t n) {
  return (n == 0) ? 0 : (1 + (n - 1) / block_size);
}

// ----------------------------------------------------------------------------
//            Block-iterable interface for random-access ranges
// ----------------------------------------------------------------------------

template<typename Range,
         std::enable_if_t<is_random_access_range_v<Range>, int> = 0>
size_t num_blocks(Range&& r) {
  auto n = parlay::size(r);
  return num_blocks_from_size(n);
}

template<typename Range,
         std::enable_if_t<is_random_access_range_v<Range>, int> = 0>
auto begin_block(Range&& r, size_t i) {
  size_t n = parlay::size(r);

  // Note: For the interface requirements of a block-iterable sequence,
  // we require begin_block(n_blocks) to be valid and point to the end
  // iterator of the sequence, since this means that we always satisfy
  // end_block(r, i) == begin_block(r, i+1).
  size_t start = (std::min)(i * block_size, n);
  return std::begin(r) + start;
}

template<typename Range,
         std::enable_if_t<is_random_access_range_v<Range>, int> = 0>
auto end_block(Range&& r, size_t i) {
  size_t n = parlay::size(r);

  size_t end = (std::min)((i+1) * block_size, n);
  return std::begin(r) + end;
}

// ----------------------------------------------------------------------------
//            Block-iterable interface for non-random-access ranges
// ----------------------------------------------------------------------------

template<typename Range,
         std::enable_if_t<!is_random_access_range_v<Range> && is_block_iterable_range_v<Range>, int> = 0>
size_t num_blocks(Range&& r) {
  return r.get_num_blocks();
}

template<typename Range,
         std::enable_if_t<!is_random_access_range_v<Range> && is_block_iterable_range_v<Range>, int> = 0>
auto begin_block(Range&& r, size_t i) {
  return r.get_begin_block(i);
}

template<typename Range,
         std::enable_if_t<!is_random_access_range_v<Range> && is_block_iterable_range_v<Range>, int> = 0>
auto end_block(Range&& r, size_t i) {
  return r.get_end_block(i);
}

// ----------------------------------------------------------------------------
//                          Base class for BID views
// ----------------------------------------------------------------------------

// Given a view over which we want to perform a delayed operation, we either
// want to store a reference to it, or a copy. If the input view is provided
// as an lvalue reference, we will keep a reference, but we will do so inside
// a reference wrapper to ensure that it is copy-assignable.
//
// If the input view is a prvalue or rvalue reference, we take ownership of it
// and keep it as a value member.
template<typename T>
using view_storage_type = std::conditional_t<std::is_lvalue_reference_v<T>,
                            std::reference_wrapper<std::remove_reference_t<T>>,
                                                   std::remove_reference_t<T>>;

template<typename UnderlyingView>
struct block_iterable_view_base_data {
  explicit block_iterable_view_base_data(UnderlyingView&& v) : view_m(static_cast<UnderlyingView&&>(v)) { }

  UnderlyingView& base_view() { return view_m; }
  const UnderlyingView& base_view() const { return view_m; }

 private:
  view_storage_type<UnderlyingView> view_m;
};

template<>
struct block_iterable_view_base_data<void> { };

// This base class helps to implement block-iterable delayed views.
//
// If the underlying view is a reference type, then it is wrapped in a reference_wrapper
// so that the resulting view type is still copy-assignable. If the underlying view is
// not a reference type, then it is just stored as a value member as usual.
//
// If the underlying view is void, then no extra data member is stored. This is useful
// if the view is actually going to move/copy the underlying data and store it itself
//
// Template arguments:
//  UnderlyingView: the type of the underlying view that is being transformed
//  ParentBidView: the type of the parent view class (i.e., the class that is deriving from this one - CRTP style)
template<typename UnderlyingView, typename ParentBidView>
struct block_iterable_view_base : public block_iterable_view_base_data<UnderlyingView> {
 protected:
  block_iterable_view_base() = default;

  template<typename... UV>
  explicit block_iterable_view_base(UV&&... v)
    : block_iterable_view_base_data<UnderlyingView>(std::forward<UV>(v)...) { }
};

// ----------------------------------------------------------------------------
//            Conversion of delayed sequences to regular sequences
// ----------------------------------------------------------------------------

template<typename UnderlyingView,
    std::enable_if_t<is_random_access_range_v<UnderlyingView>, int> = 0>
auto to_sequence(UnderlyingView&& v) {
  return parlay::to_sequence(std::forward<UnderlyingView>(v));
}

template<typename T, typename Alloc = parlay::internal::sequence_default_allocator<T>, typename UnderlyingView,
    std::enable_if_t<is_random_access_range_v<UnderlyingView>, int> = 0>
auto to_sequence(UnderlyingView&& v) {
  return parlay::to_sequence<T, Alloc>(std::forward<UnderlyingView>(v));
}

template<typename UnderlyingView,
    std::enable_if_t<!is_random_access_range_v<UnderlyingView> &&
                      is_block_iterable_range_v<UnderlyingView>, int> = 0>
auto to_sequence(UnderlyingView&& v) {
  auto sz = parlay::size(v);
  auto out = parlay::sequence<range_value_type_t<UnderlyingView>>::uninitialized(sz);
  parallel_for(0, parlay::internal::delayed::num_blocks(v), [&](size_t i) {
    std::uninitialized_copy(parlay::internal::delayed::begin_block(v, i), parlay::internal::delayed::end_block(v, i),
                            out.begin() + i * parlay::internal::delayed::block_size);
  });
  return out;
}

template<typename T, typename Alloc = parlay::internal::sequence_default_allocator<T>, typename UnderlyingView,
    std::enable_if_t<!is_random_access_range_v<UnderlyingView> &&
                      is_block_iterable_range_v<UnderlyingView>, int> = 0>
auto to_sequence(UnderlyingView&& v) {
  auto sz = parlay::size(v);
  auto out = parlay::sequence<T, Alloc>::uninitialized(sz);
  parallel_for(0, parlay::internal::delayed::num_blocks(v), [&](size_t i) {
    std::uninitialized_copy(parlay::internal::delayed::begin_block(v, i), parlay::internal::delayed::end_block(v, i),
                            out.begin() + i * parlay::internal::delayed::block_size);
  });
  return out;
}


}  // namespace delayed
}  // namespace internal
}  // namespace parlay

#endif  // PARLAY_INTERNAL_DELAYED_COMMON_H
