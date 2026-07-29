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
struct M{float rms,f0;};
static M Run(float note,float lipratio,float mouth){
  float f=440.0f*powf(2.0f,(note-69.0f)/12.0f);
  BrassParams p; p.reflect=0.995f; p.lip_zeta=0.04f; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f; p.damp=0.25f;
  p.bore_len=FS/f; p.lip_freq=f*lipratio;
  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail; double acc=0; long n=0;
  for(long i=0;i<long(FS*1.3f);++i){float o=m.Tick(p,mouth);
    if(i>long(FS*0.75f)){acc+=o*o;++n; if(tail.size()<24000)tail.push_back(o);} }
  return {(float)sqrt(acc/n),F0(tail)};
}
int main(){
  printf("Lock-zone edges as a multiple of the partial number n.\n");
  printf("%6s %7s %4s %10s %10s %8s\n","note","mouth","n","start/n","end/n","cents@mid");
  for (float note : {36.0f, 48.0f, 60.0f, 72.0f})
  for (float mouth : {0.30f, 0.45f, 0.70f})
  for (int n=1;n<=5;++n){
    float f=440.0f*powf(2.0f,(note-69.0f)/12.0f);
    float start=-1,end=-1;
    for (float x=0.80f; x<=1.10f; x+=0.005f){
      M m=Run(note,n*x,mouth);
      bool ok = m.rms>0.004f && m.f0>0 && fabsf(m.f0/(f*n)-1.0f)<0.12f;
      if (ok && start<0) start=x;
      if (ok) end=x;
    }
    if(start<0){ printf("%6.0f %7.2f %4d %10s\n",note,mouth,n,"-- none"); continue; }
    float mid=0.5f*(start+end);
    M mm=Run(note,n*mid,mouth);
    float cents = (mm.f0>0)? 1200.0f*log2f(mm.f0/(f*n)) : 0.0f;
    printf("%6.0f %7.2f %4d %10.3f %10.3f %8.1f\n",note,mouth,n,start,end,cents);
  }
  return 0;
}
