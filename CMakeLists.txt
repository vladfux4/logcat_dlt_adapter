cmake_minimum_required(VERSION 3.14)
project(logcat_dlt_adapter)

find_package(PkgConfig)
pkg_check_modules(DLT REQUIRED automotive-dlt)

include_directories(${DLT_INCLUDE_DIRS})

add_executable(logcat_dlt_adapter main.cpp)
set_property(TARGET logcat_dlt_adapter PROPERTY CXX_STANDARD 17)
target_link_libraries(logcat_dlt_adapter ${DLT_LIBRARIES})

