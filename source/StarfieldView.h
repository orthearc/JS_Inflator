#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include <vector>

namespace yg331 {
class StarfieldView : public VSTGUI::CView {
public:
    explicit StarfieldView(const VSTGUI::CRect& size);
    ~StarfieldView() override;
    void draw(VSTGUI::CDrawContext* ctx) override;
    void setViewSize(const VSTGUI::CRect& r, bool invalid = true) override;
private:
    struct Star { double x, y, b, speed; double size; };
    void rebuild();
    std::vector<Star> stars;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
    unsigned int rng = 2463534242u;      // xorshift seed (no <random> / time)
    double frac(unsigned int v) { return (v & 0xFFFFFF) / double(0x1000000); }
    unsigned int nextRand();
};
} // namespace yg331
