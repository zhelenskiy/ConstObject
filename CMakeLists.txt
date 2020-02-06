cmake_minimum_required(VERSION 3.15)
project(ConstObject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS -pthread)

add_executable(ConstObject main.cpp)