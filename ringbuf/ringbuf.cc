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

#include "logger.h"
#include "ringbuf.h"
#include "pipeline.h"
#include "tsc_clock.h"

#include <chrono>
#include <atomic>
#include <thread>

#include <type_traits>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#include <iomanip>
#include <pthread.h>

using myclock = ufw::tsc_clock;

void pin_me(size_t cpu_id)
{
    cpu_set_t cpuset {};
    CPU_SET(cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

void name_me(char const* name)
{
    pthread_setname_np(pthread_self(), name);
}

struct probe1
{
    int64_t seq;
    int64_t id;
};

struct probe2: probe1
{
    char data[sizeof(probe1)];
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

template <size_t X, class T, size_t C, size_t N, class P>
struct stage {
    std::atomic<bool>& must_continue;
    P pipe;

    void operator()(auto cpu_id, auto name)noexcept{
        LOG_DBG << name << " started";

//         auto min = myclock::duration::max();
//         auto max = myclock::duration::zero();
// 
//         auto mean = std::chrono::duration<double, std::nano>::zero();
//         double n = 0.0;
//         size_t starved = 0;
// 
//         std::array<T, N> dst;
//         size_t pos = 0;
//         auto const code = [&](T& x, auto now) {
//             if (false) // if (X)
//                 dst[pos++] = x;
//             else ++pos;
//             x = now;
//         };
// 
//         pin_me(cpu_id);
//         name_me(name);
//         auto const start = myclock::now();
//         size_t count = 0;
//         while (must_continue) {
//             myclock::time_point now; // = myclock::now();
//             pos = 0;
// 
//             bool starved_this_time = false;
//             while (must_continue && !pipe->template handle<X, N>(code, now)) {
//                 starved_this_time = true;
//                 asm volatile("pause\n": : :"memory");
//             }
//             starved += starved_this_time;
// 
//             if (false) // if (X)
//             for (size_t i = 0; i < pos; ++i)
//             {
//                 T& x = dst[i];
//                 auto lat = now - x;
//                 min = lat < min ? lat : min;
//                 max = lat > max ? lat : max;
// 
//                 n += (n <= 1000);
//                 mean += (lat - mean) / n;
//             }
// 
//             count += pos;
//         }

        auto const code = [&](T&x) noexcept {
            __asm__ __volatile__("" :: "m" (&x));
        };

        pin_me(cpu_id);
        name_me(name);
        auto const start = myclock::now();
        size_t count = 0;
        for (; must_continue; count += pipe->template invoke<X, N>(code));

        auto const end = myclock::now();
        auto const duration = std::chrono::duration<double>(end - start);

        LOG_INF << name << ": "
                << count << " cycles, " << sizeof(T) << "B msg, " << C << " in ring, " << N << " in batch: "
                << std::fixed << std::setprecision(2)
                << count / duration.count() << "/sec, "
                << 1e-9 * sizeof(T) * (count) / duration.count() << " GB/sec";
//                 << "starved " << starved << " times (" << ( 100.0 * starved / count) << "%)";
//         if (false) // if (X)
//         LOG_INF << name << ": min: "
//                 << std::chrono::duration<double, std::nano>(min).count() << "ns, max: "
//                 << std::chrono::duration<double, std::nano>(max).count() << "ns, mean: " << mean.count() << "ns";

        LOG_DBG << name << " stopped";
    }
};

template <class T, size_t C, size_t N = C>
void run_pipeline()
{
    using namespace std::literals;

    std::atomic<bool> must_continue {true};
    auto pipe = std::make_shared<ufw::pipeline<T, C, 3>>();

    std::thread writer(stage<0, T, C, N, decltype(pipe)>{must_continue, pipe}, 1, "writer");
    std::thread observer(stage<1, T, C, N, decltype(pipe)>{must_continue, pipe}, 2, "observer");
    std::thread reader(stage<2, T, C, N,  decltype(pipe)>{must_continue, pipe}, 3, "reader");

    LOG_DBG << "main thread parked";
    std::this_thread::sleep_for(5s);
    LOG_DBG << "main thread resumed";

    must_continue = false;

    if (writer.joinable()) writer.join();
    if (observer.joinable()) observer.join();
    if (reader.joinable()) reader.join();

    LOG_DBG << "writer, observer, reader - returned";
}

template <bool WRITER, class T, size_t C, size_t N, class R>
struct ringv_stage {
    std::atomic<bool>& must_continue;
    R ring;

    void operator()(auto cpu_id, auto name) noexcept{
        LOG_DBG << name << " started";

        auto const code = [&](auto* x, size_t n) noexcept {
            for (auto end = x + n; x < end; ++x)
                __asm__ __volatile__("" :: "m" (x));
        };

        pin_me(cpu_id);
        name_me(name);
        auto const start = myclock::now();
        size_t count = 0;
        for (; must_continue; count += ring->template invokev<WRITER, N>(code));

        auto const end = myclock::now();
        auto const duration = std::chrono::duration<double>(end - start);

        LOG_INF << name << ": "
                << count << " cycles, " << sizeof(T) << "B msg, " << C << " in ring, " << N << " in batch: "
                << std::fixed << std::setprecision(2)
                << count / duration.count() << "/sec, "
                << 1e-9 * sizeof(T) * (count) / duration.count() << " GB/sec";

        LOG_DBG << name << " stopped";
    }
};

template <class T, size_t C, size_t N = C - 1>
void run_ringv()
{
    using namespace std::literals;

    std::atomic<bool> must_continue {true};
    auto ring = std::make_shared<ufw::ringbuf<T, C>>();

    std::thread producer(ringv_stage<true, T, C, N, decltype(ring)>{must_continue, ring}, 1, "producer");
    std::thread consumer(ringv_stage<false, T, C, N, decltype(ring)>{must_continue, ring}, 2, "consumer");

    LOG_DBG << "main thread parked";
    std::this_thread::sleep_for(5s);
    LOG_DBG << "main thread resumed";

    must_continue = false;

    if (producer.joinable()) producer.join();
    if (consumer.joinable()) consumer.join();

    LOG_DBG << "producer, consumer - returned";
}

template <class T>
void run_ringbuf()
{
    using namespace std::literals;

    auto ring = std::make_unique<ufw::ringbuf<T, (1<<15)>>();

    bool const number_of_readers = 1;

    std::atomic<bool> reader_can_start {false};
    std::atomic<bool> must_continue {true};

    std::atomic<size_t> active_readers { number_of_readers };

    auto const reader = [&]{
        auto tid = std::this_thread::get_id();
        LOG_INF << "reader started " << tid;
        while (!reader_can_start);
        T dst;
        while (must_continue || ring->take([&](T&& val)
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
    auto const start = myclock::now();
    reader_can_start = true;
    for (size_t i = 0; i < number_of_iterations; ++i)
    {
        while(ring->put(T{}));
        (i == 1000) && (reader_can_start = true);
    }
    must_continue = false;
    while (active_readers);
    auto const end = myclock::now();
    auto const duration = std::chrono::duration<double>(end - start);
    LOG_INF << number_of_iterations << " iterations with " << sizeof(T) << " bytes payload: "<< std::fixed << std::setprecision(2)
            << number_of_iterations / duration.count() << "/sec, "
            << 1e-9 * sizeof(T) * (number_of_iterations) / duration.count() << " Gb/sec";

    for (size_t i = 0; i < number_of_readers; ++i) threads[i].join();
}

template <size_t C>
void ping_pong()
{
    auto fwd = std::make_shared<ufw::ringbuf<myclock::time_point, C>>();
    auto bck = std::make_shared<ufw::ringbuf<myclock::time_point, C>>();

    std::atomic<bool> must_continue {true};

    auto code = [&must_continue](auto cpu_id, auto name, auto fwd, auto bck)
    {
        myclock::duration duration {};
        size_t count {};

        pin_me(cpu_id);
        name_me(name);

        // submit a seed message
        while (!fwd->template invokev<true, 1>([&](auto* x, auto)
        {
            reinterpret_cast<myclock::time_point&>(*x) = myclock::now();
        })) ufw::zzz();

        // ping-pong messages while can
        while (must_continue)
        {
            while (!bck->template invokev<false, 1>([&](auto* x, auto)
            {
                duration += myclock::now() - reinterpret_cast<myclock::time_point&>(*x);
                ++count;
            })) ufw::zzz();

            while (!fwd->template invokev<true, 1>([&](auto* x, auto)
            {
                reinterpret_cast<myclock::time_point&>(*x) = myclock::now();
            })) ufw::zzz();
        }

        LOG_INF << name << ": " << C << " msg-s in the ring, " << (std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() / double(count)) << " ns/msg";
    };

    std::thread ping(code, 1, "ping", fwd, bck);
    std::thread pong(code, 2, "pong", bck, fwd);

    std::this_thread::sleep_for(std::chrono::seconds(5));

    must_continue = false;

    if (ping.joinable()) ping.join();
    if (pong.joinable()) pong.join();
}

// g++ @flags.txt -o ringbuf ringbuf.cc
int main()
{
    SET_LOG_LEVEL(info);

    LOG_INF << "TSC ticks/ps: " << ufw::tsc_clock::scale().count() << std::endl;

    if (true) {
        ufw::pipeline<int64_t, 16, 2> pipe;
        assert((pipe.invokem<1>([](auto&){}) == 0));
        assert((pipe.invokem<0>([](auto&){}) == 16));
        assert((pipe.invokem<1, 12>([](auto&){}) == 12));
        assert((pipe.invokem<1>([](auto&){}) == 4));
        assert((pipe.invokem<1>([](auto&){}) == 0));
        assert((pipe.invokem<0, 7>([](auto&){}) == 7));
        assert((pipe.invokem<1>([](auto&){}) == 7));
    }

    if (true) {
        ufw::ringbuf<int64_t, 16> ring;
        bool constexpr const WRITER = true;
        assert((ring.invokev<!WRITER>([](auto*, size_t){}) == 0));
        assert((ring.invokev<WRITER>([](auto*, size_t){}) == 15));
        assert((ring.invokev<!WRITER, 12>([](auto*, size_t){}) == 12));
        assert((ring.invokev<!WRITER>([](auto*, size_t){}) == 3));
        assert((ring.invokev<!WRITER>([](auto*, size_t){}) == 0));
        assert((ring.invokev<WRITER, 7>([](auto*, size_t){}) == 7));
        assert((ring.invokev<!WRITER>([](auto*, size_t){}) == 7));
    }

    if (false) {
        ufw::pipeline<int64_t, 16, 3> pipe;
        size_t const iterations = 48;

        std::thread del([&]
        {
            LOG_INF << "del started";
            for (size_t i = 0; i < iterations; i += pipe.invokem<2>([](auto& node)
            {
                int64_t& x = reinterpret_cast<int64_t&>(node);
                LOG_INF << "del: " << x << "->" << x*7;
                if (x%3 || x%5) { LOG_ERR << "unexpected value:" << x; abort(); }
                x *= 7;
            }));
        });

        std::thread upd([&]
        {
            LOG_INF << "upd started";
            for (size_t i = 0; i < iterations; i += pipe.invokem<1>([](auto& node)
            {
                int64_t& x = reinterpret_cast<int64_t&>(node);
                if (x%3) { LOG_ERR << "unexpected value:" << x; abort();}
                LOG_INF << "upd:" << x << "->" << x*5;
                x *= 5;
            }));
        });

        std::thread ins([&]
        {
            LOG_INF << "ins started";
            size_t counter = 0;
            for (size_t i = 0; i < iterations; i += pipe.invokem<0>([&counter](auto& node)
            {
                int64_t& x = reinterpret_cast<int64_t&>(node);
                auto const val = (counter += 3);
                LOG_INF << "ins:" << x << "->" << val;
                x = val;
            }));
        });

        ins.join();
        upd.join();
        del.join();
    }

    if (true) {
        ping_pong<1 << 6>();
        ping_pong<1 << 15>();
        ping_pong<1 << 20>();
    }

    if (true) {
        run_pipeline<probe1, 1 << 6>();
        run_pipeline<probe1, 1 << 15>();
        run_pipeline<probe1, 1 << 20>();
    }

    if (true) {
        run_ringv<probe1, 1 << 5>();
        run_ringv<probe1, 1 << 10>();
        run_ringv<probe1, 1 << 14>();
        run_ringv<probe2, 1 << 5>();
        run_ringv<probe2, 1 << 10>();
        run_ringv<probe2, 1 << 14>();
        run_ringv<probe3, 1 << 5>();
        run_ringv<probe3, 1 << 10>();
        run_ringv<probe3, 1 << 14>();
    }

    if (true) { 
        run_ringbuf<uint64_t>();
        run_ringbuf<probe1>();
        run_ringbuf<probe2>();
        run_ringbuf<probe3>();
    }

    return 0;
}
