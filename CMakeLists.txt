cmake_minimum_required(VERSION 3.16)
project(handwriting LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

# --- Zinnia (static, vendored) -------------------------------------------------
set(ZINNIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zinnia-src")

add_library(zinnia STATIC
  ${ZINNIA_DIR}/character.cpp
  ${ZINNIA_DIR}/feature.cpp
  ${ZINNIA_DIR}/libzinnia.cpp
  ${ZINNIA_DIR}/param.cpp
  ${ZINNIA_DIR}/recognizer.cpp
  ${ZINNIA_DIR}/sexp.cpp
  ${ZINNIA_DIR}/svm.cpp
  ${ZINNIA_DIR}/trainer.cpp
)
set_target_properties(zinnia PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(zinnia PUBLIC ${ZINNIA_DIR})
target_compile_definitions(zinnia PUBLIC HAVE_CONFIG_H)
if(WIN32)
  # Static build: no dllimport/dllexport; mmap.h needs <windows.h> for HANDLE.
  target_compile_definitions(zinnia PUBLIC ZINNIA_STATIC_LIBRARY HAVE_WINDOWS_H NOMINMAX)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(zinnia PRIVATE -Wno-deprecated-declarations)
endif()

# --- Shared recognition backend ----------------------------------------------
add_library(handwriting_backend STATIC
  src/ZinniaBackend.cpp
  src/SampleIO.cpp
  src/Recognizer.cpp
  src/StrokeCount.cpp
)
set_target_properties(handwriting_backend PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(handwriting_backend PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(handwriting_backend PUBLIC zinnia Qt6::Core)

# --- Headless smoke test -----------------------------------------------------
add_executable(handwriting_smoke
  src/smoke_main.cpp
)
target_link_libraries(handwriting_smoke PRIVATE handwriting_backend)

# --- GUI application ---------------------------------------------------------
set(HANDWRITING_APP_SOURCES
  src/main.cpp
  src/MainWindow.cpp
  src/DrawingCanvas.cpp
  src/CandidatePanel.cpp
  src/TrackpadSource.cpp
  src/TrackpadSourceFactory.cpp
  src/PtpFrameAssembler.cpp
  src/QtTouchCapture.cpp
  src/CursorFreeze.cpp
)

if(APPLE)
  enable_language(OBJCXX)
  list(APPEND HANDWRITING_APP_SOURCES
    src/macos/NSTouchCapture.mm
    src/macos/CursorFreeze.mm
  )
elseif(WIN32)
  list(APPEND HANDWRITING_APP_SOURCES
    src/windows/RawInputTrackpadSource.cpp
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND HANDWRITING_APP_SOURCES
    src/linux/EvdevTrackpadSource.cpp
    src/MtSlotTracker.cpp
  )
endif()

add_executable(handwriting_app ${HANDWRITING_APP_SOURCES})
target_include_directories(handwriting_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handwriting_app PRIVATE
  handwriting_backend
  Qt6::Widgets
  Qt6::Concurrent
)
if(APPLE)
  target_link_libraries(handwriting_app PRIVATE
    "-framework AppKit"
    "-framework CoreFoundation"
  )
  set_source_files_properties(src/macos/NSTouchCapture.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
  )
elseif(WIN32)
  target_link_libraries(handwriting_app PRIVATE hid setupapi)
endif()

target_compile_definitions(handwriting_smoke PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)
target_compile_definitions(handwriting_app PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
  HANDWRITING_DEFAULT_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
)

# --- Unit tests (pure mapping / jump logic) ----------------------------------
add_executable(stroke_mapping_test
  tests/stroke_mapping_test.cpp
)
target_include_directories(stroke_mapping_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stroke_mapping_test PRIVATE Qt6::Core Qt6::Gui handwriting_backend)
enable_testing()
add_test(NAME stroke_mapping_test COMMAND stroke_mapping_test)

add_executable(trackpad_source_test
  tests/trackpad_source_test.cpp
  src/TrackpadSource.cpp
)
target_include_directories(trackpad_source_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(trackpad_source_test PRIVATE Qt6::Core)
add_test(NAME trackpad_source_test COMMAND trackpad_source_test)

add_executable(ptp_frame_test
  tests/ptp_frame_test.cpp
  src/PtpFrameAssembler.cpp
)
target_include_directories(ptp_frame_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ptp_frame_test PRIVATE Qt6::Core)
add_test(NAME ptp_frame_test COMMAND ptp_frame_test)

add_executable(stroke_preprocess_test
  tests/stroke_preprocess_test.cpp
)
target_include_directories(stroke_preprocess_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stroke_preprocess_test PRIVATE Qt6::Core handwriting_backend)
add_test(NAME stroke_preprocess_test COMMAND stroke_preprocess_test)

add_executable(sample_io_test
  tests/sample_io_test.cpp
)
target_include_directories(sample_io_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sample_io_test PRIVATE Qt6::Core handwriting_backend)
add_test(NAME sample_io_test COMMAND sample_io_test)

# --- Offline evaluation tool -------------------------------------------------
add_executable(handwriting_eval
  src/eval_main.cpp
)
target_include_directories(handwriting_eval PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handwriting_eval PRIVATE handwriting_backend)
target_compile_definitions(handwriting_eval PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
  HANDWRITING_DEFAULT_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
)

add_executable(mt_slot_tracker_test
  tests/mt_slot_tracker_test.cpp
  src/MtSlotTracker.cpp
)
target_include_directories(mt_slot_tracker_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mt_slot_tracker_test PRIVATE Qt6::Core)
add_test(NAME mt_slot_tracker_test COMMAND mt_slot_tracker_test)

add_executable(recognizer_test tests/recognizer_test.cpp)
target_link_libraries(recognizer_test PRIVATE handwriting_backend)
add_test(NAME recognizer_test COMMAND recognizer_test)

add_executable(handwriting_recognize src/recognize_main.cpp)
target_link_libraries(handwriting_recognize PRIVATE handwriting_backend)
target_compile_definitions(handwriting_recognize PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")

add_executable(kanjivg_test tests/kanjivg_test.cpp)
target_include_directories(kanjivg_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kanjivg_test PRIVATE handwriting_backend)
add_test(NAME kanjivg_test COMMAND kanjivg_test)

add_executable(fusion_test tests/fusion_test.cpp)
target_link_libraries(fusion_test PRIVATE handwriting_backend)
add_test(NAME fusion_test COMMAND fusion_test)

# --- Linux evdev ↔ uinput integration (issue #4) -----------------------------
# Needs write access to /dev/uinput. Skips (ctest code 77) when unavailable so
# unprivileged CI stays green; run locally/VM after: sudo chmod a+rw /dev/uinput
# (or a udev rule). See README "Evaluation and tests".
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(evdev_uinput_test
    tests/linux/evdev_uinput_test.cpp
    src/TrackpadSource.cpp
    src/MtSlotTracker.cpp
    src/linux/EvdevTrackpadSource.cpp
  )
  target_include_directories(evdev_uinput_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/linux
  )
  target_link_libraries(evdev_uinput_test PRIVATE Qt6::Core)
  add_test(NAME evdev_uinput_test COMMAND evdev_uinput_test)
  set_tests_properties(evdev_uinput_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(stroke_variants_test tests/stroke_variants_test.cpp)
target_include_directories(stroke_variants_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(stroke_variants_test PRIVATE handwriting_backend)
add_test(NAME stroke_variants_test COMMAND stroke_variants_test)

add_executable(dump_charset src/dump_charset_main.cpp)
target_link_libraries(dump_charset PRIVATE zinnia)

add_executable(stroke_count_test tests/stroke_count_test.cpp)
target_link_libraries(stroke_count_test PRIVATE handwriting_backend)
add_test(NAME stroke_count_test COMMAND stroke_count_test)
