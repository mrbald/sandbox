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

#ifndef UFW_L1D_LINE_SIZE
#   error "macro UFW_L1D_LINE_SIZE not defined"
#endif

namespace ufw {

template <class T, size_t C> class ringbuf {
    static size_t constexpr capacity = C;
    using node_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    struct stage { alignas(UFW_L1D_LINE_SIZE) std::atomic<size_t> pos_ {0}; };
    std::array<stage, 2> stages_;

    std::array<node_t, capacity> nodes_;

    size_t next(size_t pos) noexcept {
        ++pos;
        return pos - C * (pos >= C);
    }


    template <bool WRITER, class F, class... Args>
    bool invoke(F&& func, Args&&... args) noexcept {
        auto& self_pos_ = stages_[WRITER].pos_;
        auto& party_pos_ = stages_[!WRITER].pos_;

        auto const self_pos = self_pos_.load(std::memory_order_relaxed /* single producer */);
        auto const next_self_pos = next(self_pos);

        auto const party_pos = party_pos_.load(std::memory_order_acquire);

        auto const the_pos = WRITER ? next_self_pos : self_pos;
        if (the_pos == party_pos) return false;
        func(the_pos, std::forward<Args>(args)...);

        self_pos_.store(next_self_pos, std::memory_order_release);
        return true;
    }

public:
    template <class... Args>
    bool put(Args&&... args) noexcept {
        return invoke<true>([&](size_t pos, Args&&... args) noexcept {
            new(&nodes_[pos])T(std::forward<Args>(args)...);
        }, std::forward<Args>(args)...);
    }

    template <class F>
    bool take(F const& func) noexcept {
        return invoke<false>([&](size_t pos) noexcept {
            T& val = reinterpret_cast<T&>(nodes_[pos]);
            func(std::move(val));
            val.~T();
        });
    }
};

} // namespace ufw
