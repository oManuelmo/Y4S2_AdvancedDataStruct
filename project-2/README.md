# Duplicate URL detector base (C++)

Minimal starter for a crawler component with:

- Bloom Filter for fast duplicate URL checks
- Hash set baseline (`std::unordered_set`) for comparison
- Basic bad URL detector (reject invalid/unsupported URLs)
- Synthetic benchmark at large URL counts

## Project structure

- `include/bloom_filter.hpp` - simple Bloom Filter implementation
- `include/url_utils.hpp` - bad URL detection helpers
- `src/main.cpp` - small benchmark and comparison runner

## Build

Prerequisites: `cmake` and a C++17 compiler (`g++` or `clang++`).

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/url_crawler_benchmark
./build/url_crawler_benchmark 5000000
./build/url_crawler_benchmark 0 urls.txt sweep_results.csv
```

The first argument is total generated URLs.

## What this base already shows

- Duplicate detection using Bloom Filter
- False-positive counting (against a ground-truth set)
- Rough memory comparison:
	- Bloom: exact bytes of bit array
	- Hash set: estimated bytes (buckets + strings)

## Easy places to extend

- Replace synthetic input with real extracted crawler URLs
- Tune Bloom params (`bits per URL`, `hash count`)
- Add URL normalization (lowercase host, remove fragments, etc.)
- Add stronger bad URL rules (blacklist, TLD rules, robots hints)
- Add file-backed persistence / sharding for very large crawls