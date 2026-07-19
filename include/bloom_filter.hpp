#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct BloomParams {
    std::size_t bitCount;   // m
    std::size_t hashCount;  // k
    double      fpRate;     // theoretical false-positive probability
};

// Given an expected number of insertions n and desired false-positive rate p,
// compute the optimal m and k.
inline BloomParams optimalBloomParams(std::size_t n, double p) {
    if (p <= 0.0 || p >= 1.0) throw std::invalid_argument("p must be in (0,1)");
    const double m = -static_cast<double>(n) * std::log(p) / (std::log(2.0) * std::log(2.0));
    const double k =  (m / static_cast<double>(n)) * std::log(2.0);
    const std::size_t mRounded = static_cast<std::size_t>(std::ceil(m));
    const std::size_t kRounded = std::max<std::size_t>(1, static_cast<std::size_t>(std::round(k)));
    // Theoretical FP rate: (1 - e^{-k*n/m})^k
    const double exponent = -static_cast<double>(kRounded) * static_cast<double>(n) /
                             static_cast<double>(mRounded);
    const double fpRateActual = std::pow(1.0 - std::exp(exponent), static_cast<double>(kRounded));
    return {mRounded, kRounded, fpRateActual};
}

// Theoretical FP rate for explicit m, k, n
inline double theoreticalFpRate(std::size_t m, std::size_t k, std::size_t n) {
    const double exponent = -static_cast<double>(k) * static_cast<double>(n) /
                             static_cast<double>(m);
    return std::pow(1.0 - std::exp(exponent), static_cast<double>(k));
}

// ─────────────────────────────────────────────
//  Standard Bloom Filter
// ─────────────────────────────────────────────

class BloomFilter {
public:
    BloomFilter(std::size_t bitCount, std::size_t hashCount)
        : bitCount_(bitCount),
          hashCount_(hashCount),
          bits_((bitCount + 7) / 8, 0) {}

    void add(const std::string& value) {
        for (std::size_t i = 0; i < hashCount_; ++i) {
            setBit(nthHash(value, i) % bitCount_);
        }
        ++insertions_;
    }

    bool contains(const std::string& value) const {
        for (std::size_t i = 0; i < hashCount_; ++i) {
            if (!getBit(nthHash(value, i) % bitCount_)) return false;
        }
        return true;
    }

    std::size_t memoryBytes()  const { return bits_.size(); }
    std::size_t insertions()   const { return insertions_; }
    std::size_t bitCount()     const { return bitCount_; }
    std::size_t hashCount()    const { return hashCount_; }

    double theoreticalFpRate() const {
        return ::theoreticalFpRate(bitCount_, hashCount_, insertions_);
    }

private:
    static std::uint64_t splitmix64(std::uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    std::uint64_t nthHash(const std::string& value, std::size_t n) const {
        const std::uint64_t h1 = static_cast<std::uint64_t>(std::hash<std::string>{}(value));
        const std::uint64_t h2 = splitmix64(h1 ^ 0x72657475726eULL);
        return splitmix64(h1 + n * h2 + n * n);
    }

    void setBit(std::size_t index) {
        bits_[index >> 3] |= static_cast<std::uint8_t>(1u << (index & 7));
    }

    bool getBit(std::size_t index) const {
        return (bits_[index >> 3] & static_cast<std::uint8_t>(1u << (index & 7))) != 0;
    }

    std::size_t bitCount_;
    std::size_t hashCount_;
    std::size_t insertions_ = 0;
    std::vector<std::uint8_t> bits_;
};

// ─────────────────────────────────────────────
//  Counting Bloom Filter
//  Uses 4-bit counters → supports deletion
//  Memory: ceil(m/2) bytes  (two counters per byte)
// ─────────────────────────────────────────────

class CountingBloomFilter {
public:
    CountingBloomFilter(std::size_t bitCount, std::size_t hashCount)
        : bitCount_(bitCount),
          hashCount_(hashCount),
          counters_((bitCount + 1) / 2, 0) {}

    // Returns false if any counter would overflow (saturates at 15)
    bool add(const std::string& value) {
        bool ok = true;
        for (std::size_t i = 0; i < hashCount_; ++i) {
            const std::size_t idx = nthHash(value, i) % bitCount_;
            if (getCounter(idx) < 15) incrementCounter(idx);
            else ok = false;
        }
        if (ok) ++insertions_;
        return ok;
    }

    // Returns false if the element was not present (or already deleted)
    bool remove(const std::string& value) {
        if (!contains(value)) return false;
        for (std::size_t i = 0; i < hashCount_; ++i) {
            const std::size_t idx = nthHash(value, i) % bitCount_;
            if (getCounter(idx) > 0) decrementCounter(idx);
        }
        if (insertions_ > 0) --insertions_;
        return true;
    }

    bool contains(const std::string& value) const {
        for (std::size_t i = 0; i < hashCount_; ++i) {
            if (getCounter(nthHash(value, i) % bitCount_) == 0) return false;
        }
        return true;
    }

    std::size_t memoryBytes()  const { return counters_.size(); }
    std::size_t insertions()   const { return insertions_; }
    std::size_t bitCount()     const { return bitCount_; }
    std::size_t hashCount()    const { return hashCount_; }

    double theoreticalFpRate() const {
        return ::theoreticalFpRate(bitCount_, hashCount_, insertions_);
    }

private:
    static std::uint64_t splitmix64(std::uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    std::uint64_t nthHash(const std::string& value, std::size_t n) const {
        const std::uint64_t h1 = static_cast<std::uint64_t>(std::hash<std::string>{}(value));
        const std::uint64_t h2 = splitmix64(h1 ^ 0x72657475726eULL);
        return splitmix64(h1 + n * h2 + n * n);
    }

    std::uint8_t getCounter(std::size_t idx) const {
        const std::uint8_t byte = counters_[idx >> 1];
        return (idx & 1) ? (byte >> 4) : (byte & 0x0F);
    }

    void incrementCounter(std::size_t idx) {
        std::uint8_t& byte = counters_[idx >> 1];
        if (idx & 1) byte += 0x10;
        else         byte += 0x01;
    }

    void decrementCounter(std::size_t idx) {
        std::uint8_t& byte = counters_[idx >> 1];
        if (idx & 1) byte -= 0x10;
        else         byte -= 0x01;
    }

    std::size_t bitCount_;
    std::size_t hashCount_;
    std::size_t insertions_ = 0;
    std::vector<std::uint8_t> counters_;  // packed 4-bit counters
};

// ─────────────────────────────────────────────
//  Scalable Bloom Filter
//  Grows by adding additional BloomFilter layers as insertions exceed capacity.
// ─────────────────────────────────────────────

class scalableBloomFilter {
public:
    // initialExpectedInsertions: expected insertions for the first layer
    // targetFpRate: desired overall false-positive rate across all layers
    // growthFactor (>1): capacity multiplier per layer
    // tighteningRatio (0<r<1): false-positive multiplier per layer (geometric)
    scalableBloomFilter(std::size_t initialExpectedInsertions,
                        double targetFpRate,
                        double growthFactor = 2.0,
                        double tighteningRatio = 0.9)
        : initialExpectedInsertions_(initialExpectedInsertions),
          targetFpRate_(targetFpRate),
          growthFactor_(growthFactor),
          tighteningRatio_(tighteningRatio) {
        if (initialExpectedInsertions_ == 0) {
            throw std::invalid_argument("initialExpectedInsertions must be > 0");
        }
        if (targetFpRate_ <= 0.0 || targetFpRate_ >= 1.0) {
            throw std::invalid_argument("targetFpRate must be in (0,1)");
        }
        if (!(growthFactor_ > 1.0)) {
            throw std::invalid_argument("growthFactor must be > 1");
        }
        if (tighteningRatio_ <= 0.0 || tighteningRatio_ >= 1.0) {
            throw std::invalid_argument("tighteningRatio must be in (0,1)");
        }

        ensureLayerExists(0);
    }

    void add(const std::string& value) {
        ensureCapacityForNextInsert();
        layers_.back().filter.add(value);
        ++insertions_;
    }

    bool contains(const std::string& value) const {
        for (const auto& layer : layers_) {
            if (layer.filter.contains(value)) return true;
        }
        return false;
    }

    std::size_t memoryBytes() const {
        std::size_t total = 0;
        for (const auto& layer : layers_) total += layer.filter.memoryBytes();
        return total;
    }

    std::size_t insertions() const { return insertions_; }
    std::size_t layerCount() const { return layers_.size(); }

    // Approximate overall FP rate assuming independence between layers:
    // P(fp) = 1 - Π(1 - p_i)
    double theoreticalFpRate() const {
        double product = 1.0;
        for (const auto& layer : layers_) {
            const double p = layer.filter.theoreticalFpRate();
            product *= (1.0 - std::clamp(p, 0.0, 1.0));
        }
        return 1.0 - product;
    }

private:
    struct Layer {
        BloomFilter  filter;
        std::size_t  expectedInsertions;
        double       targetLayerFpRate;
        std::size_t  index;
    };

    std::size_t expectedInsertionsForLayer(std::size_t layerIndex) const {
        const double scaled = static_cast<double>(initialExpectedInsertions_) *
                              std::pow(growthFactor_, static_cast<double>(layerIndex));
        const auto expected = static_cast<std::size_t>(std::ceil(scaled));
        return std::max<std::size_t>(1, expected);
    }

    // Choose per-layer target FP rates so that Σ p_i <= targetFpRate.
    // With p_i = p0 * r^i, Σ p_i = p0/(1-r). Choose p0 = targetFpRate*(1-r).
    double targetFpForLayer(std::size_t layerIndex) const {
        const double p0 = targetFpRate_ * (1.0 - tighteningRatio_);
        return p0 * std::pow(tighteningRatio_, static_cast<double>(layerIndex));
    }

    void ensureLayerExists(std::size_t layerIndex) {
        while (layers_.size() <= layerIndex) {
            const std::size_t idx = layers_.size();
            const std::size_t expected = expectedInsertionsForLayer(idx);
            const double pLayer = targetFpForLayer(idx);
            const auto params = optimalBloomParams(expected, pLayer);
            layers_.push_back(Layer{BloomFilter(params.bitCount, params.hashCount), expected, pLayer, idx});
        }
    }

    void ensureCapacityForNextInsert() {
        if (layers_.empty()) ensureLayerExists(0);
        auto& current = layers_.back();
        if (current.filter.insertions() < current.expectedInsertions) return;
        ensureLayerExists(current.index + 1);
    }

    std::size_t initialExpectedInsertions_;
    double targetFpRate_;
    double growthFactor_;
    double tighteningRatio_;

    std::size_t insertions_ = 0;
    std::vector<Layer> layers_;
};
