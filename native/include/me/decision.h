// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "me/common.h"

namespace me {

struct UserMacros {          // -1..+1, todos neutros em 0
    double bass = 0, mid = 0, treble = 0, vocal = 0, clarity = 0;
    double stereo = 0, loudness = 0, compression = 0;
};

struct DecisionInput {
    Analysis analysis;
    Route route = Route::Headphone;
    Preset preset = Preset::Auto;
    LoudnessMode loudnessMode = LoudnessMode::Normalized;
    UserMacros macros;
    double targetLufs = -14.0;
    bool aiStemsAvailable = false;
};

// Decide o que a MUSICA precisa (nao "qual preset aplicar").
ChainParams decide(const DecisionInput& in);

} // namespace me
