cmake_minimum_required(VERSION 3.3)

project(csdb)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    add_definitions(-DNOMINMAX)
endif()

option(CSDB_BUILD_EXAMPLES "Build csdb example(s)" OFF)
option(CSDB_BUILD_BROWSER "Build csdb browser" OFF)

option(LEVELDB_BUILD_TESTS "" OFF)
option(LEVELDB_AUTORUN_TESTS "" OFF)
option(SNAPPY_BUILD_TESTS "" OFF)

add_subdirectory(snappy)
add_subdirectory(leveldb)

add_library(${PROJECT_NAME} STATIC
  src/csdb.cpp
  include/csdb/csdb.h
  include/csdb/csdb_internal.h
)
if (WIN32)
  target_link_libraries(${PROJECT_NAME} Rpcrt4)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND lib /OUT:${CMAKE_CURRENT_SOURCE_DIR}/lib/csdbmd64$<$<CONFIG:Debug>:d>.lib
      $<TARGET_FILE:${PROJECT_NAME}>
      $<TARGET_FILE:memenv>
      $<TARGET_FILE:leveldb>
      $<TARGET_FILE:snappy>
    )
endif()
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} leveldb)
if (UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} uuid)
endif()

if(CSDB_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(CSDB_BUILD_BROWSER)
  add_subdirectory(browser)
  if (WIN32)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
    set_target_properties(csdb_browser PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin
      )
  endif()
endif()

