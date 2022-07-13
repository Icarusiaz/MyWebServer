cmake_minimum_required(VERSION 3.0)

project(SERVER)

include_directories(${CMAKE_SOURCE_DIR}/http, ${CMAKE_SOURCE_DIR}/lock)
# include_directories(${CMAKE_SOURCE_DIR}/lock)

add_executable(main_exe main.cpp http/http_conn.cpp)

target_link_libraries(main_exe pthread)