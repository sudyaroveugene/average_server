cmake_minimum_required(VERSION 2.8)

project(as)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(MODULES main.cpp queryparser.cpp )
set(HEADERS )

add_executable(${PROJECT_NAME} ${MODULES} ${HEADERS})
