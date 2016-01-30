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

namespace details {

template <class T, class S, bool CallCtor, bool CallDtor> struct lifecycle_tracker;

template <class T, class S> struct lifecycle_tracker<T, S, false, false> {
    lifecycle_tracker(S&) {}
};

template <class T, class S> struct lifecycle_tracker<T, S, true, false> {
    lifecycle_tracker(S& node) { new (&node) T; }
};

template <class T, class S> struct lifecycle_tracker<T, S, false, true> {
    S& node_;
    lifecycle_tracker(S& node): node_(node) {}
    ~lifecycle_tracker() { reinterpret_cast<T&>(node_).~T(); }
};

} // namespace details

/**
 [(X-1)%N] <<== can consume from == [X%N] == can produce for ==>> [(X+1)%N]
*/
template <class T, size_t C, size_t N, size_t L = 0> struct pipeline {
    static_assert(C <= std::numeric_limits<size_t>::max() && N >= 2 && L < N, "");
    using value_type = T;
    static constexpr auto CAP = C;
    static constexpr auto STG = N;
    static constexpr auto FIRST_STAGE_ID = L;
    static constexpr auto LAST_STAGE_ID = (L - 1) + (STG * !L);

    using node_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

private:
    static constexpr size_t CAUGHT_UP_BIT = 1ull << 63;
    struct stage { alignas(UFW_L1D_LINE_SIZE) std::atomic<size_t> pos_ {CAUGHT_UP_BIT}; };

    std::array<stage, STG> stages_;
    std::array<node_t, CAP> nodes_;

    size_t static mod_cap(size_t x) noexcept { return x - C * (x >= C); }

public:

    pipeline() noexcept {
        stages_[LAST_STAGE_ID].pos_ ^= CAUGHT_UP_BIT;
    }

    /**
     * Callback signature: void(node_t*, size_t len, Args...)
     */
    template <size_t X, size_t BATCH_SIZE = CAP, class Func, class... Args>
    size_t invoke_on_mem_vec(Func&& func, Args&&... args) noexcept {
        static_assert(X >= 0 && X < STG && BATCH_SIZE <= CAP, "");

        if (!BATCH_SIZE) return 0;

        auto& cur_stage_pos_ = stages_[X].pos_;
        auto& prev_stage_pos_ = stages_[(X - 1) + (STG * !X)].pos_;

        auto const prev_stage_pos_masked = prev_stage_pos_.load(std::memory_order_acquire);
        if (prev_stage_pos_masked & CAUGHT_UP_BIT)
            return 0;

        auto const cur_stage_pos_masked = cur_stage_pos_.load(std::memory_order_acquire);
        auto const cur_stage_pos = cur_stage_pos_masked & ~CAUGHT_UP_BIT;

        auto prev_stage_pos = prev_stage_pos_masked & ~CAUGHT_UP_BIT;

        size_t const batch_size_possible = prev_stage_pos - cur_stage_pos + CAP * (prev_stage_pos <= cur_stage_pos);
        size_t const batch_size = std::min(BATCH_SIZE, batch_size_possible);

        if (cur_stage_pos + batch_size > CAP) {
            func(&nodes_[cur_stage_pos], CAP - cur_stage_pos, std::forward<Args>(args)...);
            func(&nodes_[0], batch_size - (CAP - cur_stage_pos), std::forward<Args>(args)...);
        } else {
            func(&nodes_[cur_stage_pos], batch_size, std::forward<Args>(args)...);
        }

        // raise caught-up bit if the previous stage has not made the progress
        // and the current stage has consumed all values
        if (batch_size == batch_size_possible)
            prev_stage_pos_.compare_exchange_strong(prev_stage_pos,
                    prev_stage_pos | CAUGHT_UP_BIT,
                    std::memory_order_acq_rel, std::memory_order_relaxed);

        // save unmasked value to release the consumer
        cur_stage_pos_.store(mod_cap(cur_stage_pos + batch_size), std::memory_order_release);

        return batch_size;
    }


    /**
     * Callback signature: void(node_t&, Args...)
     */
    template <size_t X, size_t BATCH_SIZE = CAP, class Func, class... Args>
    size_t invoke_on_mem(Func&& func, Args&&... args) noexcept {
        return invoke_on_mem_vec<X, BATCH_SIZE>([&func] (node_t* beg, size_t len, Args&&... args) noexcept -> decltype(auto) {
            for (auto* ptr = beg, end = beg + len; ptr < end; ++ptr)
                func(*ptr, std::forward<Args>(args)...);

        }, std::forward<Args>(args)...);
    }


    /**
     * Callback signature: void (T&, Args...)
     * Automatically constructs the T before the first stage invocation
     * and destructs after the last stage invocation
     */
    template <size_t X, size_t n = C, class Func, class... Args>
    size_t invoke_on_obj(Func&& func, Args&&... args) noexcept {
        return invoke_on_mem<X, n>([&func] (node_t& node, Args&&... args) noexcept -> decltype(auto) {
            details::lifecycle_tracker<T, node_t,
                    X == FIRST_STAGE_ID && !std::is_trivially_constructible<T>::value,
                    X == LAST_STAGE_ID && !std::is_trivially_destructible<T>::value> _(node);
            return func(reinterpret_cast<T&>(node), std::forward<Args>(args)...);
        }, std::forward<Args>(args)...);
    }

};

} // namespace ufw
