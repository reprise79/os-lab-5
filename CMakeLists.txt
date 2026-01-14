cmake_minimum_required(VERSION 3.10.0)
project(Lab5_part1)

add_executable(simulator simulator.c)
add_executable(sender udp_sender.c)

if(WIN32)
    target_link_libraries(sender ws2_32)
endif()