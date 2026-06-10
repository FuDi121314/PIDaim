cmake_minimum_required(VERSION 3.10)
project(AutoAim)

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(AutoAim main.cpp)
target_link_libraries(AutoAim ${OpenCV_LIBS})