cmake_minimum_required(VERSION 3.16)

project(main CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Пути к заголовкам и библиотекам SoapySDR
include_directories(/usr/local/include)
link_directories(/usr/local/lib)

add_executable(main main.cpp)

# Линковка с библиотекой SoapySDR (в C++ math library линкуется автоматически)
target_link_libraries(main PRIVATE SoapySDR)