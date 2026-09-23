#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine2/struck_drum_engine.h"
#include "plaits/dsp/engine2/cymbal_engine.h"
#include "plaits/dsp/engine2/particle_burst_engine.h"
#include "plaits/dsp/engine2/plucked_engine.h"
using namespace plaits;
static const int kStrikes=8, kBlocks=1600;  // 0.4 s per strike
template<typename E> static void Run(const char* name){
  static uint8_t mem[256*1024];
  const float deltas[]={0.0f,0.005f,0.01f,0.02f,0.05f,0.10f};
  for(int d=0; d<6; ++d){
    stmlib::BufferAllocator alloc(mem,sizeof mem);
    E e; e.Init(&alloc); e.Reset();
    EngineParameters p;p.note=48.f;p.harmonics=0.5f;p.timbre=0.5f;
    p.morph=0.5f+deltas[d];p.macro=0.5f;p.accent=1.f;p.stereo=false;
    char path[256]; snprintf(path,sizeof path,"/tmp/twistbuild/perc_%s_%d.raw",name,d);
    FILE* f=fopen(path,"wb"); float o[12],x[12];
    for(int s=0;s<kStrikes;++s) for(int k=0;k<kBlocks;++k){
      p.trigger=(k==0)?(TRIGGER_RISING_EDGE|TRIGGER_HIGH):TRIGGER_LOW;
      bool en=false; e.Render(p,o,x,12,&en); fwrite(o,4,12,f);
    }
    fclose(f);
  }
}
int main(){ Run<StruckDrumEngine>("struck-drum"); Run<CymbalEngine>("cymbal");
  Run<ParticleBurstEngine>("particle-burst"); Run<PluckedEngine>("plucked"); return 0; }
