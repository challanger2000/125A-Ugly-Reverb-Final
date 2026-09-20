#include "UglyControls.h"
#include "branding_master.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cresourcedescription.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

namespace UglyReverb {
namespace {
constexpr double kPi=3.14159265358979323846;
constexpr VSTGUI::CColor kLogoSilver {217,217,217,255};
constexpr VSTGUI::CColor kLogoRed {215,25,32,255};

struct LogoSubpath { std::vector<VSTGUI::CPoint> points; };
struct LogoPath { std::vector<LogoSubpath> subpaths; bool red {false}; };

std::vector<LogoPath> parseMasterLogo()
{
    std::vector<LogoPath> result;
    result.reserve(Branding::kMasterPathCount);
    for(const auto& source:Branding::kMasterPaths) {
        const std::string_view d {source.d};
        LogoPath path;
        path.red=source.red;
        const char* p=d.data();
        const char* end=d.data()+d.size();
        char command=0;
        LogoSubpath* current=nullptr;
        while(p<end) {
            while(p<end&&(std::isspace(static_cast<unsigned char>(*p))||*p==',')) ++p;
            if(p>=end) break;
            if(std::isalpha(static_cast<unsigned char>(*p))) {
                command=*p++;
                if(command=='Z'||command=='z') {
                    command=0;
                    current=nullptr;
                    continue;
                }
            }
            if(command!='M'&&command!='m'&&command!='L'&&command!='l') {
                ++p;
                continue;
            }
            char* next=nullptr;
            const double x=std::strtod(p,&next);
            if(next==p||next>end) break;
            p=next;
            while(p<end&&(std::isspace(static_cast<unsigned char>(*p))||*p==',')) ++p;
            const double y=std::strtod(p,&next);
            if(next==p||next>end) break;
            p=next;
            if(command=='M'||command=='m') {
                path.subpaths.emplace_back();
                current=&path.subpaths.back();
                current->points.emplace_back(x,y);
                command=(command=='M')?'L':'l';
            } else if(current) {
                current->points.emplace_back(x,y);
            }
        }
        if(!path.subpaths.empty()) result.emplace_back(std::move(path));
    }
    return result;
}

void fillRound(VSTGUI::CDrawContext* c,const VSTGUI::CRect& r,double radius,
               const VSTGUI::CColor& fill,const VSTGUI::CColor& frame,double lineWidth=1.0)
{
    auto* path=c->createRoundRectGraphicsPath(r,radius);
    if(!path) return;
    c->setFillColor(fill);
    c->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathFilled);
    c->setFrameColor(frame);
    c->setLineWidth(lineWidth);
    c->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathStroked);
    path->forget();
}

void gradientRound(VSTGUI::CDrawContext* c,const VSTGUI::CRect& r,double radius,
                   const VSTGUI::CColor& top,const VSTGUI::CColor& bottom,
                   const VSTGUI::CColor& frame,double lineWidth=1.0)
{
    auto* path=c->createRoundRectGraphicsPath(r,radius);
    if(!path) return;
    auto* gradient=VSTGUI::CGradient::create(0.0,1.0,top,bottom);
    if(gradient) {
        c->fillLinearGradient(path,*gradient,r.getTopLeft(),r.getBottomLeft(),false);
        gradient->forget();
    } else {
        c->setFillColor(bottom);
        c->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathFilled);
    }
    c->setFrameColor(frame);
    c->setLineWidth(lineWidth);
    c->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathStroked);
    path->forget();
}

void modulePanel(VSTGUI::CDrawContext* c,const VSTGUI::CRect& r,
                 const VSTGUI::CColor& top,const VSTGUI::CColor& bottom)
{
    VSTGUI::CRect shadow=r;
    shadow.offset(3.0,4.0);
    fillRound(c,shadow,8.0,{0,0,0,118},{0,0,0,0},0.0);
    gradientRound(c,r,8.0,top,bottom,{42,82,124,235},1.0);
    c->setFrameColor({112,159,204,48});
    c->setLineWidth(1.0);
    c->drawLine({r.left+12.0,r.top+2.0},{r.right-12.0,r.top+2.0});
}

void degradedPlate(VSTGUI::CDrawContext* c,const VSTGUI::CRect& r)
{
    // Battered service plate: now intentionally readable at normal DAW scale.
    VSTGUI::CRect shadow=r;
    shadow.offset(2.0,2.5);
    fillRound(c,shadow,3.0,{0,0,0,118},{0,0,0,0},0.0);
    gradientRound(c,r,3.0,{28,38,46,240},{7,12,17,244},{139,151,159,150},1.2);

    c->setLineWidth(1.2);
    c->setFrameColor({225,230,233,90});
    c->drawLine({r.left+13.0,r.top+1.0},{r.left+62.0,r.top+1.0});
    c->drawLine({r.right-78.0,r.bottom-1.0},{r.right-20.0,r.bottom-1.0});
    c->setFrameColor({151,82,42,112});
    c->drawLine({r.left+5.0,r.bottom-9.0},{r.left+25.0,r.bottom-4.0});
    c->drawLine({r.right-34.0,r.top+3.0},{r.right-15.0,r.top+6.0});

    // Old fasteners.
    c->setFillColor({69,74,78,245});
    c->drawEllipse({r.left+7.0,r.top+8.0,r.left+13.0,r.top+14.0},VSTGUI::kDrawFilled);
    c->drawEllipse({r.right-13.0,r.bottom-14.0,r.right-7.0,r.bottom-8.0},VSTGUI::kDrawFilled);
    c->setFrameColor({6,8,10,230});
    c->drawLine({r.left+7.7,r.top+11.5},{r.left+12.0,r.top+9.8});
    c->drawLine({r.right-12.0,r.bottom-12.3},{r.right-7.9,r.bottom-9.2});

    c->setFont(VSTGUI::kNormalFont,9.0,VSTGUI::kBoldFace);
    c->setFontColor({202,210,215,220});
    VSTGUI::CRect title=r;
    title.inset(22.0,5.0);
    title.bottom=title.top+14.0;
    c->drawString(VSTGUI::UTF8String("DEGRADED SPACE"),title,VSTGUI::kCenterText);

    c->setFont(VSTGUI::kNormalFont,7.0,VSTGUI::kNormalFace);
    c->setFontColor({143,156,165,195});
    VSTGUI::CRect sub=r;
    sub.inset(22.0,5.0);
    sub.top+=17.0;
    c->drawString(VSTGUI::UTF8String("SERVICE UNIT // 125A"),sub,VSTGUI::kCenterText);

    // Scrape through the plate.
    c->setFrameColor({226,231,234,74});
    c->setLineWidth(1.1);
    c->drawLine({r.left+56.0,r.bottom-9.0},{r.right-46.0,r.bottom-13.0});
    c->setFrameColor({1,5,8,88});
    c->drawLine({r.left+84.0,r.top+8.0},{r.left+138.0,r.top+11.0});
}

void warningStencil(VSTGUI::CDrawContext* c,const VSTGUI::CRect& r)
{
    // Faded stencil: readable only after a second glance.
    c->setFont(VSTGUI::kNormalFont,6.0,VSTGUI::kBoldFace);
    c->setFontColor({190,154,78,96});
    c->drawString(VSTGUI::UTF8String("CAUTION / UNSTABLE"),r,VSTGUI::kCenterText);

    c->setFrameColor({190,154,78,54});
    c->setLineWidth(0.8);
    c->drawLine({r.left+5.0,r.bottom-1.0},{r.left+26.0,r.bottom-1.0});
    c->drawLine({r.right-21.0,r.bottom-1.0},{r.right-5.0,r.bottom-1.0});

    // Two tiny missing-print gaps.
    c->setFrameColor({7,24,40,112});
    c->drawLine({r.left+27.0,r.top+2.0},{r.left+34.0,r.top+2.0});
    c->drawLine({r.right-29.0,r.top+7.0},{r.right-23.0,r.top+7.0});
}


VSTGUI::CBitmap* createMultiResolutionBitmap(const std::string& resourceName)
{
    auto* bitmap=new VSTGUI::CBitmap(VSTGUI::CResourceDescription(resourceName.c_str()));
    const auto dot=resourceName.rfind(".png");
    if(dot!=std::string::npos) {
        const std::string hiName=resourceName.substr(0,dot)+"#2.0x.png";
        VSTGUI::CBitmap hiBitmap(VSTGUI::CResourceDescription(hiName.c_str()));
        if(auto hiPlatform=hiBitmap.getPlatformBitmap()) {
            hiPlatform->setScaleFactor(2.0);
            bitmap->addBitmap(hiPlatform);
        }
    }
    return bitmap;
}


} // namespace

UglyFaceplate::UglyFaceplate(const VSTGUI::CRect& r):VSTGUI::CView(r)
{
    setMouseEnabled(false);
}

void UglyFaceplate::draw(VSTGUI::CDrawContext* c)
{
    const auto r=getViewSize();
    c->setDrawMode(VSTGUI::kAntiAliasing|VSTGUI::kNonIntegralMode);

    // Almost-black chassis. It remains visible only as the outer frame,
    // header and gutters between the large blue modules.
    c->setFillColor({5,7,10,255});
    c->drawRect(r,VSTGUI::kDrawFilled);

    VSTGUI::CRect header(r.left,r.top,r.right,r.top+60.0);
    gradientRound(c,header,0.0,{18,22,28,255},{7,9,13,255},{20,25,31,255},1.0);
    c->setFrameColor({48,109,164,180});
    c->setLineWidth(1.0);
    c->drawLine({r.left+16.0,r.top+59.0},{r.right-16.0,r.top+59.0});

    modulePanel(c,{r.left+16.0,r.top+70.0,r.left+278.0,r.top+414.0},
                {22,67,112,255},{10,34,61,255});
    modulePanel(c,{r.left+288.0,r.top+70.0,r.left+618.0,r.top+414.0},
                {18,59,103,255},{8,30,56,255});
    modulePanel(c,{r.left+628.0,r.top+70.0,r.left+744.0,r.top+414.0},
                {24,74,121,255},{10,37,67,255});

    // Free space below the left controls becomes a more visible battered service plate.
    degradedPlate(c,{r.left+30.0,r.top+359.0,r.left+264.0,r.top+405.0});

    // Small faded warning stencil in the deliberately unstable network.
    warningStencil(c,{r.left+493.0,r.top+219.0,r.left+594.0,r.top+231.0});

    // Small chassis feet/details keep the black frame from looking flat.
    c->setFillColor({23,27,33,255});
    c->drawEllipse({r.left+7,r.top+7,r.left+13,r.top+13},VSTGUI::kDrawFilled);
    c->drawEllipse({r.right-13,r.top+7,r.right-7,r.top+13},VSTGUI::kDrawFilled);
    c->drawEllipse({r.left+7,r.bottom-13,r.left+13,r.bottom-7},VSTGUI::kDrawFilled);
    c->drawEllipse({r.right-13,r.bottom-13,r.right-7,r.bottom-7},VSTGUI::kDrawFilled);
    setDirty(false);
}

UglyTextureOverlay::UglyTextureOverlay(const VSTGUI::CRect& r,const char* resourceName,float alpha)
: VSTGUI::CView(r),resourceName_(resourceName?resourceName:""),alpha_(alpha)
{
    setMouseEnabled(false);
    setTransparency(true);
    bitmap_=createMultiResolutionBitmap(resourceName_);
}

UglyTextureOverlay::UglyTextureOverlay(const UglyTextureOverlay& o)
: VSTGUI::CView(o),resourceName_(o.resourceName_),alpha_(o.alpha_)
{
    setMouseEnabled(false);
    setTransparency(true);
    bitmap_=createMultiResolutionBitmap(resourceName_);
}

UglyTextureOverlay::~UglyTextureOverlay() noexcept
{
    if(bitmap_) bitmap_->forget();
}

void UglyTextureOverlay::draw(VSTGUI::CDrawContext* c)
{
    if(bitmap_ && bitmap_->isLoaded())
        bitmap_->draw(c,getViewSize(),VSTGUI::CPoint{0,0},alpha_);
    setDirty(false);
}

UglyLogo::UglyLogo(const VSTGUI::CRect& r):VSTGUI::CView(r)
{
    setMouseEnabled(false);
}

void UglyLogo::draw(VSTGUI::CDrawContext* c)
{
    static const auto logo=parseMasterLogo();
    const auto r=getViewSize();
    constexpr double masterWidth=1774.0;
    constexpr double masterHeight=887.0;
    const double scale=std::min(r.getWidth()/masterWidth,r.getHeight()/masterHeight);
    const double x0=r.left+(r.getWidth()-masterWidth*scale)*0.5;
    const double y0=r.top+(r.getHeight()-masterHeight*scale)*0.5;

    c->setDrawMode(VSTGUI::kAntiAliasing);
    for(const auto& sourcePath:logo) {
        auto* path=c->createGraphicsPath();
        if(!path) continue;
        for(const auto& subpath:sourcePath.subpaths) {
            if(subpath.points.empty()) continue;
            const auto toView=[&](const VSTGUI::CPoint& p) {
                return VSTGUI::CPoint{x0+p.x*scale,y0+p.y*scale};
            };
            path->beginSubpath(toView(subpath.points.front()));
            for(std::size_t i=1;i<subpath.points.size();++i)
                path->addLine(toView(subpath.points[i]));
            path->closeSubpath();
        }
        c->setFillColor(sourcePath.red?kLogoRed:kLogoSilver);
        c->drawGraphicsPath(path,VSTGUI::CDrawContext::kPathFilledEvenOdd);
        path->forget();
    }
    setDirty(false);
}

namespace {
VSTGUI::CMultiFrameBitmap* createKnobStripForSize(int logicalSize)
{
    const int size=(logicalSize<=62)?62:((logicalSize<=64)?64:66);
    const std::string base="ugly_knob_"+std::to_string(size)+".png";
    const std::string hi="ugly_knob_"+std::to_string(size)+"#2.0x.png";

    VSTGUI::CMultiFrameBitmapDescription desc;
    desc.frameSize={static_cast<double>(size),static_cast<double>(size)};
    desc.numFrames=101;
    desc.framesPerRow=1;

    auto* strip=new VSTGUI::CMultiFrameBitmap(VSTGUI::CResourceDescription(base.c_str()),desc);
    VSTGUI::CBitmap hiBitmap(VSTGUI::CResourceDescription(hi.c_str()));
    if(auto hiPlatform=hiBitmap.getPlatformBitmap()) {
        hiPlatform->setScaleFactor(2.0);
        strip->addBitmap(hiPlatform);
    }
    return strip;
}
} // namespace

UglyKnob::UglyKnob(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag)
: VSTGUI::CKnobBase(r,l,tag,nullptr)
{
    knobPixels_=static_cast<int>(std::lround(std::min(r.getWidth(),r.getHeight())));
    strip_=createKnobStripForSize(knobPixels_);
    setTransparency(true);
    setWantsFocus(true);
}

UglyKnob::UglyKnob(const UglyKnob& o)
: VSTGUI::CKnobBase(o),knobPixels_(o.knobPixels_)
{
    strip_=createKnobStripForSize(knobPixels_);
    setTransparency(true);
    setWantsFocus(true);
}

UglyKnob::~UglyKnob() noexcept
{
    if(strip_) strip_->forget();
}

void UglyKnob::draw(VSTGUI::CDrawContext* c)
{
    if(strip_ && strip_->isLoaded()) {
        const auto index=strip_->normalizedValueToFrameIndex(getValueNormalized());
        strip_->drawFrame(c,index,getViewSize().getTopLeft());
    }
    setDirty(false);
}

UglySelector::UglySelector(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag,
                           std::vector<std::string> labels)
: VSTGUI::CControl(r,l,tag,nullptr),labels_(std::move(labels))
{
    setTransparency(true);
    setWantsFocus(true);
}

UglySelector::UglySelector(const UglySelector& o)
: VSTGUI::CControl(o),labels_(o.labels_) {}

void UglySelector::draw(VSTGUI::CDrawContext* c)
{
    const auto r=getViewSize();
    if(labels_.empty()) { setDirty(false); return; }

    c->setDrawMode(VSTGUI::kAntiAliasing|VSTGUI::kNonIntegralMode);
    VSTGUI::CRect shadow=r;
    shadow.offset(1.5,2.0);
    fillRound(c,shadow,5.0,{0,0,0,100},{0,0,0,0},0.0);
    fillRound(c,r,5.0,{4,7,10,255},{61,78,94,255},1.0);

    VSTGUI::CRect inner=r;
    inner.inset(3.0,3.0);
    const int count=static_cast<int>(labels_.size());
    const int selected=std::clamp(
        static_cast<int>(std::lround(getValueNormalized()*static_cast<double>(std::max(1,count-1)))),
        0,count-1);
    const double segW=inner.getWidth()/static_cast<double>(count);

    c->setFont(VSTGUI::kNormalFont,7.5,VSTGUI::kBoldFace);
    for(int i=0;i<count;++i) {
        VSTGUI::CRect seg(inner.left+i*segW,inner.top,
                          i==count-1?inner.right:inner.left+(i+1)*segW,inner.bottom);
        VSTGUI::CRect face=seg;
        face.inset(1.0,1.0);
        if(i==selected) {
            gradientRound(c,face,3.0,{226,231,236,255},{116,127,139,255},
                          {239,244,248,120},1.0);
            c->setFontColor({10,16,22,255});
        } else {
            fillRound(c,face,3.0,{15,23,31,255},{37,52,66,255},1.0);
            c->setFontColor({150,170,188,255});
        }
        c->drawString(VSTGUI::UTF8String(labels_[i].c_str()),face,VSTGUI::kCenterText);
    }
    setDirty(false);
}

VSTGUI::CMouseEventResult UglySelector::onMouseDown(VSTGUI::CPoint& where,
                                                     const VSTGUI::CButtonState& buttons)
{
    if(!buttons.isLeftButton()||labels_.empty())
        return VSTGUI::kMouseEventNotHandled;
    const auto r=getViewSize();
    if(!r.pointInside(where))
        return VSTGUI::kMouseEventNotHandled;

    const int count=static_cast<int>(labels_.size());
    const double normalized=(where.x-r.left)/std::max(1.0,r.getWidth());
    const int index=std::clamp(static_cast<int>(normalized*count),0,count-1);
    const float value=count<=1?0.f:static_cast<float>(index)/static_cast<float>(count-1);

    beginEdit();
    setValueNormalized(value);
    valueChanged();
    endEdit();
    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

UglyToggle::UglyToggle(const VSTGUI::CRect& r,VSTGUI::IControlListener* l,int32_t tag)
: VSTGUI::COnOffButton(r,l,tag,nullptr)
{
    setTransparency(true);
    setWantsFocus(true);
}

UglyToggle::UglyToggle(const UglyToggle& o):VSTGUI::COnOffButton(o) {}

void UglyToggle::draw(VSTGUI::CDrawContext* c)
{
    const auto r=getViewSize();
    const bool on=getValueNormalized()>=0.5;
    c->setDrawMode(VSTGUI::kAntiAliasing|VSTGUI::kNonIntegralMode);

    VSTGUI::CRect shadow=r;
    shadow.offset(1.5,2.5);
    fillRound(c,shadow,r.getHeight()*0.5,{0,0,0,110},{0,0,0,0},0.0);
    fillRound(c,r,r.getHeight()*0.5,{3,6,9,255},{72,86,100,255},1.0);

    VSTGUI::CRect track=r;
    track.inset(3.0,3.0);
    fillRound(c,track,track.getHeight()*0.5,
              on?VSTGUI::CColor{18,62,99,255}:VSTGUI::CColor{11,17,23,255},
              on?VSTGUI::CColor{65,150,213,220}:VSTGUI::CColor{31,43,55,255},1.0);

    const double d=track.getHeight()-4.0;
    const double cy=track.getCenter().y;
    const double cx=on?(track.right-2.0-d*0.5):(track.left+2.0+d*0.5);
    VSTGUI::CRect thumb(cx-d*0.5,cy-d*0.5,cx+d*0.5,cy+d*0.5);
    auto* thumbPath=c->createRoundRectGraphicsPath(thumb,d*0.5);
    if(thumbPath) {
        auto* silver=VSTGUI::CGradient::create(0.0,1.0,
            VSTGUI::CColor{235,239,243,255},VSTGUI::CColor{112,122,133,255});
        if(silver) {
            c->fillLinearGradient(thumbPath,*silver,thumb.getTopLeft(),thumb.getBottomLeft(),false);
            silver->forget();
        }
        c->setFrameColor({28,33,39,255});
        c->setLineWidth(1.0);
        c->drawGraphicsPath(thumbPath,VSTGUI::CDrawContext::kPathStroked);
        thumbPath->forget();
    }

    c->setFont(VSTGUI::kNormalFont,7.0,VSTGUI::kBoldFace);
    c->setFontColor(on?VSTGUI::CColor{126,198,246,255}:VSTGUI::CColor{118,132,145,255});
    VSTGUI::CRect textRect=track;
    if(on) textRect.right=thumb.left-1.0;
    else textRect.left=thumb.right+1.0;
    c->drawString(VSTGUI::UTF8String(on?"ON":"OFF"),textRect,VSTGUI::kCenterText);
    setDirty(false);
}

UglyZoomControl::UglyZoomControl(const VSTGUI::CRect& r,VSTGUI::VST3Editor* editor,int* zoomIndex)
: VSTGUI::CControl(r,nullptr,-1,nullptr),editor_(editor),zoomIndex_(zoomIndex)
{
    setTransparency(true);
    setWantsFocus(true);
}

UglyZoomControl::UglyZoomControl(const UglyZoomControl& o)
: VSTGUI::CControl(o),editor_(o.editor_),zoomIndex_(o.zoomIndex_) {}

void UglyZoomControl::draw(VSTGUI::CDrawContext* c)
{
    const auto r=getViewSize();
    c->setDrawMode(VSTGUI::kAntiAliasing|VSTGUI::kNonIntegralMode);

    const double gap=4.0;
    const double w=(r.getWidth()-gap)*0.5;
    VSTGUI::CRect minus(r.left,r.top,r.left+w,r.bottom);
    VSTGUI::CRect plus(r.right-w,r.top,r.right,r.bottom);

    auto drawButton=[&](const VSTGUI::CRect& b,const char* text) {
        VSTGUI::CRect sh=b; sh.offset(1.2,1.8);
        fillRound(c,sh,4.0,{0,0,0,110},{0,0,0,0},0.0);
        gradientRound(c,b,4.0,{66,74,83,255},{24,29,35,255},{117,128,139,210},1.0);
        c->setFrameColor({255,255,255,45});
        c->drawLine({b.left+4,b.top+2},{b.right-4,b.top+2});
        c->setFont(VSTGUI::kNormalFont,11.0,VSTGUI::kBoldFace);
        c->setFontColor({225,232,238,255});
        c->drawString(VSTGUI::UTF8String(text),b,VSTGUI::kCenterText);
    };
    drawButton(minus,"-");
    drawButton(plus,"+");
    setDirty(false);
}

VSTGUI::CMouseEventResult UglyZoomControl::onMouseDown(
    VSTGUI::CPoint& where,const VSTGUI::CButtonState& buttons)
{
    if(!buttons.isLeftButton() || !editor_ || !getViewSize().pointInside(where))
        return VSTGUI::kMouseEventNotHandled;

    static constexpr double zooms[] {1.0,1.25,1.5,1.75,2.0};
    const double current=editor_->getZoomFactor();
    int index=0;
    double best=std::abs(current-zooms[0]);
    for(int i=1;i<5;++i) {
        const double d=std::abs(current-zooms[i]);
        if(d<best) { best=d; index=i; }
    }

    const bool plus=where.x>=getViewSize().getCenter().x;
    index=std::clamp(index+(plus?1:-1),0,4);
    if (zoomIndex_) *zoomIndex_=index;
    editor_->setZoomFactor(zooms[index]);
    invalid();
    return VSTGUI::kMouseDownEventHandledButDontNeedMovedOrUpEvents;
}

} // namespace UglyReverb
