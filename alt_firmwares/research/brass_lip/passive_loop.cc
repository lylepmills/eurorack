#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static const float FS=48000.0f;
// Passive check: fixed reflection coefficient, impulse in, measure the ring.
int main(){
  for (float refl_fixed : {0.9f, -0.9f}) {
    for (float damp : {0.2f, 0.575f, 0.95f}) {
      int len=367; std::vector<float> d(6000,0.0f); int w=0; float lp=0; float dcx=0,dcy=0;
      std::vector<float> out;
      for (long i=0;i<24000;++i){
        float rp=float(w)-len; while(rp<0) rp+=d.size();
        int i0=int(rp)%d.size();
        float raw=d[i0];
        lp += damp*(raw-lp);
        float bore=0.92f*lp;
        float mouth=0.0f;
        float pd=bore-mouth;
        float o = mouth + pd*refl_fixed;
        if (i==0) o += 1.0f;            // impulse
        float bl=o-dcx+0.999f*dcy; dcx=o; dcy=bl;
        d[w]=bl; w=(w+1)%d.size();
        if (i>2000) out.push_back(bl);
      }
      // zero-crossing period
      int zc=0; for(size_t i=1;i<out.size();++i) if((out[i-1]<0)!=(out[i]<0)) ++zc;
      float f_zc = zc*FS/(2.0f*out.size());
      double pk=0; for(float v:out) pk=std::max(pk,(double)fabsf(v));
      printf("refl=%+.2f damp=%.3f  len=%d (expect %.1f Hz non-inv / %.1f Hz inv)  ring f=%.1f peak=%.5f\n",
             refl_fixed, damp, len, FS/len, FS/(2*len), f_zc, pk);
    }
  }
  return 0;
}
