cmake_minimum_required(VERSION 3.20)
project(reaper_guitartabs LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

add_library(reaper_guitartabs MODULE
  src/json.cpp
  src/document.cpp
  src/transcribe.cpp
  src/session.cpp
  src/extension.cpp
  src/webview_mac.mm
)

target_include_directories(reaper_guitartabs PRIVATE src sdk)
target_compile_definitions(reaper_guitartabs PRIVATE
  "DAGNIELE_GUITARTABS_UI=\"${CMAKE_SOURCE_DIR}/ui/dist/index.html\""
)
target_compile_options(reaper_guitartabs PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(src/webview_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(reaper_guitartabs PRIVATE "-framework Cocoa" "-framework WebKit")
set_target_properties(reaper_guitartabs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_guitartabs"
  SUFFIX ".dylib"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

add_custom_command(TARGET reaper_guitartabs POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/ui/dist"
          "$<TARGET_FILE_DIR:reaper_guitartabs>/reaper_guitartabs_ui"
)

add_executable(guitartabs_tests
  tests/test_main.cpp
  src/json.cpp
  src/document.cpp
  src/transcribe.cpp
)
target_include_directories(guitartabs_tests PRIVATE src)
target_compile_features(guitartabs_tests PRIVATE cxx_std_20)
target_compile_options(guitartabs_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
