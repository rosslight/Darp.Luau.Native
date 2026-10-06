cmake_minimum_required(VERSION 3.21)
project(repro C)
add_executable(repro repro.c)
target_link_libraries(repro PRIVATE dbghelp)
