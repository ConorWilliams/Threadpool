cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Project ----

project(RiftenThiefpoolTest)

# ---- Options ----

option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

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

CPMAddPackage("gh:onqtam/doctest#2.4.5")

if(TEST_INSTALLED_VERSION)
  find_package(RiftenThiefpool REQUIRED)
else()
  CPMAddPackage(NAME RiftenThiefpool SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(RiftenThiefpool INTERFACE -Wall -Wextra -Wpedantic)
  elseif(MSVC)
    target_compile_options(RiftenThiefpool INTERFACE /W4)
  endif()
endif()

# ---- Tests ----

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(${PROJECT_NAME} "${sources}")

# Link dependencies
target_link_libraries(
  ${PROJECT_NAME} PUBLIC doctest::doctest RiftenThiefpool::RiftenThiefpool ${CMAKE_THREAD_LIBS_INIT}
)

target_compile_definitions(
  ${PROJECT_NAME} PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:DOCTEST_CONFIG_USE_STD_HEADERS>
)

enable_testing()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

doctest_discover_tests(${PROJECT_NAME})
