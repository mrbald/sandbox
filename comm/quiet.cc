#include "../ringbuf/tsc_clock.h"

#include <boost/asio.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include <thread>
#include <memory>
#include <atomic>
#include <iostream>

using namespace boost::asio;
using namespace boost::accumulators;

int main()
{
    using clock = ufw::tsc_clock;
    clock::scale();

    io_service loop;

    auto work = std::make_unique<io_service::work>(loop);

    using acc_t = accumulator_set<double, stats<tag::min, tag::max, tag::mean, tag::variance>>;
    acc_t acc;

    std::thread thread([&]
    {
        loop.run();
        std::atomic_thread_fence(std::memory_order_release);
    });

    using boost::system::error_code;
    ip::tcp::acceptor acceptor(loop, ip::tcp::endpoint(ip::tcp::v4(), 2222));
    ip::tcp::socket asocket(loop);
    acceptor.async_accept(asocket, [](auto){});

    ip::tcp::socket csocket(loop);
    csocket.connect(ip::tcp::endpoint(ip::address_v4::loopback(), 2222));
    csocket.set_option(socket_base::send_buffer_size(1u << 20));
    //csocket.set_option(socket_base::send_low_watermark(1u << 12));
    int lowat = 4096;
    if (setsockopt(csocket.native(), IPPROTO_TCP, TCP_NOTSENT_LOWAT, (const char*)&lowat, sizeof(lowat)))
    {
        std::clog << "oh no!" << std::endl;
    }

//     // this tests the latency to post a functor to the loop
//     auto fn = loop.wrap([&](auto then)
//     {
//         acc(std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - then).count());
//     });

    // this tests the writability check latency
    auto fn = loop.wrap([&]
    {
        csocket.async_write_some(null_buffers(), [&, then = clock::now()](auto, auto)
        {
            acc(std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - then).count());
        });
    });

    for (size_t i = 0; i < 10'000; ++i)
    {
        using namespace std::literals;
        std::this_thread::sleep_for(10us);
        //fn(clock::now()); 
        fn();
    }

    work.reset();
    if (thread.joinable())
        thread.join();

    std::atomic_thread_fence(std::memory_order_acquire);

    std::clog << "min: " << min(acc) << " us" << std::endl;
    std::clog << "max: " << max(acc) << " us" << std::endl;
    std::clog << "mean: " << mean(acc) << " us" << std::endl;
    std::clog << "var:  " << variance(acc) << " us" << std::endl;

    return 0;
}
