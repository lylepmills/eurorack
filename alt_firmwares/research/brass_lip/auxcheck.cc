#include "model.h"
#include <cstdio>
static const float FS=48000.0f;
int main(){
  printf("%6s %10s %10s %10s %10s\n","note","flow rms","junc rms","flow peak","junc peak");
  for(float note=34;note<=78.1f;note+=6){
    float f=440.0f*powf(2.0f,(note-69.0f)/12.0f), fb=f*0.5f;
    for(int tap=1;tap<=2;++tap){}
    double a1=0,a2=0; long c=0; float p1=0,p2=0;
    BrassParams p; p.reflect=0.995f; p.lip_zeta=0.04f; p.a0=0.60f; p.lip_k=3.0f; p.zc=0.15f;
    int top=7; while(top>2 && fb*top>3000.0f) --top;
    int n=int(2+0.5f*(top-2)); float pull=-4.8f+67.2f/float(n);
    p.bore_len=FS/fb*powf(2.0f,pull/1200.0f);
    p.damp=2.0f*float(M_PI)*14.0f*f/FS; if(p.damp<0.02f)p.damp=0.02f; if(p.damp>0.90f)p.damp=0.90f;
    p.lip_freq=(FS/p.bore_len)*float(n)*0.945f;
    BrassModel m1,m2; m1.Init(9000); m1.Clear(); m2.Init(9000); m2.Clear();
    BrassParams q1=p; q1.out_tap=1; BrassParams q2=p; q2.out_tap=2;
    for(long i=0;i<long(FS*1.1f);++i){
      float o1=m1.Tick(q1,0.45f), o2=m2.Tick(q2,0.45f);
      if(i>long(FS*0.65f)){a1+=o1*o1;a2+=o2*o2;++c;
        if(fabsf(o1)>p1)p1=fabsf(o1); if(fabsf(o2)>p2)p2=fabsf(o2);} }
    printf("%6.0f %10.4f %10.4f %10.4f %10.4f\n",note,sqrt(a1/c),sqrt(a2/c),p1,p2);
  }
  return 0;
}
