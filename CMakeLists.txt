# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 分别设置版本号
set(VERSION_MAJOR "V1")
set(VERSION_MINOR "2")
set(VERSION_PATCH "2")

# 组合版本号
set(PROJECT_VER "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 如果定义了 VERSION_2_X，则添加全局编译定义
if(VERSION_2_X)
    add_compile_definitions(VERSION_2_X)
    message(STATUS "Building with VERSION_2_X defined (New voice enumeration - WELCOME first)")
else()
    message(STATUS "Building without VERSION_2_X (Old voice enumeration - MUSIC first)")
endif()

project(HTB-002-${PROJECT_VER})