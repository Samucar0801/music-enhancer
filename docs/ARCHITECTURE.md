# Arquitetura

## Onde o processamento acontece

```
MP3 no cartão
   └─ MediaExtractor + MediaCodec (decodificação do sistema)
        └─ ExoPlayer / Media3
             └─ DefaultAudioSink
                  ├─ EnhancerAudioProcessor   ← nosso DSP, float PCM
                  └─ AudioTrack → rota de saída
```

O processamento fica **dentro** do pipeline do Media3, antes do `AudioSink`,
em ponto flutuante. Não é um `AudioEffect` do sistema.

### Por que não `AudioEffect`

| | `AudioEffect` do Android | Este app |
|---|---|---|
| Bluetooth | frequentemente não se aplica à rota A2DP | aplica, é o mesmo buffer |
| Precisão | 16 bits inteiros | float 32 bits |
| Empilhamento | soma sobre o efeito do fabricante | é o único estágio do app |
| Controle | ganhos fixos por banda | decisão medida por faixa |
| Portabilidade | varia por fabricante | idêntico em qualquer aparelho |

O empilhamento é o problema mais reportado por usuários: equalizador do app
+ Dolby Atmos do MIUI + DSP do rádio do carro em série produz distorção e
vocal embolado. Este app não pode desligar os outros dois, mas **avisa
quando os detecta** e não adiciona um quarto.

---

## Comparação das abordagens consideradas

| Abordagem | Qualidade | Custo | Roda no celular | Escolha |
|---|---|---|---|---|
| EQ tradicional com preset fixo | baixa | ~0 | sim | rejeitada: ignora o material |
| EQ adaptativo + medição BS.1770 | média-alta | baixo | sim | **adotada** |
| Demucs / HT-Demucs (separação) | alta | muito alto | talvez | planejada, não implementada |
| MDX-Net | alta | alto | improvável | rejeitada: sem export ONNX estável |
| Open-Unmix | média | médio | sim | rejeitada: qualidade abaixo do Demucs |
| Mel-/BS-RoFormer | mais alta | proibitivo | não | rejeitada: transformer pesado |

A separação por IA foi pesquisada a fundo. O achado útil: o especialista de
**um único stem** é cerca de 4× mais rápido que o modelo completo. Como só
interessa realçar a **voz**, bastaria o especialista vocal (~166 MB em
fp16), e o instrumental sairia por subtração (mix − vocal). Isso corta o
download de ~316 MB por stem e o custo para um quarto. Continua não
implementado, e o motivo está em `docs/TEST_REPORT.md`.

---

## O motor de decisão

O fluxo não é "qual preset?" e sim "o que falta e o que sobra?".

```
analyze()            → 28 medidas: LUFS, true peak, crista, 6 bandas,
                       centroide, flatness, corte de banda do MP3,
                       correlação estéreo, clipping, BPM, prob. de voz
        ↓
decide()             → alvos por rota (fone ≠ alto-falante ≠ carro),
                       correções limitadas a ±6 dB
        ↓
Chain                → EQ, realce vocal, exciter, de-esser, multibanda,
                       transientes, M/S, bass-mono, limiter (lookahead)
        ↓
reanálise            → mediu de novo o resultado real
        ↓
Quality Guard        → mudança real vs. mudança PRETENDIDA.
                       Fora da tolerância → reverte com cadeia de segurança
```

Decisões que valem registrar:

- **No alto-falante, grave não é amplificado.** Um alto-falante de celular
  não reproduz 60 Hz; empurrar energia lá só consome headroom e distorce. O
  motor aplica high-pass em 150 Hz e usa um exciter harmônico, que gera as
  harmônicas superiores e deixa o ouvido inferir o fundamental.
- **Correção limitada a ±6 dB.** Não é timidez: acima disso o resultado passa
  a soar processado, e a chance de estourar o limiter cresce.
- **Duas passagens de loudness.** A primeira estima, a segunda mede o
  resultado e corrige. Malha fechada, porque a malha aberta errou nos testes.
- **Margem de pico entre amostras.** O limiter age no pico amostrado, mas a
  métrica que importa é o true peak. A margem padrão é 0,8 dB, corrigida por
  medição no caminho offline.

---

## Cache

Chave: `uri | tamanho | data de modificação`, mais as versões de app, DSP,
análise e modelo de IA. Se qualquer uma mudar, o cache é invalidado — nunca
se reaproveita áudio processado por uma versão diferente do motor.

Gravação atômica (`.tmp` → `rename`), então uma interrupção não deixa
arquivo pela metade.

**O MP3 original nunca é aberto para escrita.** Em nenhum caminho do código.
