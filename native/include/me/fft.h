// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"

namespace me {
// FFT radix-2 iterativa, in-place. n deve ser potencia de 2.
void fftRadix2(std::vector<double>& re, std::vector<double>& im, bool inverse = false);
void hannWindow(std::vector<double>& w);
} // namespace me
