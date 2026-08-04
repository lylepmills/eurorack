// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstdio>
#include <stdint.h>
#include <utility>
#include <vector>

#include "plaits/dsp/oscillator/oscillator.h"
#include "plaits/dsp/speech/lpc_speech_synth.h"
#include "plaits/dsp/speech/lpc_speech_synth_controller.h"
#include "plaits/dsp/speech/lpc_speech_synth_words.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

namespace {
const int kRate=48000, kFrameSamples=kRate/40, kGap=kRate/10;
void U16(FILE* f,uint16_t x){fwrite(&x,sizeof(x),1,f);} void U32(FILE* f,uint32_t x){fwrite(&x,sizeof(x),1,f);}
bool Wav(const char* path,const std::vector<float>& x){FILE* f=fopen(path,"wb");if(!f)return false;uint32_t n=x.size()*2;fwrite("RIFF",4,1,f);U32(f,36+n);fwrite("WAVEfmt ",8,1,f);U32(f,16);U16(f,1);U16(f,1);U32(f,kRate);U32(f,kRate*2);U16(f,2);U16(f,16);fwrite("data",4,1,f);U32(f,n);for(size_t i=0;i<x.size();++i){float y=std::max(-1.0f,std::min(1.0f,x[i]));int16_t p=y*32767.0f;fwrite(&p,2,1,f);}fclose(f);return true;}
class Renderer{public:explicit Renderer(float p):p_(p),phase_(0),sample_(0),next_(0){s_.Init();}void Frame(const plaits::LPCSpeechSynth::Frame& f){plaits::LPCSpeechSynth::Frame pair[2]={f,f};s_.PlayFrame(pair,0,false);}float Sample(){float r=next_;next_=0;phase_+=1.0f/6.0f;if(phase_>=1){phase_-=1;float e,v,d,reset=phase_*6;s_.Render(p_,1,&e,&v,1);d=v-sample_;r+=d*stmlib::ThisBlepSample(reset);next_+=d*stmlib::NextBlepSample(reset);sample_=v;}next_+=sample_;return r;}private:plaits::LPCSpeechSynth s_;float p_,phase_,sample_,next_;};
}
int main(int argc,char** argv){if(argc!=4)return 2;int index=atoi(argv[1]);float prosody=atof(argv[3]);if(index<0||index>=LPC_SPEECH_SYNTH_NUM_WORD_BANKS)return 2;char memory[16*1024];stmlib::BufferAllocator allocator(memory,sizeof(memory));plaits::LPCSpeechSynthWordBank bank;bank.Init(plaits::word_banks_,LPC_SPEECH_SYNTH_NUM_WORD_BANKS,&allocator);bank.Load(index);std::vector<std::pair<int,int> > words;for(int i=0;i<4096;++i){int a,b;bank.GetWordBoundaries((i+.5f)/4096.0f,&a,&b);if(a>=0&&(words.empty()||words.back().first!=a))words.push_back(std::make_pair(a,b));}Renderer renderer(prosody);std::vector<float> output;const plaits::LPCSpeechSynth::Frame* frames=bank.frames();stmlib::Random::Seed(0x21);for(size_t w=0;w<words.size();++w){for(int f=words[w].first;f<=words[w].second;++f){renderer.Frame(frames[f]);for(int i=0;i<kFrameSamples;++i)output.push_back(renderer.Sample());}if(w+1<words.size())output.insert(output.end(),kGap,0);}return Wav(argv[2],output)?0:2;}
