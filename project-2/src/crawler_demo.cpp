#include "web_crawler.hpp"
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

void printCrawlerStats(const CrawlerStats& stats) {
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        stats.endTime - stats.startTime
    );
    
    std::cout << "\n=== Crawler Statistics ===\n";
    std::cout << "Pages crawled      : " << stats.pagesCrawled << "\n";
    std::cout << "Pages failed       : " << stats.pagesFailed << "\n";
    std::cout << "Duplicates skipped : " << stats.duplicatesSkipped << "\n";
    std::cout << "Out of scope       : " << stats.outOfScopeSkipped << "\n";
    std::cout << "Robots blocked     : " << stats.robotsBlocked << "\n";
    std::cout << "Total links found  : " << stats.totalLinksFound << "\n";
    std::cout << "Duration           : " << duration.count() << " seconds\n";
    std::cout << "Pages per second   : " 
              << std::fixed << std::setprecision(2)
              << (duration.count() > 0 ? 
                  static_cast<double>(stats.pagesCrawled) / duration.count() : 0) 
              << "\n";
}

template <typename BloomType>
void printBloomStats(const BloomType& bloom) {
    std::cout << "\n=== Bloom Filter Stats ===\n";
    if constexpr (std::is_same_v<BloomType, scalableBloomFilter>) {
        std::cout << "Layers      : " << bloom.layerCount() << "\n";
        std::cout << "Insertions  : " << bloom.insertions() << "\n";
        std::cout << "Memory (KB) : " << bloom.memoryBytes() / 1024 << "\n";
        std::cout << "Theoretical FP: " << std::fixed << std::setprecision(6)
                  << bloom.theoreticalFpRate() << "\n";
    } else {
        std::cout << "Bit count    : " << bloom.bitCount() << "\n";
        std::cout << "Hash count   : " << bloom.hashCount() << "\n";
        std::cout << "Insertions   : " << bloom.insertions() << "\n";
        std::cout << "Memory (KB)  : " << bloom.memoryBytes() / 1024 << "\n";
        std::cout << "Theoretical FP: " << std::fixed << std::setprecision(6)
                  << bloom.theoreticalFpRate() << "\n";
    }
}

template <typename CrawlerType>
void runCrawlerDemo(const std::string& label,
                    CrawlerConfig config,
                    const std::vector<std::string>& seeds,
                    int durationSeconds) {
    std::cout << "\n=== " << label << " ===\n";

    config.onPageFetched = [](const HttpResponse& response) {
        std::cout << "[Fetched] " << response.url
                  << " (status: " << response.statusCode
                  << ", time: " << response.fetchTime.count() << "ms";
        if (!response.contentType.empty()) {
            std::cout << ", type: " << response.contentType;
        }
        std::cout << ")\n";
    };

    config.onProgress = [](const std::string& stage, size_t current, size_t failed) {
        std::cout << "\r[Progress] " << stage << ": " << current << " pages, "
                  << failed << " failed" << std::flush;
    };

    CrawlerType crawler(config);

    std::cout << "Starting crawler with " << config.maxThreads << " threads...\n";
    crawler.start(seeds);

    std::this_thread::sleep_for(std::chrono::seconds(durationSeconds));
    crawler.stop();

    printCrawlerStats(crawler.getStats());
    printBloomStats(crawler.getBloomFilter());
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [standard|counting|scalable] [options]\n"
              << "Options:\n"
              << "  --seconds N         Crawl duration in seconds (default 30)\n"
              << "  --threads N         Worker threads (default 4)\n"
              << "  --depth N           Max crawl depth (default 3)\n"
              << "  --pages N           Max pages (default 100)\n"
              << "  --delay MS          Politeness delay in ms (default 1000)\n"
              << "  --allow-domain D    Allowed domain (repeatable)\n"
              << "  --seed URL          Seed URL (repeatable)\n";
}

struct DemoOptions {
    std::string filter = "standard";
    std::vector<std::string> seeds;
    std::vector<std::string> allowedDomains;
    int seconds = 30;
    size_t threads = 4;
    size_t depth = 3;
    size_t pages = 100;
    int delayMs = 1000;
};

bool parseInt(const std::string& s, int& value) {
    try {
        size_t idx = 0;
        int parsed = std::stoi(s, &idx);
        if (idx != s.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseSizeT(const std::string& s, size_t& value) {
    try {
        size_t idx = 0;
        size_t parsed = static_cast<size_t>(std::stoull(s, &idx));
        if (idx != s.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

int main(int argc, char** argv) {
    DemoOptions options;

    int i = 1;
    if (i < argc) {
        std::string arg1 = argv[i];
        if (arg1 == "standard" || arg1 == "counting" || arg1 == "scalable") {
            options.filter = arg1;
            ++i;
        }
    }

    for (; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--seconds" && i + 1 < argc) {
            parseInt(argv[++i], options.seconds);
        } else if (arg == "--threads" && i + 1 < argc) {
            parseSizeT(argv[++i], options.threads);
        } else if (arg == "--depth" && i + 1 < argc) {
            parseSizeT(argv[++i], options.depth);
        } else if (arg == "--pages" && i + 1 < argc) {
            parseSizeT(argv[++i], options.pages);
        } else if (arg == "--delay" && i + 1 < argc) {
            parseInt(argv[++i], options.delayMs);
        } else if (arg == "--allow-domain" && i + 1 < argc) {
            options.allowedDomains.push_back(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            options.seeds.push_back(argv[++i]);
        } else {
            std::cout << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (options.seeds.empty()) {
        options.seeds = {
            "http://httpbin.org/",
            "http://httpbin.org/html",
            "http://httpbin.org/links/10/0"
        };
    }

    if (options.allowedDomains.empty()) {
        options.allowedDomains = {
            "example.com",
            "httpbin.org"
        };
    }

    CrawlerConfig config;
    config.maxThreads = options.threads;
    config.maxDepth = options.depth;
    config.maxPages = options.pages;
    config.politenessDelayMs = options.delayMs;
    config.allowedDomains = {options.allowedDomains.begin(), options.allowedDomains.end()};

    if (options.filter == "counting") {
        runCrawlerDemo<CountingWebCrawler>("Counting Bloom", config, options.seeds, options.seconds);
    } else if (options.filter == "scalable") {
        runCrawlerDemo<ScalableWebCrawler>("Scalable Bloom", config, options.seeds, options.seconds);
    } else {
        runCrawlerDemo<WebCrawler>("Standard Bloom", config, options.seeds, options.seconds);
    }

    return 0;
}