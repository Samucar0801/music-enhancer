# Relatório de testes

Este documento separa **o que foi medido** do **que não foi**. A regra é
simples: nenhum número aqui foi estimado, arredondado a favor ou inventado.

---

## 1. O que NÃO foi testado

| Item | Situação |
|---|---|
| APK instalado no Xiaomi Redmi Note 14 | **TESTE NÃO REALIZADO** |
| Áudio real saindo por fone/Bluetooth/alto-falante | **TESTE NÃO REALIZADO** |
| Compilação do código Kotlin | **TESTE NÃO REALIZADO** — sem Android SDK no ambiente |
| Consumo de bateria e aquecimento | **TESTE NÃO REALIZADO** |
| Codec Bluetooth informado pelo Android | **TESTE NÃO REALIZADO** |
| Separação por IA (HT-Demucs) | **NÃO IMPLEMENTADA** |
| Escuta subjetiva ("ficou melhor?") | **TESTE NÃO REALIZADO** — exige ouvidos humanos |

O ambiente onde este projeto foi construído bloqueia `dl.google.com`,
`maven.google.com` e `services.gradle.org` (HTTP 403 `host_not_allowed`).
Sem Android SDK, sem Gradle e sem as dependências AndroidX, **não é possível
gerar um APK aqui**. O caminho para obter o APK está em `BUILD.md`.

---

## 2. O que FOI testado, e como

O núcleo DSP (`native/`) é C++17 sem dependências externas, então roda no
host. Foi compilado com `g++ 13.3 -O2` e executado de verdade.

Para reproduzir:

```bash
cd native
g++ -std=c++17 -O2 -Iinclude -Itests src/*.cpp tests/host_test.cpp -o metest
./metest
```

### Calibração contra padrão externo

Os medidores foram conferidos contra os casos do **EBU Tech 3341** e do
**ITU-R BS.1770-4** — não contra si mesmos.

Os coeficientes do filtro K publicados valem para 48 kHz. Redigitalizá-los
por RBJ dava erro de 0,055. A solução foi **transformada bilinear inversa**
(48 kHz → plano s → taxa alvo): erro de ida e volta em 48 kHz de 4,4e-16, e
desvio de ~2,5e-4 contra os coeficientes publicados de 44,1 kHz (<0,01 dB).

### Bugs reais encontrados por medição

Quatro, todos corrigidos e depois reconferidos:

1. **Loudness ia na direção errada.** O multibanda sem makeup, somado a um
   preGain negativo calculado em malha aberta, deixava a faixa mais baixa
   que o original. Corrigido com trim de loudness em segunda passagem.
2. **Limiter de pico contra métrica de pico real.** Picos entre amostras
   escapavam do teto. Corrigido com margem ISP + correção medida.
3. **Quality Guard revertendo processamento correto.** Normalizar pelo RMS
   falha quando o grave é removido de propósito. A referência estável é a
   banda média.
4. **Filtros sobrepostos somam.** −5,1 dB de sub mais −5,1 dB de grave dão
   −9,8 dB reais. O guard agora compara mudança real contra mudança
   **pretendida**, calculada pela resposta da cascata.

### Saída completa da suíte

```
=====================================================
 MUSIC ENHANCER - banco de testes do nucleo DSP
=====================================================

== BLOCO 1: calibracao dos medidores ==
  [ OK ] LUFS integrado (EBU 3341 caso 1) - medido -22.99 LUFS, esperado -23.00 +-0.15
  [ OK ] LUFS integrado (EBU 3341 caso 2) - medido -32.99 LUFS, esperado -33.00 +-0.15
  [ OK ] linearidade do medidor (+6 dB) - delta 6.02 LU
  [ OK ] true peak detecta pico entre amostras - sample -9.03 dBFS, true -6.10 dBTP (+2.93 dB)
  [ OK ] LUFS a 44100 Hz - medido -22.99
  [ OK ] LUFS a 48000 Hz - medido -22.99

== BLOCO 2: material musical (analise + processamento + guard) ==

caso                               LUFS in LUFS out   TP in  TP out  Cr in Cr out guard
------------------------------------------------------------------------------------------------
hino coral (voz, sem bateria)       -14.99  -14.00   -5.89   -3.65   12.0   13.8    ok
  [ OK ] [hino coral (voz, sem bateria)] true peak dentro do teto - -3.65 <= -0.80 dBTP
  [ OK ] [hino coral (voz, sem bateria)] nenhum clipping introduzido
  [ OK ] [hino coral (voz, sem bateria)] tamanho do buffer preservado
  [ OK ] [hino coral (voz, sem bateria)] saida sem NaN/Inf
  [ OK ] [hino coral (voz, sem bateria)] dinamica preservada - perda -1.80 dB
louvor banda completa               -15.28  -14.02   -3.00   -1.84   12.5   13.7    ok
  [ OK ] [louvor banda completa] true peak dentro do teto - -1.84 <= -1.00 dBTP
  [ OK ] [louvor banda completa] nenhum clipping introduzido
  [ OK ] [louvor banda completa] tamanho do buffer preservado
  [ OK ] [louvor banda completa] saida sem NaN/Inf
  [ OK ] [louvor banda completa] dinamica preservada - perda -1.17 dB
voz + violao                        -17.99  -14.00   -8.89   -3.52   12.0   14.0    ok
  [ OK ] [voz + violao] true peak dentro do teto - -3.52 <= -0.80 dBTP
  [ OK ] [voz + violao] nenhum clipping introduzido
  [ OK ] [voz + violao] tamanho do buffer preservado
  [ OK ] [voz + violao] saida sem NaN/Inf
  [ OK ] [voz + violao] dinamica preservada - perda -1.94 dB
instrumental                        -16.73  -14.00   -3.97   -2.20   12.5   11.6    ok
  [ OK ] [instrumental] true peak dentro do teto - -2.20 <= -0.80 dBTP
  [ OK ] [instrumental] nenhum clipping introduzido
  [ OK ] [instrumental] tamanho do buffer preservado
  [ OK ] [instrumental] saida sem NaN/Inf
  [ OK ] [instrumental] dinamica preservada - perda 0.91 dB
bateria forte (transientes)         -14.73  -14.00   -1.97   -2.03   12.5   11.6    ok
  [ OK ] [bateria forte (transientes)] true peak dentro do teto - -2.03 <= -1.00 dBTP
  [ OK ] [bateria forte (transientes)] nenhum clipping introduzido
  [ OK ] [bateria forte (transientes)] tamanho do buffer preservado
  [ OK ] [bateria forte (transientes)] saida sem NaN/Inf
  [ OK ] [bateria forte (transientes)] dinamica preservada - perda 0.86 dB
graves fortes                       -14.28  -14.04   -2.00   -1.94   12.5   16.2    ok
  [ OK ] [graves fortes] true peak dentro do teto - -1.94 <= -1.30 dBTP
  [ OK ] [graves fortes] nenhum clipping introduzido
  [ OK ] [graves fortes] tamanho do buffer preservado
  [ OK ] [graves fortes] saida sem NaN/Inf
  [ OK ] [graves fortes] dinamica preservada - perda -3.70 dB
mono                                -21.21  -14.64   -6.00   -1.58   12.5   12.8    ok
  [ OK ] [mono] true peak dentro do teto - -1.58 <= -1.30 dBTP
  [ OK ] [mono] nenhum clipping introduzido
  [ OK ] [mono] tamanho do buffer preservado
  [ OK ] [mono] saida sem NaN/Inf
  [ OK ] [mono] dinamica preservada - perda -0.32 dB
MP3 baixo bitrate (corte 11 kHz)    -17.35  -13.99   -5.00   -1.84   12.5   13.1   red
  [ OK ] [MP3 baixo bitrate (corte 11 kHz)] true peak dentro do teto - -1.84 <= -1.00 dBTP
  [ OK ] [MP3 baixo bitrate (corte 11 kHz)] nenhum clipping introduzido
  [ OK ] [MP3 baixo bitrate (corte 11 kHz)] tamanho do buffer preservado
  [ OK ] [MP3 baixo bitrate (corte 11 kHz)] saida sem NaN/Inf
  [ OK ] [MP3 baixo bitrate (corte 11 kHz)] dinamica preservada - perda -0.64 dB
alto bitrate (banda cheia)          -17.28  -14.01   -5.00   -1.68   12.5   13.2    ok
  [ OK ] [alto bitrate (banda cheia)] true peak dentro do teto - -1.68 <= -0.80 dBTP
  [ OK ] [alto bitrate (banda cheia)] nenhum clipping introduzido
  [ OK ] [alto bitrate (banda cheia)] tamanho do buffer preservado
  [ OK ] [alto bitrate (banda cheia)] saida sem NaN/Inf
  [ OK ] [alto bitrate (banda cheia)] dinamica preservada - perda -0.67 dB
muito baixo (-32 dBFS)              -44.28  -28.36  -32.00  -15.61   12.5   13.8    ok
  [ OK ] [muito baixo (-32 dBFS)] true peak dentro do teto - -15.61 <= -0.80 dBTP
  [ OK ] [muito baixo (-32 dBFS)] nenhum clipping introduzido
  [ OK ] [muito baixo (-32 dBFS)] tamanho do buffer preservado
  [ OK ] [muito baixo (-32 dBFS)] saida sem NaN/Inf
  [ OK ] [muito baixo (-32 dBFS)] dinamica preservada - perda -1.27 dB
muito alto (topo)                   -12.48  -14.01   -0.20   -1.68   12.5   13.2    ok
  [ OK ] [muito alto (topo)] true peak dentro do teto - -1.68 <= -0.80 dBTP
  [ OK ] [muito alto (topo)] nenhum clipping introduzido
  [ OK ] [muito alto (topo)] tamanho do buffer preservado
  [ OK ] [muito alto (topo)] saida sem NaN/Inf
  [ OK ] [muito alto (topo)] dinamica preservada - perda -0.67 dB
com clipping na fonte                -9.29  -14.00    0.51   -2.10    9.5   12.9    ok
  [ OK ] [com clipping na fonte] true peak dentro do teto - -2.10 <= -0.80 dBTP
  [ OK ] [com clipping na fonte] nenhum clipping introduzido
  [ OK ] [com clipping na fonte] tamanho do buffer preservado
  [ OK ] [com clipping na fonte] saida sem NaN/Inf
  [ OK ] [com clipping na fonte] dinamica preservada - perda -3.38 dB
48 kHz                              -17.32  -14.02   -5.00   -1.57   12.5   13.2    ok
  [ OK ] [48 kHz] true peak dentro do teto - -1.57 <= -0.80 dBTP
  [ OK ] [48 kHz] nenhum clipping introduzido
  [ OK ] [48 kHz] tamanho do buffer preservado
  [ OK ] [48 kHz] saida sem NaN/Inf
  [ OK ] [48 kHz] dinamica preservada - perda -0.68 dB

== BLOCO 3: o motor e adaptativo (nao aplica preset cego) ==
  [ OK ] grave ja forte recebe MENOS reforco que grave fraco - fraco -0.56 dB vs forte -5.10 dB
  [ OK ] alto-falante usa high-pass mais alto que fone - 150.00 Hz vs 28.00 Hz
  [ OK ] alto-falante usa exciter harmonico em vez de amplificar 60 Hz
  [ OK ] fonte com clipping nao recebe reforco agressivo de EQ
  [ OK ] banda cortada: agudo nao e inventado - 1.50 dB (teto 1.5)
  [ OK ] fonte ja esmagada nao recebe compressao adicional
  [ OK ] nenhum ganho de EQ excede +-6 dB em nenhuma rota - maior magnitude 6.00 dB

== BLOCO 4: Quality Guard, bypass e robustez ==
  [ OK ] pedido extremo de volume nao estoura o teto - -4.00 dBTP
  [ OK ] saida finita sob pedido extremo
  [ OK ] guard ligado nunca piora o true peak
  [ OK ] bypass e bit-transparente - delta max 0.00
  [ OK ] buffer vazio e rejeitado sem crash
  [ OK ] silencio total nao gera NaN nem ganho infinito
  [ OK ] buffer com NaN nao trava a analise - LUFS finito
  [ OK ] alternar bypass em tempo real nao gera estouro - maior salto entre amostras 0.36
  [ OK ] tempo real: saida finita
  [ OK ] mix=0 devolve o original atrasado pela latencia (sem filtro pente) - atraso 110 amostras, delta max 0.00
  [ OK ] mix=0.5 e o crossfade linear exato de original e aprimorado - desvio max 0.00
  [ OK ] mistura A/B nao cancela o sinal - rms orig 0.14 | mix 0.5 0.14 | aprim 0.14

== BLOCO 5: custo de processamento (host, NAO e o Redmi) ==
  tempo real : 0.421 s de CPU para 60 s de audio = 142x realtime
  [ OK ] DSP em tempo real cabe com folga - 142.44x realtime no host
  offline    : 2.973 s para 60 s de audio = 20.2x realtime (1 passagem(ns) do guard)
  latencia   : 110 amostras (2.5 ms)
  [ OK ] latencia do limiter aceitavel

=====================================================
 RESULTADO: 92 passaram, 0 falharam
=====================================================
```

---

## 3. Custo de processamento

Os números de velocidade acima foram medidos **no host de compilação, que
não é o Redmi Note 14**. Um celular é mais lento. Quanto mais lento, não sei
— não medi. Rode a suíte no aparelho via `BUILD.md` para ter o número real.

O que é independente de hardware: a **latência** de 110 amostras (2,5 ms em
44,1 kHz) é uma propriedade do lookahead do limiter, não da CPU.
