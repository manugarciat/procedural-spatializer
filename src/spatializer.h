#ifndef PROCEDURAL_SPATIALIZER_H
#define PROCEDURAL_SPATIALIZER_H

#ifdef _WIN32
  #define SPAT_API __declspec(dllexport)
#else
  #define SPAT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inicializa una instancia del espacializador Ambisonic.
 * @param ambisonics_order Orden del Ambisonics (1 para 4 canales, 2 para 9 ch, 3 para 16 ch, etc.)
 * @param sample_rate Frecuencia de muestreo (e.g. 48000)
 * @param block_size Tamaño del bloque de procesamiento (e.g. 256, 512, 1024 muestras)
 * @return Puntero opaco a la instancia del espacializador o NULL si falla.
 */
SPAT_API void* create_spatializer(int ambisonics_order, int sample_rate, int block_size);

/**
 * Actualiza la orientación del oyente (cabeza del usuario) usando un Quaternion.
 * @param instance Puntero a la instancia del espacializador.
 * @param x Componente X del Quaternion
 * @param y Componente Y del Quaternion
 * @param z Componente Z del Quaternion
 * @param w Componente W del Quaternion
 */
SPAT_API void set_listener_orientation(void* instance, float x, float y, float z, float w);

/**
 * Procesa un bloque de audio Ambisonic y genera audio estéreo espacializado (binaural).
 * @param instance Puntero a la instancia del espacializador.
 * @param input_buffer Buffer plano con las muestras de entrada de todos los canales (de tamaño block_size * num_channels)
 * @param output_buffer Buffer de salida plano para las muestras estéreo (de tamaño block_size * 2)
 */
SPAT_API void process_audio_block(void* instance, const float* input_buffer, float* output_buffer);

/**
 * Destruye la instancia del espacializador y libera toda la memoria.
 * @param instance Puntero a la instancia del espacializador.
 */
SPAT_API void destroy_spatializer(void* instance);

#ifdef __cplusplus
}
#endif

#endif // PROCEDURAL_SPATIALIZER_H
