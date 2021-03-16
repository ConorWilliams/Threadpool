cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Project ----

project(RiftenThiefpoolBench LANGUAGES CXX)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Add dependencies ----

find_package(Threads)

include(../cmake/CPM.cmake)

CPMAddPackage(NAME RiftenThiefpool SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

# ---- Benchmarking ----

add_executable(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/bench.cpp")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC RiftenThiefpool::RiftenThiefpool ${CMAKE_THREAD_LIBS_INIT})
