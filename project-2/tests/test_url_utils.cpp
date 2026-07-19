#include "url_utils.hpp"
#include <cassert>
#include <iostream>

void testBadUrlDetection() {
    std::cout << "Testing bad URL detection...\n";
    
    assert(isBadUrl("").bad);
    assert(isBadUrl(std::string(3000, 'a')).bad);
    assert(isBadUrl("http://").bad);
    assert(isBadUrl("ftp://example.com").bad);
    assert(!isBadUrl("http://example.com").bad);
    assert(!isBadUrl("https://example.com").bad);
    
    std::cout << "✓ Bad URL detection tests passed\n";
}

void testUrlNormalization() {
    std::cout << "Testing URL normalization...\n";
    
    auto norm = normalizeUrl("HTTPS://Example.COM/Path/");
    assert(norm.has_value());
    assert(norm.value() == "https://example.com/Path");
    
    norm = normalizeUrl("https://example.com:443/path");
    assert(norm.has_value());
    assert(norm.value() == "https://example.com/path");
    
    norm = normalizeUrl("http://example.com:80/");
    assert(norm.has_value());
    assert(norm.value() == "http://example.com");
    
    norm = normalizeUrl("https://example.com/path#fragment");
    assert(norm.has_value());
    assert(norm.value().find('#') == std::string::npos);
    
    norm = normalizeUrl("https://example.com/path/%2Fencoded");
    assert(norm.has_value());
    
    std::cout << "✓ URL normalization tests passed\n";
}

int main() {
    testBadUrlDetection();
    testUrlNormalization();
    return 0;
}