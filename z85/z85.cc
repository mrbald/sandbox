#include <iostream>

#include <boost/endian/arithmetic.hpp>

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

constexpr uint32_t cpow(uint32_t base, uint8_t exp)
{
    return (exp == 0) ? 1:
        (exp % 2 == 0) ? 
                cpow(base, exp / 2) * cpow(base, exp / 2):
                base * cpow(base, (exp - 1) / 2) * cpow(base, (exp - 1) / 2);
}

/* 
 * Encoder implementation code:
 *   uint32_t (native byte order) ==> array<uchar,5>
 */
template <size_t base, size_t N, size_t... Is>
std::array<uchar_t, sizeof...(Is)> _encode(uint32_t val, std::index_sequence<Is...>)
{
    std::array<uint8_t, 2> buf{};
    return { en_codes[buf[(Is + 1) & 1] = uint8_t((val -= buf[Is & 1] * cpow(base, N - Is)) / cpow(base, N - Is - 1))]... };
}

template <size_t base, size_t N>
std::array<uchar_t, N> _encode(uint32_t val) { return _encode<base, N>(val, std::make_index_sequence<N>()); }


/* 
 * Decoder implementation code:
 *   array<uchar,5> ==> uint32_t (native byte order)
 */
template <size_t base, size_t N, size_t... Is>
uint32_t _decode(std::array<uchar_t, N> val, std::index_sequence<Is...>)
{
    uint32_t dec_val{};
    auto _ {(dec_val += de_codes[val[Is]] * cpow(base, N - Is - 1))...};
    return dec_val;
}

template <size_t base, size_t N>
uint32_t _decode(std::array<uchar_t, N> val) { return _decode<base, N>(val, std::make_index_sequence<N>()); }

using boost::endian::big_uint32_t;

} // local namespace

using cursor_t = std::pair<uchar_t const*, uchar_t*>;

template <size_t base, size_t N>
cursor_t encode(cursor_t locs)
{
    auto encoded = _encode<base, N>((big_uint32_t&)(*locs.first));
    return {locs.first + sizeof(big_uint32_t), std::copy_n(encoded.begin(), N, locs.second)};
}

template <size_t base, size_t N>
cursor_t decode(cursor_t locs)
{
    std::array<uchar_t, N> buf;
    std::copy_n(locs.first, N, buf.begin());
    ((big_uint32_t&)(*locs.second)) = _decode<base, N>(buf);
    return {locs.first + N, locs.second + sizeof(big_uint32_t)};
}

} // namespace z85

using namespace z85;

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

    return 0;
}
