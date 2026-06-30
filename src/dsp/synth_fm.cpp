#include "synth_fm.h"
#include "fast_math.h"
#include <cmath>
#include <cstring>

namespace dsp {

FMSynth::FMSynth(float sr)
    : sample_rate(sr),
      active(false),
      carrier_freq(440.0f),
      mod_ratio(1.0f),
      mod_index(2.0f),
      carrier_phase(0.0f),
      mod_phase(0.0f),
      envelope_value(0.0f),
      decay_rate(0.999f),
      pan_azimuth(0.0f),
      pan_elevation(0.0f),
      pan_distance(1.0f),
      delay_send(0.0f),
      reverb_send(0.0f) {}

void FMSynth::trigger(float frequency, float velocity, float decay_time_ms) {
    carrier_freq = frequency;
    envelope_value = velocity;
    
    // Resetear las fases a cero para evitar discontinuidades al iniciar la nota
    carrier_phase = 0.0f;
    mod_phase = 0.0f;
    
    // Calcular el decay_rate exponencial por muestra:
    // La fórmula es: decay_rate = exp(-1.0 / (tiempo_segundos * sample_rate))
    if (decay_time_ms > 0.0f) {
        float decay_samples = (decay_time_ms * 0.001f) * sample_rate;
        decay_rate = std::exp(-1.0f / decay_samples);
    } else {
        decay_rate = 0.0f; // Corte abrupto
    }
    
    active = true;
}

void FMSynth::process(float* buffer, int num_samples) {
    // Si la fuente no está activa, llenamos el buffer con ceros y salimos rápido (Bypass)
    if (!active) {
        std::memset(buffer, 0, num_samples * sizeof(float));
        return;
    }

    // =========================================================================
    // EXPLICACIÓN DE OPTIMIZACIÓN (LOCAL VARIABLE CACHING)
    // =========================================================================
    // Copiar variables de miembro (que viven en el Heap) a variables locales 
    // del Stack/Registros. Esto evita accesos indirectos de memoria de puntero (dereferencing)
    // dentro del bucle de procesamiento de muestras pesadas.
    // El compilador Clang colocará estas variables en registros de hardware nativos (ej. v0, v1).
    // =========================================================================
    float c_phase = carrier_phase;
    float m_phase = mod_phase;
    float env_val = envelope_value;
    float dec_rate = decay_rate;
    float m_ratio = mod_ratio;
    float m_index = mod_index;
    float sr = sample_rate;
    float c_freq = carrier_freq;

    // Calcular los incrementos de fase de antelación
    // Dividimos por la tasa de muestreo para obtener el delta por muestra.
    float c_inc = c_freq / sr;
    float m_inc = (c_freq * m_ratio) / sr;

    for (int i = 0; i < num_samples; ++i) {
        // 1. Modulador: Generamos su oscilación mono
        //    Usamos fast_sin_normalized porque m_phase está acotado en [-0.5, 0.5]
        float mod_osc = fast_sin_normalized(m_phase);
        
        // 2. Modular el Índice por la envolvente:
        //    A medida que la nota decae en volumen, también decae el brillo (frecuencias altas).
        //    Esto emula el comportamiento de filtros acústicos naturales.
        float current_index = m_index * env_val;
        float modulation = mod_osc * current_index;
        
        // 3. Carrier: Modulación de Fase
        //    Añadimos la señal moduladora a la fase del Carrier.
        float carrier_modulated = c_phase + modulation;
        
        // 4. Envolver la fase de manera limpia y rápida
        float wrapped_carrier = wrap_phase(carrier_modulated);
        
        // 5. Salida final de audio aplicando la envolvente
        buffer[i] = fast_sin_normalized(wrapped_carrier) * env_val;
        
        // 6. Avanzar las fases acumuladas
        c_phase += c_inc;
        m_phase += m_inc;
        
        // 7. Envolver las fases base para el próximo ciclo
        c_phase = wrap_phase(c_phase);
        m_phase = wrap_phase(m_phase);
        
        // 8. Decaimiento exponencial de la envolvente
        env_val *= dec_rate;
        
        // 9. Si el volumen cae por debajo de -60dB (infrasonido), apagamos la nota
        if (env_val < 0.0001f) {
            env_val = 0.0f;
            active = false;
            // Limpiar las muestras restantes del bloque con silencio absoluto
            for (int j = i + 1; j < num_samples; ++j) {
                buffer[j] = 0.0f;
            }
            break;
        }
    }

    // Escribir el estado final procesado en el stack de vuelta al objeto en el heap
    carrier_phase = c_phase;
    mod_phase = m_phase;
    envelope_value = env_val;
}

} // namespace dsp
