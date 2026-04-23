cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

include(compilerconfig)
include(defaults)
include(helpers)

# Cross-platform dependency discovery
find_package(libobs REQUIRED)
find_package(libjpeg-turbo REQUIRED)

add_library(${PROJECT_NAME} MODULE
    src/source-to-websocket.cpp
    src/source-to-websocket.hpp
)

# Disable warnings as errors
add_compile_options(-Wno-error)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${libjpeg-turbo_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2025-08-23-universal/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OBS::libobs
    libjpeg-turbo::turbojpeg
)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Accelerate")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
)

set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
