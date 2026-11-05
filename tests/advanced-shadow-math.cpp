// Standalone CPU regression. Compile with jAdvancedShadowMath.cpp, no engine/PCH.
#include "Renderer/Features/jAdvancedShadowMath.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace
{
using namespace jAdvancedShadowMath;
int passed=0,failed=0;
void Check(bool condition,const char* name)
{if(condition)++passed;else{++failed;std::cerr<<"FAIL: "<<name<<'\n';}}
bool Near(double a,double b,double epsilon=1.0e-9){return std::abs(a-b)<=epsilon*std::max({1.0,std::abs(a),std::abs(b)});}
Vec3 Sub(const Vec3& a,const Vec3& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vec3 Cross(const Vec3& a,const Vec3& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double Dot(const Vec3& a,const Vec3& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double Length(const Vec3& p){return std::sqrt(Dot(p,p));}
FrustumCorners Frustum(double nearDistance=1,double farDistance=100)
{return {{{-nearDistance,-nearDistance,nearDistance},{nearDistance,-nearDistance,nearDistance},
    {-nearDistance,nearDistance,nearDistance},{nearDistance,nearDistance,nearDistance},
    {-farDistance,-farDistance,farDistance},{farDistance,-farDistance,farDistance},
    {-farDistance,farDistance,farDistance},{farDistance,farDistance,farDistance}}};}
Mat4 Camera(double nearDistance=1,double farDistance=100)
{return {1,0,0,0,0,1,0,0,0,0,farDistance/(farDistance-nearDistance),1,0,0,-farDistance*nearDistance/(farDistance-nearDistance),0};}
bool ContainsCorners(const Mat4& matrix,const FrustumCorners& corners)
{
    for(const auto& corner:corners){Vec3 p;if(!ProjectPoint(matrix,corner,p)||std::abs(p[0])>1.000001||std::abs(p[1])>1.000001||p[2]<-.000001||p[2]>1.000001)return false;}
    return true;
}
TriangleMesh Cube()
{
    return {{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},
        {0,2,1,0,3,2,4,5,6,4,6,7,0,4,7,0,7,3,1,2,6,1,6,5,0,1,5,0,5,4,3,7,6,3,6,2}};
}
bool ClosedOriented(const VolumeMesh& volume)
{
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::pair<int,int>> edges;
    for(std::size_t i=0;i<volume.Indices.size();i+=3)for(std::size_t e=0;e<3;++e)
    {
        const auto a=volume.Indices[i+e],b=volume.Indices[i+(e+1)%3];
        if(a>=volume.Positions.size()||b>=volume.Positions.size()||a==b)return false;
        auto& edge=edges[std::minmax(a,b)];++edge.first;edge.second+=a<b?1:-1;
    }
    return !edges.empty()&&std::all_of(edges.begin(),edges.end(),[](const auto& pair){return pair.second.first==2&&pair.second.second==0;});
}
// Independent ray/triangle parity check of the emitted closed volume.
bool InsideVolume(const VolumeMesh& volume,const Vec3& point)
{
    const Vec3 ray{1,.173,.091};std::size_t crossings=0;
    for(std::size_t i=0;i<volume.Indices.size();i+=3)
    {
        const auto& a=volume.Positions[volume.Indices[i]];const auto ab=Sub(volume.Positions[volume.Indices[i+1]],a),ac=Sub(volume.Positions[volume.Indices[i+2]],a);
        const auto h=Cross(ray,ac);const double determinant=Dot(ab,h);if(std::abs(determinant)<1e-12)continue;
        const auto s=Sub(point,a);const double u=Dot(s,h)/determinant;if(u<0||u>1)continue;
        const auto q=Cross(s,ab);const double v=Dot(ray,q)/determinant;if(v<0||u+v>1)continue;
        if(Dot(ac,q)/determinant>1e-9)++crossings;
    }
    return crossings%2==1;
}
void TestMatricesAndCascades()
{
    std::string error;Mat4 translation=Identity();translation[12]=3;translation[13]=-2;translation[14]=5;
    const Mat4 rotation{0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1};Vec3 p{};
    Check(ProjectPoint(Multiply(translation,rotation),{2,0,0},p)&&Near(p[0],3)&&Near(p[1],0)&&Near(p[2],5),"column-vector matrix composition has the engine's translation and rotation order");
    p={7,8,9};Mat4 zero{};Check(!ProjectPoint(zero,{1,2,3},p)&&p==Vec3{7,8,9},"homogeneous zero leaves projected output unchanged");
    CascadeInput input;input.Corners=Frustum(1,256);input.NearDistance=1;input.FarDistance=256;input.SplitLambda=1;input.LightDirection={0,0,1};
    CascadeResult result;Check(BuildCascades(input,result,error),"CSM accepts actual near/far world corners");
    Check(result.Splits==std::array<double,5>{1,4,16,64,256},"logarithmic cascade partition matches known geometric distances");
    bool contained=true,seams=true;
    for(std::size_t i=0;i<result.Count;++i){contained&=ContainsCorners(result.Cascades[i].WorldToShadow,result.Cascades[i].Corners);if(i)for(std::size_t k=0;k<4;++k)seams&=result.Cascades[i].Corners[k]==result.Cascades[i-1].Corners[k+4];}
    Check(contained,"each cascade contains all eight of its receiver corners");Check(seams,"neighboring cascades share the exact world-space split boundary");
    input.SplitLambda=0;input.FarDistance=101;input.Corners=Frustum(1,101);Check(BuildCascades(input,result,error)&&result.Splits==std::array<double,5>{1,26,51,76,101},"uniform partition preserves positive near and exact far endpoint");
    auto moved=input;const double shift=result.Cascades[0].WorldUnitsPerTexel*.1;for(auto& corner:moved.Corners)corner[0]+=shift;
    CascadeResult shifted;Check(BuildCascades(moved,shifted,error)&&shifted.Cascades[0].WorldToShadow==result.Cascades[0].WorldToShadow,"stabilization keeps a subtexel translated camera fixed on the shadow texel grid");
    auto reverse=input;reverse.ReverseZ=true;CascadeResult reversed;bool reversedOk=BuildCascades(reverse,reversed,error);
    for(std::size_t i=0;i<result.Count&&reversedOk;++i)for(const auto& corner:result.Cascades[i].Corners){Vec3 a,b;reversedOk&=ProjectPoint(result.Cascades[i].WorldToShadow,corner,a)&&ProjectPoint(reversed.Cascades[i].WorldToShadow,corner,b)&&Near(a[2]+b[2],1);}
    Check(reversedOk,"reverse-Z changes only the depth mapping of every cascade");
    auto tinyDirection=input;tinyDirection.LightDirection={0,0,1e-300};CascadeResult tiny;
    Check(BuildCascades(tinyDirection,tiny,error)&&tiny.Cascades[0].WorldToShadow==result.Cascades[0].WorldToShadow,"finite nonzero direction scaling does not change CSM projection");
    const auto saved=result;input.Count=5;Check(!BuildCascades(input,result,error)&&result.Splits==saved.Splits,"invalid cascade count is transactional");
    input.Count=4;input.NearDistance=0;Check(!BuildCascades(input,result,error)&&result.Splits==saved.Splits,"camera near zero is rejected");
    input.NearDistance=1;input.Corners={};Check(!BuildCascades(input,result,error),"degenerate receiver frustum is rejected");
    input.Corners=Frustum(1,101);input.LightDirection={0,0,0};Check(!BuildCascades(input,result,error),"zero light direction is rejected");
}
void TestPerspectiveAndParaboloid()
{
    std::string error;PerspectiveInput input;input.Corners=Frustum();input.CameraViewProjection=Camera();input.LightDirection={3,-2,-1};
    PerspectiveResult result;Check(BuildPerspectiveShadowMap(input,result,error)&&!result.OrthographicFallback,"PSM uses the real post-perspective transform for a finite side light");
    Check(ContainsCorners(result.WorldToShadow,input.Corners),"post-perspective fitted projection contains the full receiver frustum");
    Vec3 a,b;Check(ProjectPoint(result.WorldToShadow,{0,0,10},a)&&ProjectPoint(result.WorldToShadow,{.003,-.002,9.999},b)&&b[2]>a[2],"PSM depth ordering increases along actual light travel");
    auto reverse=input;reverse.ReverseZ=true;PerspectiveResult reversed;Check(BuildPerspectiveShadowMap(reverse,reversed,error)&&ProjectPoint(reversed.WorldToShadow,{0,0,10},b)&&Near(a[2]+b[2],1),"PSM reverse-Z has the same spatial projection");
    input.LightDirection={0,-1,0};Check(BuildPerspectiveShadowMap(input,result,error)&&result.OrthographicFallback&&!result.FallbackReason.empty()&&ContainsCorners(result.WorldToShadow,input.Corners),"PSM light at infinity reports a finite fitted fallback");
    input.LightDirection={0,-1,1};Check(BuildPerspectiveShadowMap(input,result,error)&&result.OrthographicFallback&&!result.FallbackReason.empty(),"unsafe behind-eye negative-near trick reports its fallback");
    input.Corners=Frustum(-1,100);Check(BuildPerspectiveShadowMap(input,result,error)&&result.OrthographicFallback,"receiver crossing camera clip plane uses a finite explicit fallback");
    const auto saved=result.WorldToShadow;input.CameraViewProjection[0]=std::numeric_limits<double>::quiet_NaN();Check(!BuildPerspectiveShadowMap(input,result,error)&&result.WorldToShadow==saved,"invalid PSM matrix leaves last projection unchanged");
    ParaboloidSample sample;bool roundtrip=true;
    for(const auto& point:std::array<Vec3,8>{{{0,0,5},{0,0,-5},{3,4,0},{-3,4,5},{-3,4,-5},{10,1,.001},{10,1,-.001},{0,-10,0}}})
    {Vec3 restored;roundtrip&=ProjectDualParaboloid(point,1,100,sample,error)&&UnprojectDualParaboloid(sample,1,100,restored,error);for(std::size_t k=0;k<3;++k)roundtrip&=Near(point[k],restored[k]);}
    Check(roundtrip,"dual-paraboloid front/back/axes/seam round trips preserve radial distance");
    ParaboloidSample front,back;Check(ProjectDualParaboloid({-3,4,5},1,100,front,error)&&ProjectDualParaboloid({-3,4,-5},1,100,back,error)&&front.UV==back.UV&&front.Face==Hemisphere::Front&&back.Face==Hemisphere::Back,"back hemisphere changes Z sign without mirroring X");
    Check(ProjectDualParaboloid({5,0,0},1,100,sample,error)&&sample.Face==Hemisphere::Front&&sample.UV==Vec2{1,.5},"equator has deterministic front-face ownership");
    sample.Depth=.321;Check(!ProjectDualParaboloid({0,0,0},1,100,sample,error)&&sample.Depth==.321,"point at light origin is rejected transactionally");
    Check(!ProjectDualParaboloid({0,0,101},1,100,sample,error),"radially clipped point is rejected");
    sample.UV={1,1};
    Vec3 unchanged{7,8,9};Check(!UnprojectDualParaboloid(sample,1,100,unchanged,error)&&unchanged==Vec3{7,8,9},"UV outside the hemisphere disk is rejected transactionally");
}
void TestNativePerspectiveFixture()
{
    // Native-second regression: use the exact authored camera and sun rather
    // than only an origin camera/unit frustum. This predicts CPU coverage; the
    // separate native harness remains responsible for actual GPU atlas pixels.
    constexpr double pi=3.14159265358979323846;
    const Vec3 eye{0,220,-800},target{0,80,300};
    auto forward=Sub(target,eye);for(auto& value:forward)value/=Length(Sub(target,eye));
    auto right=Cross({0,1,0},forward);const auto rightLength=Length(right);for(auto& value:right)value/=rightLength;
    const auto up=Cross(forward,right);
    const Mat4 view{right[0],up[0],forward[0],0,right[1],up[1],forward[1],0,
        right[2],up[2],forward[2],0,-Dot(right,eye),-Dot(up,eye),-Dot(forward,eye),1};
    const double tangent=std::tan(65*pi/360),aspect=1280.0/720.0;
    const Mat4 projection{1/(tangent*aspect),0,0,0,0,1/tangent,0,0,0,0,1800.0/1799.0,1,0,0,-1800.0/1799.0,0};
    PerspectiveInput input;input.CameraViewProjection=Multiply(projection,view);input.CasterPadding=250;
    const double rx=55*pi/180,ry=-20*pi/180;
    input.LightDirection={-std::sin(rx)*std::sin(ry),-std::cos(rx),-std::sin(rx)*std::cos(ry)};
    for(std::size_t i=0;i<8;++i)
    {
        const double distance=i<4?1.0:1600.0,sx=(i&1)?1.0:-1.0,sy=(i&2)?-1.0:1.0;
        for(std::size_t axis=0;axis<3;++axis)
            input.Corners[i][axis]=eye[axis]+distance*(forward[axis]+sx*tangent*aspect*right[axis]+sy*tangent*up[axis]);
    }
    const std::array<Vec3,3> centers{{{-100,70,60},{100,70,130},{30,50,-100}}};const auto cube=Cube();
    double nearestDepth=std::numeric_limits<double>::infinity();
    for(const auto& center:centers)for(const auto& vertex:cube.Positions)
    {
        Vec3 point;for(std::size_t axis=0;axis<3;++axis)point[axis]=center[axis]+50*vertex[axis];
        nearestDepth=std::min(nearestDepth,Dot(Sub(point,eye),forward));
    }
    input.VirtualSlideBack=std::max(0.0,nearestDepth-1.0);
    // The renderer passes a float camera VP and float-generated world corners.
    // In particular finite far extraction must tolerate their rounding noise.
    for(auto& value:input.CameraViewProjection)value=double(float(value));
    for(auto& corner:input.Corners)for(auto& value:corner)value=double(float(value));
    const auto originalCamera=input.CameraViewProjection;const auto originalCorners=input.Corners;
    PerspectiveResult result;std::string error;
    Check(BuildPerspectiveShadowMap(input,result,error)&&!result.OrthographicFallback,
        "actual native fixture builds the finite post-perspective branch with a scene-fitted virtual camera");
    Check(input.CameraViewProjection==originalCamera&&input.Corners==originalCorners,
        "virtual sliding preserves the actual camera matrix and receiver world frustum");
    bool allInside=true,floatAccurate=true;
    for(const auto& center:centers)
    {
        std::array<Vec3,8> screen{};
        for(std::size_t vertex=0;vertex<cube.Positions.size();++vertex)
        {
            Vec3 world;for(std::size_t axis=0;axis<3;++axis)world[axis]=center[axis]+50*cube.Positions[vertex][axis];
            Vec3 precise;allInside&=ProjectPoint(result.WorldToShadow,world,precise);
            float clip[4]{};for(std::size_t row=0;row<4;++row)for(std::size_t column=0;column<4;++column)
                clip[row]+=float(result.WorldToShadow[column*4+row])*float(column==3?1.0:world[column]);
            allInside&=clip[3]>0;
            for(std::size_t axis=0;axis<3;++axis){screen[vertex][axis]=double(clip[axis]/clip[3]);floatAccurate&=Near(screen[vertex][axis],precise[axis],1e-5);}
            allInside&=std::abs(screen[vertex][0])<1&&std::abs(screen[vertex][1])<1&&screen[vertex][2]>0&&screen[vertex][2]<.95;
            screen[vertex][0]=(screen[vertex][0]*.5+.5)*256;
            screen[vertex][1]=(.5-screen[vertex][1]*.5)*256;
        }
        std::set<std::pair<int,int>> coverage;
        for(std::size_t i=0;i<cube.Indices.size();i+=3)
        {
            const auto& a=screen[cube.Indices[i]];const auto& b=screen[cube.Indices[i+1]];const auto& c=screen[cube.Indices[i+2]];
            const double denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);if(std::abs(denominator)<1e-10)continue;
            const int x0=std::max(0,int(std::floor(std::min({a[0],b[0],c[0]})))),x1=std::min(255,int(std::ceil(std::max({a[0],b[0],c[0]}))));
            const int y0=std::max(0,int(std::floor(std::min({a[1],b[1],c[1]})))),y1=std::min(255,int(std::ceil(std::max({a[1],b[1],c[1]}))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
            {
                const double u=((b[1]-c[1])*(x+.5-c[0])+(c[0]-b[0])*(y+.5-c[1]))/denominator;
                const double v=((c[1]-a[1])*(x+.5-c[0])+(a[0]-c[0])*(y+.5-c[1]))/denominator;
                const double w=1-u-v,depth=u*a[2]+v*b[2]+w*c[2];
                if(u>=0&&v>=0&&w>=0&&depth>0&&depth<1)coverage.emplace(x,y);
            }
        }
        Check(coverage.size()>=4,"native fixture caster has software-predicted occupied pixels in the 256x256 PSM atlas");
    }
    Check(allInside,"all native caster corners remain in positive-W forward depth after float GPU-style transforms");
    Check(floatAccurate,"native PSM float quantization retains the double projection within 1e-5");
    Vec3 receiver=centers[0];const double time=-receiver[1]/input.LightDirection[1];
    for(std::size_t axis=0;axis<3;++axis)receiver[axis]+=time*input.LightDirection[axis];
    Vec3 casterShadow,receiverShadow;
    Check(ProjectPoint(result.WorldToShadow,centers[0],casterShadow)&&ProjectPoint(result.WorldToShadow,receiver,receiverShadow)
        &&Near(casterShadow[0],receiverShadow[0])&&Near(casterShadow[1],receiverShadow[1])&&receiverShadow[2]>casterShadow[2],
        "actual directional light ray maps caster and ground receiver to one texel in the correct depth order");
    const auto saved=result.WorldToShadow;auto invalid=input;invalid.VirtualSlideBack=-1;
    Check(!BuildPerspectiveShadowMap(invalid,result,error)&&result.WorldToShadow==saved,"negative virtual offset retains the last valid projection");
    invalid.VirtualSlideBack=std::numeric_limits<double>::quiet_NaN();
    Check(!BuildPerspectiveShadowMap(invalid,result,error)&&result.WorldToShadow==saved,"nonfinite virtual offset retains the last valid projection");
    invalid.VirtualSlideBack=1000001;
    Check(!BuildPerspectiveShadowMap(invalid,result,error)&&result.WorldToShadow==saved,"over-budget virtual offset retains the last valid projection");
    PerspectiveInput infinity;infinity.Corners=Frustum();infinity.LightDirection={3,-2,-1};infinity.VirtualSlideBack=25;
    infinity.CameraViewProjection={1,0,0,0,0,1,0,0,0,0,1,1,0,0,-1,0};
    Check(BuildPerspectiveShadowMap(infinity,result,error)&&!result.OrthographicFallback&&ContainsCorners(result.WorldToShadow,infinity.Corners),
        "infinite-far camera uses the finite virtual-slide limit without losing receiver corners");
    auto orthographic=infinity;orthographic.CameraViewProjection={.01,0,0,0,0,.01,0,0,0,0,1.0/99,0,0,0,-1.0/99,1};
    Check(BuildPerspectiveShadowMap(orthographic,result,error)&&result.OrthographicFallback&&!result.FallbackReason.empty(),
        "orthographic virtual offset reports a finite explicit fitted fallback");
    auto oblique=infinity;oblique.CameraViewProjection[2]=.01;
    Check(BuildPerspectiveShadowMap(oblique,result,error)&&result.OrthographicFallback&&!result.FallbackReason.empty(),
        "unsupported oblique camera depth mapping reports a finite explicit fallback");
}

void TestSubdivision()
{
    std::string error;
    const std::vector<Vec3> positions{{-4,-4,5},{4,-4,5},{4,4,5},{-4,-4,5},{4,4,5},{-4,4,5}};
    const std::vector<std::uint32_t> indices{0,1,2,3,4,5};SubdivisionResult result;
    Check(SubdivideForDualParaboloid(positions,indices,result,error)&&result.Passes>0&&result.Mesh.Indices.size()>indices.size(),"large projected triangles are actually subdivided");
    bool angular=true,boundary=true,winding=true;double area=0;std::map<std::pair<std::uint32_t,std::uint32_t>,int> edges;
    for(std::size_t i=0;i<result.Mesh.Indices.size();i+=3)
    {
        const auto& a=result.Mesh.Positions[result.Mesh.Indices[i]];const auto& b=result.Mesh.Positions[result.Mesh.Indices[i+1]];const auto& c=result.Mesh.Positions[result.Mesh.Indices[i+2]];
        const double cross=Cross(Sub(b,a),Sub(c,a))[2];area+=cross*.5;winding&=cross>0;
        for(std::size_t e=0;e<3;++e){const auto u=result.Mesh.Indices[i+e],v=result.Mesh.Indices[i+(e+1)%3];++edges[std::minmax(u,v)];const auto& p=result.Mesh.Positions[u];const auto& q=result.Mesh.Positions[v];angular&=std::atan2(Length(Cross(p,q)),Dot(p,q))*180/3.14159265358979323846<=15.000001;}
    }
    for(const auto& [edge,count]:edges)if(count==1){const auto& a=result.Mesh.Positions[edge.first];const auto& b=result.Mesh.Positions[edge.second];boundary&=(Near(std::abs(a[0]),4)&&Near(a[0],b[0]))||(Near(std::abs(a[1]),4)&&Near(a[1],b[1]));}else boundary&=count==2;
    Check(angular,"every tessellated edge respects the angular projection error budget");
    Check(boundary,"shared UV-seam geometry has no open interior tessellation edges");
    Check(winding&&Near(area,64),"subdivision preserves winding and the exact covered source surface area");
    const auto saved=result.Mesh.Indices;SubdivisionSettings budget;budget.MaximumTriangles=2;
    Check(!SubdivideForDualParaboloid(positions,indices,result,error,budget)&&result.Mesh.Indices==saved,"subdivision output budget fails without publishing partial geometry");
    budget={};budget.MaximumDepth=0;Check(!SubdivideForDualParaboloid(positions,indices,result,error,budget)&&result.Mesh.Indices==saved,"insufficient subdivision depth fails explicitly");
    budget={};budget.Cancelled=[](){return true;};Check(!SubdivideForDualParaboloid(positions,indices,result,error,budget)&&result.Mesh.Indices==saved,"cancelled tessellation retains previous output");
    auto invalid=positions;invalid[0]={0,0,0};Check(!SubdivideForDualParaboloid(invalid,indices,result,error),"triangle touching the projection origin is rejected");
    const std::vector<Vec3> edgeOrigin{{-1,0,0},{2,0,0},{0,1,0}};const std::vector<std::uint32_t> singleTriangle{0,1,2};
    Check(!SubdivideForDualParaboloid(edgeOrigin,singleTriangle,result,error)&&result.Mesh.Indices==saved,"light at a non-dyadic edge point fails within subdivision budget without partial output");
    const std::vector<Vec3> faceOrigin{{-1,-1,0},{2,-1,0},{0,2,0}};
    Check(!SubdivideForDualParaboloid(faceOrigin,singleTriangle,result,error)&&result.Mesh.Indices==saved,"light strictly inside a triangle fails rather than producing a folded paraboloid");
}
void TestVolumes()
{
    std::string error;auto cube=Cube();VolumeInput input{cube.Positions,cube.Indices,{0,0,1},false,true,32};VolumeMesh result;
    Check(BuildShadowVolume(input,result,error),"closed outward cube creates a finite directional volume");
    Check(ClosedOriented(result),"near/far caps and silhouette sides form a closed consistently oriented triangle shell");
    Check(result.SilhouetteEdges.size()==4&&result.NearCapIndexCount==6&&result.FarCapIndexCount==6&&result.SideIndexCount==24&&result.Indices.size()==36,"cube silhouette and explicit index ranges describe two caps and four side quads");
    Check(InsideVolume(result,{0,0,5})&&!InsideVolume(result,{3,0,5})&&!InsideVolume(result,{0,0,-3})&&!InsideVolume(result,{0,0,35}),"independent camera/receiver ray parity finds the finite shadow cone and lit controls");
    input.PointLight=true;input.Light={0,0,-10};Check(BuildShadowVolume(input,result,error)&&ClosedOriented(result)&&InsideVolume(result,{0,0,10})&&!InsideVolume(result,{20,0,10}),"point-light extrusion expands a closed cone away from the source");
    const auto saved=result.Indices;input.Light={0,0,0};Check(!BuildShadowVolume(input,result,error)&&result.Indices==saved,"light inside the caster is explicitly rejected without replacing previous geometry");
    input.Light={0,0,-1};Check(!BuildShadowVolume(input,result,error)&&result.Indices==saved,"light on a caster face is rejected");
    input.Light={-1,-1,-1};Check(!BuildShadowVolume(input,result,error),"light on a caster vertex is rejected");
    input.PointLight=false;input.Light={0,0,1};TriangleMesh seams;
    for(auto index:cube.Indices){seams.Indices.push_back(static_cast<std::uint32_t>(seams.Positions.size()));seams.Positions.push_back(cube.Positions[index]);}
    input.Positions=seams.Positions;input.Indices=seams.Indices;Check(BuildShadowVolume(input,result,error)&&result.Positions.size()==16&&ClosedOriented(result),"exact coordinate welding accepts a closed mesh with duplicated normal/UV seams");
    input.WeldCoincidentPositions=false;Check(!BuildShadowVolume(input,result,error),"unwelded disconnected face seams are honestly rejected as open");input.WeldCoincidentPositions=true;
    input.Positions=cube.Positions;auto open=cube.Indices;open.resize(open.size()-3);input.Indices=open;Check(!BuildShadowVolume(input,result,error),"open triangle mesh cannot masquerade as a closed shadow volume");
    auto inward=cube.Indices;for(std::size_t i=0;i<inward.size();i+=3)std::swap(inward[i+1],inward[i+2]);input.Indices=inward;Check(!BuildShadowVolume(input,result,error),"consistently inward caster winding is rejected");
    auto nonmanifold=cube.Indices;nonmanifold.insert(nonmanifold.end(),{0,2,1});input.Indices=nonmanifold;Check(!BuildShadowVolume(input,result,error),"third face on an edge is rejected as nonmanifold");
    auto pinched=cube;for(auto point:cube.Positions){for(auto& axis:point)axis+=2;pinched.Positions.push_back(point);}for(auto index:cube.Indices)pinched.Indices.push_back(index+8);
    input.Positions=pinched.Positions;input.Indices=pinched.Indices;Check(!BuildShadowVolume(input,result,error),"two closed shells touching one vertex are rejected as a pinched manifold");
    input.Positions=cube.Positions;input.Indices=cube.Indices;VolumeLimits limits;limits.MaxInputTriangles=11;Check(!BuildShadowVolume(input,result,error,limits),"input triangle budget is enforced before topology allocation");
    limits={};limits.MaxOutputTriangles=1;Check(!BuildShadowVolume(input,result,error,limits),"caps and side triangles count toward the output budget");
    int checks=0;limits={};limits.Cancelled=[&checks](){return ++checks>1;};Check(!BuildShadowVolume(input,result,error,limits)&&checks>1,"volume cancellation is observed during generation");
    limits={};input.ExtrusionDistance=std::numeric_limits<double>::infinity();Check(!BuildShadowVolume(input,result,error,limits),"infinite extrusion is rejected");
    auto distant=cube;for(auto& point:distant.Positions)point[0]+=4e8;
    input={distant.Positions,distant.Indices,{-8e8,0,0},true,true,32};
    Check(BuildShadowVolume(input,result,error)&&ClosedOriented(result),"valid large light and mesh coordinates normalize their derived vector beyond one billion units");
}
void TestMomentsAndDeep()
{
    std::string error;Vec2 moments;double visibility=0;
    Check(VsmMoments(.25,moments,error)&&VsmVisibility(moments,.1,1e-5,.2,visibility,error)&&visibility==1,"unoccluded VSM receiver retains full visibility");
    Check(VsmVisibility(moments,.75,1e-5,.2,visibility,error)&&visibility<.001,"single-occluder VSM blocks a receiver behind it");
    const Vec2 mixed{.5,.34};Check(VsmVisibility(mixed,.9,1e-5,0,visibility,error)&&Near(visibility,.36),"filtered two-depth VSM matches the independent probability bound");
    double exponential;Check(EsmMoment(.2,10,exponential,error)&&EsmVisibility(exponential,.4,10,visibility,error)&&Near(visibility,std::exp(-2)) ,"ESM single layer has analytically known exponential attenuation");
    Check(EsmVisibility(exponential,.1,10,visibility,error)&&visibility==1,"ESM receiver before occluder is clamped to fully lit");
    Vec4 evsm;Check(EvsmMoments(.4,32,8,evsm,error)&&Near(evsm[2],-std::exp(-3.2)),"EVSM has independent positive32 and negative8 warp exponents");
    Check(EvsmVisibility(evsm,.3,32,8,1e-5,.2,visibility,error)&&visibility==1,"EVSM foreground receiver remains lit");
    Check(EvsmVisibility(evsm,.7,32,8,1e-5,.2,visibility,error)&&visibility<.001,"both EVSM warps agree on the occluded receiver");
    EvsmMoments(0,1,.1,evsm,error);const double q=std::exp(-.1),warpedVariance=.001*q*q;
    Check(EvsmVisibility(evsm,1,1,.1,.1,0,visibility,error)&&Near(visibility,warpedVariance/(warpedVariance+(1-q)*(1-q))),"EVSM variance floor transforms from depth units by the receiver warp derivative");
    Check(VsmVisibility({.9,1.3},.5,1e-5,.2,visibility,error)&&visibility==1,"VSM accepts valid derivative-expanded second moments above one");
    EvsmMoments(0,32,8,evsm,error);evsm[1]+=256;evsm[3]+=16;
    Check(EvsmVisibility(evsm,.2,32,8,1e-5,.2,visibility,error),"EVSM accepts gradient variance added to both warped second moments");
    Check(EvsmMoments(1,40,40,evsm,error)&&std::all_of(evsm.begin(),evsm.end(),[](double x){return std::isfinite(static_cast<float>(x));}),"maximum EVSM exponents fit finite float4 storage");
    Vec4 lo,hi;EvsmMoments(.2,32,8,lo,error);EvsmMoments(.8,32,8,hi,error);for(std::size_t k=0;k<4;++k)evsm[k]=.5*(lo[k]+hi[k]);
    bool monotone=true;double previous=1;for(int i=0;i<=100;++i){monotone&=EvsmVisibility(evsm,i/100.0,32,8,1e-5,.2,visibility,error)&&visibility>=0&&visibility<=previous+1e-12;previous=visibility;}Check(monotone,"filtered EVSM visibility is bounded and monotonic with receiver depth");
    visibility=.432;Check(!EsmMoment(.2,41,visibility,error)&&visibility==.432,"invalid exponent leaves numerical output unchanged");
    Check(!VsmVisibility(mixed,.5,0,.2,visibility,error)&&visibility==.432,"invalid minimum variance is rejected transactionally");
    const std::vector<DeepLayer> layers{{.75,.5},{.25,.5},{.25,.5}};std::vector<DeepKnot> curve;
    Check(BuildDeepCurve(layers,curve,error)&&curve.size()==2&&curve[0].Depth==.25&&Near(curve[0].Transmittance,.25),"deep reference sorts layers and combines equal-depth opacity");
    Check(DeepTransmittance(curve,.2,visibility,error)&&visibility==1&&DeepTransmittance(curve,.5,visibility,error)&&Near(visibility,.25)&&DeepTransmittance(curve,1,visibility,error)&&Near(visibility,.125),"deep exact transmittance follows independent layer products");
    DeepTauBins bins;Check(BuildDeepTauBins(layers,1,16,bins,error)&&EvaluateDeepTauBins(bins,1,16,visibility,error)&&Near(visibility,.125),"eight optical-depth bins preserve total end-of-ray transmittance");
    const std::vector<DeepLayer> sameBin{{.25,.5},{.25,.5}};Check(BuildDeepTauBins(sameBin,1,16,bins,error)&&EvaluateDeepTauBins(bins,.3125,16,visibility,error)&&Near(visibility,.5)&&EvaluateDeepTauBins(bins,.375,16,visibility,error)&&Near(visibility,.25),"receiver advances through the fractional optical-depth bin");
    const std::vector<DeepLayer> opaque{{0,1}};Check(BuildDeepTauBins(opaque,1,.01,bins,error)&&bins[0]==16&&EvaluateDeepTauBins(bins,.0000125,.01,visibility,error)&&Near(visibility,std::exp(-.0016)),"per-fragment tau16 is not incorrectly pre-clamped before fractional evaluation");
    Check(BuildDeepTauBins(opaque,0,32,bins,error)&&EvaluateDeepTauBins(bins,1,32,visibility,error)&&visibility==1,"zero opacity multiplier removes deep attenuation");
    std::vector<DeepLayer> many(65,{.5,.5});bins.fill(.123);Check(!BuildDeepTauBins(many,1,16,bins,error)&&bins[0]==.123,"deep fragment budget fails transactionally");
    auto invalid=layers;invalid[0].Opacity=std::numeric_limits<double>::quiet_NaN();Check(!BuildDeepCurve(invalid,curve,error)&&curve.size()==2,"nonfinite opacity cannot replace an existing deep curve");
    curve={{.5,.5},{.4,.2}};visibility=.432;Check(!DeepTransmittance(curve,.8,visibility,error)&&visibility==.432,"corrupt unsorted curve is rejected");
}
}
int main()
{
    TestMatricesAndCascades();TestPerspectiveAndParaboloid();TestNativePerspectiveFixture();TestSubdivision();TestVolumes();TestMomentsAndDeep();
    std::cout<<"Advanced shadow math: "<<passed<<'/'<<(passed+failed)<<" passed\n";return failed?1:0;
}
