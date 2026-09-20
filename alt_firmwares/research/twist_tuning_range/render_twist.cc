// Host A/B renderer for the TWIST tuning-range prototype.
// Renders each affected engine at a sweep of TWIST positions.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include "stmlib/utils/buffer_allocator.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_dual_engine.h"
#include "plaits/dsp/engine/virtual_analog_crossfade_engine.h"

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

// TWIST as a CONTINUOUS gesture, not a set of stops. Sampling the knob at a
// few fixed positions makes every mode sound stepped, which hides the one
// difference worth hearing -- so the macro moves every block (4 kHz) and the
// engines' own ParameterInterpolators smooth what follows.
//
// The gesture, as a fraction of the file:
//
//   0.00 - 0.30   noon -> fully clockwise
//   0.30 - 0.60   fully clockwise -> fully counter-clockwise, THROUGH noon
//   0.60 - 0.75   back up to noon
//   0.75 - 1.00   a wobble around noon whose amplitude GROWS from 0 to +/-5%
//
// Every join is continuous in value, so nothing clicks and any discontinuity
// you hear is the mode's own stepping. The last quarter is the real test: it
// is what trying to HOLD the tuning by hand sounds like, and the amplitude
// grows because a fixed wobble cannot show both halves of the story. A small
// one is inside every quantized capture region (+/-1.46% at the tightest, on
// Phase Distortion) so quantized sits perfectly still while stock is already
// audibly adrift; a large one crosses a step and quantized jumps, which is the
// honest limit of that mode and should be heard too.
static float MacroAt(float t) {
  if (t < 0.30f) {
    return 0.5f + 0.5f * (t / 0.30f);
  } else if (t < 0.60f) {
    return 1.0f - (t - 0.30f) / 0.30f;
  } else if (t < 0.75f) {
    return 0.5f * ((t - 0.60f) / 0.15f);
  }
  const float u = (t - 0.75f) / 0.25f;
  return 0.5f + 0.05f * u * sinf(2.0f * 3.14159265358979f * 3.0f * u);
}

#ifndef TWIST_CONTROL_BUILD
template <typename E> static void SetMode(E* e, int mode) {
  e->set_twist_tuning(static_cast<TwistTuning>(mode));
}
#else
// The pristine-master control has no per-instance mode; it is stock by
// definition, so the renderer must not try to set one.
template <typename E> static void SetMode(E*, int) { }
#endif

template <typename E>
static void RenderOne(const char* name, float harmonics, float timbre,
                      float morph, float note,
                      std::vector<float>* acc, float secs, int mode) {
  static uint8_t mem[64 * 1024];
  stmlib::BufferAllocator alloc(mem, sizeof(mem));
  E engine;
  SetMode(&engine, mode);
  engine.Init(&alloc);
  engine.Reset();
  EngineParameters p;
  p.note = note; p.harmonics = harmonics; p.timbre = timbre; p.morph = morph;
  p.trigger = TRIGGER_UNPATCHED; p.accent = 1.0f;
  float out[kBlock], aux[kBlock];
  const size_t blocks = (size_t)(secs * kSr) / kBlock;
  // A short fade at each end; the engines start from a reset phase and a hard
  // cut at the tail is the only click in an otherwise continuous file.
  const size_t fade = (size_t)(0.02f * kSr) / kBlock;
  for (size_t b = 0; b < blocks; ++b) {
    p.macro = MacroAt((float)b / (float)(blocks - 1));
    bool enveloped = false;
    memset(out, 0, sizeof(out)); memset(aux, 0, sizeof(aux));
    engine.Render(p, out, aux, kBlock, &enveloped);
    float g = 0.5f;
    if (b < fade) g *= (float)b / (float)fade;
    if (b + fade >= blocks) g *= (float)(blocks - 1 - b) / (float)fade;
    for (size_t i = 0; i < kBlock; ++i) acc->push_back(out[i] * g);
  }
  (void)name;
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const char* tag = argc > 2 ? argv[2] : "x";
  const int mode = argc > 3 ? atoi(argv[3]) : 0;
  // A knob sweep: five positions across the travel, 1.2 s each, so the tuning
  // drift is audible as you walk away from centre.
  const float kSweepSeconds = 12.0f;
  const float note = 48.0f;

  { std::vector<float> a;
    // HARMONICS 0.55 lands on a quantized non-unison ratio.
    RenderOne<FMEngine>("fm",0.55f,0.45f,0.2f,note,&a,kSweepSeconds,mode);
    WriteWav(std::string(dir)+"/two-op-fm."+tag+".wav", a); }

  { std::vector<float> a;
    RenderOne<VirtualAnalogDualEngine>("vad",0.62f,0.4f,0.4f,note,&a,kSweepSeconds,mode);
    WriteWav(std::string(dir)+"/virtual-analog-dual."+tag+".wav", a); }

  { std::vector<float> a;
    RenderOne<VirtualAnalogCrossfadeEngine>("vax",0.62f,0.0f,0.4f,note,&a,kSweepSeconds,mode);
    WriteWav(std::string(dir)+"/virtual-analog-crossfade."+tag+".wav", a); }



  printf("wrote %s/*.%s.wav\n", dir, tag);
  return 0;
}
