#include "model.h"
#include <cstdio>
#include <cstring>
#include <string>
static const float FS = 48000.0f;

struct Meas { float rms, peak, f0; };

static float MeasureF0(const std::vector<float>& s) {
  // Normalised autocorrelation, first peak clearing 92% of the max.
  int n = s.size()/2; if (n < 800) return -1;
  double e0 = 0; for (int i=0;i<n;++i) e0 += s[i]*s[i];
  if (e0 < 1e-12) return -1;
  int maxlag = std::min(3000, n-1);
  std::vector<double> r(maxlag, 0.0); double mx = 0;
  for (int lag=12; lag<maxlag; ++lag) {
    double a=0,e=0; for (int i=0;i<n;++i){ a += s[i]*s[i+lag]; e += s[i+lag]*s[i+lag]; }
    r[lag] = (e>1e-12) ? a/sqrt(e0*e) : 0.0; mx = std::max(mx, r[lag]);
  }
  if (mx < 0.5) return -1;
  for (int l=13;l<maxlag-1;++l) if (r[l] > 0.92*mx && r[l] >= r[l-1] && r[l] >= r[l+1]) return FS/l;
  return -1;
}

static Meas Run(BrassParams p, float p_mouth, float seconds = 2.0f) {
  BrassModel m; m.Init(8000); m.Clear();
  std::vector<float> tail; double acc=0; long n=0; float peak=0;
  long total = long(FS*seconds), settle = long(FS*seconds*0.5f);
  for (long i=0;i<total;++i) {
    float o = m.Tick(p, p_mouth);
    if (i > settle) { acc += o*o; ++n; if (fabsf(o)>peak) peak=fabsf(o);
                      if (tail.size() < 24000) tail.push_back(o); }
  }
  return { (float)sqrt(acc/n), peak, MeasureF0(tail) };
}

// Lowest mouth pressure that sustains oscillation.
static float Threshold(BrassParams p, float lo=0.001f, float hi=2.0f) {
  const float kOn = 0.01f;
  if (Run(p, hi).rms < kOn) return -1.0f;
  for (int it=0; it<18; ++it) { float mid=0.5f*(lo+hi); if (Run(p, mid).rms >= kOn) hi=mid; else lo=mid; }
  return hi;
}

int main(int argc, char** argv) {
  std::string mode = argc>1 ? argv[1] : "threshold";
  BrassParams base;
  float f_bore = FS / base.bore_len;     // 130.8 Hz with the default length

  if (mode == "threshold") {
    printf("Bore fundamental %.1f Hz (len %.0f). Threshold mouth pressure vs lip tuning:\n", f_bore, base.bore_len);
    printf("%10s %10s %10s %10s %10s\n", "f_lip/f_bore", "threshold", "rms@1.5x", "f0", "f0/f_bore");
    for (float ratio = 0.4f; ratio <= 4.05f; ratio += 0.2f) {
      BrassParams p = base; p.lip_freq = f_bore * ratio;
      float th = Threshold(p);
      if (th < 0) { printf("%10.2f %10s\n", ratio, "-- silent"); continue; }
      Meas m = Run(p, th*1.5f);
      printf("%10.2f %10.4f %10.4f %10.1f %10.2f\n", ratio, th, m.rms, m.f0, m.f0/f_bore);
    }
  } else if (mode == "sweep") {
    // How the threshold moves with the valve constants.
    printf("%8s %8s %8s %8s %8s %10s %10s\n","a0","k","zeta","zc","flow","thresh@1.0","f0ratio");
    for (float a0 : {0.05f, 0.15f, 0.30f})
    for (float k  : {0.5f, 1.0f, 2.0f, 4.0f})
    for (float ze : {0.08f, 0.15f, 0.30f})
    for (float zc : {0.5f, 1.0f, 2.0f}) {
      BrassParams p = base; p.a0=a0; p.lip_k=k; p.lip_zeta=ze; p.zc=zc;
      p.lip_freq = f_bore * 0.95f;
      float th = Threshold(p);
      if (th < 0 || th > 1.5f) continue;
      Meas m = Run(p, th*1.4f);
      printf("%8.2f %8.2f %8.2f %8.2f %8.2f %10.4f %10.2f\n",a0,k,ze,zc,p.flow_gain,th,m.f0/f_bore);
    }
  }
  return 0;
}
