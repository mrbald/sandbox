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

#include <chrono>
#include <ratio>

namespace ufw {

#if __x86_64__
inline
#endif
namespace x86_64 {

inline void zzz() noexcept
{
    asm volatile("pause\n": : :"memory");
}

inline uint64_t rdtsc() noexcept
{
    uint32_t rax, rdx;
    asm volatile ("lfence;rdtsc;lfence;" : "=a" (rax), "=d" (rdx));
    return ((uint64_t)rdx << 32) + rax;
}

inline uint64_t rdtscp(uint32_t& aux) noexcept
{
    uint32_t rax, rdx;
    asm volatile ("rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
    return ((uint64_t)rdx << 32) + rax;
}

inline uint64_t rdtscp() noexcept
{
    uint32_t aux;
    return rdtscp(aux);
}

} // namespace x86_64

template <class duration_type> inline
auto tsc_ratio() noexcept
{
    static std::pair<uint64_t, typename duration_type::rep> const ticks_per_unit = [] {
        using namespace std::chrono;

        double const max_error = 1e-7;
        double error = std::numeric_limits<double>::max();

        size_t const max_iterations = 1'000'000'000;

        std::pair<uint64_t, typename duration_type::rep> scale {std::numeric_limits<uint64_t>::max(), {}};
        for (size_t iterations = 100'000; error >= max_error && iterations <= max_iterations; iterations += iterations)
        {
            auto hr0 = high_resolution_clock::now();
            auto tsc0 = rdtsc();
            for (size_t i = 0; i < iterations; ++i)
                zzz();
            auto hr1 = high_resolution_clock::now();
            auto tsc1 = rdtsc();

            auto prev = scale;
            scale = decltype(scale) {tsc1 - tsc0, duration_cast<duration_type>(hr1 - hr0).count()};

            error = std::abs(scale.first * prev.second - prev.first * scale.second) / double(scale.first * prev.second);
        }
        return scale;
    } ();

    return ticks_per_unit;
}

template <class duration_type> inline
auto tsc_cast(uint64_t ticks) noexcept
{
    auto const ratio = tsc_ratio<duration_type>();
    return duration_type {static_cast<typename duration_type::rep>(ticks * ratio.second / ratio.first)};
}

// Original idea by http://stackoverflow.com/a/11485388/267482
/**
 * TSC based clock usable for measuring deltas.
 * Time unit is 1/10-th of nanoseconds. Ticks to time units
 * ratio is dynamically calculated upon the first request and
 * takes a while, can be manually triggered by the call to scale().
 */
struct tsc_clock {
    using rep = double;
    using period = std::pico;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<tsc_clock>;
    static constexpr bool is_steady = true;

    static auto scale() noexcept
    {
        return tsc_cast<duration>(1u);
    }

    static auto now() noexcept
    {
        return time_point {tsc_cast<duration>(rdtsc())};
    }
};

} // namespace ufw
