package com.musicenhancer.app.ai

import android.content.Context
import com.musicenhancer.app.hardware.DeviceProfile

/**
 * ESTADO: interface e verificações prontas; inferência NÃO implementada nesta
 * versão. O app funciona 100% sem isto — a IA é um complemento opcional.
 *
 * O que a pesquisa mostrou (ver docs/RESEARCH.md):
 *  - Mel-/BS-RoFormer tem o melhor SDR hoje, mas é transformer pesado;
 *  - HT-Demucs tem export ONNX verificado e público (MIT), ~316 MB por stem
 *    em fp32 e ~166 MB na variante fp16-weights;
 *  - o especialista de UM stem é ~4x mais rápido que o pacote de 4. Como aqui
 *    só precisamos realçar a VOZ, basta o especialista de vocal: o
 *    instrumental sai por subtração (mix - vocal). Isso corta o download para
 *    ~166 MB e o custo para 1/4.
 *
 * Por que não vem embutido no APK: 166 MB dentro do APK é inviável (regra 78),
 * então fica como download sob demanda, com checksum. (Regras 53/77/79)
 */
data class ModelInfo(
    val id: String, val displayName: String, val sizeBytes: Long,
    val sha256: String, val url: String, val license: String
)

sealed interface AiStatus {
    data object NotImplemented : AiStatus
    data class Unsupported(val reason: String) : AiStatus
    data class ModelMissing(val model: ModelInfo) : AiStatus
    data object Ready : AiStatus
}

object SeparationEngine {

    /**
     * Ainda NÃO validado em hardware real. Não publicamos a URL de download
     * até medir tempo e artefatos no aparelho; prometer um recurso não testado
     * seria exatamente o tipo de invenção que este projeto evita. (Regra 102)
     */
    val plannedModel = ModelInfo(
        id = "htdemucs-vocals-fp16",
        displayName = "HT-Demucs (especialista de vocal)",
        sizeBytes = 166L * 1024 * 1024,
        sha256 = "PENDENTE — a preencher quando o modelo for fixado",
        url = "PENDENTE — ver docs/RESEARCH.md",
        license = "MIT (HT-Demucs, Rouard/Massa/Défossez, ICASSP 2023)"
    )

    fun status(ctx: Context, profile: DeviceProfile): AiStatus {
        if (!profile.canAttemptLocalAi()) {
            return AiStatus.Unsupported(
                when {
                    !profile.is64Bit -> "ABI de 32 bits: o runtime ONNX exige arm64."
                    profile.lowRamDevice -> "Aparelho marcado como low-RAM pelo Android."
                    else -> "RAM insuficiente (${profile.totalRamMb} MB; mínimo prático 6 GB)."
                }
            )
        }
        return AiStatus.NotImplemented
    }

    /** Mensagem honesta para a UI, em vez de um botão que não faz nada. */
    fun userFacingMessage(status: AiStatus): String = when (status) {
        is AiStatus.NotImplemented ->
            "Separação por IA não está implementada nesta versão. O modo " +
            "Qualidade Máxima funciona com DSP de duas passagens, que já é o " +
            "que traz a maior parte do ganho audível. O caminho para a IA está " +
            "documentado e a arquitetura já reserva o lugar dela."
        is AiStatus.Unsupported ->
            "IA local indisponível neste aparelho: ${status.reason} " +
            "Continuando com DSP de alta qualidade."
        is AiStatus.ModelMissing ->
            "Modelo ${status.model.displayName} ainda não baixado " +
            "(${status.model.sizeBytes / 1024 / 1024} MB)."
        AiStatus.Ready -> "IA local pronta."
    }
}
