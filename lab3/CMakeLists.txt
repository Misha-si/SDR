cmake_minimum_required(VERSION 3.16)

project(main C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Release)

# Добавляем стандартные пути установки SoapySDR
include_directories(/usr/local/include)
link_directories(/usr/local/lib)

add_executable(main main.c)

# Линкуем напрямую SoapySDR и математическую библиотеку m
target_link_libraries(main PRIVATE SoapySDR m)