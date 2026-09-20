// Host A/B renderer for the TWIST tuning-range prototype.
// Renders each affected engine at a sweep of TWIST positions.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"
#include "plaits/dsp/engine2/phase_distortion_engine.h"
#include "plaits/dsp/engine2/wave_paraphonic_engine.h"

using namespace plaits;

static const int kSr = 48000;
static const size_t kBlock = 12;

static void WriteWav(const std::string& path, const std::vector<float>& l) {
  std::vector<int16_t> pcm(l.size());
  for (size_t i = 0; i < l.size(); ++i) {
    float v = l[i]; if (v > 1.f) v = 1.f; if (v < -1.f) v = -1.f;
    pcm[i] = (int16_t)(v * 32000.f);
  }
  uint32_t data = pcm.size() * 2, riff = 36 + data;
  FILE* f = fopen(path.c_str(), "wb");
  fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f);
  uint32_t six=16; uint16_t one=1, ch=1, bits=16, align=2; uint32_t sr=kSr, bps=kSr*2;
  fwrite(&six,4,1,f); fwrite(&one,2,1,f); fwrite(&ch,2,1,f); fwrite(&sr,4,1,f);
  fwrite(&bps,4,1,f); fwrite(&align,2,1,f); fwrite(&bits,2,1,f);
  fwrite("data",1,4,f); fwrite(&data,4,1,f); fwrite(pcm.data(),2,pcm.size(),f);
  fclose(f);
}

// One engine, one TWIST position, `secs` of sustained tone.
template <typename E>
static void RenderOne(const char* name, float harmonics, float timbre,
                      float morph, float macro, float note,
                      std::vector<float>* acc, float secs) {
  static uint8_t mem[64 * 1024];
  stmlib::BufferAllocator alloc(mem, sizeof(mem));
  E engine;
  engine.Init(&alloc);
  engine.Reset();
  EngineParameters p;
  p.note = note; p.harmonics = harmonics; p.timbre = timbre; p.morph = morph;
  p.macro = macro; p.trigger = TRIGGER_UNPATCHED; p.accent = 1.0f;
  float out[kBlock], aux[kBlock];
  size_t blocks = (size_t)(secs * kSr) / kBlock;
  for (size_t b = 0; b < blocks; ++b) {
    bool enveloped = false;
    memset(out, 0, sizeof(out)); memset(aux, 0, sizeof(aux));
    engine.Render(p, out, aux, kBlock, &enveloped);
    for (size_t i = 0; i < kBlock; ++i) acc->push_back(out[i] * 0.5f);
  }
  (void)name;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const char* tag = argc > 2 ? argv[2] : "x";
  // A knob sweep: five positions across the travel, 1.2 s each, so the tuning
  // drift is audible as you walk away from centre.
  const float pos[] = { 0.5f, 0.55f, 0.6f, 0.75f, 1.0f };
  const int kN = sizeof(pos)/sizeof(pos[0]);
  const float note = 48.0f;

  { std::vector<float> a;
    // HARMONICS 0.55 lands on a quantized non-unison ratio.
    for (int i=0;i<kN;++i) RenderOne<FMEngine>("fm",0.55f,0.45f,0.2f,pos[i],note,&a,1.2f);
    WriteWav(std::string(dir)+"/two-op-fm."+tag+".wav", a); }

  { std::vector<float> a;
    for (int i=0;i<kN;++i) RenderOne<VirtualAnalogDualEngine>("vad",0.62f,0.4f,0.4f,pos[i],note,&a,1.2f);
    WriteWav(std::string(dir)+"/virtual-analog-dual."+tag+".wav", a); }

  { std::vector<float> a;
    for (int i=0;i<kN;++i) RenderOne<VirtualAnalogCrossfadeEngine>("vax",0.62f,0.4f,0.4f,pos[i],note,&a,1.2f);
    WriteWav(std::string(dir)+"/virtual-analog-crossfade."+tag+".wav", a); }

  { std::vector<float> a;
    for (int i=0;i<kN;++i) RenderOne<PhaseDistortionEngine>("pd",0.55f,0.5f,0.3f,pos[i],note,&a,1.2f);
    WriteWav(std::string(dir)+"/phase-distortion."+tag+".wav", a); }

  { std::vector<float> a;
    for (int i=0;i<kN;++i) RenderOne<WaveParaphonicEngine>("wp",0.35f,0.4f,0.5f,pos[i],note,&a,1.2f);
    WriteWav(std::string(dir)+"/wave-paraphonic."+tag+".wav", a); }

  printf("wrote %s/*.%s.wav\n", dir, tag);
  return 0;
}
