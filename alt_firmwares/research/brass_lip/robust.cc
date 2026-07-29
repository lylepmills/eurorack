#include "model.h"
#include <cstdio>
static const float FS=48000.0f;
static float F0(const std::vector<float>& s){
  int n=s.size()/2; if(n<800) return -1;
  double e0=0; for(int i=0;i<n;++i) e0+=s[i]*s[i];
  if(e0<1e-12) return -1;
  int ml=std::min(2400,n-1); std::vector<double> r(ml,0.0); double mx=0;
  for(int lag=18;lag<ml;++lag){double a=0,e=0;for(int i=0;i<n;++i){a+=s[i]*s[i+lag];e+=s[i+lag]*s[i+lag];}
    r[lag]=(e>1e-12)?a/sqrt(e0*e):0.0; mx=std::max(mx,r[lag]);}
  if(mx<0.5) return -1;
  for(int l=19;l<ml-1;++l) if(r[l]>0.92*mx&&r[l]>=r[l-1]&&r[l]>=r[l+1]){
    double den=r[l-1]-2.0*r[l]+r[l+1];
    double d=(fabs(den)>1e-12)?0.5*(r[l-1]-r[l+1])/den:0.0;
    if(d<-0.5)d=-0.5; if(d>0.5)d=0.5; return FS/(l+d); }
  return -1;
}
struct Out{float rms,ratio;int n;};
static Out Render(float note,float h,float ti,float mo,float ma,float zb,float zs,int topcap){
  float f=440.0f*powf(2.0f,(note-69.0f)/12.0f), fb=f*0.5f;
  BrassParams p; p.reflect=0.995f; p.lip_zeta=0.04f; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f; p.out_tap=1;
  p.bore_len=FS/fb*(0.5f+ma); float ab=FS/p.bore_len;
  float cr=4.0f+22.0f*mo; p.damp=2.0f*float(M_PI)*cr*f/FS;
  if(p.damp<0.02f)p.damp=0.02f; if(p.damp>0.90f)p.damp=0.90f;
  int top=topcap; while(top>2 && ab*top>3000.0f) --top;
  float pp=2+h*float(top-2); int n=int(pp); if(n>top)n=top; float t=pp-float(n);
  p.lip_freq=ab*float(n)*(zb+zs*t);
  float mouth=0.18f+0.50f*ti;
  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail; double acc=0; long c=0;
  for(long i=0;i<long(FS*1.1f);++i){float o=m.Tick(p,mouth);
    if(i>long(FS*0.65f)){acc+=o*o;++c; if(tail.size()<24000)tail.push_back(o);} }
  float f0=F0(tail);
  return {(float)sqrt(acc/c),f0>0?f0/f:0.0f,n};
}
int main(){
  printf("%8s %8s %5s %9s %9s %8s %8s\n","zoneBase","zoneSpan","top","misses","meanCents","maxCents","minRMS");
  for(float zb=0.900f; zb<=0.9351f; zb+=0.005f)
  for(float zs=0.015f; zs<=0.0351f; zs+=0.010f)
  for(int tc : {7, 8}) {
    int miss=0; double sum=0,mx=0; int c=0; float minr=1e9;
    for(float note=34; note<=78.1f; note+=4)
    for(float h=0; h<=1.001f; h+=0.0834f)
    for(float ti : {0.35f, 0.75f}) {
      Out o=Render(note,h,ti,0.45f,0.5f,zb,zs,tc);
      minr=std::min(minr,o.rms);
      float expect=0.5f*float(o.n);
      if(o.ratio<=0){++miss;continue;}
      float cents=1200.0f*log2f(o.ratio/expect);
      if(fabsf(cents)>120){++miss;continue;}
      sum+=fabs(cents); mx=std::max(mx,(double)fabsf(cents)); ++c;
    }
    printf("%8.3f %8.3f %5d %9d %9.1f %8.1f %8.4f\n",zb,zs,tc,miss,c?sum/c:0,mx,minr);
  }
  return 0;
}
