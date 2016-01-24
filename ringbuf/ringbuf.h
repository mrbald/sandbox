/*
   Copyright 2016 Vladimir Lysyy (mrbald@github)

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <atomic>
#include <type_traits>

namespace ufw {

template <class T, size_t C> class ringbuf {
    static size_t constexpr capacity = C;
    using node_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    alignas(64) std::atomic<size_t> write_pos_ {0};
    alignas(64) std::atomic<size_t> read_pos_ {0};

    std::array<node_t, capacity> nodes_;

    size_t next(size_t pos) noexcept {
        ++pos;
        return pos - C * (pos >= C);
    }

public:
    template <class... Args>
    bool put(Args&&... args) noexcept(noexcept(T(std::forward<Args>(args)...))) {
        auto const write_pos = write_pos_.load(std::memory_order_relaxed /* single producer */);
        auto const next_write_pos = next(write_pos);

        auto read_pos = read_pos_.load(std::memory_order_acquire);
        if (next_write_pos == read_pos)
            return false;

        new(&nodes_[next_write_pos])T(std::forward<Args>(args)...);

        write_pos_.store(next_write_pos, std::memory_order_release);
        return true;
    }

    template <class F>
    bool take(F const& f) noexcept(noexcept(f(std::move(std::declval<T>())))) {
        auto const read_pos = read_pos_.load(std::memory_order_relaxed);
        auto const next_read_pos = next(read_pos);

        auto const write_pos = write_pos_.load(std::memory_order_acquire);
        if (read_pos == write_pos)
            return false;

        T& val = reinterpret_cast<T&>(nodes_[read_pos]);
        f(std::move(val));
        val.~T();

        read_pos_.store(next_read_pos, std::memory_order_release);

        return true;
    }
};

} // namespace ufw
