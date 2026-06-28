#include "spatializer.h"
#include <vector>
#include <cmath>
#include <cstring>

// Estructura interna de control (opaca para el exterior)
struct SpatializerInstance {
    int order;
    int channels;
    int sample_rate;
    int block_size;
    
    // Quaternion de rotación actual
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;

    SpatializerInstance(int o, int rate, int size)
        : order(o), sample_rate(rate), block_size(size) {
        channels = (order + 1) * (order + 1); // Relación estándar Ambisonics (e.g. 1st order = 4 ch)
    }
};

extern "C" {

SPAT_API void* create_spatializer(int ambisonics_order, int sample_rate, int block_size) {
    if (ambisonics_order < 1 || sample_rate <= 0 || block_size <= 0) {
        return nullptr;
    }
    return new SpatializerInstance(ambisonics_order, sample_rate, block_size);
}

SPAT_API void set_listener_orientation(void* instance, float x, float y, float z, float w) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat) return;
    
    spat->qx = x;
    spat->qy = y;
    spat->qz = z;
    spat->qw = w;
}

SPAT_API void process_audio_block(void* instance, const float* input_buffer, float* output_buffer) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat || !input_buffer || !output_buffer) return;

    // TODO: Implementar procesamiento real con SAF (Spatial Audio Framework).
    // Implementación temporal "dummy":
    // Copiamos el canal 0 (W, que contiene la mezcla mono omnidireccional) a L y R.
    // Aplicamos una ganancia simple basada en el Quaternion para verificar que la rotación tiene efecto.
    
    int num_ch = spat->channels;
    for (int i = 0; i < spat->block_size; ++i) {
        // En Ambisonics ACN format: el canal 0 es W (presión de sonido general)
        float w_channel = input_buffer[i * num_ch + 0];
        
        // Simulación muy básica de paneo estéreo usando qy (rotación horizontal)
        float pan = 0.5f + (spat->qy * 0.5f); // paneo -1.0 a 1.0 mapeado a 0.0 a 1.0
        pan = std::max(0.0f, std::min(1.0f, pan));
        
        output_buffer[i * 2 + 0] = w_channel * std::sqrt(1.0f - pan); // Canal L
        output_buffer[i * 2 + 1] = w_channel * std::sqrt(pan);        // Canal R
    }
}

SPAT_API void destroy_spatializer(void* instance) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (spat) {
        delete spat;
    }
}

}
