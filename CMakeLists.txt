cmake_minimum_required(VERSION 3.0)

project(SERVER)

include_directories(${CMAKE_SOURCE_DIR}/http)

add_executable(main_exe main.cpp http/http_conn.cpp)