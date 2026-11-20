#pragma once
// MA7 CC91 reverb send: generic effect, inert unless a nonzero send is scheduled.
namespace ma7ReverbRegression {
SmafFile make(int format, int send, int note = 60) {
    auto f = fixture(format);
    setVoice(f, std::vector<uint8_t>{0x43,0x79,7,0x7f,1,0,0,0,0,0,0,1,0,
        0,0xf0,0xf0,0xfc,0,0x10,0,0,0xf0,0xf0,0,0,0x10,0});
    const uint8_t cc = format == 3 ? 0x30 : 0xb0, on = format == 3 ? 0x10 : 0x90;
    auto& s = f.tracks[0].sequenceData;
    s = {0, cc, 7, 127};
    if (send >= 0) s.insert(s.end(), {0, cc, 91, uint8_t(send)});
    s.insert(s.end(), {0, on, uint8_t(note), 100, 80, 80, 0xff, 0x2f, 0});
    return f;
}
std::vector<float> render(const SmafFile& f, int block, bool sine, bool seekFirst = false) {
    MaPlayer p;
    const bool ok = sine ? p.init(f, 8000, false, SineRenderOptions{8, FmSubframeMath::Hermite, true})
                         : p.init(f, 8000);
    check(ok, "reverb fixture initializes");
    std::vector<float> out, buffer(size_t(block) * 2);
    auto drain = [&] {
        out.clear(); int n;
        while ((n = p.render(buffer.data(), block)) > 0) {
            out.insert(out.end(), buffer.begin(), buffer.begin() + n * 2);
            check(out.size() <= 8000 * 2 * 600, "reverb render terminates");
        }
        check(p.finished(), "reverb render finishes");
    };
    drain();
    if (seekFirst) { p.seekToStart(); drain(); }
    return out;
}
double maxSide(const std::vector<float>& x) {
    double m = 0;
    for (size_t i = 0; i + 1 < x.size(); i += 2) m = std::max(m, double(std::fabs(x[i] - x[i + 1])));
    return m;
}
}
void checkMa7Reverb() {
    using namespace ma7ReverbRegression;
    for (bool sine : {false, true}) {
        const auto none = render(make(3, -1), 257, sine);
        check(render(make(3, 0), 257, sine) == none, "MA7 CC91 zero is bit-identical to no send");
        const auto wet = render(make(3, 64), 257, sine);
        check(wet != none, "MA7 CC91 send changes the output");
        for (float x : wet) check(std::isfinite(x), "reverb output finite");
        check(maxSide(wet) > 10.0 * maxSide(none) && maxSide(wet) > 1e-2, "reverb decorrelates a centered source");
        check(wet == render(make(3, 64), 1, sine) && wet == render(make(3, 64), 1024, sine),
              "reverb output is block independent");
        check(wet == render(make(3, 64), 257, sine, true), "reverb output repeats exactly after seek");
        // The tail must have drained before the render ended.
        double tail = 0;
        for (size_t i = wet.size() > 200 ? wet.size() - 200 : 0; i < wet.size(); ++i) tail = std::max(tail, double(std::fabs(wet[i])));
        check(tail < 1e-3, "reverb tail has decayed when the render ends");
        check(wet.size() >= none.size(), "reverb does not shorten the render");
        // Louder send is louder: the wet signal scales with the send.
        const auto more = render(make(3, 127), 257, sine);
        check(maxSide(more) > maxSide(wet), "a larger send adds more reverb");
        // The send is high-passed: a bass note gains much less side signal per
        // unit of output than a mid note.
        auto sideRatio = [](const std::vector<float>& x) {
            double side = 0, mid = 0;
            for (size_t i = 0; i + 1 < x.size(); i += 2) {
                side += double(x[i] - x[i + 1]) * double(x[i] - x[i + 1]);
                mid += double(x[i] + x[i + 1]) * double(x[i] + x[i + 1]);
            }
            return side / (mid + 1e-30);
        };
        check(sideRatio(render(make(3, 127, 24), 257, sine)) < 0.5 * sideRatio(render(make(3, 127, 72), 257, sine)),
              "reverb send keeps bass notes centered");
        // Mobile grammar is not evaluated: CC91 is ignored there.
        check(render(make(2, 64), 257, sine) == render(make(2, -1), 257, sine), "Mobile CC91 remains unsupported");
    }
}
