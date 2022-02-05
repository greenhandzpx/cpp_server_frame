cmake_minimum_required(VERSION 3.1)
project(Sylar)

# 引用自己编写的cmake文件
include(cmake/utils.cmake)

set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -g -std=c++17 -Wall -Wno-deprecated -Wno-unused-function")

include_directories(.)
# 添加yaml-cpp头文件
include_directories(/home/greenhandzpx/Downloads/yaml-cpp/include)
# 添加yaml-cpp库文件(.so)
link_libraries(/home/greenhandzpx/Downloads/yaml-cpp/build/libyaml-cpp.so)
#target_link_libraries(Sylar, -lyaml-cpp)



set(LIB_SRC
        sylar/log.cc
        sylar/util.cc
        sylar/config.cc
        sylar/thread.cc
        sylar/fiber.cc
        sylar/scheduler.cc
        sylar/iomanager.cc)

add_library(sylar SHARED ${LIB_SRC})
# 调用util.cmake里的函数强制修改__FILE__
force_redefine_file_macro_for_sources(sylar)

set(LIB_LIB
        sylar
        pthread)

add_executable(test tests/test.cc)
add_dependencies(test sylar)
target_link_libraries(test ${LIB_LIB})
force_redefine_file_macro_for_sources(test)

add_executable(test_config tests/test_config.cc)
add_dependencies(test_config sylar)
target_link_libraries(test_config ${LIB_LIB})
force_redefine_file_macro_for_sources(test_config)

add_executable(test_thread tests/test_thread.cc)
add_dependencies(test_thread sylar)
target_link_libraries(test_thread ${LIB_LIB})
force_redefine_file_macro_for_sources(test_thread)

add_executable(test_util tests/test_util.cc)
add_dependencies(test_util sylar)
target_link_libraries(test_util ${LIB_LIB})
force_redefine_file_macro_for_sources(test_util)

add_executable(test_fiber tests/test_fiber.cc)
add_dependencies(test_fiber sylar)
target_link_libraries(test_fiber ${LIB_LIB})
force_redefine_file_macro_for_sources(test_fiber)

add_executable(test_scheduler tests/test_scheduler.cc)
add_dependencies(test_scheduler sylar)
target_link_libraries(test_scheduler ${LIB_LIB})
force_redefine_file_macro_for_sources(test_scheduler)

add_executable(test_iomanager tests/test_iomanager.cc)
add_dependencies(test_iomanager sylar)
target_link_libraries(test_iomanager ${LIB_LIB})
force_redefine_file_macro_for_sources(test_iomanager)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)