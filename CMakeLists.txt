cmake_minimum_required(VERSION 3.15)
project(procedural_spatializer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Opciones de compilación para optimizar procesamiento de señales
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O3)
endif()

# Definir la librería compartida
add_library(procedural_spatializer SHARED
    src/spatializer.h
    src/spatializer.cpp
)

target_include_directories(procedural_spatializer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Integración de Spatial_Audio_Framework
set(SAF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Spatial_Audio_Framework")

if(EXISTS "${SAF_DIR}/CMakeLists.txt" AND DEFINED SAF_PERFORMANCE_LIB AND NOT SAF_PERFORMANCE_LIB STREQUAL "none")
    # Configuraciones requeridas para SAF
    add_subdirectory(${SAF_DIR} external/saf_build)
    target_link_libraries(procedural_spatializer PRIVATE saf)
    target_compile_definitions(procedural_spatializer PRIVATE USE_SAF)
    message(STATUS "Spatial_Audio_Framework integration enabled.")
else()
    message(STATUS "Spatial_Audio_Framework integration disabled (Dummy mode enabled).")
endif()

# Ejecutable de pruebas en PC
add_executable(spatializer_test
    src/test_main.cpp
)
target_link_libraries(spatializer_test PRIVATE procedural_spatializer)
