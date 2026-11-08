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
# Silence noisy legacy C++ warnings in vendored code.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(zinnia PRIVATE -Wno-deprecated-declarations)
endif()

# --- Shared recognition backend ----------------------------------------------
add_library(handwriting_backend STATIC
  src/ZinniaBackend.cpp
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
add_executable(handwriting_app
  src/main.cpp
  src/MainWindow.cpp
  src/DrawingCanvas.cpp
  src/CandidatePanel.cpp
)
target_link_libraries(handwriting_app PRIVATE
  handwriting_backend
  Qt6::Widgets
  Qt6::Concurrent
)

# Default model directory next to the source tree (override at runtime if needed).
target_compile_definitions(handwriting_smoke PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)
target_compile_definitions(handwriting_app PRIVATE
  HANDWRITING_DEFAULT_MODEL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models"
)
