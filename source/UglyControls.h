#pragma once
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/cview.h"
#include <string>
#include <vector>

namespace VSTGUI { class VST3Editor; class CBitmap; class CMultiFrameBitmap; }

namespace UglyReverb {

class UglyFaceplate final : public VSTGUI::CView {
public:
    explicit UglyFaceplate(const VSTGUI::CRect& r);
    UglyFaceplate(const UglyFaceplate& o) : VSTGUI::CView(o) {}
    VSTGUI::CBaseObject* newCopy() const override { return new UglyFaceplate(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
};

class UglyTextureOverlay final : public VSTGUI::CView {
public:
    UglyTextureOverlay(const VSTGUI::CRect& r,const char* resourceName,float alpha=1.f);
    UglyTextureOverlay(const UglyTextureOverlay& o);
    ~UglyTextureOverlay() noexcept override;
    VSTGUI::CBaseObject* newCopy() const override { return new UglyTextureOverlay(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
private:
    std::string resourceName_;
    float alpha_ {1.f};
    VSTGUI::CBitmap* bitmap_ {nullptr};
};

class UglyLogo final : public VSTGUI::CView {
public:
    explicit UglyLogo(const VSTGUI::CRect& r);
    UglyLogo(const UglyLogo& o) : VSTGUI::CView(o) {}
    VSTGUI::CBaseObject* newCopy() const override { return new UglyLogo(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
};

class UglyKnob final : public VSTGUI::CKnobBase {
public:
    UglyKnob(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag);
    UglyKnob(const UglyKnob& o);
    ~UglyKnob() noexcept override;
    VSTGUI::CBaseObject* newCopy() const override { return new UglyKnob(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
private:
    VSTGUI::CMultiFrameBitmap* strip_ {nullptr};
    int knobPixels_ {0};
};

class UglySelector final : public VSTGUI::CControl {
public:
    UglySelector(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag,
                 std::vector<std::string> labels);
    UglySelector(const UglySelector& o);
    VSTGUI::CBaseObject* newCopy() const override { return new UglySelector(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
private:
    std::vector<std::string> labels_;
};

class UglyToggle final : public VSTGUI::COnOffButton {
public:
    UglyToggle(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag);
    UglyToggle(const UglyToggle& o);
    VSTGUI::CBaseObject* newCopy() const override { return new UglyToggle(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
};

class UglyZoomControl final : public VSTGUI::CControl {
public:
    UglyZoomControl(const VSTGUI::CRect& r,VSTGUI::VST3Editor* editor,int* zoomIndex);
    UglyZoomControl(const UglyZoomControl& o);
    VSTGUI::CBaseObject* newCopy() const override { return new UglyZoomControl(*this); }
    void draw(VSTGUI::CDrawContext* c) override;
    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
private:
    VSTGUI::VST3Editor* editor_ {nullptr};
    int* zoomIndex_ {nullptr};
};

} // namespace UglyReverb
