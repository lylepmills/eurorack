// Copyright 2026 Matt Policastro.
// SPDX-License-Identifier: MIT
//
// Rack-free checks of LabVoice at the engine's own 48 kHz: output range, jack
// scaling, the patched-TRIG states the browser audition cannot reach, and the
// LPG's decay. They hold for any package; run them with `make test`.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "lab_voice.h"

using namespace plaits_lab_vcv;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

std::vector<float> Run(LabVoice* voice, const Controls& controls, const Inputs& inputs,
                       float seconds, std::vector<float>* aux = NULL) {
  std::vector<float> out;
  float o[kBlockSize], a[kBlockSize];
  const size_t blocks = static_cast<size_t>(seconds * kSampleRate / kBlockSize + 0.5f);
  for (size_t b = 0; b < blocks; ++b) {
    voice->Render(controls, inputs, o, a);
    out.insert(out.end(), o, o + kBlockSize);
    if (aux) aux->insert(aux->end(), a, a + kBlockSize);
  }
  return out;
}

float Rms(const std::vector<float>& x, float from_s, float to_s) {
  const size_t from = static_cast<size_t>(from_s * kSampleRate);
  const size_t to = std::min(x.size(), static_cast<size_t>(to_s * kSampleRate));
  double sum = 0.0;
  for (size_t i = from; i < to; ++i) sum += x[i] * x[i];
  return to > from ? static_cast<float>(std::sqrt(sum / (to - from))) : 0.0f;
}

float ZeroCrossingsPerSecond(const std::vector<float>& x, float from_s) {
  size_t crossings = 0;
  const size_t from = static_cast<size_t>(from_s * kSampleRate);
  for (size_t i = from + 1; i < x.size(); ++i) {
    if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
  }
  return crossings / ((x.size() - from) / kSampleRate);
}

}  // namespace

int main() {
  std::printf("engine: %s\n", LabVoice::name());
  const Controls controls;
  const Inputs unpatched;

  {
    LabVoice voice;
    const std::vector<float> x = Run(&voice, controls, unpatched, 1.0f);
    bool finite = true;
    float peak = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
      finite = finite && std::isfinite(x[i]);
      peak = std::max(peak, std::fabs(x[i]));
    }
    Check(finite && peak <= 5.0f, "unpatched output is finite and within +/-5 V");
    Check(Rms(x, 0.1f, 1.0f) > 0.05f, "unpatched output sounds");
  }

  {
    LabVoice base_voice, knob_voice, cv_voice;
    Controls up = controls;
    up.note += 12.0f;
    Inputs octave = unpatched;
    octave.v_oct = 1.0f;
    // AUX: MAIN may carry fixed-frequency processing, such as a band-pass, that
    // does not transpose.
    std::vector<float> base_aux, knob_aux, cv_aux;
    Run(&base_voice, controls, unpatched, 1.0f, &base_aux);
    Run(&knob_voice, up, unpatched, 1.0f, &knob_aux);
    Run(&cv_voice, controls, octave, 1.0f, &cv_aux);
    const float base = ZeroCrossingsPerSecond(base_aux, 0.1f);
    const float knob = ZeroCrossingsPerSecond(knob_aux, 0.1f);
    const float cv = ZeroCrossingsPerSecond(cv_aux, 0.1f);
    std::printf("      AUX zero crossings/s: base %.0f, FREQ +1 oct %.0f, V/OCT +1 V %.0f\n", base, knob, cv);
    Check(std::fabs(cv / knob - 1.0f) < 0.02f, "V/OCT +1 V matches FREQ +12 semitones");
    Check(std::fabs(knob / base - 2.0f) < 0.1f, "+1 octave doubles the pitch");
  }

  {
    LabVoice voice;
    Inputs trig = unpatched;
    trig.trigger_patched = true;
    const std::vector<float> idle = Run(&voice, controls, trig, 0.5f);
    Check(Rms(idle, 0.25f, 0.5f) < 0.01f, "TRIG patched and low is quiet");
    trig.trigger = 5.0f;
    std::vector<float> burst = Run(&voice, controls, trig, 0.002f);
    trig.trigger = 0.0f;
    const std::vector<float> rest = Run(&voice, controls, trig, 0.4f);
    burst.insert(burst.end(), rest.begin(), rest.end());
    Check(Rms(burst, 0.0f, 0.05f) > 0.05f, "a 5 V TRIG pulse sounds");
  }

  {
    // Plaits LPG mode: DECAY shapes every strike, whatever the engine does.
    float tail[2];
    for (int long_decay = 0; long_decay < 2; ++long_decay) {
      LabVoice voice;
      Controls lpg = controls;
      lpg.plaits_lpg = true;
      lpg.decay = long_decay ? 1.0f : 0.0f;
      Inputs trig = unpatched;
      trig.trigger_patched = true;
      const std::vector<float> idle = Run(&voice, lpg, trig, 0.3f);
      if (long_decay) Check(Rms(idle, 0.1f, 0.3f) < 0.01f, "Plaits LPG: TRIG patched and low is quiet");
      trig.trigger = 5.0f;
      std::vector<float> hit = Run(&voice, lpg, trig, 0.002f);
      trig.trigger = 0.0f;
      const std::vector<float> rest = Run(&voice, lpg, trig, 0.6f);
      hit.insert(hit.end(), rest.begin(), rest.end());
      if (long_decay) Check(Rms(hit, 0.0f, 0.05f) > 0.05f, "Plaits LPG: a TRIG pulse sounds");
      tail[long_decay] = Rms(hit, 0.3f, 0.5f);
    }
    std::printf("      LPG tail 0.3-0.5 s: DECAY 0 %.4f V, DECAY 1 %.4f V\n", tail[0], tail[1]);
    Check(tail[1] > 0.02f && tail[1] > 4.0f * tail[0], "Plaits LPG: DECAY lengthens the strike");
  }

  std::printf("%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
