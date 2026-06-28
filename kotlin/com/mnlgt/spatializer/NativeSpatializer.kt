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
    fun processBlock(inputBuffer: FloatArray, outputBuffer: FloatArray) {
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

    // --- Declaraciones de Métodos Nativos (JNI) ---
    private external fun create(order: Int, sampleRate: Int, blockSize: Int): Long
    private external fun setOrientation(pointer: Long, qx: Float, qy: Float, qz: Float, qw: Float)
    private external fun process(pointer: Long, input: FloatArray, output: FloatArray)
    private external fun destroy(pointer: Long)

    companion object {
        private var isLoaded = false

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
