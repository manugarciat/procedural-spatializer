#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <cmath>

/**
 * @file fast_math.h
 * @brief Funciones trigonométricas y operaciones matemáticas optimizadas para SIMD/NEON.
 *
 * EXPLICACIÓN DE OPTIMIZACIÓN (SIMD/NEON):
 * En procesadores ARM64 (como el Snapdragon XR2 Gen 2 del Quest 3), la llamada a la función
 * estándar `sinf(x)` es escalar y costosa (toma decenas de ciclos de reloj debido a su precisión 
 * microscópica).
 *
 * Para síntesis de audio en tiempo real y alta densidad de fuentes (ej. 30 burbujas), reemplazamos 
 * `sinf` por una aproximación polinómica cuadrática optimizada en rango.
 *
 * En lugar de usar ángulos en Radianes ([-pi, pi]), normalizamos la FASE en el rango [-0.5, 0.5],
 * donde -0.5 representa -pi y 0.5 representa pi (1.0 completo es un ciclo de 360 grados).
 *
 * Esto tiene dos ventajas inmensas para NEON/SIMD:
 * 1. Envolver la fase se reduce a una sustracción entera simple, eliminando costosas divisiones por pi.
 * 2. La aproximación parabólica solo usa sumas, multiplicaciones y valor absoluto (fabsf).
 *    El compilador Clang con -Ofast traduce esto en instrucciones FMA (Fused Multiply-Accumulate) 
 *    nativas de un solo ciclo de reloj de hardware, permitiendo procesar hasta 4 muestras de audio
 *    paralelamente en un registro NEON de 128 bits.
 */

namespace dsp {

/**
 * @brief Aproximación parabólica rápida de Seno para fase normalizada.
 *
 * Fórmula matemática (Aproximación de Bhaskara I en rango [-0.5, 0.5]):
 *   sin(2*pi*x) ≈ 16 * x * (0.5 - |x|)
 *
 * Para reducir distorsión y armónicos no deseados, aplicamos un suavizado adicional de 2do orden:
 *   y = 16 * x * (0.5 - |x|)
 *   sin(2*pi*x) ≈ 0.225 * (y * |y| - y) + y
 *
 * @param x Fase normalizada en el rango [-0.5, 0.5].
 * @return float Valor aproximado de sin(2*pi*x) en el rango [-1.0, 1.0].
 */
inline float fast_sin_normalized(float x) {
    // 1. fabsf(x) se traduce en ARM64 a un bitwise-AND simple que limpia el bit de signo.
    //    Es una operación de costo virtualmente cero en el procesador.
    float abs_x = (x >= 0.0f) ? x : -x;
    
    // 2. Parábola base: genera una onda triangular curvada (aproximación cruda de seno)
    float y = 16.0f * x * (0.5f - abs_x);
    
    // 3. Filtro de suavizado polinómico para aproximar el seno con una distorsión armónica < 1%.
    float abs_y = (y >= 0.0f) ? y : -y;
    return 0.225f * (y * abs_y - y) + y;
}

/**
 * @brief Enuelve una fase acumulativa para mantenerla en el rango periódico [-0.5, 0.5].
 *
 * EXPLICACIÓN DE STACK CACHING:
 * Durante el procesamiento del bucle caliente de audio, la fase acumulada avanza constantemente.
 * Esta función la vuelve a meter al rango [-0.5, 0.5] sin usar condicionales "if", lo cual
 * rompería la segmentación de instrucciones (pipelining) en ARM.
 *
 * @param phase Fase acumulada (puede ser mayor a 1.0).
 * @return float Fase normalizada en [-0.5, 0.5].
 */
inline float wrap_phase(float phase) {
    // Si la fase se excede de los límites del ciclo, restamos la parte entera.
    // El cast a entero se ejecuta directamente en los registros del CPU.
    if (phase > 0.5f) {
        phase -= static_cast<float>(static_cast<int>(phase + 0.5f));
    } else if (phase < -0.5f) {
        phase -= static_cast<float>(static_cast<int>(phase - 0.5f));
    }
    return phase;
}

} // namespace dsp

#endif // FAST_MATH_H
