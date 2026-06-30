package com.mnlgt.spatializer

/**
 * Wrapper de JNI en Kotlin para llamar al espacializador nativo Procedural Spatializer.
 *
 * Se comunica con la librería dinámica compilada en C++ (libprocedural_spatializer.so)
 * que utiliza Spatial_Audio_Framework (SAF) con OpenBLAS para cálculo en tiempo real de armónicos esféricos.
 */
class NativeSpatializer private constructor(
    val order: Int,
    val sampleRate: Int,
    val blockSize: Int
) {
    private var nativeInstancePointer: Long = 0

    init {
        nativeInstancePointer = create(order, sampleRate, blockSize)
    }

    /**
     * Actualiza la orientación actual de la cabeza del usuario.
     * @param qx Componente X del Quaternion
     * @param qy Componente Y del Quaternion
     * @param qz Componente Z del Quaternion
     * @param qw Componente W del Quaternion
     */
    fun setOrientation(qx: Float, qy: Float, qz: Float, qw: Float) {
        if (nativeInstancePointer != 0L) {
            setOrientation(nativeInstancePointer, qx, qy, qz, qw)
        }
    }

    /**
     * Procesa un bloque de audio Ambisonics y genera audio estéreo espacializado.
     * @param inputBuffer Array plano con las muestras de entrada de todos los canales entrelazadas.
     *                    (Su tamaño debe ser de: blockSize * num_channels)
     * @param outputBuffer Array de salida estéreo plano. (Su tamaño debe ser de: blockSize * 2)
     */
    fun processBlock(inputBuffer: FloatArray?, outputBuffer: FloatArray) {
        if (nativeInstancePointer != 0L) {
            process(nativeInstancePointer, inputBuffer, outputBuffer)
        }
    }

    /**
     * Libera los recursos nativos asociados a esta instancia.
     */
    fun release() {
        if (nativeInstancePointer != 0L) {
            destroy(nativeInstancePointer)
            nativeInstancePointer = 0L
        }
    }

    protected fun finalize() {
        release()
    }

    // --- Métodos Públicos para Fuentes Dinámicas ---

    /**
     * Crea un sintetizador FM nativo y lo añade al bus de mezcla.
     * @return Puntero/handle (Long) a la instancia de FMSource.
     */
    fun addFMSource(): Long {
        if (nativeInstancePointer == 0L) return 0L
        return addFMSource(nativeInstancePointer)
    }

    /**
     * Crea un generador de ruido nativo y lo añade al bus de mezcla.
     * @return Puntero/handle (Long) a la instancia de NoiseSource.
     */
    fun addNoiseSource(): Long {
        if (nativeInstancePointer == 0L) return 0L
        return addNoiseSource(nativeInstancePointer)
    }

    /**
     * Remueve una fuente del mezclador y libera su memoria nativa.
     */
    fun removeSource(sourcePointer: Long) {
        if (nativeInstancePointer != 0L && sourcePointer != 0L) {
            removeSource(nativeInstancePointer, sourcePointer)
        }
    }

    // --- Declaraciones de Métodos Nativos (JNI) ---
    private external fun create(order: Int, sampleRate: Int, blockSize: Int): Long
    private external fun setOrientation(pointer: Long, qx: Float, qy: Float, qz: Float, qw: Float)
    private external fun process(pointer: Long, input: FloatArray?, output: FloatArray)
    private external fun destroy(pointer: Long)

    // JNI de creación de fuentes
    private external fun addFMSource(pointer: Long): Long
    private external fun addNoiseSource(pointer: Long): Long
    private external fun removeSource(pointer: Long, sourcePointer: Long)

    // JNI de control de fuentes (Públicos directamente para evitar sobrecargas conflictivas)
    external fun setSourcePosition(sourcePointer: Long, azimuth: Float, elevation: Float, distance: Float)
    external fun setSourceParameter(sourcePointer: Long, paramId: Int, value: Float)
    external fun triggerSynthNote(sourcePointer: Long, frequency: Float, velocity: Float, decayTimeMs: Float)

    companion object {
        private var isLoaded = false

        // --- Identificadores de Parámetros Nativos ---
        // FM Synth
        const val PARAM_FM_RATIO = 2
        const val PARAM_FM_INDEX = 3
        const val PARAM_FM_GATE = 4

        // Envíos de efectos del bus
        const val PARAM_DELAY_SEND = 5
        const val PARAM_REVERB_SEND = 6

        // Noise Generator
        const val PARAM_NOISE_TYPE = 10
        const val PARAM_NOISE_FILTER = 11
        const val PARAM_NOISE_CUTOFF = 12
        const val PARAM_NOISE_Q = 13
        const val PARAM_NOISE_VOLUME = 14
        const val PARAM_NOISE_GATE = 15

        // Constantes del tipo de ruido
        const val NOISE_TYPE_WHITE = 0.0f
        const val NOISE_TYPE_PINK = 1.0f

        // Constantes del filtro de ruido
        const val FILTER_NONE = 0.0f
        const val FILTER_LOWPASS = 1.0f
        const val FILTER_BANDPASS = 2.0f

        /**
         * Inicializa y carga las librerías nativas compartidas.
         */
         fun loadLibrary(): Boolean {
            if (isLoaded) return true
            return try {
                // Cargar primero OpenBLAS porque procedural_spatializer depende de él
                System.loadLibrary("openblas")
                System.loadLibrary("procedural_spatializer")
                isLoaded = true
                true
            } catch (e: UnsatisfiedLinkError) {
                android.util.Log.e("NativeSpatializer", "Error cargando librerias nativas: ${e.message}")
                false
            }
        }

        /**
         * Factory method para crear una instancia del espacializador.
         */
        fun createInstance(order: Int, sampleRate: Int, blockSize: Int): NativeSpatializer? {
            if (!loadLibrary()) return null
            val ns = NativeSpatializer(order, sampleRate, blockSize)
            return if (ns.nativeInstancePointer != 0L) ns else null
        }
    }
}
