#include "effects.h"
#include <cmath>
#include <algorithm>

namespace dsp {

// =========================================================================
// IMPLEMENTACIÓN DE LA LÍNEA DE DELAY
// =========================================================================

DelayLine::DelayLine(float sr, float max_delay_seconds)
    : sample_rate(sr),
      feedback(0.5f),
      damping(0.5f),
      write_index(0),
      read_index(0),
      delay_samples(0),
      filter_state(0.0f) {
    // Reservar el tamaño del buffer circular basado en el delay máximo
    int buffer_size = static_cast<int>(max_delay_seconds * sample_rate) + 2;
    buffer.assign(buffer_size, 0.0f);
    
    // Configurar retardo inicial por defecto de 0.25 segundos (250ms)
    set_delay_time(0.25f);
}

void DelayLine::set_delay_time(float seconds) {
    // Acotar el tiempo de retardo entre 1 muestra y el límite máximo del buffer
    int max_samples = static_cast<int>(buffer.size()) - 2;
    delay_samples = static_cast<int>(seconds * sample_rate);
    delay_samples = std::max(1, std::min(max_samples, delay_samples));
    
    // Calcular el índice de lectura desfasado hacia atrás del índice de escritura
    read_index = write_index - delay_samples;
    if (read_index < 0) {
        read_index += buffer.size();
    }
}

float DelayLine::process(float input) {
    // 1. Leer el histórico demorado del buffer circular
    float output = buffer[read_index];
    
    // 2. Filtro Pasabajos One-Pole (Damping) en la realimentación:
    //    Simula la absorción de frecuencias altas del aire (las repeticiones 
    //    pierden brillo progresivamente).
    //    Fórmula: y[n] = x[n]*(1-d) + y[n-1]*d
    float local_damping = damping;
    float filtered = output * (1.0f - local_damping) + filter_state * local_damping;
    filter_state = filtered;
    
    // 3. Escribir la suma de la entrada actual y la repetición con feedback en el buffer
    buffer[write_index] = input + filtered * feedback;
    
    // 4. Avanzar los índices de forma circular
    if (++write_index >= (int)buffer.size()) write_index = 0;
    if (++read_index >= (int)buffer.size()) read_index = 0;
    
    return output;
}


// =========================================================================
// IMPLEMENTACIÓN DE LA REVERB DE SCHROEDER
// =========================================================================

ReverbEffect::ReverbEffect(float sr)
    : sample_rate(sr),
      room_size(0.5f),
      mix(0.15f) {
      
    // =========================================================================
    // EXPLICACIÓN ACÚSTICA (TIEMPOS DE RETARDO Y NÚMEROS PRIMOS)
    // =========================================================================
    // Los filtros peine (Comb) simulan las primeras reflexiones de una sala. 
    // Los retardos en muestras se eligen usando Números Primos para evitar que 
    // coincidan los armónicos comunes de los retrasos (lo cual crearía 
    // resonancias metálicas indeseables tipo "flanger" o "resortes").
    //
    // Tiempos clásicos de Schroeder escalados a la tasa de muestreo actual:
    // Comb 0: 29.7ms (1427 muestras a 48k)
    // Comb 1: 37.1ms (1781 muestras a 48k)
    // Comb 2: 41.1ms (1973 muestras a 48k)
    // Comb 3: 43.7ms (2099 muestras a 48k)
    // Allpass 0: 5.0ms (241 muestras a 48k)
    // Allpass 1: 1.7ms (83 muestras a 48k)
    // =========================================================================
    float scale = sample_rate / 48000.0f;
    
    // Inicializar filtros Comb (en paralelo)
    comb[0].init(static_cast<int>(1427.0f * scale), 0.8f);
    comb[1].init(static_cast<int>(1781.0f * scale), 0.75f);
    comb[2].init(static_cast<int>(1973.0f * scale), 0.7f);
    comb[3].init(static_cast<int>(2099.0f * scale), 0.65f);
    
    // Inicializar filtros Allpass (en serie, densifican la cola de la reverb)
    allpass[0].init(static_cast<int>(241.0f * scale), 0.5f);
    allpass[1].init(static_cast<int>(83.0f * scale), 0.5f);
}

void ReverbEffect::set_room_size(float size) {
    // Acotar el tamaño de sala entre 0.0 y 0.98 para evitar realimentaciones infinitas
    room_size = std::max(0.0f, std::min(0.98f, size));
}

void ReverbEffect::process_stereo(float* left_channel, float* right_channel, int num_samples) {
    if (mix <= 0.0f) return;

    float local_mix = mix;
    float local_room = room_size;

    for (int i = 0; i < num_samples; ++i) {
        float in_l = left_channel[i];
        float in_r = right_channel[i];
        
        // =========================================================================
        // CANAL IZQUIERDO
        // =========================================================================
        // 1. Ejecutar filtros peine (Comb) en paralelo
        float comb_out_l = comb[0].process(in_l, local_room) +
                           comb[1].process(in_l, local_room) +
                           comb[2].process(in_l, local_room) +
                           comb[3].process(in_l, local_room);
        
        // 2. Ejecutar filtros todo-paso (Allpass) en serie (difuminan el eco en una nube)
        float rev_l = allpass[1].process(allpass[0].process(comb_out_l * 0.25f));
        
        // 3. Mezcla Dry/Wet
        left_channel[i] = in_l * (1.0f - local_mix) + rev_l * local_mix;

        // =========================================================================
        // CANAL DERECHO (Descorrelación Estéreo)
        // =========================================================================
        // Para ensanchar el espacio estéreo y evitar que la reverb suene "al centro",
        // restamos la salida de algunos peines e invertimos la fase de otros.
        // Esto descorrelaciona la fase izquierda y derecha, abriendo la imagen 3D.
        // =========================================================================
        float comb_out_r = comb[0].process(in_r, local_room) -
                           comb[1].process(in_r, local_room) +
                           comb[2].process(in_r, local_room) -
                           comb[3].process(in_r, local_room);
                           
        float rev_r = allpass[1].process(allpass[0].process(comb_out_r * 0.25f));
        
        right_channel[i] = in_r * (1.0f - local_mix) + rev_r * local_mix;
    }
}

} // namespace dsp
