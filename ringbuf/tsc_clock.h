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

#ifndef UFW_CPU_FREQUENCY
#   error "macro UFW_CPU_FREQUENCY not defined"
#endif

#include <chrono>

namespace ufw {

// Idea by http://stackoverflow.com/a/11485388/267482
struct tsc_clock {
    using rep = uint64_t;
    using period = std::ratio<1, UFW_CPU_FREQUENCY>;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<tsc_clock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept
    {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
        return time_point(duration(static_cast<rep>(hi) << 32 | lo));
    }
};

}
