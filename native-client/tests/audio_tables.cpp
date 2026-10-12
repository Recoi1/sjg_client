#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/Speaker/SpkTable.hpp"
#include "Game/Speaker/SpkWave.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRFileFinder.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void put(std::vector<u8>& b,size_t p,u32 v,size_t width=4) { for(size_t i=0;i<width;++i) b[p+i]=u8(v>>(8*(width-i-1))); }
template<class F> void rejected(F f) { bool caught=false; try { f(); } catch(const std::runtime_error&) { caught=true; } assert(caught); }
std::vector<u8> names() {
    std::vector<u8> b(46); put(b,0,2); put(b,4,16); put(b,8,32);
    put(b,16,0x1234,2); b[18]=5; b[19]=100; put(b,20,200,2); put(b,22,0xabcd,2);
    put(b,24,0x4321,2); put(b,28,300,2);
    put(b,32,40); put(b,36,43); std::memcpy(b.data()+40,"aa\0bb",6); return b;
}
std::vector<u8> chord() {
    std::vector<u8> b(64); std::memcpy(b.data()+4,"CITS",4); put(b,8,64); put(b,12,1,2); put(b,14,1,2);
    put(b,16,24); put(b,20,32); for(size_t i=0;i<8;++i)b[24+i]=u8(i);
    put(b,32,40); put(b,36,52); for(size_t i=0;i<12;++i) { b[40+i]=u8(i); b[52+i]=u8(11-i); } return b;
}
std::vector<u8> wave() {
    std::vector<u8> b(32); put(b,0,32); put(b,4,1); put(b,8,12);
    put(b,12,8); put(b,16,1); put(b,20,4);
    for(size_t i=0;i<4;++i) put(b,24+i*2,u16(s16(-2+int(i))),2); return b;
}
void selfCheck() {
    auto b=names(); const auto original=b;
    SpkTable speaker; AudMePlayingParamsHolder music;
    rejected([&]{speaker.setResource(b.data());});
    speaker.setResource(b.data(),b.size()); music.setResource(b.data(),b.size());
    assert(b==original && speaker.mInitialized && speaker.mResourceCount==2 && music.mNumEntries==2);
    assert(speaker.getParameters(0)->mWaveID==0x1234 && speaker.getParameters(0)->mReleaseTime==200);
    assert(speaker.getParameters(0)->_padding==0xabcd && speaker.getParameters(0)->mVolume==100);
    assert(!speaker.getParameters(-1) && !speaker.getParameters(2));
    assert(music.mParams[0]._4==200 && music.mParams[0].mPriority==0x12 && music.mParams[0]._6==0xab);
    b.clear(); assert(std::string(speaker.mNames[1])=="bb" && std::string(music.mNames[0])=="aa");
    for(size_t n=0;n<original.size();++n) { auto bad=original; rejected([&]{speaker.setResource(bad.data(),n);}); }
    for(const auto [p,v]:{std::pair<size_t,u32>{0,0xffffffff},{4,0},{8,16},{12,1},{32,16},{32,46}}) {
        auto bad=original; put(bad,p,v); rejected([&]{speaker.setResource(bad.data(),bad.size());});
    }
    auto bad=original; bad.back()='x'; rejected([&]{music.setResource(bad.data(),bad.size());});
    assert(speaker.mInitialized && speaker.mParameters[0].mWaveID==0x1234);
    b=chord(); const auto originalChord=b; AudChordTable chords;
    assert(!chords.setChordTableResource(b.data())); assert(chords.setChordTableResource(b.data(),b.size()));
    assert(chords.mLoaded && chords.mChordCount==1 && chords.mScaleCount==1 && b==originalChord);
    b.clear(); assert(chords.mChordPtr[0]->mBass==0 && chords.mChordPtr[0]->mAddNote[2]==7);
    for(int i=0;i<12;++i) assert(chords.mScalePtr[0]->up[i]==i && chords.mScalePtr[0]->down[i]==11-i);
    for(size_t n=0;n<originalChord.size();++n) assert(!chords.setChordTableResource(const_cast<u8*>(originalChord.data()),n));
    for(const auto [p,v]:{std::pair<size_t,u32>{0,1},{4,0},{8,65},{12,0xffffffff},{16,60},{20,60},{32,53},{36,0}}) {
        auto bad=originalChord; put(bad,p,v); assert(!chords.setChordTableResource(bad.data(),bad.size()));
    }
    b=wave(); const auto originalWave=b; SpkWave waves; assert(!waves.getWaveData(0));
    waves.setResource(b.data(),b.size()); assert(b==originalWave && waves.getWaveSize(0)==8);
    assert(waves.getLoopStartPos(0)==1 && waves.getLoopEndPos(0)==4);
    b.clear(); for(int i=0;i<4;++i) assert(waves.getWave(0)[i]==-2+i);
    assert(!waves.getWave(-1) && !waves.getWave(1) && waves.getWaveSize(1)==0);
    for(size_t n=0;n<originalWave.size();++n) rejected([&]{waves.setResource(const_cast<u8*>(originalWave.data()),n);});
    for(const auto [p,v]:{std::pair<size_t,u32>{0,33},{4,0xffffffff},{8,8},{8,13},{12,7},{12,10},{16,5},{20,5}}) {
        auto bad=originalWave; put(bad,p,v); rejected([&]{waves.setResource(bad.data(),bad.size());});
    }
    auto nonloop=originalWave; put(nonloop,16,0xffffffff); put(nonloop,20,0xffffffff);
    waves.setResource(nonloop.data(),nonloop.size()); assert(waves.getLoopEndPos(0)==0xffffffff);
    auto duplicate=originalWave; duplicate.resize(36); std::copy_backward(duplicate.begin()+12,duplicate.begin()+32,duplicate.begin()+36);
    put(duplicate,0,36); put(duplicate,4,2); put(duplicate,8,16); put(duplicate,12,16);
    waves.setResource(duplicate.data(),duplicate.size()); assert(waves.getWaveData(0)==waves.getWaveData(1) && waves.getWave(1)[0]==-2);
}
void realAssets(const char* root) {
    JKRMemArchive::setNativeResourceRoot(root);
    auto* archive=JKRMemArchive::mountNativeFile("AudioRes/SpkRes/SpkRes.arc",nullptr,JKRArchive::MOUNT_DIRECTION_1);
    SpkTable speaker; SpkWave waves;
    speaker.setResource(archive->getResource(u16(0))); waves.setResource(archive->getResource(u16(1)));
    assert(speaker.mResourceCount==34 && speaker.mParameters[0].mWaveID==0 && speaker.mParameters[0].mReleaseTime==200);
    assert(waves.mNativeOffsets.size()==34 && waves.getWaveSize(0)==8096 && waves.getLoopEndPos(0)==0xffffffff);
    for(int i=0;i<34;++i) assert(waves.getWave(i) && speaker.mNames[i][0]);
    archive->unmount(); assert(waves.getWaveSize(0)==8096 && speaker.mParameters[0].mVolume==120);
    archive=JKRMemArchive::mountNativeFile("AudioRes/Info/JaiMe.arc",nullptr,JKRArchive::MOUNT_DIRECTION_1);
    AudMePlayingParamsHolder music; music.setResource(archive->getResource(u16(1)));
    assert(music.mNumEntries==40 && music.mParams[0]._4==30 && music.mParams[0].mPriority==100);
    archive->unmount(); assert(music.mNames[0][0]);
    archive=JKRMemArchive::mountNativeFile("AudioRes/Info/JaiChord.arc",nullptr,JKRArchive::MOUNT_DIRECTION_1);
    std::unique_ptr<JKRArcFinder> finder(archive->getFirstFile("/")); size_t count=0;
    for(;finder->mHasMoreFiles;finder->findNextFile()) {
        if(finder->mFileIsFolder) continue; AudChordTable chords; assert(chords.setChordTable(finder->mFileID,archive));
        assert(chords.mChordCount>0 && chords.mScaleCount>0);
        for(int i=0;i<chords.mScaleCount;++i) for(int n=0;n<12;++n) assert(chords.mScalePtr[i]->up[n]<=11 || chords.mScalePtr[i]->up[n]==0x7f);
        ++count;
    }
    assert(count>50); archive->unmount();
    std::cout << "Converted " << count << " chord tables, 40 music-event parameters and 34 speaker waves\n";
}
}
int main(int argc,char** argv) {
    selfCheck();
    if(argc==2 && std::filesystem::exists(std::filesystem::path(argv[1])/"AudioRes/SpkRes/SpkRes.arc")) realAssets(argv[1]);
    std::cout << "Audio table bounds, ownership, pointer and PCM endian checks passed\n";
}
