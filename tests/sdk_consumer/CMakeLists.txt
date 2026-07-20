cmake_minimum_required(VERSION 3.29)

project(LudusSdkConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Ludus CONFIG REQUIRED)

add_executable(ludus_sdk_consumer main.cpp)
target_link_libraries(ludus_sdk_consumer PRIVATE Ludus::FoundationBase)
