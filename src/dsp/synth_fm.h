#ifndef SYNTH_FM_H
#define SYNTH_FM_H

/**
 * @file synth_fm.h
 * @brief Sintetizador por Modulación de Frecuencia (FM) con envolvente dinámica de decaimiento.
 */
namespace dsp {

class FMSynth {
public:
    /**
     * @brief Constructor del Sintetizador FM.
     * @param sample_rate Tasa de muestreo en Hz (normalmente 48000 en Quest 3).
     */
    FMSynth(float sample_rate);

    /**
     * @brief Dispara un nuevo tono o impacto procedimental.
     * @param frequency Frecuencia fundamental del Carrier en Hz.
     * @param velocity Amplitud inicial (0.0 a 1.0).
     * @param decay_time_ms Tiempo de decaimiento del volumen en milisegundos.
     */
    void trigger(float frequency, float velocity, float decay_time_ms);

    /**
     * @brief Procesa y genera un bloque de muestras mono del sintetizador.
     *
     * EXPLICACIÓN DE OPTIMIZACIÓN (LOCAL REGISTER CACHING):
     * Durante el bucle de renderizado de audio, esta función se llama en el hilo nativo pesado.
     * Para maximizar la velocidad, la implementación lee los datos del heap (los miembros de la clase)
     * una sola vez al principio, procesa el buffer utilizando el stack y los registros de la CPU,
     * y guarda el estado final de fase y envolvente de vuelta en la clase al terminar.
     *
     * @param buffer Puntero al buffer mono de salida que será llenado.
     * @param num_samples Cantidad de muestras a generar (ej. 128 muestras por sub-bloque).
     */
    void process(float* buffer, int num_samples);

    // --- Parámetros de Síntesis FM ---
    void set_mod_ratio(float ratio) { mod_ratio = ratio; }
    void set_mod_index(float index) { mod_index = index; }
    void set_active(bool active_state) { active = active_state; }
    bool is_active() const { return active; }

    // --- Posicionamiento Espacial de esta Fuente ---
    void set_position(float azimuth, float elevation, float distance) {
        pan_azimuth = azimuth;
        pan_elevation = elevation;
        pan_distance = distance;
    }
    float get_azimuth() const { return pan_azimuth; }
    float get_elevation() const { return pan_elevation; }
    float get_distance() const { return pan_distance; }

    // --- Envíos de Efectos ---
    void set_effect_sends(float delay_send_level, float reverb_send_level) {
        delay_send = delay_send_level;
        reverb_send = reverb_send_level;
    }
    float get_delay_send() const { return delay_send; }
    float get_reverb_send() const { return reverb_send; }

private:
    // Variables de configuración
    float sample_rate;
    bool active;

    // Frecuencias y modulación
    float carrier_freq;
    float mod_ratio;       // Relación de frecuencia del Modulador (ej. 1.0 = armónico, 1.414 = inarmónico)
    float mod_index;       // Índice de modulación (controla el brillo/armónicos)

    // Acumuladores de fase (normalizados en [-0.5, 0.5])
    float carrier_phase;
    float mod_phase;

    // Envolvente de volumen (Decay exponencial simple)
    float envelope_value;
    float decay_rate;      // Factor de atenuación multiplicativa por muestra

    // Posicionamiento 3D
    float pan_azimuth;
    float pan_elevation;
    float pan_distance;

    // Envíos auxiliares
    float delay_send;
    float reverb_send;
};

} // namespace dsp

#endif // SYNTH_FM_H
