#include "bloom_filter.hpp"
#include "url_utils.hpp"
#include "web_crawler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

const std::size_t BLOOM_BITS = 500'000;

void runCrawlerDemo(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--crawl") {
        std::cout << "Running web crawler mode...\n";
        
        CrawlerConfig config;
        config.maxThreads = 4;
        config.maxDepth = 2;
        config.maxPages = 50;
        config.politenessDelayMs = 500;
        
        // Parse seed URLs from command line
        std::vector<std::string> seeds;
        for (int i = 2; i < argc; ++i) {
            seeds.push_back(argv[i]);
        }
        
        if (seeds.empty()) {
            seeds = {"https://example.com"};
        }
        
        WebCrawler crawler(config);
        
        config.onPageFetched = [](const HttpResponse& response) {
            std::cout << "✓ " << response.url << " (" << response.statusCode << ")\n";
        };
        
        crawler.start(seeds);
        
        // Let it run for a while
        std::this_thread::sleep_for(std::chrono::seconds(10));
        crawler.stop();
        
        const auto& stats = crawler.getStats();
        std::cout << "\nCrawled " << stats.pagesCrawled << " pages, "
                  << stats.totalLinksFound << " links discovered\n";
    }
}

// ─────────────────────────────────────────────
//  Data helpers
// ─────────────────────────────────────────────

std::vector<std::string> buildSyntheticInput(std::size_t targetCount, double duplicateRate) {
    std::vector<std::string> urls;
    urls.reserve(targetCount);
    
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    // Estratégia: Manter um pool de "URLs populares" para forçar triplicatas/quadruplicatas
    std::vector<std::string> uniquePool;
    uniquePool.reserve(targetCount);

    for (std::size_t i = 0; i < targetCount; ++i) {
        // Se o pool não estiver vazio e o dado rolar abaixo da taxa de duplicados
        if (!uniquePool.empty() && dist(rng) < duplicateRate) {
            
            // Escolhemos um URL do pool. 
            // Como escolhemos aleatoriamente de um pool que cresce devagar,
            // a chance de sair o mesmo URL 3, 4 ou 10 vezes aumenta drasticamente.
            std::uniform_int_distribution<std::size_t> pick(0, uniquePool.size() - 1);
            urls.push_back(uniquePool[pick(rng)]);
            
        } else {
            // Cria um URL novo e adiciona ao pool de potenciais duplicados
            std::string newUrl = "https://example.org/page/" + std::to_string(i);
            urls.push_back(newUrl);
            uniquePool.push_back(newUrl);
        }
    }
    return urls;
}

// Load URLs from a plain-text file (one URL per line).
std::vector<std::string> loadUrlsFromFile(const std::string& path) {
    std::vector<std::string> urls;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[warn] could not open " << path << ", using synthetic data\n";
        return urls;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty()) urls.push_back(std::move(line));
    }
    std::cerr << "[info] loaded " << urls.size() << " URLs from " << path << "\n";
    return urls;
}

// ─────────────────────────────────────────────
//  Benchmark structs
// ─────────────────────────────────────────────

struct BenchmarkResult {
    std::size_t total            = 0;
    std::size_t uniqueAccepted   = 0;
    std::size_t duplicatesDetected = 0;
    std::size_t falsePositives   = 0;
    double      seconds          = 0.0;
    double      empiricalFpRate  = 0.0;
    double      theoreticalFpRate = 0.0;
    std::size_t memoryBytes      = 0;
    std::size_t layers           = 0; // used by scalableBloomFilter
};

// ─────────────────────────────────────────────
//  Standard Bloom benchmark
// ─────────────────────────────────────────────

BenchmarkResult runBloomBenchmark(const std::vector<std::string>& input,
                                   std::size_t bloomBits,       // ← absolute bit count
                                   std::size_t hashCount,
                                   bool normalize) {
    BloomFilter bloom(bloomBits, hashCount);

    std::unordered_set<std::string> truth;
    truth.reserve(input.size());

    BenchmarkResult result;
    result.total = input.size();

    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& raw : input) {
        const auto bad = isBadUrl(raw);
        if (bad.bad) continue;

        const std::string url = normalize
            ? normalizeUrl(raw).value_or(raw)
            : raw;

        if (bloom.contains(url)) {
            if (truth.find(url) == truth.end()) {
                ++result.falsePositives;
                truth.insert(url);
            } else {
                ++result.duplicatesDetected;
            }
        } else {
            bloom.add(url);
            truth.insert(url);
            ++result.uniqueAccepted;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();

    const double denom = static_cast<double>(result.uniqueAccepted + result.falsePositives);
    result.empiricalFpRate   = denom > 0 ? result.falsePositives / denom : 0.0;
    result.theoreticalFpRate = bloom.theoreticalFpRate();
    result.memoryBytes       = bloom.memoryBytes();
    return result;
}

// ─────────────────────────────────────────────
//  Scalable Bloom benchmark
// ─────────────────────────────────────────────

BenchmarkResult runScalableBloomBenchmark(const std::vector<std::string>& input,
                                         std::size_t initialExpectedInsertions,
                                         double targetFpRate,
                                         bool normalize) {
    scalableBloomFilter bloom(initialExpectedInsertions, targetFpRate);

    std::unordered_set<std::string> truth;
    truth.reserve(input.size());

    BenchmarkResult result;
    result.total = input.size();

    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& raw : input) {
        const auto bad = isBadUrl(raw);
        if (bad.bad) continue;

        const std::string url = normalize
            ? normalizeUrl(raw).value_or(raw)
            : raw;

        if (bloom.contains(url)) {
            if (truth.find(url) == truth.end()) {
                ++result.falsePositives;
                truth.insert(url);
            } else {
                ++result.duplicatesDetected;
            }
        } else {
            bloom.add(url);
            truth.insert(url);
            ++result.uniqueAccepted;
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();

    const double denom = static_cast<double>(result.uniqueAccepted + result.falsePositives);
    result.empiricalFpRate   = denom > 0 ? result.falsePositives / denom : 0.0;
    result.theoreticalFpRate = bloom.theoreticalFpRate();
    result.memoryBytes       = bloom.memoryBytes();
    result.layers            = bloom.layerCount();
    return result;
}

// ─────────────────────────────────────────────
//  Counting Bloom benchmark (includes deletion demo)
// ─────────────────────────────────────────────

struct CountingResult : BenchmarkResult {
    std::size_t successfulDeletes = 0;
    std::size_t deleteVerifyOk    = 0;
};

CountingResult runCountingBloomBenchmark(const std::vector<std::string>& input,
                                          std::size_t bloomBits,
                                          std::size_t hashCount,
                                          bool normalize) {
    CountingBloomFilter bloom(bloomBits, hashCount);

    std::unordered_set<std::string> truth;
    truth.reserve(input.size());

    CountingResult result;
    result.total = input.size();

    // --- insertion phase ---
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& raw : input) {
        const auto bad = isBadUrl(raw);
        if (bad.bad) continue;
        const std::string url = normalize ? normalizeUrl(raw).value_or(raw) : raw;

        if (bloom.contains(url)) {
            if (truth.find(url) == truth.end()) {
                ++result.falsePositives;
                truth.insert(url);
            } else {
                ++result.duplicatesDetected;
            }
        } else {
            bloom.add(url);
            truth.insert(url);
            ++result.uniqueAccepted;
        }
    }

    // --- deletion demo: remove every 5th unique URL ---
    std::size_t i = 0;
    for (const auto& url : truth) {
        if ((i++ % 5) == 0) {
            if (bloom.remove(url)) {
                ++result.successfulDeletes;
                // Verify it is now absent
                if (!bloom.contains(url)) ++result.deleteVerifyOk;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();

    const double denom = static_cast<double>(result.uniqueAccepted + result.falsePositives);
    result.empiricalFpRate   = denom > 0 ? result.falsePositives / denom : 0.0;
    result.theoreticalFpRate = bloom.theoreticalFpRate();
    result.memoryBytes       = bloom.memoryBytes();
    return result;
}

// ─────────────────────────────────────────────
//  Hash-set baseline
// ─────────────────────────────────────────────

BenchmarkResult runHashSetBaseline(const std::vector<std::string>& input, bool normalize) {
    std::unordered_set<std::string> seen;
    seen.reserve(input.size());

    BenchmarkResult result;
    result.total = input.size();

    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& raw : input) {
        const auto bad = isBadUrl(raw);
        if (bad.bad) continue;
        const std::string url = normalize ? normalizeUrl(raw).value_or(raw) : raw;

        const auto [it, inserted] = seen.insert(url);
        if (inserted) ++result.uniqueAccepted;
        else          ++result.duplicatesDetected;
        (void)it;
    }
    const auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();

    // Estimate hash-set memory: bucket array + string storage
    std::size_t mem = seen.bucket_count() * sizeof(void*);
    for (const auto& s : seen) mem += sizeof(std::string) + s.size() + 1;
    result.memoryBytes = mem;
    return result;
}

// ─────────────────────────────────────────────
//  Printing helpers
// ─────────────────────────────────────────────

void printResult(const char* title, const BenchmarkResult& r) {
    std::cout << "\n[" << title << "]\n";
    std::cout << "  Total checked        : " << r.total              << "\n";
    std::cout << "  Unique accepted      : " << r.uniqueAccepted     << "\n";
    std::cout << "  Duplicates detected  : " << r.duplicatesDetected << "\n";
    std::cout << "  False positives      : " << r.falsePositives     << "\n";
    std::cout << "  FP rate (empirical)  : " << std::fixed << std::setprecision(6) << r.empiricalFpRate   << "\n";
    if (r.theoreticalFpRate > 0.0)
        std::cout << "  FP rate (theoretical): " << std::fixed << std::setprecision(6) << r.theoreticalFpRate << "\n";
    if (r.layers > 0)
        std::cout << "  Layers               : " << r.layers << "\n";
    std::cout << "  Time (s)             : " << std::fixed << std::setprecision(4) << r.seconds       << "\n";
    std::cout << "  Memory (bytes)       : " << r.memoryBytes << "\n";
}

// ─────────────────────────────────────────────
//  Parametric sweep → CSV
// ─────────────────────────────────────────────

void runParametricSweep(const std::vector<std::string>& input, const std::string& csvPath) {
    std::ofstream csv(csvPath);
    if (!csv.is_open()) {
        std::cerr << "[warn] cannot write sweep CSV to " << csvPath << "\n";
        return;
    }
    csv << "bits_per_url,hash_count,fp_empirical,fp_theoretical,memory_bytes,time_s\n";

    const std::vector<std::size_t> bpuList   = {5, 8, 10, 14, 20};
    const std::vector<std::size_t> hashList  = {3, 5, 7,  9};

    std::cout << "\n[Parametric Sweep]  (writing to " << csvPath << ")\n";
    std::cout << std::left
              << std::setw(14) << "bits/url"
              << std::setw(12) << "hash_k"
              << std::setw(16) << "FP_empirical"
              << std::setw(16) << "FP_theoretical"
              << std::setw(14) << "mem_KB"
              << "time_s\n";
    std::cout << std::string(82, '-') << "\n";

    for (auto bpu : bpuList) {
        for (auto k : hashList) {
            auto r = runBloomBenchmark(input, input.size() * bpu, k, true);
            csv << bpu << "," << k << ","
                << std::fixed << std::setprecision(8)
                << r.empiricalFpRate << ","
                << r.theoreticalFpRate << ","
                << r.memoryBytes << ","
                << r.seconds << "\n";

            std::cout << std::left
                      << std::setw(14) << bpu
                      << std::setw(12) << k
                      << std::setw(16) << std::fixed << std::setprecision(6) << r.empiricalFpRate
                      << std::setw(16) << r.theoreticalFpRate
                      << std::setw(14) << (r.memoryBytes / 1024)
                      << std::setprecision(4) << r.seconds << "\n";
        }
    }
    std::cout << "[Sweep CSV saved to " << csvPath << "]\n";
}

// ─────────────────────────────────────────────
//  Normalization demo
// ─────────────────────────────────────────────

void runNormalizationDemo() {
    const std::vector<std::string> examples = {
        "HTTPS://Example.COM/Path/To/Page",
        "https://example.com:443/path/to/page",
        "https://example.com/path/to/page#section",
        "https://example.com/path/%2Fencoded/page",
        "http://example.com:80/",
        "http://example.com/search?q=hello%20world",
    };
    std::cout << "\n[URL Normalization Demo]\n";
    std::cout << std::string(80, '-') << "\n";
    for (const auto& url : examples) {
        const auto norm = normalizeUrl(url);
        std::cout << "  IN : " << url << "\n";
        std::cout << "  OUT: " << (norm ? *norm : "<invalid>") << "\n\n";
    }
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────

int main(int argc, char** argv) {
    // Args: [url_count] [url_file] [sweep_csv_output]
    std::size_t n             = 500'000;
    std::string urlFile       = "";
    std::string sweepCsvPath  = "sweep_results.csv";

    if (argc > 1) n           = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (argc > 2) urlFile     = argv[2];
    if (argc > 3) sweepCsvPath = argv[3];

    // Load or generate input
    std::vector<std::string> input;
    if (!urlFile.empty()) {
        input = loadUrlsFromFile(urlFile);
    }
    if (input.empty()) {
        std::cout << "[info] generating " << n << " synthetic URLs (20% duplicate rate)\n";
        input = buildSyntheticInput(n, 0.20);
    }

    // ── Normalization demo ───────────────────
    runNormalizationDemo();

    // ── Optimal params for n URLs at 1% FP ──
    const auto params = optimalBloomParams(input.size(), 0.01);
    std::cout << "\n[Optimal Bloom params for n=" << input.size() << ", p=1%]\n";
    std::cout << "  bits (m)   = " << params.bitCount  << "\n";
    std::cout << "  hashes (k) = " << params.hashCount << "\n";
    std::cout << "  FP theory  = " << std::fixed << std::setprecision(6) << params.fpRate << "\n";

    // ── Standard Bloom (optimal params) ─────
    const auto bloomRes  = runBloomBenchmark(input, BLOOM_BITS, 2, true);
    printResult("Bloom Filter (normalized)", bloomRes);

    // ── Scalable Bloom (overall p=1%) ───────
    // Use a smaller initial expectation so it can actually grow on this workload.
    const std::size_t initialExpected = std::max<std::size_t>(1, 100'000);
    const auto scalableRes = runScalableBloomBenchmark(input, initialExpected, 0.01, /*normalize=*/true);
    printResult("Scalable Bloom Filter (p=1%, normalized)", scalableRes);

    // ── Counting Bloom ───────────────────────
    const auto countRes  = runCountingBloomBenchmark(input, BLOOM_BITS, 2, true);
    printResult("Counting Bloom Filter", countRes);
    std::cout << "  Deletes attempted    : " << countRes.successfulDeletes << "\n";
    std::cout << "  Delete verify OK     : " << countRes.deleteVerifyOk    << "\n";

    // ── Hash-set baseline ────────────────────
    const auto hashRes = runHashSetBaseline(input, /*normalize=*/true);
    printResult("Hash Set Baseline (normalized)", hashRes);

    // ── Comparison summary ───────────────────
    std::cout << "\n[Memory Comparison]\n";
    std::cout << "  Bloom   : " << bloomRes.memoryBytes / 1024 << " KB\n";
    std::cout << "  Scalable: " << scalableRes.memoryBytes / 1024 << " KB  (layers=" << scalableRes.layers << ")\n";
    std::cout << "  Counting: " << countRes.memoryBytes / 1024 << " KB  (2x Bloom, gains deletion)\n";
    std::cout << "  HashSet : " << hashRes.memoryBytes  / 1024 << " KB\n";
    std::cout << "  HashSet/Bloom ratio: "
              << std::fixed << std::setprecision(1)
              << static_cast<double>(hashRes.memoryBytes) / static_cast<double>(bloomRes.memoryBytes)
              << "x\n";

    // ── Parametric sweep ─────────────────────
    runParametricSweep(input, sweepCsvPath);

    std::cout << "\nTip: pass args  <url_count> [url_file] [sweep_csv]\n";
    return 0;
}
