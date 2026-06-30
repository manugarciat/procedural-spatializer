#ifndef EFFECTS_H
#define EFFECTS_H

#include <vector>

/**
 * @file effects.h
 * @brief Efectos de Audio (Delay y Reverb Algorítmica) para dar profundidad espacial.
 */
namespace dsp {

/**
 * @brief Línea de Retardo Dinámica con atenuación pasabajos (Feedback Delay Line).
 */
class DelayLine {
public:
    DelayLine(float sample_rate, float max_delay_seconds = 2.0f);
    
    /**
     * @brief Configura el tiempo de retardo en segundos.
     */
    void set_delay_time(float seconds);
    
    /**
     * @brief Configura la realimentación (0.0 = sin repetición, 0.9 = ecos infinitos).
     */
    void set_feedback(float fb) { feedback = fb; }
    
    /**
     * @brief Configura el amortiguamiento de altas frecuencias (damping).
     */
    void set_damping(float damp) { damping = damp; }

    /**
     * @brief Procesa una muestra de audio. Introduce la muestra de entrada y retorna la retrasada.
     * @param input Muestra de audio entrante.
     * @return float Muestra con efecto de eco.
     */
    float process(float input);

private:
    float sample_rate;
    float feedback;
    float damping;
    
    // Buffer circular para almacenar el histórico de muestras
    std::vector<float> buffer;
    int write_index;
    int read_index;
    int delay_samples;
    
    // Filtro pasabajos interno simple (One-pole filter) para simular la pérdida de agudos en el aire
    float filter_state;
};

/**
 * @brief Reverberación Algorítmica básica basada en el diseño clásico de Schroeder.
 *
 * Consta de 4 filtros Comb (peine) en paralelo para simular las reflexiones iniciales de la sala,
 * seguidos de 2 filtros Allpass (todo-paso) en serie para densificar el eco y suavizar la reverb.
 */
class ReverbEffect {
public:
    ReverbEffect(float sample_rate);

    /**
     * @brief Configura el factor de decaimiento (tamaño de sala, 0.0 a 1.0).
     */
    void set_room_size(float size);

    /**
     * @brief Configura la mezcla final de la reverb (0.0 a 1.0).
     */
    void set_mix(float reverb_mix) { mix = reverb_mix; }

    /**
     * @brief Procesa un bloque de audio estéreo aplicando reverberación espacial en lugar.
     *        (Modifica los canales Left y Right in-place).
     * @param left_channel Buffer de audio del canal izquierdo.
     * @param right_channel Buffer de audio del canal derecho.
     * @param num_samples Cantidad de muestras a procesar.
     */
    void process_stereo(float* left_channel, float* right_channel, int num_samples);

private:
    // Estructuras internas para filtros de Reverb
    struct CombFilter {
        std::vector<float> buffer;
        int index;
        float feedback;
        float filter_state;
        
        void init(int size, float fb) {
            buffer.assign(size, 0.0f);
            index = 0;
            feedback = fb;
            filter_state = 0.0f;
        }
        
        inline float process(float input, float room_size) {
            float output = buffer[index];
            // Filtro one-pole pasabajos interno del comb filter (damping)
            filter_state = output * 0.2f + filter_state * 0.8f;
            buffer[index] = input + filter_state * (feedback * room_size);
            if (++index >= (int)buffer.size()) index = 0;
            return output;
        }
    };

    struct AllpassFilter {
        std::vector<float> buffer;
        int index;
        float feedback;
        
        void init(int size, float fb) {
            buffer.assign(size, 0.0f);
            index = 0;
            feedback = fb;
        }
        
        inline float process(float input) {
            float buf_out = buffer[index];
            float output = -feedback * input + buf_out;
            buffer[index] = input + feedback * buf_out;
            if (++index >= (int)buffer.size()) index = 0;
            return output;
        }
    };

    float sample_rate;
    float room_size;
    float mix;

    // Schroeder Reverb tiene 4 filtros Comb en paralelo y 2 Allpass en serie
    CombFilter comb[4];
    AllpassFilter allpass[2];
};

} // namespace dsp

#endif // EFFECTS_H
