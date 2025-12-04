cmake_minimum_required(VERSION 3.16)
project(EpiGrimp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

include_directories(${CMAKE_SOURCE_DIR}/include)

qt_standard_project_setup()
qt_add_executable(EpiGrimp
    src/main.cpp
    src/mainwindow.cpp
    include/mainwindow.h
)

target_link_libraries(EpiGrimp PRIVATE Qt6::Widgets)
