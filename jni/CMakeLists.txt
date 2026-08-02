cmake_minimum_required(VERSION 3.18.1)
project(hdr_killer)

add_library(hdr_killer SHARED main.cpp)

find_library(log-lib log)

target_link_libraries(hdr_killer ${log-lib})
target_compile_options(hdr_killer PRIVATE -Wall -Wextra -O3 -fvisibility=hidden)
