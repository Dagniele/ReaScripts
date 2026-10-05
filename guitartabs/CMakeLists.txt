cmake_minimum_required(VERSION 3.20)
project(reaper_guitartabs LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

set(GUITARTABS_UI_HTML "${CMAKE_SOURCE_DIR}/ui/dist/index.html")
set(GUITARTABS_UI_EMBED "${CMAKE_BINARY_DIR}/ui_embed.cpp")
add_custom_command(
  OUTPUT "${GUITARTABS_UI_EMBED}"
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/embed_ui.py" "${GUITARTABS_UI_HTML}" "${GUITARTABS_UI_EMBED}"
  DEPENDS "${GUITARTABS_UI_HTML}" "${CMAKE_SOURCE_DIR}/tools/embed_ui.py"
  VERBATIM
)

set(GUITARTABS_MODEL_DIR "${CMAKE_SOURCE_DIR}/third_party/basic-pitch/nmp.mlpackage")
set(GUITARTABS_MODEL_EMBED "${CMAKE_BINARY_DIR}/model_embed.cpp")
add_custom_command(
  OUTPUT "${GUITARTABS_MODEL_EMBED}"
  COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/embed_model.py" "${GUITARTABS_MODEL_DIR}" "${GUITARTABS_MODEL_EMBED}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/embed_model.py"
    "${GUITARTABS_MODEL_DIR}/Manifest.json"
    "${GUITARTABS_MODEL_DIR}/Data/com.apple.CoreML/model.mlmodel"
    "${GUITARTABS_MODEL_DIR}/Data/com.apple.CoreML/weights/weight.bin"
  VERBATIM
)

add_library(reaper_guitartabs MODULE
  src/json.cpp
  src/document.cpp
  src/transcribe.cpp
  src/basic_pitch.cpp
  src/basic_pitch_mac.mm
  src/session.cpp
  src/extension.cpp
  src/webview_mac.mm
  "${GUITARTABS_UI_EMBED}"
  "${GUITARTABS_MODEL_EMBED}"
)

target_include_directories(reaper_guitartabs PRIVATE src sdk)
target_compile_options(reaper_guitartabs PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(src/webview_mac.mm src/basic_pitch_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(reaper_guitartabs PRIVATE "-framework Cocoa" "-framework WebKit" "-framework CoreML")
set_target_properties(reaper_guitartabs PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_guitartabs"
  SUFFIX ".dylib"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

add_executable(guitartabs_tests
  tests/test_main.cpp
  src/json.cpp
  src/document.cpp
  src/transcribe.cpp
  src/basic_pitch.cpp
  src/basic_pitch_mac.mm
  "${GUITARTABS_MODEL_EMBED}"
)
target_include_directories(guitartabs_tests PRIVATE src)
target_compile_features(guitartabs_tests PRIVATE cxx_std_20)
target_compile_options(guitartabs_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(src/basic_pitch_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_link_libraries(guitartabs_tests PRIVATE "-framework Foundation" "-framework CoreML")
