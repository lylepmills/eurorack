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
static Out Render(float note,float h,float ti,float bore_trim){
  float f=440.0f*powf(2.0f,(note-69.0f)/12.0f), fb=f*0.5f;
  BrassParams p; p.reflect=0.995f; p.lip_zeta=0.04f; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f; p.out_tap=1;
  p.damp=2.0f*float(M_PI)*14.0f*f/FS; if(p.damp<0.02f)p.damp=0.02f; if(p.damp>0.90f)p.damp=0.90f;
  float ab0=fb;
  int top=7; while(top>2 && ab0*top>3000.0f) --top;
  float pp=2+h*float(top-2); int n=int(pp); if(n>top)n=top; float t=pp-float(n);
  // The lip pulls the sounding pitch sharp of the bore mode, by an amount that
  // fits -4.8 + 67.2/n cents almost exactly. Only one partial sounds at a time,
  // so it is nulled by lengthening the bore for the selected partial.
  float pull = (-4.8f + 67.2f/float(n)) * bore_trim;
  p.bore_len=FS/fb*powf(2.0f, pull/1200.0f);
  float ab=FS/p.bore_len;
  p.lip_freq=ab*float(n)*(0.935f+0.025f*t);
  float mouth=0.18f+0.50f*ti;
  BrassModel m; m.Init(9000); m.Clear();
  std::vector<float> tail; double acc=0; long c=0;
  for(long i=0;i<long(FS*1.1f);++i){float o=m.Tick(p,mouth);
    if(i>long(FS*0.65f)){acc+=o*o;++c; if(tail.size()<24000)tail.push_back(o);} }
  float f0=F0(tail);
  return {(float)sqrt(acc/c),f0>0?f0/f:0.0f,n};
}
int main(){
  printf("signed cents by partial (bore_trim=1.0):\n%6s",  "note");
  for(int n=2;n<=7;++n) printf(" %8s%d","P",n); printf("\n");
  double per[8]={0}; int cnt[8]={0};
  for(float note=34;note<=78.1f;note+=8){
    printf("%6.0f",note);
    for(int n=2;n<=7;++n){
      float h=float(n-2)/5.0f;
      Out o=Render(note,h,0.55f,1.0f);
      if(o.ratio<=0||o.n!=n){printf(" %9s","--");continue;}
      float c=1200.0f*log2f(o.ratio/(0.5f*float(n)));
      printf(" %9.1f",c); per[n]+=c; cnt[n]++;
    }
    printf("\n");
  }
  printf("%6s","mean"); double tot=0; int tc=0;
  for(int n=2;n<=7;++n){ if(cnt[n]){printf(" %9.1f",per[n]/cnt[n]); tot+=per[n]; tc+=cnt[n];} else printf(" %9s","--"); }
  printf("\n overall mean signed: %.1f cents -> bore_trim %.4f\n", tot/tc, powf(2.0f,(tot/tc)/1200.0f));
  float trim = powf(2.0f,(tot/tc)/1200.0f);
  printf("\nre-measured with bore_trim=%.4f:\n", trim);
  double sum=0,mx=0; int c2=0, miss=0;
  for(float note=34;note<=78.1f;note+=4)
  for(float h=0;h<=1.001f;h+=0.0834f)
  for(float ti : {0.35f,0.75f}){
    Out o=Render(note,h,ti,trim);
    if(o.ratio<=0){++miss;continue;}
    float c=1200.0f*log2f(o.ratio/(0.5f*float(o.n)));
    if(fabsf(c)>120){++miss;continue;}
    sum+=fabs(c); mx=std::max(mx,(double)fabsf(c)); ++c2;
  }
  printf(" mean |cents| %.1f  max %.1f  misses %d / %d\n", sum/c2, mx, miss, c2+miss);
  return 0;
}
