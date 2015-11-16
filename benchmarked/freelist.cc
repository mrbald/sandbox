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


#include <benchmark/benchmark.h>

namespace
{

struct MyFixture: benchmark::Fixture
{
    MyFixture(){}

    ~MyFixture(){}
};

//void benchme(benchmark::State& state) { ... }
// BENCHMARK(benchme)->ThreadRange(1, 16);

BENCHMARK_DEFINE_F(MyFixture, benchme)(benchmark::State& state)
{
    if (state.thread_index == 0) { /* set-up */ }

    std::string x = "hello";
    size_t bytes = 0;
    size_t items = 0;

    while (state.KeepRunning())
    {
        benchmark::DoNotOptimize(bytes += std::string(x).size());
        ++items;
    }

    state.SetBytesProcessed(bytes);
    state.SetItemsProcessed(items);

    if (state.thread_index == 0) { /* tear-down */ }
}

BENCHMARK_REGISTER_F(MyFixture, benchme)->ThreadRange(1, 16);

} // local namespace

BENCHMARK_MAIN()

