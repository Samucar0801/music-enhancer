# 🎧 Music Enhancer

Player de MP3 para Android que **decide o que cada música precisa** em vez de
aplicar um preset fixo. Você aponta uma pasta, ela vira uma fila, e o motor
analisa o áudio antes de mexer nele.

Feito para rodar offline, sem conta, sem anúncio, sem telemetria e sem
serviço pago. Licença Apache-2.0.

---

## Comece por aqui

⚠️ **Este repositório não vem com o APK pronto.** Ele é gerado
automaticamente pelo GitHub Actions quando você faz push. Passo a passo em
[`BUILD.md`](BUILD.md).

O relatório honesto do que foi testado e do que **não** foi está em
[`docs/TEST_REPORT.md`](docs/TEST_REPORT.md). Leia antes de esperar
qualquer coisa do app.

---

## O que ele faz

**Uma pasta é uma playlist.** Selecione `/Músicas/Hinos/` e as 40 faixas
entram na fila em ordem natural — `hino2.mp3` vem antes de `hino10.mp3`,
como você espera, não como o alfabeto manda. A próxima toca sozinha.

**O motor mede antes de agir.** Ele pergunta "o que esta música precisa?",
não "qual preset aplicar?". Uma faixa com grave já forte recebe corte; uma
faixa magra recebe reforço. Nos testes, a mesma configuração aplicou
−5,10 dB de grave em um caso e −0,56 dB em outro.

**Ele se recusa a inventar.** Se o MP3 foi cortado em 11 kHz, não há agudo
para recuperar — o app aplica no máximo 1,5 dB e informa a limitação em vez
de fingir que restaurou o que não existe. Se a fonte já veio clipada, ele
não empilha ganho por cima.

**Ele desiste quando piora.** Depois de processar, o áudio é reanalisado. Se
o resultado ficou pior que o original, o processamento é revertido e o app
diz que reverteu.

**Seu arquivo não é tocado.** Nunca. A versão aprimorada vai para o cache do
app, em arquivo separado, com invalidação por hash e versão do DSP.

**A/B é real.** O controle deslizante mistura original e processado com a
latência compensada — em 0% a saída é o original atrasado 2,5 ms, bit-exato.
Sem isso, misturar duas cópias desalinhadas do mesmo sinal vira filtro pente.

**Bluetooth sem chute.** O codec só aparece se o Android informar. Se não
informar, a tela diz "não informado pelo Android" — e nenhuma decisão de
processamento é tomada com base nisso.

**Internet é opcional e desligada por padrão.** Se você ligar a busca de
metadados, só o nome e o artista saem daqui, para o MusicBrainz. O áudio
nunca sai do aparelho.

---

## O que ele NÃO faz

Vale mais dizer isso do que prometer.

- **Não separa vocal de instrumental por IA.** Está pesquisado e planejado
  (HT-Demucs, ONNX, MIT), mas não implementado. Um botão que não funciona
  seria pior que a ausência dele.
- **Não recupera informação que o MP3 perdeu.** Nenhum DSP faz isso.
- **Não foi testado em nenhum celular.** Veja `docs/TEST_REPORT.md`.
- **Não substitui um bom fone.** O motor limita as correções a ±6 dB de
  propósito. Quem promete transformar áudio ruim em áudio de estúdio está
  vendendo distorção.

---

## Estrutura

```
native/          núcleo DSP em C++17, sem dependências, testado no host
  include/me/    biquad, fft, loudness, analyzer, decision, chain, engine
  src/           implementação
  tests/         92 testes + CLI para processar WAV no PC
app/             camada Android (Kotlin, Compose, Media3)
  src/main/cpp/  ponte JNI
  .../dsp/       espelho Kotlin do motor
  .../playback/  AudioProcessor inserido antes do AudioSink
  .../ui/        Player, Biblioteca, Ajustes
docs/            pesquisa, arquitetura, relatório de testes
.github/         CI que roda os testes e gera o APK
```

---

## Por que não usar o equalizador do Android

O `AudioEffect` do sistema tem dois problemas conhecidos que aparecem em
reclamações reais de usuários: ele frequentemente **não se aplica à rota
Bluetooth**, e ele **empilha** sobre o processamento do fabricante (no
Redmi, `com.miui.audioeffect` e Dolby Atmos). Três equalizadores em série —
app, sistema e carro — é a receita do "vocal sujo".

Este app processa em ponto flutuante **dentro do pipeline do Media3**, antes
do `AudioSink`, e avisa na tela quando detecta efeitos do sistema ativos.

---

## Licença

Apache-2.0. Veja `LICENSE` e `THIRD_PARTY_LICENSES.md`.
