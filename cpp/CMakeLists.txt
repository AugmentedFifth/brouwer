cmake_minimum_required (VERSION 3.8)
project (brouwer)

# Set compiler flags
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O3 -march=native -Wall -Wextra -pedantic -pedantic-errors -Werror -Wfloat-equal -Wundef -Wshadow -Wpointer-arith -Wcast-align -Wstrict-overflow=5 -Wunreachable-code")

add_library(token src/Token.cpp)
add_library(parser src/Parser.cpp)

# Target executable
add_executable(brouwer src/brouwer.cpp)

target_link_libraries(parser token)

target_link_libraries(brouwer token)
target_link_libraries(brouwer parser)

include_directories("./src")
