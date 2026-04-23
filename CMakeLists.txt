cmake_minimum_required(VERSION 3.16...3.26)

set(CMAKE_PREFIX_PATH 
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-2025-08-23-universal"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/Frameworks/libobs.framework/Resources/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps"
    ${CMAKE_PREFIX_PATH}
)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Cross-platform dependency discovery
find_package(libobs REQUIRED)
find_package(libjpeg-turbo REQUIRED)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${PROJECT_NAME} MODULE)

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

target_sources(${PROJECT_NAME} PRIVATE
    src/source-to-websocket.cpp
    src/source-to-websocket.hpp
)

set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
