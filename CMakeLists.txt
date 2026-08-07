cmake_minimum_required(VERSION 3.16)
project(Pool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(POOL_BUILD_SHARED "Build Pool as a shared library" ON)
option(POOL_BUILD_STATIC "Build Pool as a static library" ON)

if(NOT POOL_BUILD_SHARED AND NOT POOL_BUILD_STATIC)
  message(FATAL_ERROR
    "At least one of POOL_BUILD_SHARED or POOL_BUILD_STATIC must be enabled")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(PLATFORM_ARCHITECTURE "64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(PLATFORM_ARCHITECTURE "32")
else()
  message(FATAL_ERROR
    "Unsupported pointer size: ${CMAKE_SIZEOF_VOID_P}")
endif()

#
# Equivalent to:
#
#   $(SolutionDir)bin
#   $(SolutionDir)lib
#
# When Pool is included by mqme, CMAKE_SOURCE_DIR refers to the
# mqme root. When Pool is built by itself, it refers to the Pool root.
#
set(POOL_BIN_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(POOL_LIB_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

set(POOL_SOURCES
  Source/Pool.cpp
)

function(configure_pool_target target static_library)
  target_include_directories(${target}
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/Include"
  )

  target_compile_features(${target}
    PUBLIC
      cxx_std_20
  )

  if(static_library)
    set(static_suffix "s")

    target_compile_definitions(${target}
      PUBLIC
        POOL_STATIC
    )
  else()
    set(static_suffix "")

    target_compile_definitions(${target}
      PRIVATE
        POOL_EXPORTS
    )
  endif()

  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME
      "pool${PLATFORM_ARCHITECTURE}${static_suffix}"

    DEBUG_POSTFIX
      "d"

    RUNTIME_OUTPUT_DIRECTORY
      "${POOL_BIN_DIRECTORY}"

    LIBRARY_OUTPUT_DIRECTORY
      "${POOL_BIN_DIRECTORY}"

    ARCHIVE_OUTPUT_DIRECTORY
      "${POOL_LIB_DIRECTORY}"
  )

  if(MSVC)
    target_compile_options(${target}
      PRIVATE
        /W4
        /permissive-
    )
  else()
    target_compile_options(${target}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
  endif()
endfunction()

#
# Shared library:
#
#   bin/pool64.dll
#   bin/pool64d.dll
#
#   lib/pool64.lib
#   lib/pool64d.lib
#
if(POOL_BUILD_SHARED)
  add_library(Pool SHARED
    ${POOL_SOURCES}
  )

  configure_pool_target(Pool FALSE)
endif()

#
# Static library:
#
#   lib/pool64s.lib
#   lib/pool64sd.lib
#
if(POOL_BUILD_STATIC)
  add_library(Pool_static STATIC
    ${POOL_SOURCES}
  )

  configure_pool_target(Pool_static TRUE)
endif()