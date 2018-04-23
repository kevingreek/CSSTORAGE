cmake_minimum_required(VERSION 3.1)

project(csdb_browser)

find_package(Qt5Core)
find_package(Qt5Gui)
find_package(Qt5Widgets)

qt5_wrap_cpp(MOC_FILES
  main_window.h
  analize_database_dialog.h
  progress_dialog.h
  )

qt5_wrap_ui(UI_FILES
  main_window.ui
  analize_database_dialog.ui
  progress_dialog.ui
  )

add_executable(${PROJECT_NAME} WIN32
  main.cpp
  main_window.cpp
  main_window.h
  analize_database_dialog.cpp
  analize_database_dialog.h
  progress_dialog.cpp
  progress_dialog.h
  ${MOC_FILES}
  ${UI_FILES}
  )

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME}
  csdb
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  )
