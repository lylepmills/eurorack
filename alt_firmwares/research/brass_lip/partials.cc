#include "model.h"
#include <cstdio>
#include <cstring>
static const float FS = 48000.0f;
static float MeasureF0(const std::vector<float>& s) {
  int n = s.size()/2; if (n < 800) return -1;
  double e0=0; for(int i=0;i<n;++i) e0+=s[i]*s[i];
  if (e0 < 1e-12) return -1;
  int maxlag = std::min(2400, n-1);
  std::vector<double> r(maxlag,0.0); double mx=0;
  for(int lag=20; lag<maxlag; ++lag){ double a=0,e=0; for(int i=0;i<n;++i){a+=s[i]*s[i+lag]; e+=s[i+lag]*s[i+lag];}
    r[lag]=(e>1e-12)?a/sqrt(e0*e):0.0; mx=std::max(mx,r[lag]); }
  if (mx < 0.5) return -1;
  for(int l=21;l<maxlag-1;++l) if(r[l]>0.92*mx && r[l]>=r[l-1] && r[l]>=r[l+1]) return FS/l;
  return -1;
}
struct Meas { float rms, f0; };
static Meas Run(BrassParams p, float mouth, float sec=1.6f) {
  BrassModel m; m.Init(8000); m.Clear();
  std::vector<float> tail; double acc=0; long n=0;
  long total=long(FS*sec), settle=long(FS*sec*0.55f);
  for(long i=0;i<total;++i){ float o=m.Tick(p,mouth);
    if(i>settle){acc+=o*o;++n; if(tail.size()<24000)tail.push_back(o);} }
  return {(float)sqrt(acc/n), MeasureF0(tail)};
}
int main(){
  BrassParams base; float fb = FS/base.bore_len;
  printf("Does lip tension select the partial? score = mean |measured/intended - 1| over partials 1..5\n");
  printf("%6s %6s %6s %6s %6s %8s %8s   %s\n","a0","k","zeta","zc","damp","score","minRMS","measured f0/f_bore at lip=1,2,3,4,5");
  float best=1e9; BrassParams bestp;
  for (float a0 : {0.10f, 0.25f, 0.45f})
  for (float k  : {1.0f, 3.0f, 8.0f, 20.0f})
  for (float ze : {0.05f, 0.12f, 0.25f})
  for (float zc : {0.3f, 1.0f, 3.0f})
  for (float dp : {0.25f, 0.6f}) {
    BrassParams p=base; p.a0=a0; p.lip_k=k; p.lip_zeta=ze; p.zc=zc; p.damp=dp;
    float score=0; float minrms=1e9; char buf[256]; int off=0; bool ok=true;
    for (int n=1;n<=5;++n){
      p.lip_freq = fb*n*0.97f;                 // just under the target partial
      Meas m = Run(p, 0.45f);
      minrms = std::min(minrms, m.rms);
      if (m.rms < 0.005f || m.f0 <= 0) { ok=false; break; }
      float got = m.f0/fb;
      score += fabsf(got/float(n) - 1.0f);
      off += snprintf(buf+off, sizeof(buf)-off, "%.2f ", got);
    }
    if(!ok) continue;
    score /= 5.0f;
    printf("%6.2f %6.1f %6.2f %6.1f %6.2f %8.3f %8.4f   %s\n",a0,k,ze,zc,dp,score,minrms,buf);
    if(score<best){best=score; bestp=p;}
  }
  printf("\nbest score %.3f\n", best);
  return 0;
}
