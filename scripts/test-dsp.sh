#!/usr/bin/env bash
# Compila e roda o núcleo DSP no PC. Não precisa de Android.
set -e
cd "$(dirname "$0")/../native"
echo ">> compilando a suíte de testes…"
g++ -std=c++17 -O2 -Iinclude -Itests src/*.cpp tests/host_test.cpp -o metest
echo ">> compilando a CLI…"
g++ -std=c++17 -O2 -Iinclude -Itests src/*.cpp tests/cli.cpp -o mecli
echo
./metest
echo
echo "CLI pronta: native/mecli entrada.wav saida.wav"
