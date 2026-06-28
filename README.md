# Procedural Spatializer

Un motor de audio espacial liviano y multiplataforma escrito en C++ diseñado para espacializar audio Ambisonics de cualquier orden en tiempo real para **Meta Quest (Horizon OS)**, **Unity**, **Unreal Engine**, y aplicaciones Android.

Utiliza [Spatial_Audio_Framework (SAF)](https://github.com/leomccormack/Spatial_Audio_Framework) como núcleo para el cálculo de armónicos esféricos y binauralización mediante filtros HRTF optimizados.

---

## Características

- **API C-Compatible limpia:** Fácil de llamar desde C# (Unity), C++ (Unreal) y JNI/Kotlin (Android).
- **Procesamiento Asíncrono de bloques de cualquier tamaño:** Diseñado con un sistema de troceado interno (*sub-blocking*) de 128 muestras para integrarse sin cortes de audio con hilos rápidos de motores de juego o `AudioTrack` de Android.
- **Soporte multi-orden:** Decodificación binaural en base al dataset Genelec KEMAR por defecto para 1er orden (4 canales), 2do orden (9 canales) y 3er orden (16 canales).
- **Compatibilidad Extrema en Android ARM64:** Enlazado dinámicamente con OpenBLAS optimizado para evitar deadlocks de hilos nativos en procesadores Snapdragon (big.LITTLE).

---

## Estructura del Proyecto

```text
procedural-spatializer/
├── CMakeLists.txt              # Configuración de CMake para PC y Android NDK
├── README.md                   # Esta documentación
├── .gitignore                  # Exclusión de archivos temporales y dependencias locales
├── build_android.bat           # Script por lotes para compilar cruzado para Android en Windows
├── src/
│   ├── spatializer.h           # Interfaz pública en C
│   ├── spatializer.cpp         # Implementación del DSP, JNI y de-entrelazador
│   └── test_main.cpp           # Ejecutable de prueba offline para PC
└── external/
    ├── Spatial_Audio_Framework/ # Submódulo Git de SAF (Núcleo de audio)
    ├── openblas/                # Cabeceras y binarios precompilados de OpenBLAS
    └── eigen/                   # Cabeceras header-only de Eigen
```

---

## API Pública (C / JNI)

La interfaz C expone métodos planos para el DSP:

1. **`create_spatializer(int order, int sample_rate, int block_size)`**: Inicializa el decodificador binaural.
2. **`set_listener_orientation(void* instance, float x, float y, float z, float w)`**: Pasa la rotación de cabeza de la cámara (en Quaternions) al rotador de armónicos esféricos de SAF.
3. **`process_audio_block(void* instance, const float* input, float* output)`**: Toma el buffer de audio entrelazado de $N$ canales Ambisonics y escribe la señal binauralizada estéreo.
4. **`destroy_spatializer(void* instance)`**: Libera los recursos asociados de la memoria de C++.

---

## Consideraciones Técnicas Críticas (DSP y JNI)

Al integrar Spatial Audio Framework (`ambi_bin`) en motores móviles, se resolvieron dos desafíos de diseño:

### 1. Requisito de Bloque Fijo (`AMBI_BIN_FRAME_SIZE = 128`)
SAF procesa el audio en el dominio tiempo-frecuencia (STFT) usando un tamaño de bloque rígido de **128 muestras**. Si se le pasa un buffer de tamaño diferente (como 512 muestras de Android), SAF descarta el procesamiento y devuelve silencio absoluto.
- **Solución:** En `spatializer.cpp`, la función `process_audio_block` divide cualquier bloque entrante en fragmentos fijos de 128 muestras, realiza el de-entrelazado plano para cada fragmento, llama a SAF, y vuelve a entrelazar la salida estéreo.

### 2. Orden de Inicialización en SAF
La llamada a la función `ambi_bin_init` resetea internamente el estado de inicialización del decodificador (`codecStatus`) a `NOT_INITIALISED`.
- **Solución:** Las funciones se deben llamar obligatoriamente en este orden:
  1. `ambi_bin_init(hAmbi, sample_rate)`: Configura el vector de frecuencias y el STFT.
  2. `ambi_bin_initCodec(hAmbi)`: Computa los filtros binaurales.
  *(Llamarlas al revés provoca que el procesador nativo devuelva silencio).*

---

## Compilación para Android (ARM64) desde Windows

Para compilar la librería nativa para el Quest 3 desde Windows, necesitas tener instalado el **Android NDK** y **CMake**.

### Paso 1: Configurar el Entorno
Asegúrate de que tienes el Android NDK instalado en tu PC (normalmente en `C:\Users\<Usuario>\AppData\Local\Android\Sdk\ndk\<Versión>`).

### Paso 2: Ejecutar el Script de Compilación
El repositorio incluye el archivo [build_android.bat](file:///d:/code/procedural-spatializer/build_android.bat) que automatiza todo el proceso de compilación cruzada usando el generador **Ninja**:

1. Abre una terminal de PowerShell o CMD en la raíz del repositorio.
2. Ejecuta:
   ```powershell
   .\build_android.bat
   ```
3. El script detectará el NDK instalado y compilará la librería compartida `.so`.
4. El archivo generado se guardará en:
   `D:\code\procedural-spatializer\build_android\libprocedural_spatializer.so`

---

## Compilación para Windows Local (PC)

Si quieres probar y compilar la librería nativa para correr en Windows (por ejemplo, para debuggear en CLion, Visual Studio o testear localmente):

### 1. Sin dependencias externas (Modo Dummy)
Si solo quieres compilar para verificar la API sin lidiar con librerías matemáticas en PC:
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DSAF_PERFORMANCE_LIB=none
cmake --build build --config Release
```
*Esto compilará el ejecutable `spatializer_test.exe` en `build\Release\`. Al rotar el quaternion, simulará un paneo estéreo plano.*

### 2. Con decodificación binaural completa en PC
Para correr la decodificación binaural real de SAF en PC, necesitas proveer una librería CBLAS/LAPACK compatible para Windows.
La forma más sencilla en Windows es instalar **OpenBLAS**:

1. Descarga el zip de desarrollo de OpenBLAS para Windows (ej. `OpenBLAS-v0.3.X-x64.zip`).
2. Descomprímelo en una ruta local.
3. Configura CMake apuntando a OpenBLAS:
   ```powershell
   cmake -B build -G "Visual Studio 17 2022" -A x64 `
     -DSAF_PERFORMANCE_LIB=SAF_USE_OPEN_BLAS_AND_LAPACKE `
     -DOPENBLAS_DIR="C:/path/to/openblas"
   cmake --build build --config Release
   ```

---

## Integración en Kotlin / Android

Para usar la librería compilada en una aplicación nativa de Horizon OS / Android:

1. Copia `libprocedural_spatializer.so` y `libopenblas.so` en el directorio de tu proyecto Android:
   `app/src/main/jniLibs/arm64-v8a/`
2. Carga las librerías dinámicas en tu código Kotlin asegurándote de cargar `openblas` primero:
   ```kotlin
   init {
       System.loadLibrary("openblas")
       System.loadLibrary("procedural_spatializer")
   }
   ```
3. Lanza la preparación de `WavPlayer` siempre en un **hilo secundario (Background Thread)** para evitar colgar el hilo principal de renderizado de VR, lo cual provocaría que Oculus cierre la aplicación con `signal 9`:
   ```kotlin
   Thread {
       val wp = WavPlayer(file, ::log)
       if (wp.parse() && wp.prepare()) {
           runOnUiThread {
               wp.play()
           }
       }
   }.start()
   ```
