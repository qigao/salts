#ifndef C11_STREAM_HPP
#define C11_STREAM_HPP

#include "stream.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

namespace turbo {
namespace stream {

namespace detail {

template <typename T, bool IsBoxed>
class iterator {
public:
    static_assert(std::is_trivially_copyable<T>::value,
                  "stream<T> requires trivially copyable T");

    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer =
        typename std::conditional<IsBoxed, const T *, const value_type *>::type;
    using reference =
        typename std::conditional<IsBoxed, const T *, const value_type &>::type;

    iterator() noexcept
        : stream_(nullptr)
        , at_end_(true)
        , has_value_(false)
        , current_sequence_(0)
    {
    }

    explicit iterator(stream_t *stream)
        : stream_(stream)
        , at_end_(stream == nullptr)
        , has_value_(false)
        , current_sequence_(0)
    {
        fetch();
    }

    reference operator*() const
    {
        if constexpr (IsBoxed) {
            return boxed_;
        } else {
            return value_;
        }
    }

    pointer operator->() const
    {
        if constexpr (IsBoxed) {
            return boxed_;
        } else {
            return &value_;
        }
    }

    iterator &operator++()
    {
        fetch();
        return *this;
    }

    iterator operator++(int)
    {
        iterator copy(*this);
        ++(*this);
        return copy;
    }

    bool operator==(const iterator &other) const
    {
        if (at_end_ && other.at_end_) {
            return true;
        }
        if (stream_ != other.stream_) {
            return false;
        }
        return at_end_ == other.at_end_ &&
            has_value_ == other.has_value_ &&
            (!at_end_ ? current_sequence_ == other.current_sequence_ : true);
    }

    bool operator!=(const iterator &other) const
    {
        return !(*this == other);
    }

private:
    void fetch()
    {
        if (!stream_) {
            at_end_ = true;
            has_value_ = false;
            return;
        }

        if constexpr (!IsBoxed) {
            if (stream_->current_element_size != sizeof(T)) {
                stream_->error = STREAM_ERR_BAD_ARGUMENT;
                at_end_ = true;
                has_value_ = false;
                return;
            }
        } else {
            if (stream_->current_element_size != sizeof(const void *)) {
                stream_->error = STREAM_ERR_BAD_ARGUMENT;
                at_end_ = true;
                has_value_ = false;
                return;
            }
        }

        stream_item_t item = {nullptr, 0, 0, 0};

        if (IsBoxed) {
            void *raw = nullptr;
            item.data = &raw;
            item.size = sizeof(raw);

            stream_result_t r = stream_next(stream_, &item);
            if (r != STREAM_OK) {
                at_end_ = true;
                has_value_ = false;
                return;
            }

            boxed_ = static_cast<const T *>(raw);
        } else {
            item.data = static_cast<void *>(&value_);
            item.size = sizeof(value_);

            stream_result_t r = stream_next(stream_, &item);
            if (r != STREAM_OK) {
                at_end_ = true;
                has_value_ = false;
                return;
            }
        }

        current_sequence_ = item.sequence;
        has_value_ = true;
        at_end_ = false;
    }

    stream_t *stream_;
    value_type value_{};
    const T *boxed_{nullptr};
    bool at_end_;
    bool has_value_;
    uint64_t current_sequence_;
};

} // namespace detail

template <typename T>
using iterator = detail::iterator<T, false>;

template <typename T>
using boxed_iterator = detail::iterator<T, true>;

template <typename T>
class boxed_view;

template <typename T>
class view {
public:
    view() noexcept
        : stream_(nullptr)
    {
    }

    explicit view(stream_t &stream) : stream_(&stream) {}
    explicit view(stream_t *stream) : stream_(stream) {}

    iterator<T> begin() const
    {
        return iterator<T>{stream_};
    }

    iterator<T> end() const
    {
        return iterator<T>{};
    }

    template <typename F>
    void for_each(F &&consumer)
    {
        for (const auto &value : *this) {
            consumer(value);
        }
    }

    stream_result_t count(size_t &out_count) const
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->count(stream_, &out_count);
    }

    template <typename U>
    stream_result_t reduce(U &accumulator, stream_reducer_fn reducer)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->reduce(stream_, &accumulator, reducer);
    }

    template <typename U>
    stream_result_t collect(U &result, stream_reducer_fn accumulator)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->collect(stream_, &result, accumulator);
    }

    stream_result_t find_first(T &out)
    {
        stream_item_t item = {};
        item.data = &out;
        item.size = sizeof(out);
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->find_first(stream_, &item);
    }

    stream_result_t find_any(T &out)
    {
        stream_item_t item = {};
        item.data = &out;
        item.size = sizeof(out);
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->find_any(stream_, &item);
    }

    stream_result_t any_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->any_match(stream_, predicate, &out_matches);
    }

    stream_result_t all_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->all_match(stream_, predicate, &out_matches);
    }

    stream_result_t none_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->none_match(stream_, predicate, &out_matches);
    }

    stream_result_t contains(const T &target, stream_equal_fn equals, bool &out_contains)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->contains(stream_, &target, equals, &out_contains);
    }

    stream_result_t to_array(T *out_values, size_t capacity, size_t &out_count) const
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->to_array(
            stream_,
            out_values,
            capacity,
            sizeof(T),
            &out_count);
    }

    void clear()
    {
        if (stream_) {
            stream_->clear(stream_);
        }
    }

    void close()
    {
        if (stream_) {
            stream_->close(stream_);
        }
    }

    stream_result_t reset()
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->reset(stream_);
    }

    view<T> filter(stream_predicate_fn predicate)
    {
        if (stream_) {
            stream_->filter(stream_, predicate);
        }
        return *this;
    }

    view<T> peek(stream_consumer_fn consumer)
    {
        if (stream_) {
            stream_->peek(stream_, consumer);
        }
        return *this;
    }

    view<T> skip(size_t n)
    {
        if (stream_) {
            stream_->skip(stream_, n);
        }
        return *this;
    }

    view<T> take(size_t n)
    {
        if (stream_) {
            stream_->take(stream_, n);
        }
        return *this;
    }

    view<T> limit(size_t n)
    {
        if (stream_) {
            stream_->limit(stream_, n);
        }
        return *this;
    }

    view<T> take_while(stream_predicate_fn predicate)
    {
        if (stream_) {
            stream_->take_while(stream_, predicate);
        }
        return *this;
    }

    view<T> drop_while(stream_predicate_fn predicate)
    {
        if (stream_) {
            stream_->drop_while(stream_, predicate);
        }
        return *this;
    }

    view<T> snapshot(stream_t &snapshot_stream) const
    {
        if (!stream_) {
            return view<T>{nullptr};
        }

        if (stream_snapshot_init(&snapshot_stream, stream_) != STREAM_OK) {
            return view<T>{nullptr};
        }
        return view<T>{snapshot_stream};
    }

    view<T> snapshot(stream_t *snapshot_stream) const
    {
        if (!snapshot_stream) {
            return view<T>{nullptr};
        }
        return snapshot(*snapshot_stream);
    }

    template <typename U>
    class mapped_view {
    public:
        mapped_view() noexcept
            : stream_(nullptr)
        {
        }

        explicit mapped_view(stream_t *stream) : stream_(stream) {}

        iterator<U> begin() const { return iterator<U>{stream_}; }
        iterator<U> end() const { return iterator<U>{}; }

        template <typename F>
        void for_each(F &&consumer)
        {
            for (const auto &value : *this) {
                consumer(value);
            }
        }

        stream_result_t count(size_t &out_count) const
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->count(stream_, &out_count);
        }

        template <typename Accumulator>
        stream_result_t reduce(Accumulator &accumulator, stream_reducer_fn reducer)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->reduce(stream_, &accumulator, reducer);
        }

        template <typename Result>
        stream_result_t collect(Result &result, stream_reducer_fn accumulator)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->collect(stream_, &result, accumulator);
        }

        stream_result_t find_first(U &out)
        {
            stream_item_t item = {};
            item.data = &out;
            item.size = sizeof(out);
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->find_first(stream_, &item);
        }

        stream_result_t find_any(U &out)
        {
            stream_item_t item = {};
            item.data = &out;
            item.size = sizeof(out);
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->find_any(stream_, &item);
        }

        stream_result_t any_match(stream_predicate_fn predicate, bool &out_matches)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->any_match(stream_, predicate, &out_matches);
        }

        stream_result_t all_match(stream_predicate_fn predicate, bool &out_matches)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->all_match(stream_, predicate, &out_matches);
        }

        stream_result_t none_match(stream_predicate_fn predicate, bool &out_matches)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->none_match(stream_, predicate, &out_matches);
        }

        stream_result_t contains(const U &target, stream_equal_fn equals, bool &out_contains)
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->contains(stream_, &target, equals, &out_contains);
        }

        stream_result_t to_array(U *out_values, size_t capacity, size_t &out_count) const
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->to_array(
                stream_,
                out_values,
                capacity,
                sizeof(U),
                &out_count);
        }

        void clear()
        {
            if (stream_) {
                stream_->clear(stream_);
            }
        }

        void close()
        {
            if (stream_) {
                stream_->close(stream_);
            }
        }

        stream_result_t reset()
        {
            if (!stream_) {
                return STREAM_ERROR;
            }
            return stream_->reset(stream_);
        }

        mapped_view<U> filter(stream_predicate_fn predicate)
        {
            if (stream_) {
                stream_->filter(stream_, predicate);
            }
            return *this;
        }

        mapped_view<U> peek(stream_consumer_fn consumer)
        {
            if (stream_) {
                stream_->peek(stream_, consumer);
            }
            return *this;
        }

        mapped_view<U> skip(size_t n)
        {
            if (stream_) {
                stream_->skip(stream_, n);
            }
            return *this;
        }

        mapped_view<U> take(size_t n)
        {
            if (stream_) {
                stream_->take(stream_, n);
            }
            return *this;
        }

        mapped_view<U> limit(size_t n)
        {
            if (stream_) {
                stream_->limit(stream_, n);
            }
            return *this;
        }

        mapped_view<U> take_while(stream_predicate_fn predicate)
        {
            if (stream_) {
                stream_->take_while(stream_, predicate);
            }
            return *this;
        }

        mapped_view<U> drop_while(stream_predicate_fn predicate)
        {
            if (stream_) {
                stream_->drop_while(stream_, predicate);
            }
            return *this;
        }

        mapped_view<U> snapshot(stream_t &snapshot_stream) const
        {
            if (!stream_) {
                return mapped_view<U>{nullptr};
            }
            if (stream_snapshot_init(&snapshot_stream, stream_) != STREAM_OK) {
                return mapped_view<U>{nullptr};
            }
            return mapped_view<U>{snapshot_stream};
        }

        mapped_view<U> snapshot(stream_t *snapshot_stream) const
        {
            if (!snapshot_stream) {
                return mapped_view<U>{nullptr};
            }
            return snapshot(*snapshot_stream);
        }

        template <typename V>
        mapped_view<V> map(size_t output_size, stream_mapper_fn mapper)
        {
            if (stream_) {
                stream_->map(stream_, output_size, mapper);
            }
            return mapped_view<V>{stream_};
        }

        boxed_view<U> boxed()
        {
            if (stream_) {
                stream_->boxed(stream_);
            }
            return boxed_view<U>{stream_};
        }

    private:
        stream_t *stream_;
    };

    template <typename U>
    mapped_view<U> map(size_t output_size, stream_mapper_fn mapper)
    {
        if (stream_) {
            stream_->map(stream_, output_size, mapper);
        }
        return mapped_view<U>{stream_};
    }

    boxed_view<T> boxed()
    {
        if (stream_) {
            stream_->boxed(stream_);
        }
        return boxed_view<T>{stream_};
    }

private:
    stream_t *stream_;
};

template <typename T>
class boxed_view {
public:
    boxed_view() noexcept
        : stream_(nullptr)
    {
    }

    explicit boxed_view(stream_t &stream) : stream_(&stream) {}
    explicit boxed_view(stream_t *stream) : stream_(stream) {}

    boxed_iterator<T> begin() const
    {
        return boxed_iterator<T>{stream_};
    }

    boxed_iterator<T> end() const
    {
        return boxed_iterator<T>{};
    }

    template <typename F>
    void for_each(F &&consumer)
    {
        for (const auto &value : *this) {
            consumer(value);
        }
    }

    stream_result_t count(size_t &out_count) const
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->count(stream_, &out_count);
    }

    template <typename U>
    stream_result_t reduce(U &accumulator, stream_reducer_fn reducer)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->reduce(stream_, &accumulator, reducer);
    }

    template <typename U>
    stream_result_t collect(U &result, stream_reducer_fn accumulator)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->collect(stream_, &result, accumulator);
    }

    stream_result_t find_first(T &out)
    {
        stream_item_t item = {};
        item.data = &out;
        item.size = sizeof(out);
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->find_first(stream_, &item);
    }

    stream_result_t find_any(T &out)
    {
        stream_item_t item = {};
        item.data = &out;
        item.size = sizeof(out);
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->find_any(stream_, &item);
    }

    stream_result_t any_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->any_match(stream_, predicate, &out_matches);
    }

    stream_result_t all_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->all_match(stream_, predicate, &out_matches);
    }

    stream_result_t none_match(stream_predicate_fn predicate, bool &out_matches)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->none_match(stream_, predicate, &out_matches);
    }

    stream_result_t contains(const T &target, stream_equal_fn equals, bool &out_contains)
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->contains(stream_, &target, equals, &out_contains);
    }

    stream_result_t to_array(T *out_values, size_t capacity, size_t &out_count) const
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->to_array(
            stream_,
            out_values,
            capacity,
            sizeof(T),
            &out_count);
    }

    void clear()
    {
        if (stream_) {
            stream_->clear(stream_);
        }
    }

    void close()
    {
        if (stream_) {
            stream_->close(stream_);
        }
    }

    stream_result_t reset()
    {
        if (!stream_) {
            return STREAM_ERROR;
        }
        return stream_->reset(stream_);
    }

private:
    stream_t *stream_;
};

template <typename T>
view<T> from(stream_t &stream)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "stream<T> requires trivially copyable T");
    return view<T>{stream};
}

template <typename T>
view<T> from(stream_t *stream)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "stream<T> requires trivially copyable T");
    return view<T>{stream};
}

template <typename T>
stream_result_t snapshot(stream_t &snapshot_stream, const stream_t &source)
{
    return stream_snapshot_init(&snapshot_stream, &source);
}

template <typename T>
view<T> from_snapshot(stream_t &source, stream_t &snapshot_stream)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "stream<T> requires trivially copyable T");
    if (stream_snapshot_init(&snapshot_stream, &source) != STREAM_OK) {
        return view<T>{nullptr};
    }
    return view<T>{snapshot_stream};
}

template <typename T>
view<T> from_snapshot(stream_t *source, stream_t &snapshot_stream)
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "stream<T> requires trivially copyable T");
    if (!source) {
        return view<T>{nullptr};
    }
    if (stream_snapshot_init(&snapshot_stream, source) != STREAM_OK) {
        return view<T>{nullptr};
    }
    return view<T>{snapshot_stream};
}

template <typename T>
boxed_view<T> from_boxed(stream_t &stream)
{
    return boxed_view<T>{stream};
}

template <typename T>
boxed_view<T> from_boxed(stream_t *stream)
{
    return boxed_view<T>{stream};
}

} // namespace stream
} // namespace turbo

#endif
