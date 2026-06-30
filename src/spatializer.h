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

// =========================================================================
// API DE SÍNTESIS PROCEDIMENTAL INTERACTIVA Y FUENTES DINÁMICAS
// =========================================================================

/**
 * @brief Crea e inicializa una fuente de Sintetizador FM nativa y la añade al mezclador.
 * @param instance Puntero a la instancia del espacializador.
 * @return Puntero opaco a la instancia del sintetizador FM creado.
 */
SPAT_API void* add_fm_source(void* instance);

/**
 * @brief Crea e inicializa una fuente de Ruido Procedimental nativa y la añade al mezclador.
 * @param instance Puntero a la instancia del espacializador.
 * @return Puntero opaco a la instancia del generador de ruido creado.
 */
SPAT_API void* add_noise_source(void* instance);

/**
 * @brief Elimina una fuente sonora del mezclador y libera su memoria.
 * @param instance Puntero a la instancia del espacializador.
 * @param source_ptr Puntero opaco a la fuente a eliminar (puede ser FM o Ruido).
 */
SPAT_API void remove_source(void* instance, void* source_ptr);

/**
 * @brief Configura la posición espacial 3D de una fuente en coordenadas esféricas.
 * @param source_ptr Puntero opaco a la fuente sonora.
 * @param azimuth_deg Ángulo horizontal en grados (-180 a 180).
 * @param elevation_deg Ángulo vertical en grados (-90 a 90).
 * @param distance Distancia relativa (afecta la atenuación).
 */
SPAT_API void set_source_position(void* source_ptr, float azimuth_deg, float elevation_deg, float distance);

/**
 * @brief Modula un parámetro numérico de una fuente de audio.
 * @param source_ptr Puntero opaco a la fuente sonora.
 * @param param_id Identificador del parámetro a modificar (específico por tipo de fuente).
 * @param value Valor flotante a asignar.
 */
SPAT_API void set_source_parameter(void* source_ptr, int param_id, float value);

/**
 * @brief Dispara un pulso/nota en un sintetizador FM nativo.
 * @param source_ptr Puntero opaco a la fuente sonora (debe ser un FMSynth).
 * @param frequency Frecuencia fundamental en Hz.
 * @param velocity Amplitud o volumen del pulso (0.0 a 1.0).
 * @param decay_time_ms Tiempo de decaimiento en milisegundos.
 */
SPAT_API void trigger_synth_note(void* source_ptr, float frequency, float velocity, float decay_time_ms);

#ifdef __cplusplus
}
#endif

#endif // PROCEDURAL_SPATIALIZER_H
