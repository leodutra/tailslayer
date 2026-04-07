[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)
[![GitHub stars](https://img.shields.io/github/stars/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/network/members)
[![GitHub contributors](https://img.shields.io/github/contributors/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/graphs/contributors)
[![Follow @lauriewired](https://img.shields.io/twitter/follow/lauriewired?style=social)](https://twitter.com/lauriewired)

# Tailslayer

Tailslayer is a C++ library that reduces tail latency in RAM reads caused by DRAM refresh stalls. It works by replicating data across multiple different channels and waiting for a read request. Once the request comes in, it hedges the reads across the replicas, allowing the work to be performed on whichever replica access was fastest.

## Usage

The library code is available in [hedged_reader.cpp](https://github.com/LaurieWired/tailslayer/blob/main/include/tailslayer/hedged_reader.hpp) and the example using the library can be found in [tailslayer_example.cpp](https://github.com/LaurieWired/tailslayer/blob/main/tailslayer_example.cpp). To use it, copy `include/tailslayer` into your project and `#include <tailslayer/hedged_reader.hpp>`.

You provide the value type and two functions as template parameters:

1. **Signal function**: Add the loop that waits for the external signal. This determines when to read. Return the desired index to read, and the read immediately fires.
2. **Final work function**: This receives the value immediately after it is read. Add the desired value processing code here.

```cpp
#include <tailslayer/hedged_reader.hpp>

[[gnu::always_inline]] inline std::size_t my_signal() {
    // Wait for your event, then return the index to read
    return index_to_read;
}

template <typename T>
[[gnu::always_inline]] inline void my_work(T val) {
    // Use the value
}

int main() {
    using T = uint8_t;
    tailslayer::pin_to_core(tailslayer::CORE_MAIN);

    tailslayer::HedgedReader<T, my_signal, my_work<T>> reader{};
    reader.insert(0x43);
    reader.insert(0x44);
    reader.start_workers();
}
```

Arguments can be passed to either function via `ArgList`:

```cpp
tailslayer::HedgedReader<T, my_signal, my_work<T>,
    tailslayer::ArgList<1, 2>,   // args to signal function
    tailslayer::ArgList<2>       // args to final work function
> reader{};
```

You can also optionally pass in a different channel offset, channel bit, and number of replicas to the constructor. *Note:* Each insert copies the element N times where N is the number of replicas. It does the address calculation work on the backend, allowing tailslayer to act as a hedged vector that uses logical indices. Additionally, each replica is pinned to a separate core, and will spin on that core according to the signal function until the read happens.

## Build the example

```bash
make
./tailslayer_example
```

## Benchmarks and spike timing

The `discovery/` directory contains supporting code used to characterize DRAM refresh behavior:

- `discovery/benchmark/`: Channel-hedged read benchmark
- `discovery/trefi_probe.c`: Spike timing probe for measuring the refresh cycle

```bash
cd discovery/benchmark
make
sudo chrt -f 99 ./hedged_read_cpp --all --channel-bit 8
```
