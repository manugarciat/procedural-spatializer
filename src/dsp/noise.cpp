#include "noise.h"
#include <cmath>
#include <cstring>

namespace dsp {

NoiseGenerator::NoiseGenerator(float sr)
    : sample_rate(sr),
      active(false),
      noise_type(NOISE_WHITE),
      filter_type(FILTER_NONE),
      volume(0.5f),
      rand_state(0xACE1U), // Semilla inicial para Xorshift (cualquier número excepto cero)
      cutoff_hz(1000.0f),
      q_value(0.707f),
      b0(1.0f), b1(0.0f), b2(0.0f),
      a1(0.0f), a2(0.0f),
      x1(0.0f), x2(0.0f),
      y1(0.0f), y2(0.0f),
      pink_b0(0.0f), pink_b1(0.0f), pink_b2(0.0f), pink_b3(0.0f), pink_b4(0.0f), pink_b5(0.0f), pink_b6(0.0f),
      pan_azimuth(0.0f),
      pan_elevation(0.0f),
      pan_distance(1.0f),
      delay_send(0.0f),
      reverb_send(0.0f) {
    update_filter_coefficients();
}

void NoiseGenerator::set_cutoff(float freq_hz) {
    // Limitar la frecuencia de corte entre 20Hz y el límite de Nyquist (Fs / 2)
    float nyquist = sample_rate * 0.5f;
    cutoff_hz = (freq_hz < 20.0f) ? 20.0f : (freq_hz > nyquist - 100.0f ? nyquist - 100.0f : freq_hz);
    update_filter_coefficients();
}

void NoiseGenerator::set_q(float resonance) {
    q_value = (resonance < 0.05f) ? 0.05f : (resonance > 20.0f ? 20.0f : resonance);
    update_filter_coefficients();
}

void NoiseGenerator::update_filter_coefficients() {
    // Fórmulas estándar del recetario de filtros EQ de Robert Bristow-Johnson (RBJ)
    float w0 = 2.0f * 3.14159265f * cutoff_hz / sample_rate;
    float cos_w0 = std::cos(w0);
    float sin_w0 = std::sin(w0);
    float alpha = sin_w0 / (2.0f * q_value);

    float a0_inv = 1.0f;

    switch (filter_type) {
        case FILTER_NONE:
            b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
            a1 = 0.0f; a2 = 0.0f;
            break;
            
        case FILTER_LOWPASS: {
            float a0 = 1.0f + alpha;
            a0_inv = 1.0f / a0; // Pre-calculamos la división para usar multiplicaciones más rápidas
            b0 = ((1.0f - cos_w0) * 0.5f) * a0_inv;
            b1 = (1.0f - cos_w0) * a0_inv;
            b2 = ((1.0f - cos_w0) * 0.5f) * a0_inv;
            a1 = (-2.0f * cos_w0) * a0_inv;
            a2 = (1.0f - alpha) * a0_inv;
            break;
        }
            
        case FILTER_BANDPASS: {
            float a0 = 1.0f + alpha;
            a0_inv = 1.0f / a0;
            b0 = alpha * a0_inv;
            b1 = 0.0f;
            b2 = -alpha * a0_inv;
            a1 = (-2.0f * cos_w0) * a0_inv;
            a2 = (1.0f - alpha) * a0_inv;
            break;
        }
    }
}

void NoiseGenerator::process(float* buffer, int num_samples) {
    if (!active) {
        std::memset(buffer, 0, num_samples * sizeof(float));
        return;
    }

    // =========================================================================
    // EXPLICACIÓN DE STACK CACHING (BIQUAD FILTER)
    // =========================================================================
    // Almacenamos el estado del filtro biquad y el ruido en variables locales.
    // Si procesamos muestra por muestra directamente escribiendo en los punteros
    // de la clase, el CPU tendría un cuello de botella por accesos a memoria cache L1.
    // Cargando los coeficientes y las muestras demoradas (x1, x2, y1, y2) en el stack,
    // Clang los guarda en los registros ultra-rápidos de punto flotante de ARM.
    // =========================================================================
    float local_b0 = b0; float local_b1 = b1; float local_b2 = b2;
    float local_a1 = a1; float local_a2 = a2;
    float lx1 = x1; float lx2 = x2;
    float ly1 = y1; float ly2 = y2;
    
    // Cargar estado de ruido rosa
    float pk0 = pink_b0; float pk1 = pink_b1; float pk2 = pink_b2;
    float pk3 = pink_b3; float pk4 = pink_b4; float pk5 = pink_b5; float pk6 = pink_b6;

    float vol = volume;
    bool is_pink = (noise_type == NOISE_PINK);
    bool has_filter = (filter_type != FILTER_NONE);

    for (int i = 0; i < num_samples; ++i) {
        float raw_sample = 0.0f;

        if (is_pink) {
            // =========================================================================
            // RUIDO ROSA (Filtro de Paul Kellet)
            // =========================================================================
            // El ruido rosa decae a 3dB/octava, sonando mucho más natural que el ruido blanco.
            // La aproximación polinómica filtra el ruido blanco con una red de polos y ceros
            // que distribuyen la energía de forma idéntica al ruido de la naturaleza (viento, mar).
            // =========================================================================
            float white = fast_random();
            pk0 = 0.99886f * pk0 + white * 0.0555179f;
            pk1 = 0.99332f * pk1 + white * 0.0750759f;
            pk2 = 0.96900f * pk2 + white * 0.1538520f;
            pk3 = 0.86650f * pk3 + white * 0.3104856f;
            pk4 = 0.55000f * pk4 + white * 0.5329522f;
            pk5 = -0.7616f * pk5 - white * 0.0168980f;
            
            float pink = pk0 + pk1 + pk2 + pk3 + pk4 + pk5 + pk6 + white * 0.5362f;
            pk6 = white * 0.115926f;
            
            raw_sample = pink * 0.11f; // Compensación aproximada de ganancia para evitar saturación
        } else {
            // RUIDO BLANCO (Distribución uniforme de energía en frecuencia)
            raw_sample = fast_random();
        }

        float out_sample = raw_sample;

        if (has_filter) {
            // =========================================================================
            // FILTRO BIQUAD (Direct Form I)
            // =========================================================================
            // Ecuación de diferencias en tiempo real:
            //   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
            // =========================================================================
            out_sample = local_b0 * raw_sample + local_b1 * lx1 + local_b2 * lx2 
                         - local_a1 * ly1 - local_a2 * ly2;

            // Desplazar el delay del filtro en registros (muy veloz)
            lx2 = lx1;
            lx1 = raw_sample;
            ly2 = ly1;
            ly1 = out_sample;
        }

        buffer[i] = out_sample * vol;
    }

    // Escribir los registros locales de vuelta a las variables miembro de la clase
    x1 = lx1; x2 = lx2;
    y1 = ly1; y2 = ly2;
    
    pink_b0 = pk0; pink_b1 = pk1; pink_b2 = pk2;
    pink_b3 = pk3; pink_b4 = pk4; pink_b5 = pk5; pink_b6 = pk6;
}

} // namespace dsp
