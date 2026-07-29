#include "model.h"
#include <cstdio>
#include <string>
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
struct Out{float rms,peak,ratio;int n;};
static Out Render(float note,float h,float timbre,float morph,float macro){
  float f=440.0f*powf(2.0f,(note-69.0f)/12.0f), fb=f*0.5f;
  BrassParams p;
  p.reflect=0.995f; p.lip_zeta=0.04f; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f; p.out_tap=1;
  p.bore_len = FS/fb*(0.5f+macro);
  float ab = FS/p.bore_len;
  float cr = 4.0f+22.0f*morph;
  p.damp = 2.0f*float(M_PI)*cr*f/FS; if(p.damp<0.02f)p.damp=0.02f; if(p.damp>0.90f)p.damp=0.90f;
  int top=8; while(top>2 && ab*top>3000.0f) --top;
  float pp=2+h*float(top-2); int n=int(pp); if(n>top)n=top; float t=pp-float(n);
  p.lip_freq=ab*float(n)*(0.905f+0.020f*t);
  float mouth=0.18f+0.50f*timbre;
  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail; double acc=0; long c=0; float pk=0;
  for(long i=0;i<long(FS*1.2f);++i){float o=m.Tick(p,mouth);
    if(i>long(FS*0.7f)){acc+=o*o;++c;if(fabsf(o)>pk)pk=fabsf(o); if(tail.size()<24000)tail.push_back(o);} }
  float f0=F0(tail);
  return {(float)sqrt(acc/c),pk,f0>0?f0/f:0.0f,n};
}
int main(int argc,char**argv){
  std::string m = argc>1?argv[1]:"all";
  if (m=="all"||m=="stair") {
    printf("=== HARMONICS staircase (expect 1, 1.5, 2, 2.5, 3, 3.5, 4) ===\n");
    for (float note : {36.0f,48.0f,60.0f,72.0f}) {
      printf(" note %2.0f: ",note);
      for (float h=0.0f;h<=1.001f;h+=0.0834f){ Out o=Render(note,h,0.55f,0.45f,0.5f);
        printf("%.2f%s ", o.ratio, o.rms<0.004f?"!":""); }
      printf("\n");
    }
  }
  if (m=="all"||m=="ctl") {
    printf("\n=== every control alive? rms across each sweep (others at 0.5) ===\n");
    for (float note : {40.0f,55.0f,70.0f}) {
      printf(" note %2.0f\n",note);
      printf("   HARM :"); for(float v=0;v<=1.001f;v+=0.2f) printf(" %.4f",Render(note,v,0.55f,0.45f,0.5f).rms); printf("\n");
      printf("   TIMB :"); for(float v=0;v<=1.001f;v+=0.2f) printf(" %.4f",Render(note,0.35f,v,0.45f,0.5f).rms); printf("\n");
      printf("   MORPH:"); for(float v=0;v<=1.001f;v+=0.2f) printf(" %.4f",Render(note,0.35f,0.55f,v,0.5f).rms); printf("\n");
      printf("   MACRO:"); for(float v=0;v<=1.001f;v+=0.2f) printf(" %.4f",Render(note,0.35f,0.55f,0.45f,v).rms); printf("\n");
    }
  }
  if (m=="all"||m=="tune") {
    printf("\n=== tuning + peak safety over the grid ===\n");
    double sum=0,mx=0; int c=0; float peak=0, minrms=1e9;
    for (float note=32; note<=80.1f; note+=6)
    for (float h=0; h<=1.001f; h+=0.125f)
    for (float ti : {0.3f,0.7f})
    for (float mo : {0.2f,0.8f}) {
      Out o=Render(note,h,ti,mo,0.5f);
      peak=std::max(peak,o.peak); minrms=std::min(minrms,o.rms);
      if(o.ratio<=0) continue;
      float cents=1200.0f*log2f(o.ratio/(0.5f*float(o.n)));
      if(fabsf(cents)>250) continue;
      sum+=fabs(cents); mx=std::max(mx,(double)fabsf(cents)); ++c;
    }
    printf(" mean |cents| %.1f   max %.1f   over %d points\n",sum/c,mx,c);
    printf(" worst peak %.4f   min rms %.5f\n",peak,minrms);
  }
  return 0;
}
