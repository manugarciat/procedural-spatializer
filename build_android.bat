@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Compilando Procedural Spatializer para Android ARM64
echo ===================================================

:: 1. Intentar detectar la ruta del SDK de Android
if "%ANDROID_HOME%" neq "" (
    set "ANDROID_SDK=%ANDROID_HOME%"
) else if "%ANDROID_SDK_ROOT%" neq "" (
    set "ANDROID_SDK=%ANDROID_SDK_ROOT%"
) else (
    :: Fallback al path tipico en Windows usando %LOCALAPPDATA%
    set "ANDROID_SDK=%LOCALAPPDATA%\Android\Sdk"
)

:: 2. Intentar buscar el CMake y Ninja del SDK
set "SDK_CMAKE="
set "SDK_NINJA="
for /d %%d in ("%ANDROID_SDK%\cmake\*") do (
    if exist "%%d\bin\cmake.exe" (
        set "SDK_CMAKE=%%d\bin\cmake.exe"
        set "SDK_NINJA=%%d\bin\ninja.exe"
    )
)

:: 3. Intentar detectar el NDK de Android
set "NDK_PATH="
if "%ANDROID_NDK_HOME%" neq "" (
    set "NDK_PATH=%ANDROID_NDK_HOME%"
) else (
    :: Buscar la ultima version instalada del NDK en el SDK
    for /d %%d in ("%ANDROID_SDK%\ndk\*") do (
        set "NDK_PATH=%%d"
    )
)

:: Validar detecciones
if not exist "!SDK_CMAKE!" (
    echo [ERROR] No se encontro CMake de Android en: %ANDROID_SDK%\cmake
    echo Por favor instala CMake desde el SDK Manager de Android Studio.
    exit /b 1
)

if not exist "!NDK_PATH!" (
    echo [ERROR] No se encontro el Android NDK en: %ANDROID_SDK%\ndk
    echo Por favor instala el NDK desde el SDK Manager de Android Studio.
    exit /b 1
)

echo [INFO] Usando Android SDK: %ANDROID_SDK%
echo [INFO] Usando CMake: !SDK_CMAKE!
echo [INFO] Usando NDK: !NDK_PATH!

:: Limpiar compilación previa de Android si existe
if exist build_android rmdir /s /q build_android

:: Configurar build con el toolchain de Android NDK usando el generador Ninja
echo [1/2] Configurando CMake para Android...
"!SDK_CMAKE!" -G "Ninja" -B build_android -S . ^
  -DCMAKE_MAKE_PROGRAM="!SDK_NINJA!" ^
  -DCMAKE_TOOLCHAIN_FILE="!NDK_PATH!\build\cmake\android.toolchain.cmake" ^
  -DANDROID_ABI=arm64-v8a ^
  -DANDROID_PLATFORM=android-26 ^
  -DSAF_PERFORMANCE_LIB=SAF_USE_OPEN_BLAS_AND_LAPACKE ^
  -DSAF_ENABLE_SIMD=OFF

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Fallo la configuracion de CMake
    exit /b %ERRORLEVEL%
)

:: Compilar la librería compartida
echo [2/2] Compilando la libreria...
"!SDK_CMAKE!" --build build_android --config Release

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Fallo la compilacion
    exit /b %ERRORLEVEL%
)

echo ===================================================
echo   ¡COMPILADO EXITOSAMENTE!
echo   Libreria generada en:
echo   build_android/libprocedural_spatializer.so
echo ===================================================
pause
