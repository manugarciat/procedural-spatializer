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
    add_compile_options(-Wall -Wextra -Ofast -ffast-math -ftree-vectorize)
endif()

# Definir la librería compartida
add_library(procedural_spatializer SHARED
    src/spatializer.h
    src/spatializer.cpp
    src/dsp/fast_math.h
    src/dsp/synth_fm.h
    src/dsp/synth_fm.cpp
    src/dsp/noise.h
    src/dsp/noise.cpp
    src/dsp/effects.h
    src/dsp/effects.cpp
)

target_include_directories(procedural_spatializer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# Integración de Spatial_Audio_Framework
set(SAF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/Spatial_Audio_Framework")

if(EXISTS "${SAF_DIR}/CMakeLists.txt")
    if(ANDROID)
        # Configuración específica para Android: usar OpenBLAS precompilado
        set(SAF_PERFORMANCE_LIB "SAF_USE_OPEN_BLAS_AND_LAPACKE" CACHE STRING "" FORCE)
        
        # Paths de OpenBLAS
        set(OPENBLAS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/openblas")
        set(OPENBLAS_LIBRARY "${OPENBLAS_DIR}/lib/arm64-v8a/libopenblas.so" CACHE FILEPATH "" FORCE)
        set(LAPACKE_LIBRARY "${OPENBLAS_DIR}/lib/arm64-v8a/libopenblas.so" CACHE FILEPATH "" FORCE)
        
        # Directorios de inclusión para OpenBLAS
        include_directories(SYSTEM "${OPENBLAS_DIR}/include")
        set(OPENBLAS_HEADER_PATH "${OPENBLAS_DIR}/include" CACHE PATH "" FORCE)
        set(LAPACKE_HEADER_PATH "${OPENBLAS_DIR}/include" CACHE PATH "" FORCE)
        
        message(STATUS "Configuring Android build with OpenBLAS at: ${OPENBLAS_DIR}")
    endif()

    # Habilitar ejemplos de SAF para poder usar ambi_bin, pero deshabilitar tests
    set(SAF_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SAF_BUILD_TESTS OFF CACHE BOOL "" FORCE)

    add_subdirectory(${SAF_DIR} external/saf_build)
    target_link_libraries(procedural_spatializer PRIVATE saf saf_example_ambi_bin saf_example_ambi_enc)
    
    if(ANDROID)
        # Forzar el enlace de la librería de OpenBLAS en Android
        target_link_libraries(procedural_spatializer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external/openblas/lib/arm64-v8a/libopenblas.so")
    endif()

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
