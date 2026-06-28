#include "spatializer.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifdef USE_SAF
// Incluir las cabeceras de Spatial_Audio_Framework
#include "saf.h"
#include "../external/Spatial_Audio_Framework/examples/include/ambi_bin.h"
extern "C" void openblas_set_num_threads(int num_threads);
#endif

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

#ifdef USE_SAF
    void* hAmbi = nullptr;
    // Buffers temporales no entrelazados (SAF requiere buffers separados por canal)
    std::vector<float*> input_channels;
    std::vector<float*> output_channels;
    std::vector<float> input_flat_buffers;
    std::vector<float> output_flat_buffers;
#endif

    SpatializerInstance(int o, int rate, int size)
        : order(o), sample_rate(rate), block_size(size) {
        channels = (order + 1) * (order + 1);

#ifdef USE_SAF
        // 1. Crear instancia de SAF ambi_bin
        ambi_bin_create(&hAmbi);
        
        // 2. Mapear el orden de Ambisonics para SAF
        SH_ORDERS saf_order = SH_ORDER_FIRST;
        if (order == 2) saf_order = SH_ORDER_SECOND;
        else if (order == 3) saf_order = SH_ORDER_THIRD;
        else if (order > 3) saf_order = SH_ORDER_FOURTH; // Soporta hasta 4to orden
        
        ambi_bin_setInputOrderPreset(hAmbi, saf_order);
        ambi_bin_setEnableRotation(hAmbi, SAF_TRUE);
        ambi_bin_setDecodingMethod(hAmbi, DECODING_METHOD_LSDIFFEQ); // LSDIFFEQ - Standard stable decoder
        
        // Evitar deadlocks de hilos en OpenBLAS para Android
        #ifdef USE_SAF
        openblas_set_num_threads(1);
        #endif

        ambi_bin_init(hAmbi, sample_rate);
        ambi_bin_initCodec(hAmbi);

        // Allocación de buffers temporales internos de tipo float* para SAF
        // Forzamos el tamaño fijo de 128 muestras por canal, que es el que exige SAF
        const int SAF_FRAME_SIZE = 128;
        input_channels.resize(channels);
        input_flat_buffers.resize(channels * SAF_FRAME_SIZE, 0.0f);
        for (int c = 0; c < channels; ++c) {
            input_channels[c] = &input_flat_buffers[c * SAF_FRAME_SIZE];
        }

        output_channels.resize(2); // Estéreo
        output_flat_buffers.resize(2 * SAF_FRAME_SIZE, 0.0f);
        output_channels[0] = &output_flat_buffers[0 * SAF_FRAME_SIZE];
        output_channels[1] = &output_flat_buffers[1 * SAF_FRAME_SIZE];
#endif
    }

    ~SpatializerInstance() {
#ifdef USE_SAF
        if (hAmbi) {
            ambi_bin_destroy(&hAmbi);
        }
#endif
    }
};

extern "C" {

SPAT_API void* create_spatializer(int ambisonics_order, int sample_rate, int block_size) {
    if (ambisonics_order < 1 || sample_rate <= 0 || block_size <= 0) {
        return nullptr;
    }
    return new SpatializerInstance(ambisonics_order, sample_rate, block_size);
}

// Convierte un Quaternion a ángulos Euler Yaw, Pitch, Roll (en grados) para SAF
static void quaternion_to_euler(float qx, float qy, float qz, float qw, float& yaw, float& pitch, float& roll) {
    // Ecuaciones estándar de conversión de Quaternion a Euler (Z-Y-X Tait-Bryan)
    // Yaw (Z-axis rotation)
    double siny_cosp = 2.0 * (qw * qz + qx * qy);
    double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = static_cast<float>(std::atan2(siny_cosp, cosy_cosp) * (180.0 / 3.141592653589793));

    // Pitch (Y-axis rotation)
    double sinp = 2.0 * (qw * qy - qz * qx);
    if (std::abs(sinp) >= 1.0)
        pitch = static_cast<float>(std::copysign(3.141592653589793 / 2.0, sinp) * (180.0 / 3.141592653589793));
    else
        pitch = static_cast<float>(std::asin(sinp) * (180.0 / 3.141592653589793));

    // Roll (X-axis rotation)
    double sinr_cosp = 2.0 * (qw * qx + qy * qz);
    double cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy);
    roll = static_cast<float>(std::atan2(sinr_cosp, cosr_cosp) * (180.0 / 3.141592653589793));
}

SPAT_API void set_listener_orientation(void* instance, float x, float y, float z, float w) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat) return;
    
    spat->qx = x;
    spat->qy = y;
    spat->qz = z;
    spat->qw = w;

#ifdef USE_SAF
    if (spat->hAmbi) {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        quaternion_to_euler(x, y, z, w, yaw, pitch, roll);
        
        // SAF espera los ángulos en grados para la rotación del campo sonoro
        // Invertimos el ángulo para rotar el sonido de forma contraria a la cabeza del oyente
        ambi_bin_setYaw(spat->hAmbi, -yaw);
        ambi_bin_setPitch(spat->hAmbi, -pitch);
        ambi_bin_setRoll(spat->hAmbi, -roll);
    }
#endif
}

SPAT_API void process_audio_block(void* instance, const float* input_buffer, float* output_buffer) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat || !input_buffer || !output_buffer) return;

#ifdef USE_SAF
    if (spat->hAmbi) {
        int num_ch = spat->channels;
        int size = spat->block_size;
        const int SAF_FRAME_SIZE = 128; // El tamaño fijo que SAF exige para procesar

        for (int offset = 0; offset < size; offset += SAF_FRAME_SIZE) {
            int current_chunk = std::min(SAF_FRAME_SIZE, size - offset);
            
            // 1. De-entrelazar el sub-bloque actual de la entrada
            for (int i = 0; i < current_chunk; ++i) {
                for (int c = 0; c < num_ch; ++c) {
                    spat->input_flat_buffers[c * SAF_FRAME_SIZE + i] = input_buffer[(offset + i) * num_ch + c];
                }
            }
            
            // Si el bloque final es menor a 128, rellenamos el resto con ceros
            if (current_chunk < SAF_FRAME_SIZE) {
                for (int c = 0; c < num_ch; ++c) {
                    for (int i = current_chunk; i < SAF_FRAME_SIZE; ++i) {
                        spat->input_flat_buffers[c * SAF_FRAME_SIZE + i] = 0.0f;
                    }
                }
            }

            // 2. Procesar con SAF usando el sub-bloque de 128 muestras
            ambi_bin_process(spat->hAmbi, spat->input_channels.data(), spat->output_channels.data(),
                             num_ch, 2, SAF_FRAME_SIZE);

            // 3. Volver a entrelazar los datos del sub-bloque al output de salida estéreo
            for (int i = 0; i < current_chunk; ++i) {
                output_buffer[(offset + i) * 2 + 0] = spat->output_flat_buffers[0 * SAF_FRAME_SIZE + i]; // Left
                output_buffer[(offset + i) * 2 + 1] = spat->output_flat_buffers[1 * SAF_FRAME_SIZE + i]; // Right
            }
        }
        return;
    }
#endif

    // Fallback Dummy (si compilamos en Windows sin SAF)
    int num_ch = spat->channels;
    for (int i = 0; i < spat->block_size; ++i) {
        float w_channel = input_buffer[i * num_ch + 0]; // ACN ch 0 = W (omni)
        
        float pan = 0.5f + (spat->qy * 0.5f); // paneo simple
        pan = std::max(0.0f, std::min(1.0f, pan));
        
        output_buffer[i * 2 + 0] = w_channel * std::sqrt(1.0f - pan);
        output_buffer[i * 2 + 1] = w_channel * std::sqrt(pan);
    }
}

SPAT_API void destroy_spatializer(void* instance) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (spat) {
        delete spat;
    }
}

}

#ifdef __ANDROID__
#include <jni.h>

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_mnlgt_procedural_1vr_NativeSpatializer_create(JNIEnv *env, jobject thiz, jint order, jint sample_rate, jint block_size) {
    (void)env; (void)thiz;
    return reinterpret_cast<jlong>(create_spatializer(order, sample_rate, block_size));
}

JNIEXPORT void JNICALL
Java_com_mnlgt_procedural_1vr_NativeSpatializer_setOrientation(JNIEnv *env, jobject thiz, jlong pointer, jfloat qx, jfloat qy, jfloat qz, jfloat qw) {
    (void)env; (void)thiz;
    set_listener_orientation(reinterpret_cast<void*>(pointer), qx, qy, qz, qw);
}

JNIEXPORT void JNICALL
Java_com_mnlgt_procedural_1vr_NativeSpatializer_process(JNIEnv *env, jobject thiz, jlong pointer, jfloatArray input, jfloatArray output) {
    (void)thiz;
    auto* spat = reinterpret_cast<SpatializerInstance*>(pointer);
    if (!spat) return;

    jfloat* input_data = env->GetFloatArrayElements(input, nullptr);
    jfloat* output_data = env->GetFloatArrayElements(output, nullptr);

    if (input_data && output_data) {
        process_audio_block(spat, input_data, output_data);
    }

    if (input_data) env->ReleaseFloatArrayElements(input, input_data, JNI_ABORT); // read-only
    if (output_data) env->ReleaseFloatArrayElements(output, output_data, 0);      // write-back
}

JNIEXPORT void JNICALL
Java_com_mnlgt_procedural_1vr_NativeSpatializer_destroy(JNIEnv *env, jobject thiz, jlong pointer) {
    (void)env; (void)thiz;
    destroy_spatializer(reinterpret_cast<void*>(pointer));
}

}
#endif

