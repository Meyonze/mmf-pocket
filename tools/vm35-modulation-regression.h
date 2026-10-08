#pragma once
// Synthetic register patches only. Explicit operator-edge references verify
// routing and profile scope, not bit-accurate MA hardware emulation.
namespace vm35_modulation_test {
std::vector<uint8_t> packFm(const std::vector<uint8_t>& direct){
    check(direct.size()>=27,"synthetic VM35 direct image complete");
    const auto* g=direct.data()+10;
    const int nops=(g[2]&7)>=2?4:2;
    check(direct.size()>=size_t(13+7*nops),"synthetic VM35 operator image complete");
    std::vector<uint8_t> raw(36,0),packed(direct.begin(),direct.begin()+10);packed[2]=6;
    raw[1]=g[0];raw[2]=g[1]&127;raw[3]=g[2]&127;
    raw[0]|=uint8_t(((g[1]&128)>>2)|((g[2]&128)>>3));
    for(int op=0;op<nops;++op){const auto* p=g+3+op*7;
        for(int j=0;j<4;++j){raw[4+op*8+j]=p[j]&127;raw[op*8]|=uint8_t((p[j]>>7)<<(3-j));}
        raw[9+op*8]=p[4];raw[10+op*8]=p[5]&127;raw[11+op*8]=p[6]&127;
        raw[8+op*8]|=uint8_t(((p[5]&128)>>2)|((p[6]&128)>>3));
    }
    packed.insert(packed.end(),raw.begin(),raw.end());return packed;
}
FmVoicePatch patch(int algorithm) {
    FmVoicePatch p{};p.algorithm=uint8_t(algorithm);p.fourOp=algorithm>=2;p.modernTimbre=true;
    for(int i=0;i<4;++i){auto& op=p.ops[i];op.ma35Multipliers=true;
        op.multi=uint8_t(i%2+1);op.tl=uint8_t(12+i*3);op.ar=15;op.dr=0;op.sr=0;op.sl=0;op.rr=15;}
    return p;
}
std::vector<float> reference(const FmVoicePatch& p,double depth,int count=2048) {
    std::array<FmOperator,4> ops;
    // Base300Hz has BLOCK4, high0, KSR0 -> rate offset2, as in voice note-on.
    const int nops=p.fourOp?4:2;
    for(int i=0;i<nops;++i){ops[i].configure(p.ops[i],48000,2,p.modernTimbre);ops[i].noteOn(300);}
    std::vector<float> result;result.reserve(count);
    for(int i=0;i<count;++i){double out=0;
        auto tick=[&](int op,double input){return double(ops[op].tick(input));};
        switch(p.algorithm){
        case 0:{const double a=tick(0,0);out=tick(1,a*depth);break;}
        case 1:{const double a=tick(0,0),b=tick(1,0);out=(a+b)*.5;break;}
        case 2:{const double a=tick(0,0),b=tick(1,0),c=tick(2,0),d=tick(3,0);out=(a+b+c+d)*.35;break;}
        case 3:{const double a=tick(0,0),b=tick(1,0),c=tick(2,b*depth);out=tick(3,(a+c)*depth);break;}
        case 4:{const double a=tick(0,0),b=tick(1,a*depth),c=tick(2,b*depth);out=tick(3,c*depth);break;}
        case 5:{const double a=tick(0,0),b=tick(1,a*depth),c=tick(2,0),d=tick(3,c*depth);out=(b+d)*.5;break;}
        case 6:{const double a=tick(0,0),b=tick(1,0),c=tick(2,b*depth),d=tick(3,c*depth);out=(a+d)*.5;break;}
        case 7:{const double a=tick(0,0),b=tick(1,0),c=tick(2,b*depth),d=tick(3,0);out=(a+c+d)*.4;break;}
        }
        result.push_back(float(out*.73f*.61f*.7));
    }
    return result;
}
std::vector<float> actual(FmVoice& voice,const FmVoicePatch& p,int count=2048){
    voice.setSampleRate(48000);voice.noteOn(p,300,.73f);voice.setVolume(.61f);
    std::vector<float> result;result.reserve(count);
    for(int i=0;i<count;++i){const float x=voice.tick();check(std::isfinite(x),"VM35 PM finite output");result.push_back(x);}
    return result;
}
void checkReference(FmVoice& voice,const FmVoicePatch& p,double depth){
    const auto a=actual(voice,p),b=reference(p,depth);
    for(size_t i=0;i<a.size();++i)check(std::fabs(a[i]-b[i])<1e-7f,"PM edge scale matches explicit operator reference");
}
}
void checkVm35Modulation(){
    using namespace vm35_modulation_test;
    FmVoice reused;
    for(int alg=0;alg<8;++alg){
        const auto vm=patch(alg);checkReference(reused,vm,4);
        auto compat=vm;compat.modernTimbre=false;checkReference(reused,compat,.42);
        compat=vm;for(auto& op:compat.ops)op.ma35Multipliers=false;
        checkReference(reused,compat,.42); // VMA/default/fallback family
        for(auto& op:compat.ops)op.ma7Multipliers=true;
        checkReference(reused,compat,.42); // native expanded MA7
        compat=vm;compat.ops[1].ma35Multipliers=false;checkReference(reused,compat,.42);
        compat=vm;compat.ops[1].ma7Multipliers=true;checkReference(reused,compat,.42);
        checkReference(reused,vm,4); // reuse must restore VM35 depth
        if(alg==1||alg==2)check(reference(vm,4)==reference(vm,.42),"additive algorithms ignore PM depth");
        else check(reference(vm,4)!=reference(vm,.42),"modulation-edge fixture distinguishes both PM depths");
    }
    // Inactive operators do not invalidate a complete two-operator family.
    auto two=patch(0);two.ops[2].ma35Multipliers=false;two.ops[3].ma7Multipliers=true;
    checkReference(reused,two,4);
    // Synthetic saw->square ratio2, TL16 is intentionally not a song fixture.
    auto square=patch(5);
    for(int i=0;i<4;++i){square.ops[i].multi=uint8_t(i%2?0:1);
        square.ops[i].wave=uint8_t(i%2?6:24);square.ops[i].tl=uint8_t(i%2?4:16);}
    auto harmonic=[&](bool modern,int h){
        square.modernTimbre=modern;FmVoice v;v.setSampleRate(48000);v.noteOn(square,400,1);v.setVolume(1);
        for(int i=0;i<4800;++i)v.tick();
        double re=0,im=0;
        for(int i=0;i<4800;++i){const double x=v.tick(),angle=6.283185307179586*200*h*i/48000;
            re+=x*std::cos(angle);im+=x*std::sin(angle);}
        return std::hypot(re,im)/2400;
    };
    check(harmonic(true,5)>harmonic(true,1)*10,"VM35 saw-square retains authored modulation harmonics");
    check(harmonic(false,1)>harmonic(false,5)*3,"legacy saw-square spectrum retains compatibility scale");
    // Source transports select the same family; score behavior stays deterministic.
    std::vector<uint8_t> direct{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,4};
    for(int i=0;i<4;++i)direct.insert(direct.end(),{0,0xf0,0xf0,uint8_t((12+i*3)<<2),0,0x10,0});
    const auto packed=packFm(direct);
    std::vector<uint8_t> shortForm{0x43,5,1,0,0};shortForm.insert(shortForm.end(),direct.begin()+10,direct.end());
    auto d=fixture();setVoice(d,direct);auto p=fixture();setVoice(p,packed);auto s=fixture();setVoice(s,shortForm);
    const auto rendered=renderAll(d);
    check(rendered==renderAll(p)&&rendered==renderAll(s),"VM35 PM agrees across direct packed short transports");
    check(rendered==renderAll(d,1)&&rendered==renderAll(d,512),"VM35 PM independent of render chunks");
    MaPlayer player;check(player.init(d,8000),"VM35 seek fixture initializes");
    std::vector<float> buffer(rendered.size());
    check(player.render(buffer.data(),int(buffer.size()/2))==int(buffer.size()/2),"VM35 seek fixture length");
    check(buffer==rendered,"VM35 whole fixture baseline");player.seekToStart();
    std::fill(buffer.begin(),buffer.end(),0);
    check(player.render(buffer.data(),int(buffer.size()/2))==int(buffer.size()/2)&&buffer==rendered,"VM35 seek replays exact PM state");
}
