cmake_minimum_required(VERSION 3.0)

project(SERVER)

include_directories(${CMAKE_SOURCE_DIR}/http, ${CMAKE_SOURCE_DIR}/lock,${CMAKE_SOURCE_DIR}/CGImysql,${CMAKE_SOURCE_DIR}/log)
# include_directories(${CMAKE_SOURCE_DIR}/lock)

add_executable(main_exe main.cpp http/http_conn.cpp CGImysql/sql_connection_pool.cpp utf8/utf8.cpp log/log.cpp)

target_link_libraries(main_exe pthread mysqlclient)