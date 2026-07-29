#include "model.h"
#include <cstdio>
static const float FS=48000.0f;
static float F0(const std::vector<float>& s){
  int n=s.size()/2; if(n<800) return -1;
  double e0=0; for(int i=0;i<n;++i) e0+=s[i]*s[i];
  if(e0<1e-12) return -1;
  int ml=std::min(2400,n-1); std::vector<double> r(ml,0.0); double mx=0;
  for(int lag=20;lag<ml;++lag){double a=0,e=0;for(int i=0;i<n;++i){a+=s[i]*s[i+lag];e+=s[i+lag]*s[i+lag];}
    r[lag]=(e>1e-12)?a/sqrt(e0*e):0.0; mx=std::max(mx,r[lag]);}
  if(mx<0.5) return -1;
  for(int l=21;l<ml-1;++l) if(r[l]>0.92*mx&&r[l]>=r[l-1]&&r[l]>=r[l+1]){
    double den=r[l-1]-2.0*r[l]+r[l+1];
    double d=(fabs(den)>1e-12)?0.5*(r[l-1]-r[l+1])/den:0.0;
    if(d<-0.5)d=-0.5; if(d>0.5)d=0.5; return FS/(l+d); }
  return -1;
}
int main(int argc,char**argv){
  float ze = argc>1?atof(argv[1]):0.04f;
  float note=48.0f, f=440.0f*powf(2.0f,(note-69.0f)/12.0f);
  printf("# lip/f_bore   f0/f_bore   rms    (zeta=%.2f)\n", ze);
  for (float ratio=0.5f; ratio<=6.01f; ratio+=0.05f) {
    BrassParams p; p.reflect=0.995f; p.lip_zeta=ze; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f; p.damp=0.25f;
    p.bore_len=FS/f; p.lip_freq=f*ratio;
    BrassModel m; m.Init(9000); m.Clear();
    std::vector<float> tail; double acc=0; long n=0;
    for(long i=0;i<long(FS*1.4f);++i){float o=m.Tick(p,0.45f);
      if(i>long(FS*0.8f)){acc+=o*o;++n; if(tail.size()<24000)tail.push_back(o);} }
    float rms=sqrt(acc/n), f0=F0(tail);
    printf("%12.2f %12.3f %8.4f\n", ratio, f0>0?f0/f:0.0f, rms);
  }
  return 0;
}
