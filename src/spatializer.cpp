#include "spatializer.h"
#include "dsp/synth_fm.h"
#include "dsp/noise.h"
#include "dsp/effects.h"

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <mutex>

#ifdef USE_SAF
// Incluir cabeceras de Spatial_Audio_Framework
#include "saf.h"
#include "../external/Spatial_Audio_Framework/examples/include/ambi_bin.h"
#include "../external/Spatial_Audio_Framework/examples/include/ambi_enc.h"
extern "C" void openblas_set_num_threads(int num_threads);
#endif

// =========================================================================
// CLASE BASE POLIMÓRFICA PARA FUENTES SONORAS (SoundSource)
// =========================================================================
// Permite que el espacializador maneje múltiples tipos de generadores (FM, Ruido)
// de forma genérica en una sola lista, desacoplando el mezclador del DSP concreto.
// =========================================================================
class SoundSource {
public:
    virtual ~SoundSource() {
#ifdef USE_SAF
        if (hEncoder) {
            ambi_enc_destroy(&hEncoder);
        }
#endif
    }

    // Método virtual puro que cada sintetizador/generador debe implementar
    virtual void process_audio(float* buffer, int num_samples) = 0;
    virtual void set_parameter(int param_id, float value) = 0;

    void set_position(float az, float el, float dist) {
        azimuth = az;
        elevation = el;
        distance = dist;
#ifdef USE_SAF
        if (hEncoder) {
            // Indicar a SAF la posición esférica de esta fuente
            ambi_enc_setSourceAzi_deg(hEncoder, 0, azimuth);
            ambi_enc_setSourceElev_deg(hEncoder, 0, elevation);
        }
#endif
    }

    void set_effect_sends(float delay, float reverb) {
        delay_send = delay;
        reverb_send = reverb;
    }

    // Inicializa el codificador Ambisonic de SAF para esta fuente mono
    void init_encoder(int order, int sample_rate) {
#ifdef USE_SAF
        ambi_enc_create(&hEncoder);
        
        // Configurar el orden del Ambisonics de salida y la cantidad de fuentes
        ambi_enc_setOutputOrder(hEncoder, order);
        ambi_enc_setNumSources(hEncoder, 1);
        
        ambi_enc_init(hEncoder, sample_rate);
        
        // Configurar la posición inicial en el encoder
        set_position(azimuth, elevation, distance);
#else
        (void)order; (void)sample_rate;
#endif
    }

    // Variables de posicionamiento 3D
    float azimuth = 0.0f;
    float elevation = 0.0f;
    float distance = 1.0f;

    // Envíos auxiliares
    float delay_send = 0.0f;
    float reverb_send = 0.0f;

#ifdef USE_SAF
    void* hEncoder = nullptr;
#endif
};

// --- Especialización: Sintetizador FM ---
class FMSource : public SoundSource {
public:
    FMSource(float sample_rate) : synth(sample_rate) {}
    
    void process_audio(float* buffer, int num_samples) override {
        synth.process(buffer, num_samples);
    }
    
    void set_parameter(int param_id, float value) override {
        switch (param_id) {
            case 2: synth.set_mod_ratio(value); break;
            case 3: synth.set_mod_index(value); break;
            case 4: synth.set_active(value > 0.5f); break;
        }
    }
    
    dsp::FMSynth synth;
};

// --- Especialización: Generador de Ruido ---
class NoiseSource : public SoundSource {
public:
    NoiseSource(float sample_rate) : noise(sample_rate) {}
    
    void process_audio(float* buffer, int num_samples) override {
        noise.process(buffer, num_samples);
    }
    
    void set_parameter(int param_id, float value) override {
        switch (param_id) {
            case 10: noise.set_noise_type(static_cast<dsp::NoiseType>(static_cast<int>(value))); break;
            case 11: noise.set_filter_type(static_cast<dsp::FilterType>(static_cast<int>(value))); break;
            case 12: noise.set_cutoff(value); break;
            case 13: noise.set_q(value); break;
            case 14: noise.set_volume(value); break;
            case 15: noise.set_active(value > 0.5f); break;
        }
    }
    
    dsp::NoiseGenerator noise;
};


// =========================================================================
// ESTRUCTURA PRINCIPAL DEL ESPACIALIZADOR (SpatializerInstance)
// =========================================================================
struct SpatializerInstance {
    int order;
    int channels;
    int sample_rate;
    int block_size;
    
    // Lista de fuentes dinámicas activas y mutex para thread-safety
    std::vector<SoundSource*> sources;
    std::mutex sources_mutex;

    // Procesadores de efectos globales del bus
    dsp::DelayLine global_delay;
    dsp::ReverbEffect global_reverb;
    
    // Quaternion de rotación
    float qx = 0.0f; float qy = 0.0f; float qz = 0.0f; float qw = 1.0f;

#ifdef USE_SAF
    void* hAmbi = nullptr;
    // Buffers planos requeridos por SAF
    std::vector<float*> input_channels;
    std::vector<float*> output_channels;
    std::vector<float> input_flat_buffers;
    std::vector<float> output_flat_buffers;
    
    // Buffers temporales de mezcla
    std::vector<float> mono_source_buffer; // Contiene audio temporal de la burbuja (128 samples)
    std::vector<float> enc_flat_buffer;    // Contiene el audio codificado de la burbuja a Ambisonics (128 * ch)
    std::vector<float*> enc_channels;      // Punteros a los canales codificados
#endif

    SpatializerInstance(int o, int rate, int size)
        : order(o), sample_rate(rate), block_size(size),
          global_delay(static_cast<float>(rate)),
          global_reverb(static_cast<float>(rate)) {
        channels = (order + 1) * (order + 1);

#ifdef USE_SAF
        // 1. Crear instancia de SAF ambi_bin (Binauralizador)
        ambi_bin_create(&hAmbi);
        
        SH_ORDERS saf_order = SH_ORDER_FIRST;
        if (order == 2) saf_order = SH_ORDER_SECOND;
        else if (order == 3) saf_order = SH_ORDER_THIRD;
        else if (order > 3) saf_order = SH_ORDER_FOURTH;
        
        ambi_bin_setInputOrderPreset(hAmbi, saf_order);
        ambi_bin_setEnableRotation(hAmbi, SAF_TRUE);
        ambi_bin_setDecodingMethod(hAmbi, DECODING_METHOD_LSDIFFEQ);
        
        // Desactivar hilos internos redundantes en OpenBLAS
        openblas_set_num_threads(1);

        // Inicialización crítica: INIT primero, INITCODEC después
        ambi_bin_init(hAmbi, sample_rate);
        ambi_bin_initCodec(hAmbi);

        // Reserva de buffers fijos de 128 muestras para procesamiento de SAF
        const int SAF_FRAME_SIZE = 128;
        input_channels.resize(channels);
        input_flat_buffers.resize(channels * SAF_FRAME_SIZE, 0.0f);
        for (int c = 0; c < channels; ++c) {
            input_channels[c] = &input_flat_buffers[c * SAF_FRAME_SIZE];
        }

        output_channels.resize(2);
        output_flat_buffers.resize(2 * SAF_FRAME_SIZE, 0.0f);
        output_channels[0] = &output_flat_buffers[0 * SAF_FRAME_SIZE];
        output_channels[1] = &output_flat_buffers[1 * SAF_FRAME_SIZE];

        // Reservar memoria para mezcla dinámica de fuentes
        mono_source_buffer.resize(SAF_FRAME_SIZE, 0.0f);
        enc_flat_buffer.resize(channels * SAF_FRAME_SIZE, 0.0f);
        enc_channels.resize(channels);
        for (int c = 0; c < channels; ++c) {
            enc_channels[c] = &enc_flat_buffer[c * SAF_FRAME_SIZE];
        }
#endif
        // Configuración inicial de efectos globales
        global_delay.set_feedback(0.35f);
        global_delay.set_damping(0.4f);
        global_reverb.set_room_size(0.6f);
        global_reverb.set_mix(0.18f); // 18% Reverb por defecto
    }

    ~SpatializerInstance() {
        // Bloquear mutex y destruir fuentes activas
        std::lock_guard<std::mutex> lock(sources_mutex);
        for (auto* s : sources) {
            delete s;
        }
        sources.clear();

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

static void quaternion_to_euler(float qx, float qy, float qz, float qw, float& yaw, float& pitch, float& roll) {
    double siny_cosp = 2.0 * (qw * qz + qx * qy);
    double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    yaw = static_cast<float>(std::atan2(siny_cosp, cosy_cosp) * (180.0 / 3.141592653589793));

    double sinp = 2.0 * (qw * qy - qz * qx);
    if (std::abs(sinp) >= 1.0)
        pitch = static_cast<float>(std::copysign(3.141592653589793 / 2.0, sinp) * (180.0 / 3.141592653589793));
    else
        pitch = static_cast<float>(std::asin(sinp) * (180.0 / 3.141592653589793));

    double sinr_cosp = 2.0 * (qw * qx + qy * qz);
    double cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy);
    roll = static_cast<float>(std::atan2(sinr_cosp, cosr_cosp) * (180.0 / 3.141592653589793));
}

SPAT_API void set_listener_orientation(void* instance, float x, float y, float z, float w) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat) return;
    
    spat->qx = x; spat->qy = y; spat->qz = z; spat->qw = w;

#ifdef USE_SAF
    if (spat->hAmbi) {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        quaternion_to_euler(x, y, z, w, yaw, pitch, roll);
        
        // Rotación contraria al movimiento de cabeza
        ambi_bin_setYaw(spat->hAmbi, -yaw);
        ambi_bin_setPitch(spat->hAmbi, -pitch);
        ambi_bin_setRoll(spat->hAmbi, -roll);
    }
#endif
}

SPAT_API void process_audio_block(void* instance, const float* input_buffer, float* output_buffer) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat || !output_buffer) return;

#ifdef USE_SAF
    if (spat->hAmbi) {
        int num_ch = spat->channels;
        int size = spat->block_size;
        const int SAF_FRAME_SIZE = 128; // Tamaño STFT de SAF

        // Buffers para envíos de efectos globales (estéreo pre-binaural o post)
        std::vector<float> delay_input_l(SAF_FRAME_SIZE, 0.0f);
        std::vector<float> delay_input_r(SAF_FRAME_SIZE, 0.0f);

        for (int offset = 0; offset < size; offset += SAF_FRAME_SIZE) {
            int current_chunk = std::min(SAF_FRAME_SIZE, size - offset);
            
            // 1. Limpiar el bus Ambisonic compartido a cero para este sub-bloque
            std::memset(spat->input_flat_buffers.data(), 0, num_ch * SAF_FRAME_SIZE * sizeof(float));
            std::memset(delay_input_l.data(), 0, SAF_FRAME_SIZE * sizeof(float));
            std::memset(delay_input_r.data(), 0, SAF_FRAME_SIZE * sizeof(float));

            // 2. Si hay un buffer de WAV entrante (reproducción pasiva), lo copiamos al bus
            if (input_buffer) {
                for (int i = 0; i < current_chunk; ++i) {
                    for (int c = 0; c < num_ch; ++c) {
                        spat->input_flat_buffers[c * SAF_FRAME_SIZE + i] = input_buffer[(offset + i) * num_ch + c];
                    }
                }
            }

            // 3. MEZCLAR FUENTES DINÁMICAS (Burbujas, Sintetizadores procedimentales)
            //    Bloqueamos la lista de fuentes con mutex por si Kotlin está agregando/quitando fuentes
            {
                std::lock_guard<std::mutex> lock(spat->sources_mutex);
                
                for (auto* source : spat->sources) {
                    // Generar bloque mono de esta fuente
                    source->process_audio(spat->mono_source_buffer.data(), current_chunk);
                    
                    // Rellenar con ceros si el chunk final es menor a 128 (requisito de SAF)
                    if (current_chunk < SAF_FRAME_SIZE) {
                        std::memset(spat->mono_source_buffer.data() + current_chunk, 0, (SAF_FRAME_SIZE - current_chunk) * sizeof(float));
                    }

                    // Codificar la señal mono a Ambisonics (1, 4, 9 o 16 canales según el orden)
                    float* mono_ptr = spat->mono_source_buffer.data();
                    ambi_enc_process(source->hEncoder, &mono_ptr, spat->enc_channels.data(), 1, num_ch, SAF_FRAME_SIZE);

                    // =========================================================================
                    // OPTIMIZACIÓN SIMD (SUMA VECTORIAL AL BUS COMPARTIDO)
                    // =========================================================================
                    // Sumamos el resultado del codificador de esta fuente al bus de mezcla global.
                    // El compilador vectoriza este loop lineal utilizando instrucciones NEON.
                    // =========================================================================
                    float* global_bus = spat->input_flat_buffers.data();
                    float* source_bus = spat->enc_flat_buffer.data();
                    int total_samples = num_ch * SAF_FRAME_SIZE;
                    for (int s = 0; s < total_samples; ++s) {
                        global_bus[s] += source_bus[s];
                    }

                    // Acumular envíos a efectos (usando el canal W de Ambisonics como fuente mono del efecto)
                    float d_send = source->delay_send;
                    float r_send = source->reverb_send;
                    if (d_send > 0.0f || r_send > 0.0f) {
                        for (int i = 0; i < current_chunk; ++i) {
                            float mono_val = spat->mono_source_buffer[i];
                            // Atenuación simple por distancia
                            float att = 1.0f / (source->distance + 0.001f);
                            delay_input_l[i] += mono_val * d_send * att;
                            delay_input_r[i] += mono_val * r_send * att; 
                        }
                    }
                }
            }

            // 4. DECODIFICACIÓN BINAURAL (1 sola vez para todo el bus mixto)
            ambi_bin_process(spat->hAmbi, spat->input_channels.data(), spat->output_channels.data(),
                             num_ch, 2, SAF_FRAME_SIZE);

            // 5. PROCESAR EFECTOS GLOBALES (Delay & Reverb) EN LA SALIDA ESTÉREO
            //    Esto le da tridimensionalidad y colas acústicas a la mezcla.
            for (int i = 0; i < current_chunk; ++i) {
                // Procesar Delay
                float delay_out = spat->global_delay.process(delay_input_l[i]);
                
                // Sumar Delay a la salida binauralizada estéreo
                spat->output_flat_buffers[0 * SAF_FRAME_SIZE + i] += delay_out;
                spat->output_flat_buffers[1 * SAF_FRAME_SIZE + i] += delay_out;
            }

            // Procesar Reverb en estéreo (in-place)
            spat->global_reverb.process_stereo(
                spat->output_channels[0],
                spat->output_channels[1],
                current_chunk
            );

            // 6. Entrelazar y escribir los datos del sub-bloque estéreo al output final
            for (int i = 0; i < current_chunk; ++i) {
                output_buffer[(offset + i) * 2 + 0] = spat->output_flat_buffers[0 * SAF_FRAME_SIZE + i]; // Left
                output_buffer[(offset + i) * 2 + 1] = spat->output_flat_buffers[1 * SAF_FRAME_SIZE + i]; // Right
            }
        }
        return;
    }
#endif

    // Fallback Modo Dummy (si no se enlaza SAF)
    int num_ch = spat->channels;
    std::memset(output_buffer, 0, spat->block_size * 2 * sizeof(float));
    if (input_buffer) {
        for (int i = 0; i < spat->block_size; ++i) {
            float w_channel = input_buffer[i * num_ch + 0];
            float pan = 0.5f + (spat->qy * 0.5f);
            pan = std::max(0.0f, std::min(1.0f, pan));
            output_buffer[i * 2 + 0] = w_channel * std::sqrt(1.0f - pan);
            output_buffer[i * 2 + 1] = w_channel * std::sqrt(pan);
        }
    }
}

SPAT_API void destroy_spatializer(void* instance) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (spat) {
        delete spat;
    }
}

// =========================================================================
// IMPLEMENTACIÓN DE LA API DE FUENTES DINÁMICAS (C++)
// =========================================================================

SPAT_API void* add_fm_source(void* instance) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat) return nullptr;

    auto* source = new FMSource(static_cast<float>(spat->sample_rate));
    source->init_encoder(spat->order, spat->sample_rate);

    std::lock_guard<std::mutex> lock(spat->sources_mutex);
    spat->sources.push_back(source);
    return source;
}

SPAT_API void* add_noise_source(void* instance) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    if (!spat) return nullptr;

    auto* source = new NoiseSource(static_cast<float>(spat->sample_rate));
    source->init_encoder(spat->order, spat->sample_rate);

    std::lock_guard<std::mutex> lock(spat->sources_mutex);
    spat->sources.push_back(source);
    return source;
}

SPAT_API void remove_source(void* instance, void* source_ptr) {
    auto* spat = static_cast<SpatializerInstance*>(instance);
    auto* source = static_cast<SoundSource*>(source_ptr);
    if (!spat || !source) return;

    std::lock_guard<std::mutex> lock(spat->sources_mutex);
    auto it = std::find(spat->sources.begin(), spat->sources.end(), source);
    if (it != spat->sources.end()) {
        spat->sources.erase(it);
        delete source;
    }
}

SPAT_API void set_source_position(void* source_ptr, float azimuth_deg, float elevation_deg, float distance) {
    auto* source = static_cast<SoundSource*>(source_ptr);
    if (source) {
        source->set_position(azimuth_deg, elevation_deg, distance);
    }
}

SPAT_API void set_source_parameter(void* source_ptr, int param_id, float value) {
    auto* source = static_cast<SoundSource*>(source_ptr);
    if (!source) return;
    
    // Parámetros comunes de efectos se interceptan aquí
    if (param_id == 5) {
        source->set_effect_sends(value, source->reverb_send);
    } else if (param_id == 6) {
        source->set_effect_sends(source->delay_send, value);
    } else {
        // Pasar el parámetro al DSP concreto (FM o Ruido)
        source->set_parameter(param_id, value);
    }
}

SPAT_API void trigger_synth_note(void* source_ptr, float frequency, float velocity, float decay_time_ms) {
    auto* source = static_cast<SoundSource*>(source_ptr);
    if (!source) return;
    
    // Verificar dinámicamente si la fuente es un sintetizador FM
    auto* fm_src = dynamic_cast<FMSource*>(source);
    if (fm_src) {
        fm_src->synth.trigger(frequency, velocity, decay_time_ms);
    }
}

} // extern "C"


// =========================================================================
// WRAPPERS DE ENLACE JNI (JAVA NATIVE INTERFACE) PARA ANDROID
// =========================================================================
#ifdef __ANDROID__
#include <jni.h>

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_create(JNIEnv *env, jobject thiz, jint order, jint sample_rate, jint block_size) {
    (void)env; (void)thiz;
    return reinterpret_cast<jlong>(create_spatializer(order, sample_rate, block_size));
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_setOrientation(JNIEnv *env, jobject thiz, jlong pointer, jfloat qx, jfloat qy, jfloat qz, jfloat qw) {
    (void)env; (void)thiz;
    set_listener_orientation(reinterpret_cast<void*>(pointer), qx, qy, qz, qw);
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_process(JNIEnv *env, jobject thiz, jlong pointer, jfloatArray input, jfloatArray output) {
    (void)thiz;
    auto* spat = reinterpret_cast<SpatializerInstance*>(pointer);
    if (!spat) return;

    jfloat* input_data = nullptr;
    if (input) {
        input_data = env->GetFloatArrayElements(input, nullptr);
    }
    jfloat* output_data = env->GetFloatArrayElements(output, nullptr);

    if (output_data) {
        process_audio_block(spat, input_data, output_data);
    }

    if (input && input_data) {
        env->ReleaseFloatArrayElements(input, input_data, JNI_ABORT); // read-only
    }
    if (output_data) {
        env->ReleaseFloatArrayElements(output, output_data, 0);      // write-back
    }
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_destroy(JNIEnv *env, jobject thiz, jlong pointer) {
    (void)env; (void)thiz;
    destroy_spatializer(reinterpret_cast<void*>(pointer));
}

// --- Métodos JNI de Gestión de Fuentes Dinámicas ---

JNIEXPORT jlong JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_addFMSource(JNIEnv *env, jobject thiz, jlong pointer) {
    (void)env; (void)thiz;
    return reinterpret_cast<jlong>(add_fm_source(reinterpret_cast<void*>(pointer)));
}

JNIEXPORT jlong JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_addNoiseSource(JNIEnv *env, jobject thiz, jlong pointer) {
    (void)env; (void)thiz;
    return reinterpret_cast<jlong>(add_noise_source(reinterpret_cast<void*>(pointer)));
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_removeSource(JNIEnv *env, jobject thiz, jlong pointer, jlong sourcePointer) {
    (void)env; (void)thiz;
    remove_source(reinterpret_cast<void*>(pointer), reinterpret_cast<void*>(sourcePointer));
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_setSourcePosition(JNIEnv *env, jobject thiz, jlong sourcePointer, jfloat azimuth, jfloat elevation, jfloat distance) {
    (void)env; (void)thiz;
    set_source_position(reinterpret_cast<void*>(sourcePointer), azimuth, elevation, distance);
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_setSourceParameter(JNIEnv *env, jobject thiz, jlong sourcePointer, jint paramId, jfloat value) {
    (void)env; (void)thiz;
    set_source_parameter(reinterpret_cast<void*>(sourcePointer), paramId, value);
}

JNIEXPORT void JNICALL
Java_com_mnlgt_spatializer_NativeSpatializer_triggerSynthNote(JNIEnv *env, jobject thiz, jlong sourcePointer, jfloat frequency, jfloat velocity, jfloat decayTimeMs) {
    (void)env; (void)thiz;
    trigger_synth_note(reinterpret_cast<void*>(sourcePointer), frequency, velocity, decayTimeMs);
}

} // extern "C"
#endif
