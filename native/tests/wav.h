// SPDX-License-Identifier: Apache-2.0  -- leitor/escritor WAV minimo (PCM 16/24/32f)
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

struct Wav { std::vector<float> pcm; int sampleRate = 0, channels = 0; };

inline bool wavWrite(const std::string& path, const std::vector<float>& pcm, int fs, int ch) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    uint32_t n = (uint32_t)pcm.size(), dataBytes = n * 2;
    uint32_t riff = 36 + dataBytes;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    uint32_t sz = 16; uint16_t fmt = 1, chans = (uint16_t)ch, bits = 16;
    uint32_t rate = (uint32_t)fs, byteRate = rate * ch * 2; uint16_t align = (uint16_t)(ch * 2);
    std::fwrite(&sz,4,1,f); std::fwrite(&fmt,2,1,f); std::fwrite(&chans,2,1,f);
    std::fwrite(&rate,4,1,f); std::fwrite(&byteRate,4,1,f); std::fwrite(&align,2,1,f); std::fwrite(&bits,2,1,f);
    std::fwrite("data",1,4,f); std::fwrite(&dataBytes,4,1,f);
    for (float v : pcm) {
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        int16_t s = (int16_t)(v * 32767.0f); std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f); return true;
}

inline bool wavRead(const std::string& path, Wav& out) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    char id[4]; uint32_t sz;
    if (std::fread(id,1,4,f)!=4 || std::memcmp(id,"RIFF",4)) { std::fclose(f); return false; }
    std::fread(&sz,4,1,f); std::fread(id,1,4,f);
    if (std::memcmp(id,"WAVE",4)) { std::fclose(f); return false; }
    uint16_t fmt=0, ch=0, bits=0; uint32_t rate=0; bool haveFmt=false;
    while (std::fread(id,1,4,f)==4 && std::fread(&sz,4,1,f)==1) {
        long next = std::ftell(f) + (long)sz + (sz & 1);
        if (!std::memcmp(id,"fmt ",4)) {
            std::fread(&fmt,2,1,f); std::fread(&ch,2,1,f); std::fread(&rate,4,1,f);
            uint32_t br; uint16_t al; std::fread(&br,4,1,f); std::fread(&al,2,1,f); std::fread(&bits,2,1,f);
            haveFmt = true;
        } else if (!std::memcmp(id,"data",4) && haveFmt) {
            out.sampleRate=(int)rate; out.channels=(int)ch;
            size_t frames = sz / (ch * (bits/8));
            out.pcm.resize(frames*ch);
            for (size_t i=0;i<frames*ch;++i) {
                if (bits==16){int16_t s;std::fread(&s,2,1,f);out.pcm[i]=s/32768.0f;}
                else if (bits==24){uint8_t b[3];std::fread(b,1,3,f);int32_t s=(b[2]<<24)|(b[1]<<16)|(b[0]<<8);out.pcm[i]=s/2147483648.0f;}
                else if (bits==32 && fmt==3){float s;std::fread(&s,4,1,f);out.pcm[i]=s;}
                else if (bits==32){int32_t s;std::fread(&s,4,1,f);out.pcm[i]=s/2147483648.0f;}
                else { std::fclose(f); return false; }
            }
            std::fclose(f); return true;
        }
        std::fseek(f, next, SEEK_SET);
    }
    std::fclose(f); return false;
}
