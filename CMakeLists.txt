cmake_minimum_required(VERSION 3.14)
project(url_crawler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_CRAWLER "Build web crawler" ON)
option(BUILD_BENCHMARK "Build benchmark tests" ON)

# Threading
find_package(Threads REQUIRED)

# Main executable (original benchmark)
add_executable(url_crawler_benchmark 
    src/main.cpp
)

target_include_directories(url_crawler_benchmark PRIVATE include)
target_compile_options(url_crawler_benchmark PRIVATE 
    -O2 -Wall -Wextra -Wpedantic
)
target_link_libraries(url_crawler_benchmark PRIVATE Threads::Threads)

# Web crawler executable
if(BUILD_CRAWLER)
    add_executable(url_crawler 
        src/crawler_demo.cpp
    )
    
    target_include_directories(url_crawler PRIVATE include)
    target_compile_options(url_crawler PRIVATE 
        -O2 -Wall -Wextra -Wpedantic
    )
    target_link_libraries(url_crawler PRIVATE Threads::Threads)
    
    # For Windows Winsock2
    if(WIN32)
        target_link_libraries(url_crawler PRIVATE ws2_32)
    endif()
endif()

# Unit tests - build each test separately
if(BUILD_BENCHMARK)
    enable_testing()
    
    # Test 1: Bloom Filter
    add_executable(test_bloom 
        tests/test_bloom.cpp
    )
    target_include_directories(test_bloom PRIVATE include)
    target_link_libraries(test_bloom PRIVATE Threads::Threads)
    
    # Test 2: URL Utils
    add_executable(test_url_utils 
        tests/test_url_utils.cpp
    )
    target_include_directories(test_url_utils PRIVATE include)
    target_link_libraries(test_url_utils PRIVATE Threads::Threads)
    
    # Test 3: Crawler
    add_executable(test_crawler 
        tests/test_crawler.cpp
    )
    target_include_directories(test_crawler PRIVATE include)
    target_link_libraries(test_crawler PRIVATE Threads::Threads)
    
    if(WIN32)
        target_link_libraries(test_bloom PRIVATE ws2_32)
        target_link_libraries(test_url_utils PRIVATE ws2_32)
        target_link_libraries(test_crawler PRIVATE ws2_32)
    endif()
    
    # Register tests with CTest
    add_test(NAME BloomFilter COMMAND test_bloom)
    add_test(NAME URLUtils COMMAND test_url_utils)
    add_test(NAME Crawler COMMAND test_crawler)
endif()

# Installation rules
install(TARGETS url_crawler_benchmark url_crawler
    RUNTIME DESTINATION bin
)

# Print configuration
message(STATUS "Build configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build crawler: ${BUILD_CRAWLER}")
message(STATUS "  Build tests: ${BUILD_BENCHMARK}")