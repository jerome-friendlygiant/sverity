cmake_minimum_required(VERSION 3.28)
project(sverity VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "SVerity: native SystemVerilog lint / CDC / RDC analyzer built on slang")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SVERITY_BUILD_TESTS "Build the SVerity test suite" ON)
option(SVERITY_WERROR "Treat SVerity warnings as errors" OFF)

# ---- slang: the single mandatory HDL frontend (pinned git submodule) -------
# Offline builds: pre-populate the FetchContent caches slang uses (see README).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/slang/CMakeLists.txt")
  message(FATAL_ERROR "slang submodule missing. Run: git submodule update --init --depth 1 third_party/slang")
endif()
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/slang EXCLUDE_FROM_ALL)

# ---- embedded rule catalog ----------------------------------------------------
# docs/rules/catalog.json is compiled into the binary (as a byte array) so the executable is self-contained and
# its rule IDs can never drift from the file the tests check. Re-runs CMake automatically when the JSON changes.
set(CATALOG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/docs/rules/catalog.json")
set(CATALOG_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/catalog_embedded.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CATALOG_JSON}")
file(READ "${CATALOG_JSON}" CATALOG_HEX HEX)
string(LENGTH "${CATALOG_HEX}" CATALOG_HEX_LEN)
math(EXPR CATALOG_SIZE "${CATALOG_HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," CATALOG_BYTES "${CATALOG_HEX}")
file(WRITE "${CATALOG_CPP}.tmp"
"// Generated from docs/rules/catalog.json - do not edit.
#include <cstddef>
namespace sverity::embedded {
extern const unsigned char catalog_data[];
extern const std::size_t catalog_size;
const unsigned char catalog_data[] = {${CATALOG_BYTES}0x00};
const std::size_t catalog_size = ${CATALOG_SIZE};
}
")
file(COPY_FILE "${CATALOG_CPP}.tmp" "${CATALOG_CPP}" ONLY_IF_DIFFERENT)

# ---- library ----------------------------------------------------------------
file(GLOB_RECURSE SVERITY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(sverity_lib STATIC ${SVERITY_SOURCES} "${CATALOG_CPP}")
target_include_directories(sverity_lib PUBLIC include PRIVATE src)
target_link_libraries(sverity_lib PUBLIC slang::slang)
target_compile_features(sverity_lib PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sverity_lib PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  if(SVERITY_WERROR)
    target_compile_options(sverity_lib PRIVATE -Werror)
  endif()
endif()
set_target_properties(sverity_lib PROPERTIES OUTPUT_NAME sverity)
target_compile_definitions(sverity_lib PUBLIC SVERITY_VERSION="${PROJECT_VERSION}")

# ---- CLI --------------------------------------------------------------------
add_executable(sverity tools/sverity/main.cpp)
target_link_libraries(sverity PRIVATE sverity_lib)

# ---- tests ------------------------------------------------------------------
if(SVERITY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
