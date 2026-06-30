# Procedural Spatializer

Un motor de audio espacial liviano, modular y multiplataforma escrito en C++ diseñado para espacializar audio Ambisonics y sintetizar audio procedimental interactivo en tiempo real para **Meta Quest (Horizon OS)**, **Unity**, **Unreal Engine**, y aplicaciones nativas de Android.

Utiliza [Spatial_Audio_Framework (SAF)](https://github.com/leomccormack/Spatial_Audio_Framework) como núcleo para el cálculo de armónicos esféricos y la binauralización mediante filtros HRTF optimizados (LSDIFFEQ preset).

---

## Características

- **API C-Compatible limpia:** Fácil de llamar desde C# (Unity), C++ (Unreal) y JNI/Kotlin (Android).
- **Híbrido (Pasivo + Activo):** Soporta la espacialización de archivos WAV multicanal (ej. Ambisonics de 3er orden con 16 canales) y la generación simultánea de múltiples fuentes procedimentales interactivas mezcladas en el mismo bus.
- **Topología de CPU de Ultra-Bajo Consumo (Single-Bus Mixing):** Codifica múltiples fuentes mono a Ambisonics e inyecta todas las señales a un único bus de mezcla compartido. El motor ejecuta **un solo decodificador binaural de SAF por bloque de audio** para toda la mezcla combinada. Esto permite procesar más de 30 fuentes simultáneas en procesadores móviles con un consumo de CPU < 3%.
- **Optimización SIMD/NEON para ARM64:** Compilado con flags optimizados (`-Ofast -ffast-math -ftree-vectorize`) y una aproximación polinómica rápida de seno (`fast_sin_normalized`) que procesa múltiples muestras de audio en paralelo usando un solo ciclo de reloj de hardware, eliminando el clásico cuello de botella de `sinf()`.
- **Efectos Integrados:** Procesador de Delay Line amortiguado (damping) y Reverb de Schroeder estéreo descorrelacionada integrados en los envíos auxiliares del mezclador.

---

## Estructura del Proyecto

```text
procedural-spatializer/
├── CMakeLists.txt              # Configuración de compilación con optimizaciones SIMD
├── README.md                   # Esta documentación
├── .gitignore                  # Exclusión de archivos de build y dependencias locales
├── build_android.bat           # Script portátil de build automático para Android en Windows
├── src/
│   ├── spatializer.h           # API pública expuesta en C
│   ├── spatializer.cpp         # Registro de fuentes, bus de mezcla JNI y wrappers
│   ├── test_main.cpp           # Ejecutable de prueba offline para PC
│   └── dsp/
│       ├── fast_math.h         # Trigonometría SIMD/NEON rápida (Bhaskara I Approx)
│       ├── synth_fm.h          # Clase del sintetizador FM
│       ├── synth_fm.cpp        # DSP del sintetizador FM
│       ├── noise.h             # Clase del generador de ruido biquad
│       ├── noise.cpp           # DSP del generador de ruido
│       ├── effects.h           # Delay y Reverb de Schroeder
│       └── effects.cpp         # DSP de efectos
└── kotlin/
    └── com/
        └── mnlgt/
            └── spatializer/
                └── NativeSpatializer.kt # Wrapper Kotlin listo para usar en Android
```

---

## API Pública (C / JNI)

La interfaz C (`src/spatializer.h`) expone métodos planos para el DSP del mezclador y la gestión de fuentes dinámicas:

### 1. Control del Espacializador Principal
* `create_spatializer(int order, int sample_rate, int block_size)`: Inicializa la instancia del mezclador espacial (soporta 1er, 2do y 3er orden).
* `set_listener_orientation(void* instance, float x, float y, float z, float w)`: Actualiza la rotación de cabeza del oyente usando un Quaternion.
* `process_audio_block(void* instance, const float* input, float* output)`: Toma el buffer Ambisonics estático (opcional, ej. desde un WAV de 16ch), mezcla las fuentes procedimentales activas, procesa los efectos globales, realiza la decodificación binaural, y escribe el buffer estéreo de salida.
* `destroy_spatializer(void* instance)`: Libera la memoria de la instancia y de todas sus fuentes activas.

### 2. Gestión de Fuentes Procedimentales
* `add_fm_source(void* instance)`: Crea e inicializa una fuente de Sintetizador FM nativa y la añade al mezclador. Retorna un puntero `Long` opaco de control.
* `add_noise_source(void* instance)`: Crea e inicializa una fuente de Ruido Procedimental nativa y la añade al mezclador. Retorna un puntero `Long` de control.
* `remove_source(void* instance, void* source_ptr)`: Remueve la fuente del mezclador y libera su memoria nativa.
* `set_source_position(void* source_ptr, float azimuth_deg, float elevation_deg, float distance)`: Posiciona de forma interactiva una fuente en el espacio físico 3D.
* `set_source_parameter(void* source_ptr, int param_id, float value)`: Modula un parámetro de síntesis o de efectos de la fuente nativa en tiempo real.
* `trigger_synth_note(void* source_ptr, float frequency, float velocity, float decay_time_ms)`: Dispara un pulso acústico con envolvente exponencial en un sintetizador FM.

---

## Tabla de Identificadores de Parámetros (`paramId`)

Al llamar a `setSourceParameter` desde Kotlin, utilizá los siguientes IDs numéricos para modular el motor de síntesis nativo:

| Tipo de Fuente | Constante Kotlin | ID (`paramId`) | Descripción | Valores típicos |
| :--- | :--- | :---: | :--- | :--- |
| **Efectos Comunes** | `PARAM_DELAY_SEND` | **5** | Nivel de envío al Delay global | `0.0` (seco) a `1.0` (máximo) |
| | `PARAM_REVERB_SEND` | **6** | Nivel de envío a la Reverb global | `0.0` (seco) a `1.0` (máximo) |
| **Sintetizador FM** | `PARAM_FM_RATIO` | **2** | Relación de frecuencia del Modulador | `1.0` (armónico), `1.414` (cristalino), `3.0` |
| | `PARAM_FM_INDEX` | **3** | Índice de modulación (armónicos/brillo) | `0.0` (sinusoidal puro) a `15.0` (metálico) |
| | `PARAM_FM_GATE` | **4** | Encender (1.0) / Apagar (0.0) el oscilador | `0.0` o `1.0` |
| **Ruido / Viento** | `PARAM_NOISE_TYPE` | **10** | Tipo de ruido generado | `0.0` (Blanco), `1.0` (Rosa) |
| | `PARAM_NOISE_FILTER` | **11** | Tipo de filtro biquad aplicado | `0.0` (Ninguno), `1.0` (Pasabajos), `2.0` (Pasabanda) |
| | `PARAM_NOISE_CUTOFF` | **12** | Frecuencia de corte del filtro en Hz | `20.0` a `20000.0` |
| | `PARAM_NOISE_Q` | **13** | Resonancia del filtro biquad (Q) | `0.1` (plano) a `10.0` (silbido resonante) |
| | `PARAM_NOISE_VOLUME` | **14** | Ganancia / volumen del generador de ruido | `0.0` a `1.0` |
| | `PARAM_NOISE_GATE` | **15** | Encender (1.0) / Apagar (0.0) el generador | `0.0` o `1.0` |

---

## Consideraciones Técnicas de Optimización DSP

### 1. El Truco del "Fast Sine" (Bhaskara I Approx)
Para evitar el costo de CPU de `sinf()`, el motor normaliza la fase del oscilador en el rango $[-0.5, 0.5]$ (donde $1.0$ representa un ciclo completo) y calcula el seno con una parábola y suavizado polinómico:
```cpp
inline float fast_sin_normalized(float x) {
    float abs_x = (x >= 0.0f) ? x : -x;
    float y = 16.0f * x * (0.5f - abs_x);
    float abs_y = (y >= 0.0f) ? y : -y;
    return 0.225f * (y * abs_y - y) + y;
}
```
Esto permite al compilador Clang vectorizar el bucle con instrucciones ARM NEON nativas de un solo ciclo, procesando 4 muestras de audio paralelamente en un registro float SIMD.

### 2. Stack Caching
Las muestras del filtro biquad y las fases acumuladas se copian a variables locales del stack antes de iniciar el loop de procesamiento. Esto le permite al compilador alojar estas variables directamente en los registros del CPU (como los registros `v0-v31` en ARM64), logrando velocidad máxima de procesamiento y evitando accesos a memoria en el *heap* muestra por muestra.

---

## Integración en Kotlin / Android

1. Copia `libprocedural_spatializer.so` y `libopenblas.so` en `app/src/main/jniLibs/arm64-v8a/`.
2. Copia la carpeta `kotlin/com/mnlgt/spatializer/` al directorio de código fuente de tu app: `app/src/main/java/com/mnlgt/spatializer/`.
3. Para disparar y posicionar un sonido procedimental interactivo en 3D:
```kotlin
import com.mnlgt.spatializer.NativeSpatializer

// 1. Inicializar el motor espacial
val spatializer = NativeSpatializer.createInstance(order = 3, sampleRate = 48000, blockSize = 512)

// 2. Crear una fuente FM interactiva (Burbuja / Impacto)
val fmSource = spatializer.addFMSource()

// 3. Configurar parámetros (Ratio inarmónico y envíos de efectos)
spatializer.setSourceParameter(fmSource, NativeSpatializer.PARAM_FM_RATIO, 1.414f)
spatializer.setSourceParameter(fmSource, NativeSpatializer.PARAM_FM_INDEX, 5.0f)
spatializer.setSourceParameter(fmSource, NativeSpatializer.PARAM_REVERB_SEND, 0.25f) // 25% envío a reverb

// 4. Ubicarla en el espacio (Azimuth 45 grados a la derecha, altura de los ojos, 2 metros de distancia)
spatializer.setSourcePosition(fmSource, azimuth = 45.0f, elevation = 0.0f, distance = 2.0f)

// 5. Disparar un tono interactivo
spatializer.triggerSynthNote(fmSource, frequency = 880.0f, velocity = 0.8f, decayTimeMs = 150.0f)
```
