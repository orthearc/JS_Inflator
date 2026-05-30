#include "StarfieldView.h"
#include "vstgui/lib/cdrawcontext.h"
using namespace VSTGUI;
namespace yg331 {

StarfieldView::StarfieldView(const CRect& size) : CView(size) {
    rebuild();
    timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) {
        for (auto& s : stars) { s.y += s.speed; if (s.y > 1.0) { s.y -= 1.0; s.x = frac(nextRand()); } }
        invalid();
    }, 33);  // ~30 fps
    timer->start();
}
StarfieldView::~StarfieldView() { if (timer) timer->stop(); }

unsigned int StarfieldView::nextRand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void StarfieldView::rebuild() {
    stars.clear();
    for (int i = 0; i < 180; i++) {            // 180 stars, matching Arcorrect
        Star s;
        s.x = frac(nextRand()); s.y = frac(nextRand());
        s.b = 0.5 + 0.5 * frac(nextRand());
        s.size = 0.5 + 2.0 * frac(nextRand());
        s.speed = 0.0005 + 0.0015 * frac(nextRand());
        stars.push_back(s);
    }
}

void StarfieldView::setViewSize(const CRect& r, bool invalid) { CView::setViewSize(r, invalid); }

void StarfieldView::draw(CDrawContext* ctx) {
    const CRect b = getViewSize();
    ctx->setFillColor(CColor(8, 8, 16, 255));            // #080810 navy base
    ctx->drawRect(b, kDrawFilled);
    for (const auto& s : stars) {
        const CCoord px = b.left + s.x * b.getWidth();
        const CCoord py = b.top  + s.y * b.getHeight();
        const uint8_t a = (uint8_t)(180 + 60 * s.b);     // whitish-blue
        ctx->setFillColor(CColor(200, 215, 255, a));
        CRect dot(px, py, px + s.size, py + s.size);
        ctx->drawEllipse(dot, kDrawFilled);
    }
    setDirty(false);
}
} // namespace yg331
