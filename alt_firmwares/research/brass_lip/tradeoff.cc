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
struct Meas{float rms,f0;};
static Meas Run(BrassParams p,float mouth,float sec=1.6f){
  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail; double acc=0; long n=0;
  long tot=long(FS*sec), st=long(FS*sec*0.55f);
  for(long i=0;i<tot;++i){float o=m.Tick(p,mouth);
    if(i>st){acc+=o*o;++n; if(tail.size()<24000)tail.push_back(o);} }
  return {(float)sqrt(acc/n),F0(tail)};
}
int main(){
  printf("%7s %6s %6s %6s %6s %9s %9s %8s %7s\n",
         "reflect","zeta","a0","k","zc","worstCents","selMiss","minRMS","meanRMS");
  for (float rf : {0.96f, 0.98f, 0.99f, 0.995f, 0.998f})
  for (float ze : {0.04f, 0.08f, 0.15f, 0.28f})
  for (float a0 : {0.35f, 0.45f, 0.60f})
  for (float k  : {2.0f, 3.0f, 5.0f})
  for (float zc : {0.15f, 0.3f, 0.6f}) {
    double worst=0; int miss=0; float minr=1e9, sumr=0; int c=0; bool dead=false;
    for (float note : {40.0f, 55.0f}) {
      float f=440.0f*powf(2.0f,(note-69.0f)/12.0f);
      for (int n=1;n<=4;++n){
        BrassParams p; p.reflect=rf; p.lip_zeta=ze; p.a0=a0; p.lip_k=k; p.zc=zc; p.damp=0.25f;
        p.bore_len=FS/f; p.lip_freq=f*n*0.97f;
        Meas m=Run(p,0.45f);
        if(m.rms<0.002f||m.f0<=0){dead=true;break;}
        minr=std::min(minr,m.rms); sumr+=m.rms; ++c;
        float got=m.f0/f;
        if (fabsf(got-float(n)) > 0.35f) ++miss;                  // wrong partial
        else worst=std::max(worst,(double)fabsf(1200.0*log2(m.f0/(f*n))));
      }
      if(dead)break;
    }
    if(dead||c==0) continue;
    printf("%7.3f %6.2f %6.2f %6.1f %6.2f %9.1f %9d %8.4f %7.4f\n",rf,ze,a0,k,zc,worst,miss,minr,sumr/c);
  }
  return 0;
}
