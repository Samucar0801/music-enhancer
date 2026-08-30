// SPDX-License-Identifier: Apache-2.0 -- geradores de sinal para os testes
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>

namespace sig {

struct Rng { uint32_t s = 12345;
    float next() { s = s*1664525u+1013904223u; return ((s>>8)&0xFFFFFF)/8388608.0f - 1.0f; } };

inline void addTone(std::vector<float>& b, int fs, int ch, double f, double amp,
                    double t0, double t1, double pan = 0.0) {
    size_t a = (size_t)(t0*fs), z = (size_t)(t1*fs);
    for (size_t i=a; i<z && i*ch+ch<=b.size(); ++i) {
        double v = amp*std::sin(2*M_PI*f*i/fs);
        double env = 1.0;
        double loc = (double)(i-a)/fs, dur = t1-t0;
        if (loc < 0.02) env = loc/0.02;
        if (dur-loc < 0.05) env = std::max(0.0,(dur-loc)/0.05);
        for (int c=0;c<ch;++c) {
            double g = (ch<2)?1.0:((c==0)?(1.0-std::max(0.0,pan)):(1.0+std::min(0.0,pan)));
            b[i*ch+c] += (float)(v*env*g);
        }
    }
}

// "Voz": fundamental + formantes, com vibrato -> ocupa 200-4000 Hz
inline void addVoice(std::vector<float>& b, int fs, int ch, double f0, double amp,
                     double t0, double t1) {
    size_t a=(size_t)(t0*fs), z=(size_t)(t1*fs);
    const double form[4]={700,1220,2600,3400}, fa[4]={1.0,0.6,0.35,0.18};
    for (size_t i=a;i<z && i*ch+ch<=b.size();++i) {
        double t=(double)i/fs;
        double f=f0*(1.0+0.012*std::sin(2*M_PI*5.2*t));
        double v=0;
        for (int h=1;h<=14;++h) {
            double fh=f*h; if (fh>fs*0.45) break;
            double g=1.0/h;
            for (int k=0;k<4;++k) { double d=(fh-form[k])/220.0; g+=fa[k]*std::exp(-d*d)*0.9; }
            v+=g*std::sin(2*M_PI*fh*t+h*0.7);
        }
        double env=1.0; double loc=t-t0, dur=t1-t0;
        if (loc<0.06) env=loc/0.06;
        if (dur-loc<0.12) env=std::max(0.0,(dur-loc)/0.12);
        for (int c=0;c<ch;++c) b[i*ch+c]+=(float)(v*amp*env*0.09);
    }
}

// Kick + snare + hihat com transientes reais
inline void addDrums(std::vector<float>& b, int fs, int ch, double bpm, double amp,
                     double t0, double t1) {
    Rng r; double beat=60.0/bpm;
    for (double t=t0; t<t1; t+=beat/2.0) {
        bool kick = std::fmod((t-t0)/beat, 1.0) < 0.01 || std::fmod((t-t0)/beat,2.0) < 0.01;
        size_t a=(size_t)(t*fs);
        for (size_t i=0;i<(size_t)(0.30*fs) && (a+i)*ch+ch<=b.size();++i) {
            double lt=(double)i/fs, v=0;
            if (kick) {
                double f=110.0*std::exp(-lt*32.0)+48.0;
                v=std::sin(2*M_PI*f*lt)*std::exp(-lt*11.0)*1.3;
            } else {
                v=r.next()*std::exp(-lt*38.0)*0.55
                 +std::sin(2*M_PI*195*lt)*std::exp(-lt*26.0)*0.35;
            }
            v+=r.next()*std::exp(-lt*95.0)*0.14;    // hihat
            for (int c=0;c<ch;++c) b[(a+i)*ch+c]+=(float)(v*amp);
        }
    }
}

inline void addNoise(std::vector<float>& b, double amp) {
    Rng r; for (auto& v : b) v += r.next()*(float)amp;
}

inline void hardClip(std::vector<float>& b, double th) {
    for (auto& v : b) { if (v>th) v=(float)th; else if (v<-th) v=(float)-th; }
}

inline void scaleTo(std::vector<float>& b, double peakDb) {
    double pk=0; for (float v:b) pk=std::max(pk,(double)std::fabs(v));
    if (pk<1e-9) return;
    double g=std::pow(10.0,peakDb/20.0)/pk;
    for (auto& v:b) v=(float)(v*g);
}

// Passa-baixa simples de 1a ordem repetida (simula corte de banda do MP3)
inline void bandLimit(std::vector<float>& b, int fs, int ch, double fc, int order=6) {
    double a=std::exp(-2*M_PI*fc/fs);
    for (int o=0;o<order;++o)
        for (int c=0;c<ch;++c) {
            double z=0;
            for (size_t i=c;i<b.size();i+=ch) { z=a*z+(1-a)*b[i]; b[i]=(float)z; }
        }
}

} // namespace sig
