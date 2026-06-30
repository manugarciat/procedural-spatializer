#ifndef NOISE_H
#define NOISE_H

#include <cstdint>

/**
 * @file noise.h
 * @brief Generador de Ruido Procedimental (Blanco y Rosa) con filtro Biquad dinámico integrado.
 */
namespace dsp {

enum NoiseType {
    NOISE_WHITE = 0,
    NOISE_PINK = 1
};

enum FilterType {
    FILTER_NONE = 0,
    FILTER_LOWPASS = 1,
    FILTER_BANDPASS = 2
};

class NoiseGenerator {
public:
    /**
     * @brief Constructor del Generador de Ruido.
     * @param sample_rate Tasa de muestreo en Hz.
     */
    NoiseGenerator(float sample_rate);

    /**
     * @brief Procesa y genera un bloque de audio mono de ruido filtrado.
     * @param buffer Puntero al buffer mono de salida.
     * @param num_samples Cantidad de muestras a generar.
     */
    void process(float* buffer, int num_samples);

    // --- Parámetros de Control ---
    void set_noise_type(NoiseType type) { noise_type = type; }
    void set_filter_type(FilterType type) { filter_type = type; }
    void set_cutoff(float freq_hz);
    void set_q(float resonance);
    void set_volume(float vol) { volume = vol; }
    void set_active(bool active_state) { active = active_state; }
    bool is_active() const { return active; }

    // --- Posicionamiento Espacial ---
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
    /**
     * @brief Recalcula los coeficientes del filtro biquad analógico a digital.
     */
    void update_filter_coefficients();

    /**
     * @brief Genera un número flotante pseudo-aleatorio en [-1.0, 1.0] usando un generador rápido de 32-bits (Xorshift).
     *
     * EXPLICACIÓN DE OPTIMIZACIÓN (XORSHIFT VS RAND):
     * La función estándar `std::rand()` es lenta y tiene bloqueos internos.
     * Para que el compilador Clang vectorice el loop de ruido usando NEON, implementamos un
     * generador Xorshift de 32-bits en registros. Solo usa operaciones de desplazamiento de bits (shifts)
     * y operaciones XOR que toman 1 ciclo de CPU.
     */
    inline float fast_random() {
        // Algoritmo Xorshift32
        uint32_t x = rand_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rand_state = x;
        
        // Mapear el entero sin signo resultante al rango float [-1.0f, 1.0f]
        // Se hace mediante operaciones matemáticas en registros float
        return (static_cast<float>(x) * 4.6566129e-10f) - 1.0f;
    }

    // Configuración y estado
    float sample_rate;
    bool active;
    NoiseType noise_type;
    FilterType filter_type;
    float volume;

    // Estado del generador rápido de números pseudo-aleatorios
    uint32_t rand_state;

    // --- Filtro Biquad: Coeficientes y Estado ---
    float cutoff_hz;
    float q_value;
    
    // Coeficientes de transferencia de filtro digital (Biquad Direct Form I)
    float b0, b1, b2; // Numerador (Input feedforward)
    float a1, a2;     // Denominador (Output feedback)
    
    // Registro de retraso de muestras previas (Stack cached)
    float x1, x2;     // Estado de entrada previo
    float y1, y2;     // Estado de salida previo

    // --- Estado de Ruido Rosa (Filtro de Paul Kellet) ---
    // El ruido rosa decae a 3dB por octava (ruido de espectro natural).
    // Se logra con una red de filtros de 3 polos y 3 ceros que aproximan el espectro.
    float pink_b0, pink_b1, pink_b2, pink_b3, pink_b4, pink_b5, pink_b6;

    // Posicionamiento 3D
    float pan_azimuth;
    float pan_elevation;
    float pan_distance;

    // Envíos auxiliares
    float delay_send;
    float reverb_send;
};

} // namespace dsp

#endif // NOISE_H
