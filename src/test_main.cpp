#include "spatializer.h"
#include <iostream>
#include <vector>
#include <cmath>

int main() {
    std::cout << "[Test] Iniciando prueba offline del espacializador..." << std::endl;

    const int order = 1;
    const int channels = (order + 1) * (order + 1); // 4 canales para 1er orden
    const int sample_rate = 48000;
    const int block_size = 256;
    const int num_blocks = 20;

    // Crear la instancia
    void* spat = create_spatializer(order, sample_rate, block_size);
    if (!spat) {
        std::cerr << "[Test] Error al crear el espacializador" << std::endl;
        return 1;
    }
    std::cout << "[Test] Espacializador creado con éxito." << std::endl;

    // Buffers de entrada y salida
    std::vector<float> input_buf(block_size * channels, 0.0f);
    std::vector<float> output_buf(block_size * 2, 0.0f);

    // Simular entrada: Una señal senoidal constante en el canal W (canal 0)
    for (int i = 0; i < block_size; ++i) {
        float sample = std::sin(2.0f * 3.14159f * 440.0f * i / sample_rate); // 440Hz sinewave
        input_buf[i * channels + 0] = sample; // W
        input_buf[i * channels + 1] = sample * 0.5f; // Y (direccional)
    }

    // Simular que el oyente va girando en el plano horizontal (e.g. girando a la derecha e izquierda)
    for (int b = 0; b < num_blocks; ++b) {
        // Generar un ángulo que oscila entre -90 y 90 grados
        float angle_rad = std::sin(b * 0.5f) * 1.57f;
        
        // Convertir a Quaternion en el eje Y (yaw)
        // qy = sin(yaw/2), qw = cos(yaw/2)
        float qy = std::sin(angle_rad / 2.0f);
        float qw = std::cos(angle_rad / 2.0f);

        set_listener_orientation(spat, 0.0f, qy, 0.0f, qw);

        // Procesar bloque
        process_audio_block(spat, input_buf.data(), output_buf.data());

        // Medir RMS básico del canal izquierdo y derecho para ver la respuesta
        float rms_l = 0.0f;
        float rms_r = 0.0f;
        for (int i = 0; i < block_size; ++i) {
            rms_l += output_buf[i * 2 + 0] * output_buf[i * 2 + 0];
            rms_r += output_buf[i * 2 + 1] * output_buf[i * 2 + 1];
        }
        rms_l = std::sqrt(rms_l / block_size);
        rms_r = std::sqrt(rms_r / block_size);

        std::cout << "Bloque " << b 
                  << " | Ángulo Yaw: " << (angle_rad * 180.0f / 3.14159f) << " grados"
                  << " | RMS L: " << rms_l 
                  << " | RMS R: " << rms_r 
                  << std::endl;
    }

    destroy_spatializer(spat);
    std::cout << "[Test] Prueba terminada. ¡Espacializador destruido!" << std::endl;
    return 0;
}
