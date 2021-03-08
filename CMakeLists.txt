cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

# ---- Project ----

# Note: update this to your new project's name and version
project(
  Threadpool
  VERSION 0.1
  LANGUAGES CXX
)

# ---- Include guards ----

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# ---- Add dependencies ----

find_package( Threads )

include(cmake/CPM.cmake)

CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.4.1")
CPMAddPackage("gh:Naios/function2#4.1.0")




# ---- Create library ----



# Note: for header-only libraries change all PUBLIC flags to INTERFACE and create an interface
# target: add_library(Threadpool INTERFACE)
add_library(Threadpool INTERFACE ${sources})

target_compile_features(Threadpool INTERFACE cxx_std_20)

target_compile_options(Threadpool INTERFACE -Wall -Wextra -Wpedantic)

# Being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(Threadpool INTERFACE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive>")

# Link dependencies
target_link_libraries(Threadpool INTERFACE function2::function2 ${CMAKE_THREAD_LIBS_INIT})

target_include_directories(
  Threadpool INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                    $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.

# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageProject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}
  INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include
  INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
  VERSION_HEADER "${VERSION_HEADER_LOCATION}"
  COMPATIBILITY SameMajorVersion
  DEPENDENCIES ""
)


# ---- Conditionally build examples ----

option(BUILD_EXAMPLES "Build the examples" OFF)

if(BUILD_EXAMPLES)
 add_executable(example "include/main.cpp")
 target_link_libraries(example PUBLIC Threadpool::Threadpool)
endif()
