// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"

namespace me {
// Analise completa de um buffer PCM float intercalado (-1..1).
// Nao modifica a entrada. Seguro para buffers longos (processa em blocos).
Analysis analyze(const float* interleaved, size_t frames, int sampleRate, int channels);
} // namespace me
