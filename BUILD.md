# Como obter o APK

O APK **não** está neste repositório. Escolha um dos caminhos.

---

## Caminho 1 — GitHub Actions (recomendado, não instala nada)

1. Crie um repositório no GitHub e envie estes arquivos:
   ```bash
   git init
   git add .
   git commit -m "Music Enhancer"
   git branch -M main
   git remote add origin https://github.com/SEU_USUARIO/music-enhancer.git
   git push -u origin main
   ```
2. Abra a aba **Actions** do repositório. O workflow `build` roda sozinho.
3. Ele faz duas coisas, nesta ordem:
   - compila e roda os **92 testes do núcleo DSP** (se algum falhar, o build
     para aqui de propósito);
   - monta o APK de release.
4. Terminado, baixe em **Artifacts → MusicEnhancer-APK**.

Leva por volta de 8 a 12 minutos na primeira vez, porque o NDK é baixado.

### Instalar no celular

O APK é assinado com a chave de debug, então instala direto. No Redmi:
Ajustes → Aplicativos → Acesso especial → Instalar apps desconhecidos →
autorize o navegador ou gerenciador de arquivos. O HyperOS mostra um aviso
de segurança; é esperado para app fora da loja.

---

## Caminho 2 — Android Studio

Requer Android Studio Ladybug ou mais novo, JDK 17 e o NDK
(`27.0.12077973`, instalável pelo SDK Manager).

```bash
./gradlew assembleRelease
# saída: app/build/outputs/apk/release/
```

Se o `gradlew` não existir no repositório:

```bash
gradle wrapper --gradle-version 8.11.1
```

---

## Caminho 3 — Testar o DSP no PC, sem Android

Este é o caminho para **ouvir e conferir os números** sem celular nenhum.
Só precisa de `g++`.

```bash
cd native
g++ -std=c++17 -O2 -Iinclude -Itests src/*.cpp tests/host_test.cpp -o metest
./metest                       # roda os 92 testes

g++ -std=c++17 -O2 -Iinclude -Itests src/*.cpp tests/cli.cpp -o mecli
./mecli entrada.wav saida.wav  # processa e imprime a análise antes/depois
```

A CLI aceita WAV PCM de 16 ou 24 bits. Para converter um MP3 antes:

```bash
ffmpeg -i hino01.mp3 -acodec pcm_s16le entrada.wav
```

Compare `entrada.wav` e `saida.wav` no seu player e veja os números que a
CLI imprime. É a forma mais rápida de julgar se o motor faz sentido para o
seu material antes de gastar tempo com o APK.
