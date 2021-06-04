cmake_minimum_required(VERSION 3.0.0)
project(noslam VERSION 0.1.0)

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

add_library(${PROJECT_NAME} SHARED
src/Transceiver.cc
)

target_link_libraries(${PROJECT_NAME}
${OpenCV_LIBS}
Threads::Threads
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/exe)

add_executable(OmniCar
src/main.cpp)
target_link_libraries(OmniCar ${PROJECT_NAME})