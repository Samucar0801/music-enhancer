# Pesquisa

O que foi consultado antes de escrever código, e o que cada achado mudou.

## Bibliotecas de reprodução

**Media3 1.11.0** (julho/2026) é o estável atual e substitui o ExoPlayer
independente. Adotado. `MediaSessionService` dá controle na tela de bloqueio
e sobrevive ao app em segundo plano — uma queixa recorrente sobre players é
precisar reabrir o app depois de um tempo.

Android 14 exige `foregroundServiceType="mediaPlayback"` declarado. Android
15 limita o tempo de serviços `mediaProcessing`, o que restringe quanto o
aprimoramento offline pode durar em segundo plano.

## Separação de fontes

- **HT-Demucs** tem export ONNX público e licença MIT. É o único da lista com
  caminho verificável para Android. Cerca de 316 MB por stem em fp32, ~166 MB
  na variante de pesos fp16.
- **Mel-RoFormer / BS-RoFormer** lideram em SDR, mas são transformers pesados
  demais para celular.
- **MDX-Net** não tem export ONNX estável e público.
- **Open-Unmix** é leve, mas a qualidade fica abaixo do Demucs.

O achado que mudaria o projeto: o modelo especialista em **um stem** é ~4×
mais rápido que o de quatro stems. Para realçar voz basta o especialista
vocal; o instrumental sai por subtração.

## Reclamações reais de usuários

Estas viraram requisitos, não notas de rodapé:

- Bass boost sem limiter causa clipping audível.
- Equalizadores empilhados (app + sistema + carro) sujam o vocal.
- EQ via `AudioEffect` muitas vezes não se aplica ao Bluetooth.
- Efeitos do sistema deixam o som "robótico".
- Players param de tocar e exigem reabrir o app.

## O aparelho

O Redmi Note 14 tem pelo menos três variantes (4G com Helio G99-Ultra e 6 ou
8 GB; 5G global com Dimensity 7025-Ultra; uma variante indiana), rodando
HyperOS sobre Android 14. Por isso **nada de SoC, RAM ou codec é presumido** —
tudo é lido em tempo de execução por `DeviceProfiler`.

A variante 4G traz Dolby Atmos, o que confirma o risco de pós-processamento
empilhado junto com `com.miui.audioeffect`.

## Metadados

**MusicBrainz**: gratuito, sem chave de API, dados em CC0. Exige
`User-Agent` identificável e no máximo 1 requisição por segundo — ambos
implementados. Fica **desligado por padrão**.

## Padrões de medição

- **ITU-R BS.1770-4** para loudness (filtro K, gating absoluto em −70 LUFS e
  relativo em −10 LU) e true peak por sobreamostragem 4×.
- **EBU Tech 3341** para calibrar os medidores contra casos conhecidos.
- **EBU R128** como referência de faixa de loudness (LRA).
