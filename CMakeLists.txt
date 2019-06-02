cmake_minimum_required(VERSION 2.8)

project(as)

#find_library(RT_LIBRARY rt REQUIRED)
#find_package(Boost COMPONENTS chrono filesystem REQUIRED)

set(CMAKE_CXX_COMPILER g++ )
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(MODULES main.cpp queryparser.cpp )
set(HEADERS )

add_executable(${PROJECT_NAME} ${MODULES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} rt )
#target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})
