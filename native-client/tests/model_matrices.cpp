#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
bool near(float a,float b) { return std::fabs(a-b)<1e-5f; }
void same(const Mtx a,const Mtx b) { for(int r=0;r<3;++r) for(int c=0;c<4;++c) assert(near(a[r][c],b[r][c])); }
}
int main() {
    Mtx44 identity, copied;
    C_MTX44Identity(identity);
    for (unsigned row = 0; row < 4; ++row) for (unsigned column = 0; column < 4; ++column)
        assert(identity[row][column] == (row == column ? 1.0f : 0.0f));
    C_MTX44Copy(identity, copied); C_MTX44Copy(copied, copied);
    assert(std::memcmp(identity, copied, sizeof(identity)) == 0);
    bool rejected = false;
    try { C_MTX44Copy(nullptr, copied); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    Mtx transform={{2,1,0,40},{0,3,0,50},{0,0,4,60}};
    Mtx33 normal; J3DPSCalcInverseTranspose(transform,normal);
    // A transformed tangent remains perpendicular to its inverse-transpose normal.
    const Vec tangent={transform[0][1],transform[1][1],transform[2][1]};
    const Vec perpendicular={normal[0][0],normal[1][0],normal[2][0]};
    assert(near(tangent.x*perpendicular.x+tangent.y*perpendicular.y+tangent.z*perpendicular.z,0));
    assert(near(normal[0][0],0.5f) && near(normal[1][0],-1.0f/6) && near(normal[2][2],0.25f));
    Mtx singular={}; Mtx33 unchanged; std::memcpy(unchanged,normal,sizeof(normal));
    J3DPSCalcInverseTranspose(singular,normal); assert(std::memcmp(normal,unchanged,sizeof(normal))==0);
    const Vec scale={2,3,4}; Mtx scaled; PSMTXCopy(transform,scaled);
    J3DScaleNrmMtx(scaled,scale); J3DScaleNrmMtx33(normal,scale);
    for(int r=0;r<3;++r) {
        assert(scaled[r][3]==transform[r][3]);
        assert(near(normal[r][0],unchanged[r][0]*2) && near(normal[r][1],unchanged[r][1]*3) && near(normal[r][2],unchanged[r][2]*4));
        assert(near(scaled[r][0],transform[r][0]*2) && near(scaled[r][1],transform[r][1]*3) && near(scaled[r][2],transform[r][2]*4));
    }
    Mtx camera={{1,0,0,2},{0,1,0,3},{0,0,1,4}};
    Mtx44 projection={{2,0,0,0},{0,3,0,0},{0,0,4,-1},{0,0,5,0}};
    const Mtx expected={{2,0,10,0},{0,3,15,0},{0,0,24,-1}};
    Mtx result; J3DMtxProjConcat(camera,projection,result); same(result,expected);
    J3DMtxProjConcat(camera,projection,camera); same(camera,expected);
    Mtx base={{1,0,0,10},{0,1,0,20},{0,0,1,30}};
    Mtx bones[2]={{{1,0,0,1},{0,1,0,2},{0,0,1,3}},{{2,0,0,4},{0,3,0,5},{0,0,4,6}}};
    Mtx output[2];
    J3DPSMtxArrayConcat(nullptr,nullptr,nullptr,0);
    J3DPSMtxArrayConcat(base,bones[0],output[0],1);
    const Mtx first={{1,0,0,11},{0,1,0,22},{0,0,1,33}};
    same(output[0],first);
    J3DPSMtxArrayConcat(base,bones[0],bones[0],2);
    const Mtx second={{2,0,0,14},{0,3,0,25},{0,0,4,36}};
    same(bones[0],first); same(bones[1],second);

    Mtx world[2] = {{{0,-1,0,10},{1,0,0,20},{0,0,1,30}},{{2,0,0,40},{0,3,0,50},{0,0,4,60}}};
    Mtx inverse[2] = {{{1,0,0,-2},{0,1,0,-3},{0,0,1,-4}},{{1,0,0,-5},{0,1,0,-6},{0,0,1,-7}}};
    u8 jointScale[2] = {1,0}, counts[3] = {1,2,1}, scales[3] = {};
    u16 indices[4] = {0,0,1,1};
    f32 weights[4] = {1,0.25f,0.75f,1};
    Mtx envelopes[3];
    J3DCalcWeightEnvelopeMtx(world,inverse,jointScale,2,counts,indices,weights,3,envelopes,scales);
    const Mtx firstEnvelope = {{0,-1,0,13},{1,0,0,18},{0,0,1,26}};
    const Mtx secondEnvelope = {{1.5f,-0.25f,0,25.75f},{0.25f,2.25f,0,28.5f},{0,0,3.25f,30.5f}};
    const Mtx thirdEnvelope = {{2,0,0,30},{0,3,0,32},{0,0,4,32}};
    same(envelopes[0],firstEnvelope); same(envelopes[1],secondEnvelope); same(envelopes[2],thirdEnvelope);
    assert(scales[0]==1 && scales[1]==0 && scales[2]==0);
    J3DCalcWeightEnvelopeMtx(nullptr,nullptr,nullptr,0,nullptr,nullptr,nullptr,0,nullptr,nullptr);
    indices[3]=2;
    try { J3DCalcWeightEnvelopeMtx(world,inverse,jointScale,2,counts,indices,weights,3,envelopes,scales); assert(false); }
    catch(const std::invalid_argument&) {}
    same(envelopes[0],firstEnvelope); same(envelopes[2],thirdEnvelope);
    indices[3]=1; weights[3]=std::numeric_limits<float>::quiet_NaN();
    try { J3DCalcWeightEnvelopeMtx(world,inverse,jointScale,2,counts,indices,weights,3,envelopes,scales); assert(false); }
    catch(const std::invalid_argument&) {}
    weights[3]=1; counts[1]=0;
    try { J3DCalcWeightEnvelopeMtx(world,inverse,jointScale,2,counts,indices,weights,3,envelopes,scales); assert(false); }
    catch(const std::invalid_argument&) {}

    Mtx33 copiedNormal;
    J3DPSMtx33CopyFrom34(world[0],copiedNormal);
    for(int row=0;row<3;++row) for(int column=0;column<3;++column) assert(copiedNormal[row][column]==world[0][row][column]);
    Mtx33 otherNormal;
    J3DPSMtx33Copy(copiedNormal,otherNormal);
    assert(std::memcmp(copiedNormal,otherNormal,sizeof(copiedNormal))==0);
}
