/*
   Copyright 2015 Vladimir Lysyy (mrbald@github)

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

#include "logger.h"

#include <chrono>
#include <atomic>
#include <thread>

#include <type_traits>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#include <iomanip>

template <class T, size_t C> struct rungbuf
{
    static size_t constexpr capacity = C;
    using node_t = std::aligned_storage_t<sizeof(T), alignof(T)>;

    alignas(64) std::atomic<size_t> write_pos_ {0};
    alignas(64) std::atomic<size_t> read_pos_ {0};

    std::array<node_t, capacity> nodes_;

private:
    size_t next(size_t pos)
    {
        ++pos;
        return pos - capacity * (pos >= capacity);
    }

public:

    template <class... X>
    bool put(X&&... args) noexcept(noexcept(T(std::forward<X>(args)...)))
    {
        auto const write_pos = write_pos_.load(std::memory_order_relaxed /* single producer */);
        auto const next_write_pos = next(write_pos);

        auto read_pos = read_pos_.load(std::memory_order_acquire);
        if (next_write_pos == read_pos)
            return false;

        new(&nodes_[next_write_pos])T(std::forward<X>(args)...);
        write_pos_.store(next_write_pos, std::memory_order_release);

        return true;
    }

    template <class F>
    bool take(F const& f) noexcept(noexcept(f(std::move(std::declval<T>()))))
    {
        auto const read_pos = read_pos_.load(std::memory_order_relaxed);

        auto const write_pos = write_pos_.load(std::memory_order_acquire);
        if (read_pos == write_pos)
            return false;

        T& val = reinterpret_cast<T&>(nodes_[read_pos]);
        f(std::move(val));
        val.~T();

        auto const next_read_pos = next(read_pos);
        read_pos_.store(next_read_pos, std::memory_order_release);

        return false;
    }
};

struct probe1
{
    uint64_t seq;
    uint32_t key;
};

struct probe2: probe1
{
    char data[sizeof(seq)];
};

struct probe3: probe1
{
    struct
    {
        struct
        {
            int64_t px;
            int64_t qty;
        } book[32];
        uint8_t depth;
    } sides[2];
};

template <class T>
void runme()
{
    using namespace std::literals;

    auto ring = std::make_unique<rungbuf<T, (1<<20)>>();

    bool const number_of_readers = 1;

    std::atomic<bool> reader_can_start {false};
    std::atomic<bool> reader_must_continue {true};

    std::atomic<size_t> active_readers { number_of_readers };

    auto const reader = [&]{
        auto tid = std::this_thread::get_id();
        LOG_INF << "reader started " << tid;
        while (!reader_can_start);
        T dst;
        while (reader_must_continue || ring->take([&](T&& val)
        {
            dst = std::move(val);
        }));
        --active_readers;
        LOG_INF << "reader stopped " << tid;
    };

    std::thread threads[number_of_readers];
    for (size_t i = 0; i < number_of_readers; ++i)
        threads[i] = std::thread(reader);
    LOG_INF << "done spawning readers";

    std::this_thread::sleep_for(500ms);
    size_t const number_of_iterations = 1<<26;
    auto const start = std::chrono::high_resolution_clock::now();
    reader_can_start = true;
    for (size_t i = 0; i < number_of_iterations; ++i)
    {
        while(ring->put(T{}));
        (i == 1000) && (reader_can_start = true);
    }
    reader_must_continue = false;
    while (active_readers);
    auto const end = std::chrono::high_resolution_clock::now();
    auto const duration = std::chrono::duration<double>(end - start);
    LOG_INF << number_of_iterations << " iterations with " << sizeof(T) << " bytes payload: "<< std::fixed << std::setprecision(2)
            << number_of_iterations / duration.count() << "/sec, "
            << 1e-9 * sizeof(T) * (number_of_iterations) / duration.count() << " GB/sec";

    for (size_t i = 0; i < number_of_readers; ++i) threads[i].join();
}

// g++ @flags.txt -o ringbuf ringbuf.cc
int main()
{
    // $ fgrep -m1 'model name' /proc/cpuinfo 
    // model name      : Intel(R) Core(TM)2 Quad CPU    Q9650  @ 3.00GHz

    runme<probe1>();
    // [2016-01-10 23:13:13.385782] [0x00007fd486da5740] [info]    67108864 iterations with 16 bytes payload: 459663971.52/sec, 7.35 GB/sec

    runme<probe2>();
    // [2016-01-10 23:13:14.065785] [0x00007fd486da5740] [info]    67108864 iterations with 24 bytes payload: 397716986.44/sec, 9.55 GB/sec

    // 67108864 iterations with 352 bytes payload: 43938311.34/sec, 15.47 GB/sec
    // [2016-01-10 23:13:18.440787] [0x00007fd486da5740] [info]    67108864 iterations with 1056 bytes payload: 23151382.66/sec, 24.45 GB/sec
    return 0;
}
