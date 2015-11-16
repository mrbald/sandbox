#include <iostream>

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

#include <boost/endian/arithmetic.hpp>
#include <boost/integer.hpp>

#include <cstdint>
#include <utility>
#include <array>
#include <algorithm>

namespace z85
{

using uchar_t = unsigned char;

namespace
{

uchar_t const en_codes[]{"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#"};

std::array<uint8_t, std::numeric_limits<uint8_t>::max() + 1> const de_codes = []
{
    std::array<uint8_t, std::numeric_limits<uint8_t>::max() + 1> x{};
    for (uint8_t i = 0; i < sizeof(en_codes) - 1; ++i)
    {
        x[en_codes[i]] = i;
    }
    return x;
}();

constexpr inline
uint32_t cpow(uint32_t base, uint8_t exp) noexcept
{
    return (exp == 0) ? 1:
        (exp % 2 == 0) ? 
                cpow(base, exp / 2) * cpow(base, exp / 2):
                base * cpow(base, (exp - 1) / 2) * cpow(base, (exp - 1) / 2);
}

template <class X> inline
constexpr X sum(X&& x) noexcept { return std::forward<X>(x); }

template <class X, class... Xs> inline
constexpr 
std::enable_if_t<!!sizeof...(Xs), std::common_type_t<X, Xs...>> sum(X&& x, Xs&&... xs) noexcept { return x + sum(std::forward<Xs>(xs)...); }

/* 
 * Encoder implementation code:
 *   uint32_t (native byte order) ==> array<uchar,5>
 */
template <size_t base, size_t N, size_t... Is> inline
std::array<uchar_t, sizeof...(Is)> _encode(uint32_t val, std::index_sequence<Is...>) noexcept
{
    uint32_t buf{};
    return { en_codes[buf = (val -= buf * cpow(base, N - Is)) / cpow(base, N - Is - 1)] ... };
}

template <size_t base, size_t N> inline
std::array<uchar_t, N> _encode(uint32_t val) noexcept { return _encode<base, N>(val, std::make_index_sequence<N>()); }


/* 
 * Decoder implementation code:
 *   array<uchar,5> ==> uint32_t (native byte order)
 */
template <size_t base, size_t N, size_t... Is> inline
uint32_t _decode(std::array<uchar_t, N> const& val, std::index_sequence<Is...>) noexcept
{
    return sum(de_codes[val[Is]] * cpow(base, N - Is - 1)...);
}

template <size_t base, size_t N> inline
uint32_t _decode(std::array<uchar_t, N> const& val) noexcept { return _decode<base, N>(val, std::make_index_sequence<N>()); }

using boost::endian::big_uint32_t;

} // local namespace

using cursor_t = std::pair<uchar_t const*, uchar_t*>;

template <size_t base, size_t N> inline
cursor_t encode(cursor_t locs) noexcept
{
    auto encoded = _encode<base, N>((big_uint32_t&)(*locs.first));
    std::copy_n(encoded.begin(), N, locs.second);
    return {locs.first + sizeof(big_uint32_t), locs.second + N};
}

template <size_t base, size_t N> inline
cursor_t decode(cursor_t locs) noexcept
{
    std::array<uchar_t, N> buf;
    std::copy_n(locs.first, N, buf.begin());
    ((big_uint32_t&)(*locs.second)) = _decode<base, N>(buf);
    return {locs.first + N, locs.second + sizeof(big_uint32_t)};
}

} // namespace z85

using namespace z85;

#include <random>
#include <chrono>
#include <functional>

// g++ -g -Og -std=c++14 -Wall -pedantic -Wno-unused -isystem /path/to/boost/headers z85.cc -o z85
int main()
{
    std::array<uchar_t, 8> const sample {0x86, 0x4F, 0xD2, 0x6F, 0xB5, 0x59, 0xF7, 0x5B};
    std::array<uchar_t, 10> encoded {};
    std::array<uchar_t, sample.size()> decoded {};

    cursor_t encoded_locs{sample.data(), encoded.data()};
    cursor_t decoded_locs{encoded.data(), decoded.data()};

    std::cout << "source: ";
    for (auto x : sample)
        std::cout << std::hex << "0x" << ((unsigned)x) << ' ';
    std::cout << std::endl;

    while (encoded_locs.first < sample.data() + sample.size())
    {
        std::cout << "encoding " << ((void*)encoded_locs.first) << "...\n";
        encoded_locs = encode<85, 5>(encoded_locs);
    }

    std::cout << "encoded: ";
    for (auto x : encoded)
        std::cout << x << '.';
    std::cout << std::endl;

    while (decoded_locs.first < encoded.data() + encoded.size())
    {
        std::cout << "decoding " << ((void*)decoded_locs.first) << "...\n";
        decoded_locs = decode<85, 5>(decoded_locs);
    }

    std::cout << "decoded: ";
    for (auto x : decoded)
        std::cout << std::hex << "0x" << ((unsigned)x) << ' ';
    std::cout << std::endl;

    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uchar_t> dis;
        std::array<uchar_t, (4 * 1ul<<10)> samples {};
        std::array<uchar_t, (5 * 1ul<<10)> encoded {};
        std::array<uchar_t, (4 * 1ul<<10)> decoded {};

        std::generate_n(samples.begin(), samples.size(), std::bind(dis, std::mt19937(rd())));

        auto const iterations = 20000;

        using clock = std::chrono::steady_clock;

        auto en_code = [&] {
            cursor_t encoded_locs{samples.data(), encoded.data()};
            while (encoded_locs.first < samples.data() + samples.size())
                encoded_locs = encode<85, 5>(encoded_locs);
        };
        en_code();

        auto encoder_started = clock::now();
        for (int i = 0; i < iterations; ++i) en_code();
        auto encoder_stopped = clock::now();

        auto de_code = [&]{
            cursor_t decoded_locs{encoded.data(), decoded.data()};
            while (decoded_locs.first < encoded.data() + encoded.size())
                decoded_locs = decode<85, 5>(decoded_locs);
        };
        de_code();

        auto decoder_started = clock::now();
        for (int i = 0; i < iterations; ++i) de_code();
        auto decoder_stopped = clock::now();

        auto encoder_usec = std::chrono::duration_cast<std::chrono::microseconds>(encoder_stopped - encoder_started);
        auto decoder_usec = std::chrono::duration_cast<std::chrono::microseconds>(decoder_stopped - decoder_started);
        std::cout << "encoder: " << std::dec << encoder_usec.count() << " us, " << ((samples.size() * iterations) / encoder_usec.count()) << " bytes/us \n";
        std::cout << "decoder: " << std::dec << decoder_usec.count() << " us, " << ((samples.size() * iterations) / decoder_usec.count()) << " bytes/us \n";
    }

    return 0;
}
