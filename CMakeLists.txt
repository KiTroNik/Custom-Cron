cmake_minimum_required(VERSION 3.22)
project(custom_cron)

set(CMAKE_CXX_STANDARD 23)
SET(CMAKE_CXX_FLAGS -pthread)

add_executable(custom_cron library.c library.h main.cpp cron.h)
target_link_libraries(custom_cron rt)
