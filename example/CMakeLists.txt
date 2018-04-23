cmake_minimum_required(VERSION 3.1)

project(csdb_example)

add_executable(${PROJECT_NAME}
  main.cpp
  )
target_link_libraries(${PROJECT_NAME} csdb)

if(WIN32)
  target_link_libraries(${PROJECT_NAME} Rpcrt4)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} uuid)
endif()
