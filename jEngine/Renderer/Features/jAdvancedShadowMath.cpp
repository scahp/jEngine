#include "jAdvancedShadowMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

// Sources retained read-only: scahp/Shadows CSM 3b7a12ba/d87f1e7a,
// PSM 4d37398e:jGame.cpp, DualParaboloid ccfec06c and master jShadowVolume.cpp.
// The original GLSL/OpenGL ownership is replaced with bounded plain CPU data.
namespace jAdvancedShadowMath
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double MaximumCoordinate = 1.0e9;
bool Reject(std::string& error, const char* reason) { error = reason; return false; }
bool Finite(double value, double bound = MaximumCoordinate)
{ return std::isfinite(value) && std::abs(value) <= bound; }
bool Finite(const Vec3& value)
{ return Finite(value[0]) && Finite(value[1]) && Finite(value[2]); }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Vec3 Scale(const Vec3& a, double scale) { return {a[0]*scale,a[1]*scale,a[2]*scale}; }
double Dot(const Vec3& a, const Vec3& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Vec3 Cross(const Vec3& a, const Vec3& b)
{ return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double Length(const Vec3& value) { return std::hypot(std::hypot(value[0],value[1]),value[2]); }
bool Unit(const Vec3& input, Vec3& output)
{
    const double length = Length(input);
    if (!std::all_of(input.begin(),input.end(),[](double x){return std::isfinite(x);}) || !std::isfinite(length) || length <= 0.0) return false;
    // Divide components directly: reciprocal(length) can overflow for a valid
    // subnormal direction. Derived light-to-vertex vectors may also exceed the
    // authored coordinate bound even though each input coordinate is valid.
    output = {input[0]/length,input[1]/length,input[2]/length}; return true;
}
bool MatrixFinite(const Mat4& matrix)
{ return std::all_of(matrix.begin(),matrix.end(),[](double x){return Finite(x,1.0e30);}); }
bool FrustumValid(const FrustumCorners& corners)
{
    for (const auto& point : corners) if (!Finite(point)) return false;
    // The engine's frustum corner order need not be a perimeter. Find the best
    // independent near/far face edges, rather than assuming a triangle winding.
    double nearArea = 0.0, farArea = 0.0;
    Vec3 normal{};
    for (std::size_t a=1;a<4;++a) for (std::size_t b=a+1;b<4;++b)
    {
        const auto cross = Cross(Sub(corners[a],corners[0]),Sub(corners[b],corners[0]));
        if (Length(cross)>nearArea) { nearArea=Length(cross); normal=cross; }
        farArea=std::max(farArea,Length(Cross(Sub(corners[a+4],corners[4]),Sub(corners[b+4],corners[4]))));
    }
    if (nearArea<=1.0e-18 || farArea<=1.0e-18) return false;
    Vec3 separation{};
    for (std::size_t i=0;i<4;++i) separation=Add(separation,Sub(corners[i+4],corners[i]));
    return Length(separation)>1.0e-12 &&
        std::abs(Dot(normal,separation))>1.0e-12*nearArea*Length(separation);
}
bool Basis(const Vec3& direction, Vec3& x, Vec3& y, Vec3& z)
{
    if (!Unit(direction,z)) return false;
    const Vec3 up=std::abs(z[1])>.95?Vec3{0,0,1}:Vec3{0,1,0};
    if (!Unit(Cross(up,z),x)) return false;
    y=Cross(z,x); return true;
}
bool ProjectBounds(const FrustumCorners& points,const Vec3& x,const Vec3& y,const Vec3& z,Vec3& lower,Vec3& upper)
{
    lower.fill(std::numeric_limits<double>::infinity());upper.fill(-std::numeric_limits<double>::infinity());
    for(const auto& point:points)
    {
        if(!Finite(point))return false;
        const Vec3 local{Dot(x,point),Dot(y,point),Dot(z,point)};
        for(std::size_t axis=0;axis<3;++axis){lower[axis]=std::min(lower[axis],local[axis]);upper[axis]=std::max(upper[axis],local[axis]);}
    }
    return true;
}
Mat4 View(const Vec3& eye,const Vec3& x,const Vec3& y,const Vec3& z)
{
    return {x[0],y[0],z[0],0,x[1],y[1],z[1],0,x[2],y[2],z[2],0,
        -Dot(x,eye),-Dot(y,eye),-Dot(z,eye),1};
}
bool FitOrthographic(const FrustumCorners& corners,const Vec3& direction,double padding,
    bool stabilize,std::uint32_t resolution,bool reverse,Mat4& output,double& texel)
{
    Vec3 x,y,z;
    if (!Basis(direction,x,y,z)) return false;
    Vec3 center{};
    for (const auto& corner:corners) center=Add(center,Scale(corner,1.0/8.0));
    double radius=0.0;
    for (const auto& corner:corners) radius=std::max(radius,Length(Sub(corner,center)));
    if (!std::isfinite(radius) || radius<=1.0e-10) return false;
    if (stabilize) radius=std::ceil(radius*16.0)/16.0;
    // Extra texel guards keep all corners contained after center snapping.
    if (resolution>2) radius*=double(resolution)/double(resolution-2);
    texel=2.0*radius/double(resolution);
    double centerX=Dot(x,center),centerY=Dot(y,center);
    if (stabilize) { centerX=std::round(centerX/texel)*texel; centerY=std::round(centerY/texel)*texel; }
    double loZ=std::numeric_limits<double>::infinity(),hiZ=-loZ;
    for (const auto& corner:corners) { const double depth=Dot(z,corner); loZ=std::min(loZ,depth); hiZ=std::max(hiZ,depth); }
    loZ-=padding;hiZ+=padding;
    if (hiZ-loZ<=1.0e-10) return false;
    const double sz=(reverse?-1.0:1.0)/(hiZ-loZ);
    const double tz=reverse?hiZ/(hiZ-loZ):-loZ/(hiZ-loZ);
    output={x[0]/radius,y[0]/radius,sz*z[0],0,
        x[1]/radius,y[1]/radius,sz*z[1],0,x[2]/radius,y[2]/radius,sz*z[2],0,
        -centerX/radius,-centerY/radius,tz,1};
    return MatrixFinite(output);
}
void FlipDepth(Mat4& matrix)
{ for(std::size_t column=0;column<4;++column)matrix[column*4+2]=matrix[column*4+3]-matrix[column*4+2]; }
bool Depth(double value) { return std::isfinite(value) && value>=0.0 && value<=1.0; }
bool Exponent(double value) { return std::isfinite(value) && value>=0.1 && value<=MaximumMomentExponent; }
double Chebyshev(double first,double second,double receiver,double minimumVariance,double bleed)
{
    if(receiver<=first)return 1.0;
    const double variance=std::max(second-first*first,minimumVariance);
    const double delta=receiver-first;
    const double probability=variance/(variance+delta*delta);
    return std::clamp((probability-bleed)/(1.0-bleed),0.0,1.0);
}
bool VarianceParameters(double variance,double bleed)
{return std::isfinite(variance)&&variance>0.0&&variance<=1.0&&std::isfinite(bleed)&&bleed>=0.0&&bleed<1.0;}
bool LayerValid(const DeepLayer& layer) { return Depth(layer.Depth)&&Depth(layer.Opacity); }
using Edge=std::pair<std::uint32_t,std::uint32_t>;
Edge EdgeKey(std::uint32_t a,std::uint32_t b) {return std::minmax(a,b);}
bool IsCancelled(const std::function<bool()>& cancelled) {return cancelled&&cancelled();}
}

Mat4 Identity() noexcept { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
Mat4 Multiply(const Mat4& a,const Mat4& b) noexcept
{
    Mat4 result{};
    for(std::size_t c=0;c<4;++c)for(std::size_t r=0;r<4;++r)
        for(std::size_t k=0;k<4;++k)result[c*4+r]+=a[k*4+r]*b[c*4+k];
    return result;
}
Vec4 Transform(const Mat4& matrix,const Vec4& value) noexcept
{
    Vec4 result{};
    for(std::size_t r=0;r<4;++r)for(std::size_t c=0;c<4;++c)result[r]+=matrix[c*4+r]*value[c];
    return result;
}
bool ProjectPoint(const Mat4& matrix,const Vec3& point,Vec3& output) noexcept
{
    const auto clip=Transform(matrix,{point[0],point[1],point[2],1});
    if(!std::all_of(clip.begin(),clip.end(),[](double x){return std::isfinite(x);})||std::abs(clip[3])<1.0e-14)return false;
    Vec3 projected{clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]};
    if(!std::all_of(projected.begin(),projected.end(),[](double x){return std::isfinite(x);}))return false;
    output=projected;return true;
}
bool BuildCascades(const CascadeInput& input,CascadeResult& output,std::string& error)
{
    error.clear();Vec3 direction;
    if(!FrustumValid(input.Corners)||!Unit(input.LightDirection,direction)||
        !std::isfinite(input.NearDistance)||input.NearDistance<1.0e-5||
        !Finite(input.FarDistance)||input.FarDistance<=input.NearDistance||input.Count<1||input.Count>MaximumCascades||
        input.Resolution<4||input.Resolution>16384||!Depth(input.SplitLambda)||
        !Finite(input.CasterPadding)||input.CasterPadding<0.0)
        return Reject(error,"CSM requires a finite nondegenerate frustum, positive near/far, 1..4 cascades and bounded settings.");
    CascadeResult candidate;candidate.Count=input.Count;candidate.Splits[0]=input.NearDistance;
    for(std::uint32_t i=1;i<=input.Count;++i)
    {
        const double fraction=double(i)/input.Count;
        const double logarithmic=input.NearDistance*std::pow(input.FarDistance/input.NearDistance,fraction);
        const double uniform=input.NearDistance+(input.FarDistance-input.NearDistance)*fraction;
        candidate.Splits[i]=i==input.Count?input.FarDistance:input.SplitLambda*logarithmic+(1.0-input.SplitLambda)*uniform;
        auto& cascade=candidate.Cascades[i-1];cascade.NearDistance=candidate.Splits[i-1];cascade.FarDistance=candidate.Splits[i];
        const double lo=(cascade.NearDistance-input.NearDistance)/(input.FarDistance-input.NearDistance);
        const double hi=(cascade.FarDistance-input.NearDistance)/(input.FarDistance-input.NearDistance);
        for(std::size_t corner=0;corner<4;++corner)
        {
            const auto ray=Sub(input.Corners[corner+4],input.Corners[corner]);
            cascade.Corners[corner]=Add(input.Corners[corner],Scale(ray,lo));
            cascade.Corners[corner+4]=Add(input.Corners[corner],Scale(ray,hi));
        }
        if(!FitOrthographic(cascade.Corners,direction,input.CasterPadding,input.Stabilize,input.Resolution,
            input.ReverseZ,cascade.WorldToShadow,cascade.WorldUnitsPerTexel))
            return Reject(error,"CSM light-space fitting is degenerate.");
    }
    output=candidate;return true;
}
bool BuildDirectionalFit(const DirectionalFitInput& input,DirectionalFitResult& output,std::string& error)
{
    error.clear();Vec3 x,y,z;
    if(!FrustumValid(input.ViewCorners)||!Basis(input.LightDirection,x,y,z)||
        input.ReceiverBoxes.empty()||input.CasterBoxes.empty()||input.ReceiverBoxes.size()>MaximumDirectionalFitBoxes||
        input.CasterBoxes.size()>MaximumDirectionalFitBoxes||input.Resolution<4||input.Resolution>16384||
        !std::isfinite(input.CasterPadding)||input.CasterPadding<0.0||input.CasterPadding>1.0e5)
        return Reject(error,"Directional fitting requires a finite view/light, nonempty bounded receiver/caster boxes and valid resolution/padding.");
    Vec3 viewLo,viewHi;
    if(!ProjectBounds(input.ViewCorners,x,y,z,viewLo,viewHi))return Reject(error,"Directional view corners are invalid.");
    DirectionalFitResult candidate;
    candidate.LightSpaceMinimum.fill(std::numeric_limits<double>::infinity());
    candidate.LightSpaceMaximum.fill(-std::numeric_limits<double>::infinity());
    for(const auto& box:input.ReceiverBoxes)
    {
        Vec3 lower,upper;
        if(!ProjectBounds(box,x,y,z,lower,upper))return Reject(error,"Directional receiver box is nonfinite/out of bounds.");
        bool intersects=true;
        for(std::size_t axis=0;axis<3;++axis)
        {lower[axis]=std::max(lower[axis],viewLo[axis]);upper[axis]=std::min(upper[axis],viewHi[axis]);intersects&=lower[axis]<=upper[axis];}
        if(!intersects)continue;
        ++candidate.ReceiverBoxCount;
        for(std::size_t axis=0;axis<3;++axis)
        {candidate.LightSpaceMinimum[axis]=std::min(candidate.LightSpaceMinimum[axis],lower[axis]);candidate.LightSpaceMaximum[axis]=std::max(candidate.LightSpaceMaximum[axis],upper[axis]);}
    }
    if(!candidate.ReceiverBoxCount)return Reject(error,"Directional fitting has no receiver bounds overlapping the view.");
    for(std::size_t axis=0;axis<2;++axis)
    {
        const double extent=candidate.LightSpaceMaximum[axis]-candidate.LightSpaceMinimum[axis];
        if(!std::isfinite(extent)||extent<=1.0e-9)return Reject(error,"Directional receiver footprint is degenerate.");
        double half=extent*.5,center=candidate.LightSpaceMinimum[axis]+half;
        if(input.Stabilize)
        {
            // Reserve a texel around the raw intersection before snapping. A
            // half-texel center move cannot clip the original receiver bounds.
            half*=double(input.Resolution)/double(input.Resolution-2);
            const double texel=2.0*half/input.Resolution;
            center=std::round(center/texel)*texel;
        }
        candidate.LightSpaceMinimum[axis]=center-half;
        candidate.LightSpaceMaximum[axis]=center+half;
        candidate.WorldUnitsPerTexel[axis]=2.0*half/input.Resolution;
    }
    // Off-camera casters can still shadow visible receivers. Only reject a
    // caster for a disjoint light-XY footprint or if it is entirely downstream
    // of every receiver; never clip its near depth to the camera's depth box.
    for(const auto& box:input.CasterBoxes)
    {
        Vec3 lower,upper;
        if(!ProjectBounds(box,x,y,z,lower,upper))return Reject(error,"Directional caster box is nonfinite/out of bounds.");
        if(upper[0]<candidate.LightSpaceMinimum[0]||lower[0]>candidate.LightSpaceMaximum[0]||
            upper[1]<candidate.LightSpaceMinimum[1]||lower[1]>candidate.LightSpaceMaximum[1]||
            lower[2]>candidate.LightSpaceMaximum[2])continue;
        ++candidate.OverlappingCasterBoxCount;
        candidate.LightSpaceMinimum[2]=std::min(candidate.LightSpaceMinimum[2],lower[2]);
    }
    if(!candidate.OverlappingCasterBoxCount)return Reject(error,"Directional fitting has no overlapping upstream caster bounds.");
    candidate.LightSpaceMinimum[2]-=input.CasterPadding;
    const auto& lower=candidate.LightSpaceMinimum;const auto& upper=candidate.LightSpaceMaximum;
    const double depthRange=upper[2]-lower[2];
    if(!std::isfinite(depthRange)||depthRange<=1.0e-9)return Reject(error,"Directional fitted depth interval is degenerate.");
    const double sx=2.0/(upper[0]-lower[0]),sy=2.0/(upper[1]-lower[1]);
    const double sz=(input.ReverseZ?-1.0:1.0)/depthRange;
    candidate.WorldToShadow={sx*x[0],sy*y[0],sz*z[0],0,sx*x[1],sy*y[1],sz*z[1],0,
        sx*x[2],sy*y[2],sz*z[2],0,-.5*sx*(upper[0]+lower[0]),-.5*sy*(upper[1]+lower[1]),
        input.ReverseZ?upper[2]/depthRange:-lower[2]/depthRange,1};
    if(!MatrixFinite(candidate.WorldToShadow))return Reject(error,"Directional fitted matrix overflowed.");
    output=candidate;return true;
}
bool BuildPerspectiveShadowMap(const PerspectiveInput& input,PerspectiveResult& output,std::string& error)
{
    error.clear();Vec3 direction;
    if(!std::isfinite(input.VirtualSlideBack)||input.VirtualSlideBack<0.0||input.VirtualSlideBack>1.0e6)
        return Reject(error,"PSM virtual slide back must be finite and between 0 and 1000000 world units.");
    if(!FrustumValid(input.Corners)||!Unit(input.LightDirection,direction)||!MatrixFinite(input.CameraViewProjection)||
        !Finite(input.CasterPadding)||input.CasterPadding<0.0)
        return Reject(error,"PSM requires a finite camera matrix, nondegenerate receiver frustum and light direction.");
    const auto fallback=[&](const char* reason)
    {
        PerspectiveResult candidate;double texel=0;
        if(!FitOrthographic(input.Corners,direction,input.CasterPadding,false,2048,input.ReverseZ,candidate.WorldToShadow,texel))
            return Reject(error,"PSM fallback fitting failed.");
        candidate.OrthographicFallback=true;candidate.FallbackReason=reason;output=std::move(candidate);return true;
    };
    auto camera=input.CameraViewProjection;
    if(input.VirtualSlideBack>0.0)
    {
        // Source PSM jGame.cpp365–422 fits/slides a virtual camera before the
        // post-perspective light projection. Without it, a real near=1/far=1600
        // scene can collapse 100-unit casters to <0.05-pixel sliver triangles.
        // For a standard perspective matrix Zclip=a*Zview+b, Wclip=Zview:
        // moving the eye back S with N'=N+S, F'=F+S gives
        // Zclip'=(1+S/F)*Zclip and Wclip'=Wclip+S. The infinite-F limit needs
        // no Z scaling. Extract from normalized rows, tolerating float VP data.
        const Vec3 wRow{camera[3],camera[7],camera[11]},zRow{camera[2],camera[6],camera[10]};
        const double wLength=Length(wRow);
        if(!std::isfinite(wLength)||wLength<=1.0e-12)
            return fallback("Virtual PSM sliding requires a standard perspective camera; using fitted orthographic shadows.");
        const double coefficient=Dot(zRow,wRow)/Dot(wRow,wRow);
        const auto residual=Sub(zRow,Scale(wRow,coefficient));
        const double intercept=(camera[14]-coefficient*camera[15])/wLength;
        if(!std::isfinite(coefficient)||!std::isfinite(intercept)||coefficient<1.0-1.0e-7||intercept>=0.0||
            Length(residual)>1.0e-6*std::max(Length(zRow),wLength))
            return fallback("Virtual PSM camera has an unsupported depth mapping; using fitted orthographic shadows.");
        const double nearDistance=-intercept/coefficient;
        const double inverseFar=std::max(coefficient-1.0,0.0)/(-intercept);
        if(!std::isfinite(nearDistance)||nearDistance<=0.0||!std::isfinite(inverseFar)||
            (inverseFar>0.0&&1.0/inverseFar<=nearDistance))
            return fallback("Virtual PSM near/far extraction is unstable; using fitted orthographic shadows.");
        const double scale=1.0+input.VirtualSlideBack*inverseFar;
        for(std::size_t column=0;column<4;++column)camera[column*4+2]*=scale;
        camera[15]+=input.VirtualSlideBack*wLength;
        if(!MatrixFinite(camera))return fallback("Virtual PSM camera overflowed; using fitted orthographic shadows.");
    }
    FrustumCorners post{};Vec3 center{};
    for(std::size_t i=0;i<8;++i)
    {
        const auto& point=input.Corners[i];const auto clip=Transform(camera,{point[0],point[1],point[2],1});
        if(!std::isfinite(clip[3])||clip[3]<=1.0e-10||!ProjectPoint(camera,point,post[i]))
            return fallback("Receiver frustum crosses the virtual camera plane.");
        if(std::abs(post[i][0])>1.0001||std::abs(post[i][1])>1.0001||post[i][2]<-.0001||post[i][2]>1.0001)
            return Reject(error,"PSM camera and receiver frustum use inconsistent clip coordinates.");
        center=Add(center,Scale(post[i],1.0/8.0));
    }
    const auto light=Transform(camera,{-direction[0],-direction[1],-direction[2],0});
    const double lightMagnitude=std::max({std::abs(light[0]),std::abs(light[1]),std::abs(light[2]),1.0});
    if(std::abs(light[3])<=1.0e-6*lightMagnitude)return fallback("Post-perspective light is at infinity; use a fitted directional projection.");
    // Original negative-near trick is unsafe at a homogeneous clipping pole.
    // Preserve the real post-perspective branch for valid finite front lights,
    // and explicitly retain ordinary directional visibility for this case.
    if(light[3]<0.0)return fallback("Post-perspective light lies behind the eye; negative-near projection is unsafe.");
    const Vec3 eye{light[0]/light[3],light[1]/light[3],light[2]/light[3]};
    double radius=0.0;for(const auto& point:post)radius=std::max(radius,Length(Sub(point,center)));
    if(Length(Sub(eye,center))<=radius*(1.0+1.0e-6))return fallback("Post-perspective light lies inside the receiver bounding sphere.");
    Vec3 x,y,z;if(!Basis(Sub(center,eye),x,y,z))return fallback("Post-perspective light view is degenerate.");
    const auto view=View(eye,x,y,z);
    double nearZ=std::numeric_limits<double>::infinity(),farZ=-nearZ;
    double left=nearZ,right=-nearZ,bottom=nearZ,top=-nearZ;
    for(const auto& point:post)
    {
        const auto p=Transform(view,{point[0],point[1],point[2],1});
        if(p[2]<=1.0e-8)return fallback("Post-perspective receiver crosses the light projection pole.");
        nearZ=std::min(nearZ,p[2]);farZ=std::max(farZ,p[2]);
        left=std::min(left,p[0]/p[2]);right=std::max(right,p[0]/p[2]);
        bottom=std::min(bottom,p[1]/p[2]);top=std::max(top,p[1]/p[2]);
    }
    if(farZ-nearZ<=1.0e-10||right-left<=1.0e-10||top-bottom<=1.0e-10)return fallback("Post-perspective receiver bounds are degenerate.");
    nearZ*=1.0-1.0e-6;farZ*=1.0+1.0e-6;
    left-=1.0e-6;right+=1.0e-6;bottom-=1.0e-6;top+=1.0e-6;
    const Mat4 projection{2/(right-left),0,0,0,0,2/(top-bottom),0,0,
        -(right+left)/(right-left),-(top+bottom)/(top-bottom),farZ/(farZ-nearZ),1,
        0,0,-farZ*nearZ/(farZ-nearZ),0};
    auto matrix=Multiply(Multiply(projection,view),camera);
    const auto travel=Transform(matrix,{direction[0],direction[1],direction[2],0});
    int sign=0;
    for(const auto& point:input.Corners)
    {
        const auto clip=Transform(matrix,{point[0],point[1],point[2],1});
        if(clip[3]<=1.0e-10)return fallback("PSM final receiver clipping is not positive homogeneous.");
        const double derivative=travel[2]*clip[3]-clip[2]*travel[3];
        if(std::abs(derivative)<=1.0e-14)return fallback("PSM light-depth derivative is degenerate.");
        const int current=derivative>0.0?1:-1;
        if(sign&&sign!=current)return fallback("PSM light-depth ordering changes within the receiver frustum.");
        sign=current;
    }
    if(sign<0)FlipDepth(matrix);
    if(input.ReverseZ)FlipDepth(matrix);
    if(!MatrixFinite(matrix))return fallback("PSM final matrix is nonfinite.");
    PerspectiveResult candidate;candidate.WorldToShadow=matrix;output=std::move(candidate);return true;
}
bool ProjectDualParaboloid(const Vec3& local,double nearDistance,double farDistance,ParaboloidSample& output,std::string& error)
{
    error.clear();
    if(!Finite(local)||!std::isfinite(nearDistance)||nearDistance<0.0||!Finite(farDistance)||farDistance<=nearDistance)
        return Reject(error,"Dual paraboloid requires finite coordinates and ordered nonnegative near/far.");
    const double length=Length(local);
    if(length<=1.0e-12||length<nearDistance||length>farDistance)return Reject(error,"Dual paraboloid point lies at the light or outside the radial clip range.");
    const double denominator=length+std::abs(local[2]);
    ParaboloidSample candidate;candidate.Face=local[2]>=0?Hemisphere::Front:Hemisphere::Back;
    candidate.UV={.5+.5*local[0]/denominator,.5+.5*local[1]/denominator};
    candidate.Depth=(length-nearDistance)/(farDistance-nearDistance);output=candidate;return true;
}
bool UnprojectDualParaboloid(const ParaboloidSample& sample,double nearDistance,double farDistance,Vec3& output,std::string& error)
{
    error.clear();
    if((sample.Face!=Hemisphere::Front&&sample.Face!=Hemisphere::Back)||!Depth(sample.UV[0])||!Depth(sample.UV[1])||!Depth(sample.Depth)||
        !std::isfinite(nearDistance)||nearDistance<0||!Finite(farDistance)||farDistance<=nearDistance)
        return Reject(error,"Dual paraboloid sample/clip bounds are invalid.");
    const double x=2*sample.UV[0]-1,y=2*sample.UV[1]-1,r2=x*x+y*y;
    if(r2>1.0+1.0e-12)return Reject(error,"Dual paraboloid UV lies outside its hemisphere disk.");
    const double radius=nearDistance+sample.Depth*(farDistance-nearDistance);
    if(radius<=1.0e-12)return Reject(error,"Dual paraboloid sample resolves to the light origin.");
    const double sign=sample.Face==Hemisphere::Front?1.0:-1.0;
    output={2*x*radius/(1+r2),2*y*radius/(1+r2),sign*(1-r2)*radius/(1+r2)};return true;
}
bool SubdivideForDualParaboloid(std::span<const Vec3> positions,std::span<const std::uint32_t> indices,
    SubdivisionResult& output,std::string& error,const SubdivisionSettings& settings)
{
    error.clear();
    if(positions.empty()||positions.size()>3*MaximumVolumeTriangles||indices.empty()||indices.size()%3||
        !std::isfinite(settings.MaximumEdgeAngleDegrees)||settings.MaximumEdgeAngleDegrees<1.0||settings.MaximumEdgeAngleDegrees>90.0||
        settings.MaximumDepth>5||settings.MaximumTriangles<1||settings.MaximumTriangles>MaximumVolumeTriangles||
        indices.size()/3>settings.MaximumTriangles)
        return Reject(error,"Dual paraboloid subdivision input/angular/depth/triangle budget is invalid.");
    try
    {
        SubdivisionResult candidate;std::map<Vec3,std::uint32_t> welded;
        for(const auto& point:positions)if(!Finite(point))return Reject(error,"Subdivision contains a nonfinite/out-of-range position.");
        candidate.Mesh.Indices.reserve(indices.size());
        for(std::size_t i=0;i<indices.size();++i)
        {
            if((i&255)==0&&IsCancelled(settings.Cancelled))return Reject(error,"Dual paraboloid subdivision cancelled.");
            if(indices[i]>=positions.size())return Reject(error,"Subdivision index is outside vertex storage.");
            const auto& point=positions[indices[i]];
            if(Length(point)<=1.0e-10)return Reject(error,"Subdivision vertex lies at the light origin.");
            auto [entry,inserted]=welded.emplace(point,static_cast<std::uint32_t>(candidate.Mesh.Positions.size()));
            if(inserted)candidate.Mesh.Positions.push_back(point);
            candidate.Mesh.Indices.push_back(entry->second);
        }
        const double threshold=std::cos(settings.MaximumEdgeAngleDegrees*Pi/180.0);
        for(std::uint32_t pass=0;pass<=settings.MaximumDepth;++pass)
        {
            std::map<Edge,std::uint32_t> marked;
            const auto& current=candidate.Mesh.Indices;
            for(std::size_t i=0;i<current.size();i+=3)
            {
                if((i&255)==0&&IsCancelled(settings.Cancelled))return Reject(error,"Dual paraboloid subdivision cancelled.");
                const auto& a=candidate.Mesh.Positions[current[i]];const auto& b=candidate.Mesh.Positions[current[i+1]];const auto& c=candidate.Mesh.Positions[current[i+2]];
                const auto ab=Sub(b,a),ac=Sub(c,a);
                if(Length(Cross(ab,ac))<=1.0e-12*Length(ab)*Length(ac)||Length(ab)<=1.0e-12||Length(ac)<=1.0e-12)
                    return Reject(error,"Subdivision triangle is degenerate.");
                for(std::size_t e=0;e<3;++e)
                {
                    const auto key=EdgeKey(current[i+e],current[i+(e+1)%3]);
                    const auto& p=candidate.Mesh.Positions[key.first];const auto& q=candidate.Mesh.Positions[key.second];
                    const double cosine=Dot(Scale(p,1.0/Length(p)),Scale(q,1.0/Length(q)));
                    if(cosine<threshold-1.0e-12)marked.emplace(key,0);
                }
            }
            if(marked.empty()){candidate.Passes=pass;output=std::move(candidate);return true;}
            if(pass==settings.MaximumDepth)return Reject(error,"Dual paraboloid angular limit exceeds the subdivision depth budget.");
            std::size_t triangleCount=0;
            for(std::size_t i=0;i<current.size();i+=3)
            {
                std::size_t splits=0;for(std::size_t e=0;e<3;++e)splits+=marked.count(EdgeKey(current[i+e],current[i+(e+1)%3]));
                triangleCount+=splits+1;
                if(triangleCount>settings.MaximumTriangles)return Reject(error,"Dual paraboloid subdivision exceeds the output triangle budget.");
            }
            for(auto& [key,index]:marked)
            {
                const auto middle=Scale(Add(candidate.Mesh.Positions[key.first],candidate.Mesh.Positions[key.second]),.5);
                if(Length(middle)<=1.0e-10)return Reject(error,"Subdivision edge passes through the light origin.");
                index=static_cast<std::uint32_t>(candidate.Mesh.Positions.size());candidate.Mesh.Positions.push_back(middle);
            }
            std::vector<std::uint32_t> next;next.reserve(triangleCount*3);
            const auto emit=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c){next.insert(next.end(),{a,b,c});};
            for(std::size_t i=0;i<current.size();i+=3)
            {
                std::array<std::uint32_t,3> p{current[i],current[i+1],current[i+2]},m{};std::array<bool,3> split{};
                std::size_t count=0;
                for(std::size_t e=0;e<3;++e)if(const auto found=marked.find(EdgeKey(p[e],p[(e+1)%3]));found!=marked.end())
                    {split[e]=true;m[e]=found->second;++count;}
                if(count==0)emit(p[0],p[1],p[2]);
                else if(count==3){emit(p[0],m[0],m[2]);emit(m[0],p[1],m[1]);emit(m[2],m[1],p[2]);emit(m[0],m[1],m[2]);}
                else
                {
                    std::size_t start=0;
                    if(count==1){while(!split[start])++start;}
                    else {while(!(split[start]&&split[(start+1)%3]))++start;}
                    const auto a=p[start],b=p[(start+1)%3],c=p[(start+2)%3],ab=m[start];
                    if(count==1){emit(a,ab,c);emit(ab,b,c);}
                    else{const auto bc=m[(start+1)%3];emit(ab,b,bc);emit(a,ab,bc);emit(a,bc,c);}
                }
            }
            candidate.Mesh.Indices=std::move(next);
        }
    }
    catch(const std::exception& exception){error=std::string("Dual paraboloid subdivision failed: ")+exception.what();return false;}
    return Reject(error,"Dual paraboloid subdivision did not converge.");
}

bool PerspectiveMomentDepth(double ndcDepth,double nearDistance,double farDistance,double& output,std::string& error)
{
    error.clear();
    if(!Depth(ndcDepth)||!std::isfinite(nearDistance)||!std::isfinite(farDistance)||nearDistance<=0.0||farDistance<=nearDistance)
        return Reject(error,"Perspective moment depth requires finite NDC [0,1] and positive near<far distances.");
    // Equivalent to near*d/(far-d*(far-near)), but this positive weighted
    // denominator avoids subtractive cancellation at the far endpoint.
    const double numerator=nearDistance*ndcDepth;
    const double denominator=farDistance*(1.0-ndcDepth)+numerator;
    const double candidate=numerator/denominator;
    if(!Depth(candidate))return Reject(error,"Perspective moment depth conversion is not finite in [0,1].");
    output=candidate;return true;
}
bool VsmMoments(double depth,Vec2& output,std::string& error)
{error.clear();if(!Depth(depth))return Reject(error,"VSM depth must be finite in [0,1].");output={depth,depth*depth};return true;}
bool EsmMoment(double depth,double exponent,double& output,std::string& error)
{error.clear();if(!Depth(depth)||!Exponent(exponent))return Reject(error,"ESM depth/exponent is outside its finite bounds.");output=std::exp(exponent*depth);return true;}
bool EvsmMoments(double depth,double positiveExponent,double negativeExponent,Vec4& output,std::string& error)
{
    error.clear();if(!Depth(depth)||!Exponent(positiveExponent)||!Exponent(negativeExponent))return Reject(error,"EVSM depth/exponents are outside their finite bounds.");
    const double positive=std::exp(positiveExponent*depth),negative=-std::exp(-negativeExponent*depth);
    output={positive,positive*positive,negative,negative*negative};return true;
}
bool VsmVisibility(const Vec2& moments,double receiver,double variance,double bleed,double& output,std::string& error)
{
    error.clear();if(!Depth(receiver)||!Depth(moments[0])||!std::isfinite(moments[1])||moments[1]<0.0||!VarianceParameters(variance,bleed))
        return Reject(error,"VSM moments/receiver/variance/bleeding reduction are invalid.");
    output=Chebyshev(moments[0],moments[1],receiver,std::max(variance,1.0e-8),std::min(bleed,.999));return true;
}
bool EsmVisibility(double moment,double receiver,double exponent,double& output,std::string& error)
{
    error.clear();if(!Depth(receiver)||!Exponent(exponent)||!std::isfinite(moment)||moment<=0.0||moment>std::exp(exponent)*(1.0+1.0e-6))
        return Reject(error,"ESM filtered moment/receiver/exponent is invalid.");
    output=std::clamp(std::exp(std::log(moment)-exponent*receiver),0.0,1.0);return true;
}
bool EvsmVisibility(const Vec4& moments,double receiver,double positiveExponent,double negativeExponent,double variance,double bleed,double& output,std::string& error)
{
    error.clear();
    if(!Depth(receiver)||!Exponent(positiveExponent)||!Exponent(negativeExponent)||!VarianceParameters(variance,bleed)||
        !std::all_of(moments.begin(),moments.end(),[](double x){return std::isfinite(x);})||
        moments[0]<1.0||moments[0]>std::exp(positiveExponent)*(1.0+1.0e-6)||moments[1]<1.0||moments[1]>std::numeric_limits<float>::max()||
        moments[2]<-1.0||moments[2]>0.0||moments[3]<0.0||moments[3]>std::numeric_limits<float>::max())
        return Reject(error,"EVSM moments/receiver/parameters are invalid.");
    const double positive=std::exp(positiveExponent*receiver),negative=-std::exp(-negativeExponent*receiver);
    const double positiveDerivative=positiveExponent*positive,negativeDerivative=-negativeExponent*negative;
    const double depthVariance=std::max(variance,1.0e-8),reduction=std::min(bleed,.999);
    output=std::min(Chebyshev(moments[0],moments[1],positive,depthVariance*positiveDerivative*positiveDerivative,reduction),
        Chebyshev(moments[2],moments[3],negative,depthVariance*negativeDerivative*negativeDerivative,reduction));return true;
}
bool BuildDeepCurve(std::span<const DeepLayer> layers,std::vector<DeepKnot>& output,std::string& error,std::size_t maximum)
{
    error.clear();if(maximum<1||maximum>MaximumDeepLayers||layers.size()>maximum)return Reject(error,"Deep shadow layer count exceeds its bounded budget.");
    for(const auto& layer:layers)if(!LayerValid(layer))return Reject(error,"Deep shadow layer depth/opacity must be finite in [0,1].");
    std::vector<DeepLayer> sorted(layers.begin(),layers.end());
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.Depth<b.Depth;});
    std::vector<DeepKnot> candidate;candidate.reserve(sorted.size());double transmittance=1.0;
    for(const auto& layer:sorted)
    {
        transmittance*=1.0-layer.Opacity;
        if(!candidate.empty()&&candidate.back().Depth==layer.Depth)candidate.back().Transmittance=transmittance;
        else candidate.push_back({layer.Depth,transmittance});
    }
    output=std::move(candidate);return true;
}
bool DeepTransmittance(std::span<const DeepKnot> curve,double receiver,double& output,std::string& error)
{
    error.clear();if(curve.size()>MaximumDeepLayers||!Depth(receiver))return Reject(error,"Deep shadow curve/receiver exceeds its bounds.");
    double lastDepth=-1.0,lastTransmission=1.0,result=1.0;
    for(const auto& knot:curve)
    {
        if(!Depth(knot.Depth)||!Depth(knot.Transmittance)||knot.Depth<=lastDepth||knot.Transmittance>lastTransmission)
            return Reject(error,"Deep shadow curve must have ordered depths and nonincreasing finite transmission.");
        if(knot.Depth<=receiver)result=knot.Transmittance;
        lastDepth=knot.Depth;lastTransmission=knot.Transmittance;
    }
    output=result;return true;
}
bool BuildDeepTauBins(std::span<const DeepLayer> layers,double multiplier,double maxTau,DeepTauBins& output,std::string& error,std::size_t maximum)
{
    error.clear();
    if(maximum<1||maximum>MaximumDeepLayers||layers.size()>maximum||!std::isfinite(multiplier)||multiplier<0.0||multiplier>16.0||
        !std::isfinite(maxTau)||maxTau<.01||maxTau>32.0)return Reject(error,"Deep shadow bin count/opacity multiplier/tau bounds are invalid.");
    DeepTauBins candidate{};
    for(const auto& layer:layers)
    {
        if(!LayerValid(layer))return Reject(error,"Deep shadow layer depth/opacity must be finite in [0,1].");
        const double opacity=std::clamp(layer.Opacity*multiplier,0.0,1.0);
        const double tau=std::min(-std::log(std::max(1.0-opacity,1.0e-7)),16.0);
        const auto bin=std::min<std::size_t>(static_cast<std::size_t>(layer.Depth*8.0),7);
        candidate[bin]+=tau;
    }
    output=candidate;return true;
}
bool EvaluateDeepTauBins(const DeepTauBins& bins,double receiver,double maxTau,double& output,std::string& error)
{
    error.clear();if(!Depth(receiver)||!std::isfinite(maxTau)||maxTau<.01||maxTau>32.0||
        !std::all_of(bins.begin(),bins.end(),[](double x){return std::isfinite(x)&&x>=0.0&&x<=16.0*MaximumDeepLayers;}))
        return Reject(error,"Deep shadow bins/receiver/tau are outside their finite bounds.");
    const double scaled=receiver*8.0;const auto complete=static_cast<std::size_t>(scaled);
    double tau=std::accumulate(bins.begin(),bins.begin()+static_cast<std::ptrdiff_t>(complete),0.0);
    if(complete<bins.size())tau+=(scaled-double(complete))*bins[complete];
    output=std::exp(-std::min(tau,maxTau));return true;
}
bool BuildShadowVolume(const VolumeInput& input,VolumeMesh& output,std::string& error,const VolumeLimits& limits)
{
    error.clear();Vec3 direction;
    if(input.Positions.empty()||input.Positions.size()>3*MaximumVolumeTriangles||input.Indices.empty()||input.Indices.size()%3||
        limits.MaxInputTriangles<1||limits.MaxInputTriangles>MaximumVolumeTriangles||input.Indices.size()/3>limits.MaxInputTriangles||
        limits.MaxOutputTriangles<1||limits.MaxOutputTriangles>8*MaximumVolumeTriangles||
        !std::isfinite(input.ExtrusionDistance)||input.ExtrusionDistance<32.0||input.ExtrusionDistance>1.0e6||
        !Finite(input.Light)||(!input.PointLight&&!Unit(input.Light,direction)))
        return Reject(error,"Shadow volume requires bounded triangle storage, a finite light and extrusion in [32,1000000].");
    try
    {
        struct Face{std::array<std::uint32_t,3> Vertex{};Vec3 Normal{};bool Front=false;};
        struct Incidence{std::uint32_t A=0,B=0;std::size_t First=0,Second=std::numeric_limits<std::size_t>::max();};
        std::vector<Vec3> points;std::vector<Face> faces;std::map<Vec3,std::uint32_t> welded;
        std::vector<std::uint32_t> remap(input.Positions.size(),std::numeric_limits<std::uint32_t>::max());
        for(const auto& point:input.Positions)if(!Finite(point))return Reject(error,"Shadow volume position is nonfinite/out of bounds.");
        std::vector<std::uint32_t> indices;indices.reserve(input.Indices.size());
        for(std::size_t i=0;i<input.Indices.size();++i)
        {
            if((i&255)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
            const auto index=input.Indices[i];if(index>=input.Positions.size())return Reject(error,"Shadow volume index is outside vertex storage.");
            if(remap[index]==std::numeric_limits<std::uint32_t>::max())
            {
                if(input.WeldCoincidentPositions)
                {
                    auto [entry,inserted]=welded.emplace(input.Positions[index],static_cast<std::uint32_t>(points.size()));
                    if(inserted)points.push_back(input.Positions[index]);remap[index]=entry->second;
                }
                else{remap[index]=static_cast<std::uint32_t>(points.size());points.push_back(input.Positions[index]);}
            }
            indices.push_back(remap[index]);
        }
        const auto count=indices.size()/3;faces.reserve(count);
        std::map<Edge,Incidence> edges;
        std::vector<std::vector<std::pair<std::uint32_t,std::uint32_t>>> links(points.size());
        for(std::size_t i=0;i<count;++i)
        {
            if((i&127)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
            Face face;face.Vertex={indices[i*3],indices[i*3+1],indices[i*3+2]};
            const auto& a=points[face.Vertex[0]];const auto& b=points[face.Vertex[1]];const auto& c=points[face.Vertex[2]];
            const auto ab=Sub(b,a),ac=Sub(c,a);face.Normal=Cross(ab,ac);
            if(face.Vertex[0]==face.Vertex[1]||face.Vertex[1]==face.Vertex[2]||face.Vertex[2]==face.Vertex[0]||
                Length(ab)<=1.0e-12||Length(ac)<=1.0e-12||Length(face.Normal)<=1.0e-12*Length(ab)*Length(ac))
                return Reject(error,"Shadow volume contains a degenerate triangle.");
            for(std::size_t e=0;e<3;++e)
            {
                const auto u=face.Vertex[e],v=face.Vertex[(e+1)%3];
                auto [entry,inserted]=edges.emplace(EdgeKey(u,v),Incidence{u,v,i});
                if(!inserted)
                {
                    if(entry->second.Second!=std::numeric_limits<std::size_t>::max())return Reject(error,"Shadow volume contains a nonmanifold edge.");
                    if(entry->second.A!=v||entry->second.B!=u)return Reject(error,"Shadow volume triangle winding is inconsistent.");
                    entry->second.Second=i;
                }
                links[u].push_back({v,face.Vertex[(e+2)%3]});
            }
            faces.push_back(face);
        }
        std::vector<std::vector<std::size_t>> neighbors(count);
        for(const auto& [key,edge]:edges)
        {
            (void)key;
            if(edge.Second==std::numeric_limits<std::size_t>::max())return Reject(error,"Shadow volume requires a closed mesh; an open edge was found.");
            neighbors[edge.First].push_back(edge.Second);neighbors[edge.Second].push_back(edge.First);
        }
        // Every vertex link must be one circle; edge incidence alone misses
        // pinched vertices where two otherwise closed shells touch.
        for(std::size_t vertex=0;vertex<links.size();++vertex)
        {
            if((vertex&127)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
            std::map<std::uint32_t,std::vector<std::uint32_t>> ring;
            for(const auto& [a,b]:links[vertex]){ring[a].push_back(b);ring[b].push_back(a);}
            if(ring.empty())continue;
            for(const auto& [other,adjacent]:ring){(void)other;if(adjacent.size()!=2)return Reject(error,"Shadow volume contains a nonmanifold vertex link.");}
            std::vector<std::uint32_t> stack{ring.begin()->first};std::set<std::uint32_t> visited;
            while(!stack.empty())
            {
                if((visited.size()&127)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
                const auto v=stack.back();stack.pop_back();if(!visited.insert(v).second)continue;
                for(auto next:ring.at(v))if(!visited.count(next))stack.push_back(next);
            }
            if(visited.size()!=ring.size())return Reject(error,"Shadow volume contains a pinched/nonmanifold vertex.");
        }
        std::vector<bool> visited(count,false);
        for(std::size_t seed=0;seed<count;++seed)if(!visited[seed])
        {
            std::vector<std::size_t> stack{seed},component;visited[seed]=true;
            while(!stack.empty())
            {
                const auto face=stack.back();stack.pop_back();component.push_back(face);
                for(auto neighbor:neighbors[face])if(!visited[neighbor]){visited[neighbor]=true;stack.push_back(neighbor);}
            }
            const auto origin=points[faces[seed].Vertex[0]];double sixVolume=0.0,scale=0.0,solidAngle=0.0;
            for(auto id:component)
            {
                if((id&127)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
                const auto& face=faces[id];const auto& a=points[face.Vertex[0]];const auto& b=points[face.Vertex[1]];const auto& c=points[face.Vertex[2]];
                sixVolume+=Dot(Sub(a,origin),Cross(Sub(b,origin),Sub(c,origin)));
                scale=std::max({scale,Length(Sub(a,origin)),Length(Sub(b,origin)),Length(Sub(c,origin))});
                if(input.PointLight)
                {
                    const auto la=Sub(a,input.Light),lb=Sub(b,input.Light),lc=Sub(c,input.Light);
                    const double al=Length(la),bl=Length(lb),cl=Length(lc);
                    if(std::min({al,bl,cl})<=1.0e-10)return Reject(error,"Point light lies on a shadow-volume vertex.");
                    const auto ab=Sub(b,a),ac=Sub(c,a),ap=Sub(input.Light,a);
                    const double denominator=Dot(face.Normal,face.Normal);
                    const double v=Dot(Cross(ap,ac),face.Normal)/denominator;
                    const double w=Dot(Cross(ab,ap),face.Normal)/denominator;
                    const double planeDistance=std::abs(Dot(face.Normal,ap))/Length(face.Normal);
                    if(planeDistance<=1.0e-10*std::max({Length(ab),Length(ac),1.0})&&v>=-1.0e-10&&w>=-1.0e-10&&v+w<=1.0+1.0e-10)
                        return Reject(error,"Point light lies on a shadow-volume face or edge.");
                    solidAngle+=2.0*std::atan2(Dot(la,Cross(lb,lc)),al*bl*cl+Dot(la,lb)*cl+Dot(lb,lc)*al+Dot(lc,la)*bl);
                }
            }
            if(sixVolume<=1.0e-12*scale*scale*scale)return Reject(error,"Shadow volume shell has zero volume or inward winding.");
            if(input.PointLight&&std::abs(solidAngle)>2.0*Pi)return Reject(error,"Point light is inside the closed caster; volume extrusion is undefined.");
        }
        std::size_t frontCount=0;
        for(auto& face:faces)
        {
            const auto ray=input.PointLight?Sub(points[face.Vertex[0]],input.Light):direction;
            face.Front=Dot(face.Normal,ray)<-1.0e-12*Length(face.Normal)*std::max(Length(ray),1.0);
            frontCount+=face.Front?1:0;
        }
        VolumeMesh candidate;
        for(const auto& [key,edge]:edges)if(faces[edge.First].Front!=faces[edge.Second].Front)
        {
            (void)key;
            candidate.SilhouetteEdges.push_back(faces[edge.First].Front?std::array<std::uint32_t,2>{edge.A,edge.B}:std::array<std::uint32_t,2>{edge.B,edge.A});
        }
        if(frontCount==0)return Reject(error,"Shadow volume has no light-facing faces.");
        const auto triangleCount=2*frontCount+2*candidate.SilhouetteEdges.size();
        if(triangleCount>limits.MaxOutputTriangles)return Reject(error,"Shadow volume exceeds its output triangle budget.");
        candidate.Positions=points;candidate.Positions.reserve(points.size()*2);
        for(std::size_t i=0;i<points.size();++i)
        {
            if((i&255)==0&&IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
            Vec3 ray=direction;if(input.PointLight&&!Unit(Sub(points[i],input.Light),ray))return Reject(error,"Point-light extrusion direction is degenerate.");
            const auto extruded=Add(points[i],Scale(ray,input.ExtrusionDistance));
            if(!std::all_of(extruded.begin(),extruded.end(),[](double x){return std::isfinite(x);}))return Reject(error,"Shadow volume extrusion overflowed.");
            candidate.Positions.push_back(extruded);
        }
        const auto farOffset=static_cast<std::uint32_t>(points.size());candidate.Indices.reserve(triangleCount*3);
        for(const auto& face:faces)if(face.Front)candidate.Indices.insert(candidate.Indices.end(),face.Vertex.begin(),face.Vertex.end());
        candidate.NearCapIndexCount=candidate.Indices.size();candidate.FarCapFirstIndex=candidate.Indices.size();
        for(const auto& face:faces)if(face.Front)candidate.Indices.insert(candidate.Indices.end(),
            {face.Vertex[0]+farOffset,face.Vertex[2]+farOffset,face.Vertex[1]+farOffset});
        candidate.FarCapIndexCount=candidate.Indices.size()-candidate.FarCapFirstIndex;candidate.SideFirstIndex=candidate.Indices.size();
        for(const auto& edge:candidate.SilhouetteEdges)
        {
            const auto a=edge[0],b=edge[1],af=a+farOffset,bf=b+farOffset;
            candidate.Indices.insert(candidate.Indices.end(),{b,a,af,b,af,bf});
        }
        candidate.SideIndexCount=candidate.Indices.size()-candidate.SideFirstIndex;
        if(IsCancelled(limits.Cancelled))return Reject(error,"Shadow volume cancelled.");
        output=std::move(candidate);return true;
    }
    catch(const std::exception& exception){error=std::string("Shadow volume generation failed: ")+exception.what();return false;}
}
}
