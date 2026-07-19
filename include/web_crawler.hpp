#pragma once

#include "bloom_filter.hpp"
#include "url_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <regex>
#include <sstream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

// HTTP response structure
struct HttpResponse {
    std::string url;
    int statusCode = 0;
    std::string contentType;
    std::string body;
    std::vector<std::string> links;
    std::chrono::milliseconds fetchTime;
    bool success = false;
    std::string errorMessage;
};

// Crawler configuration
struct CrawlerConfig {
    std::string userAgent = "WebCrawler/1.0";  // Added user agent field
    size_t maxThreads = 4;
    size_t maxQueueSize = 10000;
    int politenessDelayMs = 500;
    size_t maxDepth = 5;
    size_t maxPages = 1000;
    size_t maxRetries = 3;
    double desiredFpRate = 0.01;
    bool normalizeUrls = true;
    
    std::set<std::string> allowedDomains;
    std::set<std::string> blockedExtensions = {
        ".jpg", ".jpeg", ".png", ".gif", ".svg", ".webp",
        ".mp4", ".mp3", ".pdf", ".zip", ".tar", ".gz"
    };
    
    std::function<void(const HttpResponse&)> onPageFetched = nullptr;
    std::function<void(const std::string&, size_t, size_t)> onProgress = nullptr;
};

// Robots.txt entry
struct RobotsTxtEntry {
    std::string userAgent;
    std::vector<std::string> disallowed;
    std::vector<std::string> allowed;
    int crawlDelay = 0;
};

// Crawler statistics - fixed atomic assignment
struct CrawlerStats {
    std::atomic<size_t> pagesCrawled{0};
    std::atomic<size_t> pagesFailed{0};
    std::atomic<size_t> duplicatesSkipped{0};
    std::atomic<size_t> outOfScopeSkipped{0};
    std::atomic<size_t> robotsBlocked{0};
    std::atomic<size_t> totalLinksFound{0};
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    
    // Reset function instead of assignment
    void reset() {
        pagesCrawled = 0;
        pagesFailed = 0;
        duplicatesSkipped = 0;
        outOfScopeSkipped = 0;
        robotsBlocked = 0;
        totalLinksFound = 0;
    }
};

// Main crawler class
template <typename BloomType>
class BasicWebCrawler {
public:
    explicit BasicWebCrawler(const CrawlerConfig& config);
    ~BasicWebCrawler();
    
    void start(const std::vector<std::string>& seedUrls);
    void stop();
    bool isRunning() const { return running_; }
    const CrawlerStats& getStats() const { return stats_; }
    const BloomType& getBloomFilter() const { return *bloomFilter_; }
    std::unordered_set<std::string> getVisitedUrls() const;

private:
    struct QueuedUrl {
        std::string url;
        std::string domain;
        size_t depth;
        size_t retryCount;
    };
    
    struct DomainState {
        std::chrono::steady_clock::time_point lastRequestTime;
        std::unique_ptr<RobotsTxtEntry> robotsTxt;
        std::mutex mutex;
    };
    
    CrawlerConfig config_;
    CrawlerStats stats_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    
    std::unique_ptr<BloomType> bloomFilter_;
    mutable std::mutex visitedMutex_;
    std::unordered_set<std::string> visitedSet_;
    
    std::queue<QueuedUrl> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    
    std::unordered_map<std::string, std::unique_ptr<DomainState>> domains_;
    std::mutex domainsMutex_;
    
    std::vector<std::thread> workers_;
    
    void workerThread();
    void processUrl(const QueuedUrl& queuedUrl);
    bool shouldCrawl(const QueuedUrl& queuedUrl);
    bool isAllowedByRobots(const std::string& url, const std::string& domain);
    RobotsTxtEntry fetchRobotsTxt(const std::string& domain);
    HttpResponse fetchUrl(const std::string& url);
    std::vector<std::string> extractLinks(const HttpResponse& response, const std::string& baseUrl);
    std::optional<std::string> normalizeAndValidate(const std::string& url);  // Fixed return type
    std::string getDomain(const std::string& url);
    void addToQueue(const std::string& url, const std::string& parentDomain, size_t depth);
    void respectPoliteness(const std::string& domain);
    HttpResponse httpGet(const std::string& url);
};

// Implementation
template <typename BloomType>
BasicWebCrawler<BloomType>::BasicWebCrawler(const CrawlerConfig& config) : config_(config) {
    auto params = optimalBloomParams(config_.maxPages, config_.desiredFpRate);
    if constexpr (std::is_same_v<BloomType, BloomFilter> || std::is_same_v<BloomType, CountingBloomFilter>) {
        bloomFilter_ = std::make_unique<BloomType>(params.bitCount, params.hashCount);
    } else if constexpr (std::is_same_v<BloomType, scalableBloomFilter>) {
        bloomFilter_ = std::make_unique<BloomType>(config_.maxPages, config_.desiredFpRate);
    } else {
        static_assert(std::is_same_v<BloomType, BloomFilter>, "Unsupported Bloom filter type");
    }
}

template <typename BloomType>
BasicWebCrawler<BloomType>::~BasicWebCrawler() {
    stop();
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::start(const std::vector<std::string>& seedUrls) {
    if (running_) return;
    
    running_ = true;
    stopRequested_ = false;
    stats_.reset();  // Use reset instead of assignment
    stats_.startTime = std::chrono::steady_clock::now();
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    
    for (const auto& seedUrl : seedUrls) {
        auto normalized = normalizeAndValidate(seedUrl);
        if (normalized.has_value()) {
            std::string domain = getDomain(normalized.value());
            addToQueue(normalized.value(), domain, 0);
        }
    }
    
    workers_.clear();
    for (size_t i = 0; i < config_.maxThreads; ++i) {
        workers_.emplace_back(&BasicWebCrawler<BloomType>::workerThread, this);
    }
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::stop() {
    if (!running_) return;
    
    stopRequested_ = true;
    queueCv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    running_ = false;
    stats_.endTime = std::chrono::steady_clock::now();
    
#ifdef _WIN32
    WSACleanup();
#endif
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::workerThread() {
    while (running_ && !stopRequested_) {
        QueuedUrl url;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait_for(lock, std::chrono::seconds(1), [this] {
                return !queue_.empty() || stopRequested_;
            });
            
            if (stopRequested_ || queue_.empty()) continue;
            
            url = queue_.front();
            queue_.pop();
        }
        
        processUrl(url);
        
        if (config_.onProgress) {
            config_.onProgress("Crawling", stats_.pagesCrawled, stats_.pagesFailed);
        }
    }
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::processUrl(const QueuedUrl& queuedUrl) {
    if (!shouldCrawl(queuedUrl)) {
        if (queuedUrl.retryCount < config_.maxRetries) {
            QueuedUrl retryUrl = queuedUrl;
            retryUrl.retryCount++;
            addToQueue(retryUrl.url, retryUrl.domain, retryUrl.depth);
        }
        return;
    }
    
    respectPoliteness(queuedUrl.domain);
    
    HttpResponse response = fetchUrl(queuedUrl.url);
    
    if (response.success && response.statusCode == 200) {
        stats_.pagesCrawled++;
        
        if (response.contentType.find("text/html") != std::string::npos) {
            auto links = extractLinks(response, queuedUrl.url);
            stats_.totalLinksFound += links.size();
            
            for (const auto& link : links) {
                auto normalized = normalizeAndValidate(link);
                if (normalized.has_value()) {
                    std::string domain = getDomain(normalized.value());
                    if (queuedUrl.depth + 1 <= config_.maxDepth) {
                        addToQueue(normalized.value(), domain, queuedUrl.depth + 1);
                    }
                }
            }
        }
    } else {
        stats_.pagesFailed++;
    }
    
    if (config_.onPageFetched) {
        config_.onPageFetched(response);
    }
}

template <typename BloomType>
bool BasicWebCrawler<BloomType>::shouldCrawl(const QueuedUrl& queuedUrl) {
    if (stats_.pagesCrawled >= config_.maxPages) {
        stop();
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(visitedMutex_);
        if (visitedSet_.find(queuedUrl.url) != visitedSet_.end()) {
            stats_.duplicatesSkipped++;
            return false;
        }
    }
    
    if (bloomFilter_->contains(queuedUrl.url)) {
        std::lock_guard<std::mutex> lock(visitedMutex_);
        if (visitedSet_.find(queuedUrl.url) != visitedSet_.end()) {
            stats_.duplicatesSkipped++;
            return false;
        }
    }
    
    if (!config_.allowedDomains.empty()) {
        if (config_.allowedDomains.find(queuedUrl.domain) == config_.allowedDomains.end()) {
            stats_.outOfScopeSkipped++;
            return false;
        }
    }
    
    if (!isAllowedByRobots(queuedUrl.url, queuedUrl.domain)) {
        stats_.robotsBlocked++;
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(visitedMutex_);
        visitedSet_.insert(queuedUrl.url);
        bloomFilter_->add(queuedUrl.url);
    }
    
    return true;
}

template <typename BloomType>
bool BasicWebCrawler<BloomType>::isAllowedByRobots(const std::string& url, const std::string& domain) {
    std::lock_guard<std::mutex> lock(domainsMutex_);
    
    auto it = domains_.find(domain);
    if (it == domains_.end()) {
        auto robots = fetchRobotsTxt(domain);
        domains_[domain] = std::make_unique<DomainState>();
        domains_[domain]->robotsTxt = std::make_unique<RobotsTxtEntry>(std::move(robots));
    }
    
    const auto& robots = *domains_[domain]->robotsTxt;
    
    std::string path;
    size_t pathStart = url.find('/', url.find("://") + 3);
    if (pathStart != std::string::npos) {
        path = url.substr(pathStart);
    } else {
        path = "/";
    }
    
    for (const auto& disallowed : robots.disallowed) {
        if (path.find(disallowed) == 0) {
            return false;
        }
    }
    
    return true;
}

template <typename BloomType>
RobotsTxtEntry BasicWebCrawler<BloomType>::fetchRobotsTxt(const std::string& domain) {
    RobotsTxtEntry entry;
    entry.userAgent = "*";
    
    std::string robotsUrl = "http://" + domain + "/robots.txt";
    auto response = httpGet(robotsUrl);
    
    if (!response.success) {
        return entry;
    }
    
    std::istringstream stream(response.body);
    std::string line;
    std::string currentUserAgent;
    bool appliesToUs = false;
    
    while (std::getline(stream, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty() || line[0] == '#') continue;
        
        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (key == "User-agent") {
            currentUserAgent = value;
            appliesToUs = (value == "*" || value == config_.userAgent);
        } else if (appliesToUs && key == "Disallow") {
            entry.disallowed.push_back(value);
        } else if (appliesToUs && key == "Allow") {
            entry.allowed.push_back(value);
        } else if (appliesToUs && key == "Crawl-delay") {
            entry.crawlDelay = std::stoi(value);
        }
    }
    
    return entry;
}

template <typename BloomType>
HttpResponse BasicWebCrawler<BloomType>::fetchUrl(const std::string& url) {
    auto startTime = std::chrono::steady_clock::now();
    HttpResponse response = httpGet(url);
    auto endTime = std::chrono::steady_clock::now();
    response.fetchTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    response.url = url;
    return response;
}

template <typename BloomType>
std::vector<std::string> BasicWebCrawler<BloomType>::extractLinks(const HttpResponse& response, const std::string& baseUrl) {
    std::vector<std::string> links;
    
    std::regex hrefRegex(R"(href=["']([^"']+)["'])", std::regex::icase);
    std::regex srcRegex(R"(src=["']([^"']+)["'])", std::regex::icase);
    
    auto extract = [&](const std::regex& regex) {
        auto begin = std::sregex_iterator(response.body.begin(), response.body.end(), regex);
        auto end = std::sregex_iterator();
        
        for (auto it = begin; it != end; ++it) {
            std::string link = (*it)[1].str();
            
            if (link.find("://") == std::string::npos) {
                if (!link.empty() && link[0] == '/') {
                    size_t schemeEnd = baseUrl.find("://");
                    size_t pathStart = baseUrl.find('/', schemeEnd + 3);
                    if (pathStart != std::string::npos) {
                        link = baseUrl.substr(0, pathStart) + link;
                    } else {
                        link = baseUrl + link;
                    }
                } else if (link.find("//") == 0) {
                    size_t schemeEnd = baseUrl.find("://");
                    if (schemeEnd != std::string::npos) {
                        link = baseUrl.substr(0, schemeEnd) + ":" + link;
                    }
                } else {
                    size_t lastSlash = baseUrl.rfind('/');
                    if (lastSlash > baseUrl.find("://") + 2) {
                        link = baseUrl.substr(0, lastSlash + 1) + link;
                    } else {
                        link = baseUrl + "/" + link;
                    }
                }
            }
            
            std::string lowerLink = link;
            std::transform(lowerLink.begin(), lowerLink.end(), lowerLink.begin(), ::tolower);
            
            bool blocked = false;
            for (const auto& ext : config_.blockedExtensions) {
                if (lowerLink.find(ext) != std::string::npos) {
                    blocked = true;
                    break;
                }
            }
            
            if (!blocked) {
                links.push_back(link);
            }
        }
    };
    
    extract(hrefRegex);
    extract(srcRegex);
    
    std::sort(links.begin(), links.end());
    links.erase(std::unique(links.begin(), links.end()), links.end());
    
    return links;
}

template <typename BloomType>
std::optional<std::string> BasicWebCrawler<BloomType>::normalizeAndValidate(const std::string& url) {
    auto normalized = config_.normalizeUrls ? normalizeUrl(url) : std::optional<std::string>(url);
    
    if (!normalized.has_value()) {
        return std::nullopt;
    }
    
    auto check = isBadUrl(normalized.value());
    if (check.bad) {
        return std::nullopt;
    }
    
    return normalized.value();
}

template <typename BloomType>
std::string BasicWebCrawler<BloomType>::getDomain(const std::string& url) {
    std::string domain;
    size_t schemeEnd = url.find("://");
    
    if (schemeEnd != std::string::npos) {
        size_t hostStart = schemeEnd + 3;
        size_t hostEnd = url.find('/', hostStart);
        
        if (hostEnd != std::string::npos) {
            domain = url.substr(hostStart, hostEnd - hostStart);
        } else {
            domain = url.substr(hostStart);
        }
        
        size_t colonPos = domain.rfind(':');
        if (colonPos != std::string::npos && colonPos != domain.find(']')) {
            domain = domain.substr(0, colonPos);
        }
    }
    
    return domain;
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::addToQueue(const std::string& url, const std::string& domain, size_t depth) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        if (queue_.size() >= config_.maxQueueSize) {
            return;
        }
        
        queue_.push({url, domain, depth, 0});
    }
    queueCv_.notify_one();
}

template <typename BloomType>
void BasicWebCrawler<BloomType>::respectPoliteness(const std::string& domain) {
    std::lock_guard<std::mutex> lock(domainsMutex_);
    
    auto it = domains_.find(domain);
    if (it != domains_.end()) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLast = now - it->second->lastRequestTime;
        
        int delay = config_.politenessDelayMs;
        if (it->second->robotsTxt && it->second->robotsTxt->crawlDelay > 0) {
            delay = std::max(delay, it->second->robotsTxt->crawlDelay * 1000);
        }
        
        auto delayMs = std::chrono::milliseconds(delay);
        if (timeSinceLast < delayMs) {
            std::this_thread::sleep_for(delayMs - timeSinceLast);
        }
        
        it->second->lastRequestTime = std::chrono::steady_clock::now();
    } else {
        domains_[domain] = std::make_unique<DomainState>();
        domains_[domain]->lastRequestTime = std::chrono::steady_clock::now();
    }
}

template <typename BloomType>
std::unordered_set<std::string> BasicWebCrawler<BloomType>::getVisitedUrls() const {
    std::lock_guard<std::mutex> lock(visitedMutex_);  // removed const
    return visitedSet_;
}

// Simple HTTP GET implementation
template <typename BloomType>
HttpResponse BasicWebCrawler<BloomType>::httpGet(const std::string& url) {
    HttpResponse response;
    response.url = url;
    
    std::string host;
    std::string path = "/";
    int port = 80;
    bool isHttps = false;
    
    size_t schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) {
        std::string scheme = url.substr(0, schemeEnd);
        isHttps = (scheme == "https");
        if (isHttps) port = 443;
    }
    
    size_t hostStart = (schemeEnd != std::string::npos) ? schemeEnd + 3 : 0;
    size_t pathStart = url.find('/', hostStart);
    
    if (pathStart != std::string::npos) {
        host = url.substr(hostStart, pathStart - hostStart);
        path = url.substr(pathStart);
    } else {
        host = url.substr(hostStart);
        path = "/";
    }
    
    size_t colonPos = host.rfind(':');
    if (colonPos != std::string::npos && colonPos != host.find(']')) {
        port = std::stoi(host.substr(colonPos + 1));
        host = host.substr(0, colonPos);
    }
    
    if (isHttps) {
        response.success = false;
        response.errorMessage = "HTTPS not implemented in simple version";
        return response;
    }
    
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
#endif
        response.success = false;
        response.errorMessage = "Socket creation failed";
        return response;
    }
    
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        response.success = false;
        response.errorMessage = "Host resolution failed";
        return response;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
#ifdef _WIN32
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
#else
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
#endif
        response.success = false;
        response.errorMessage = "Connection failed";
        return response;
    }
    
    std::string request = "GET " + path + " HTTP/1.1\r\n" +
                         "Host: " + host + "\r\n" +
                         "User-Agent: " + config_.userAgent + "\r\n" +
                         "Connection: close\r\n" +
                         "\r\n";
    
#ifdef _WIN32
    if (send(sock, request.c_str(), request.length(), 0) == SOCKET_ERROR) {
        closesocket(sock);
#else
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
        close(sock);
#endif
        response.success = false;
        response.errorMessage = "Send failed";
        return response;
    }
    
    char buffer[8192];
    std::string fullResponse;
    int bytesReceived;
    
    while ((bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        fullResponse += buffer;
    }
    
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    
    size_t headerEnd = fullResponse.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        std::string headers = fullResponse.substr(0, headerEnd);
        response.body = fullResponse.substr(headerEnd + 4);
        
        std::regex statusRegex(R"(HTTP/\d\.\d\s+(\d+))");
        std::smatch match;
        if (std::regex_search(headers, match, statusRegex)) {
            response.statusCode = std::stoi(match[1]);
            response.success = (response.statusCode >= 200 && response.statusCode < 400);
        }
        
        std::regex contentTypeRegex(R"(Content-Type:\s*([^\r\n]+))", std::regex::icase);
        if (std::regex_search(headers, match, contentTypeRegex)) {
            response.contentType = match[1];
        }
    } else {
        response.success = false;
        response.errorMessage = "Invalid HTTP response";
    }
    
    return response;
}

using WebCrawler = BasicWebCrawler<BloomFilter>;
using CountingWebCrawler = BasicWebCrawler<CountingBloomFilter>;
using ScalableWebCrawler = BasicWebCrawler<scalableBloomFilter>;