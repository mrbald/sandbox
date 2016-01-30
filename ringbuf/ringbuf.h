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

template <class T, size_t CAP> class ringbuf {
    static size_t constexpr capacity = CAP;
    using node_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    struct stage { alignas(UFW_L1D_LINE_SIZE) std::atomic<size_t> pos_ {0}; };
    std::array<stage, 2> stages_;

    std::array<node_t, capacity> nodes_;

    size_t static mod_cap(size_t x) noexcept { return x - CAP * (x >= CAP); }
    size_t static next(size_t pos) noexcept { ++pos; return mod_cap(pos); }

public:
    template <bool WRITER, class Func, class... Args>
    bool invoke(Func&& func, Args&&... args) noexcept {
        auto& self_pos_ = stages_[WRITER].pos_;
        auto& party_pos_ = stages_[!WRITER].pos_;

        auto const self_pos = self_pos_.load(std::memory_order_relaxed /* single producer */);
        auto const party_pos = party_pos_.load(std::memory_order_acquire);

        auto const next_self_pos = next(self_pos);
        auto const the_pos = WRITER ? next_self_pos : self_pos;

        if (the_pos == party_pos) return false;
        func(nodes_[self_pos], std::forward<Args>(args)...);

        self_pos_.store(next_self_pos, std::memory_order_release);
        return true;
    }


    template <bool WRITER, size_t BATCH_SIZE = CAP - 1, class Func, class... Args>
    size_t invokev(Func&& func, Args&&... args) noexcept {
        static_assert(BATCH_SIZE <= CAP - 1, "");

        auto& self_pos_ = stages_[WRITER].pos_;
        auto& party_pos_ = stages_[!WRITER].pos_;

        auto const self_pos = self_pos_.load(std::memory_order_relaxed /* single producer */);
        auto const party_pos = party_pos_.load(std::memory_order_acquire);

        auto const next_self_pos = next(self_pos);
        auto the_pos = WRITER ? next_self_pos : self_pos;

        size_t const batch_size_possible = party_pos - the_pos + CAP * (party_pos < the_pos);
        size_t const batch_size = std::min(BATCH_SIZE, batch_size_possible);

        if (self_pos + batch_size > CAP) {
            func(&nodes_[self_pos], CAP - self_pos, std::forward<Args>(args)...);
            func(&nodes_[0], batch_size - (CAP - self_pos), std::forward<Args>(args)...);
        } else {
            func(&nodes_[self_pos], batch_size, std::forward<Args>(args)...);
        }

        self_pos_.store(mod_cap(self_pos + batch_size), std::memory_order_release);
        return batch_size;
    }


    template <class... Args>
    bool put(Args&&... args) noexcept {
        return invoke<true>([&](node_t& node, Args&&... args) noexcept {
            new(&node)T(std::forward<Args>(args)...);
        }, std::forward<Args>(args)...);
    }


    template <class F>
    bool take(F const& func) noexcept {
        return invoke<false>([&](node_t& node) noexcept {
            T& val = reinterpret_cast<T&>(node);
            func(std::move(val));
            val.~T();
        });
    }
};

} // namespace ufw
