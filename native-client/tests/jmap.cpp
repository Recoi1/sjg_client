#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/StringUtil.hpp"
#include "JSystem/JGadget/hashcode.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRFileFinder.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifdef GALAXY_EMBEDDED_TABLES
#include "rom_resources.hpp"
#endif

namespace {
void put(std::vector<u8>& b, size_t pos, u32 value, size_t width = 4) {
    for (size_t i = 0; i < width; ++i) b[pos+i] = u8(value >> (8*(width-i-1)));
}
std::vector<u8> fixture() {
    std::vector<u8> b(16 + 8*12 + 2*48 + 6);
    put(b, 0, 2); put(b, 4, 8); put(b, 8, 112); put(b, 12, 48);
    struct Field { const char* name; u32 mask; u16 offset; u8 shift, type; };
    const Field fields[] = {{"int", 0xffffffff, 0, 0, 0}, {"text", 0, 12, 0, 1},
        {"float", 0xffffffff, 4, 0, 2}, {"flags", 0xf0, 8, 4, 3},
        {"short", 0xffff, 9, 0, 4}, {"byte", 0xff, 11, 0, 5},
        {"name", 0xffffffff, 44, 0, 6}, {"flag2", 0x0f, 40, 0, 3}};
    for (size_t i = 0; i < 8; ++i) {
        const auto& f = fields[i]; size_t p = 16+i*12;
        put(b, p, JGadget::getHashCode(f.name)); put(b, p+4, f.mask);
        put(b, p+8, f.offset, 2); b[p+10]=f.shift; b[p+11]=f.type;
    }
    put(b, 16+3*12+8, 40, 2);
    for (size_t r=0;r<2;++r) {
        const auto p=112+r*48;
        put(b,p,0xfffffffe); put(b,p+4,0x3fc00000); b[p+8]=0xab;
        put(b,p+9,0xfffd,2); b[p+11]=0xfc;
        std::memcpy(b.data()+p+12,"inline",7); put(b,p+40,0xab); put(b,p+44,r*3);
    }
    std::memcpy(b.data()+208,"aa\0bb",6);
    return b;
}
void check() {
    auto b=fixture(); const auto original=b;
    JMapInfo info; assert(!info.attach(b.data())); assert(info.attach(b.data(),b.size()));
    assert(b==original && info.getNumEntries()==2 && info.getNumFields()==8);
    s32 integer; u32 bits; float number; const char* text; bool flag;
    assert(info.getValue(0,"int",&integer) && integer==-2);
    assert(info.getValue(0,"short",&integer) && integer==-3);
    assert(info.getValue(0,"byte",&integer) && integer==-4);
    assert(info.getValue(0,"float",&number) && number==1.5f);
    assert(info.getValue(0,"flags",&bits) && bits==10);
    assert(info.getValue(0,"flag2",&bits) && bits==11);
    assert(info.getValue(0,"flags",&flag) && flag);
    assert(info.getValue(0,"text",&text) && std::string(text)=="inline");
    assert(info.getValue(1,"name",&text) && std::string(text)=="bb");
    assert(!info.getValue(-1,"int",&integer) && !info.getValue(2,"int",&integer));
    assert(!info.getValue(0,"missing",&integer) && !info.getValue(0,"float",&integer));
    assert(!info.getValueFast(0,-1,&integer) && !info.getValueFast(0,8,&integer));
    assert(!info.getValue(0,"int",static_cast<s32*>(nullptr)));
    assert(info.findElement("missing",s32(0),0)==info.end());
    assert(info.findElement("int",s32(-2),-1).mIndex==0);
    assert(info.findElementBinary("name","bb").mIndex==1);
    assert(info.findElementBinary("missing","bb")==info.end());
    assert(MR::findJMapInfoElementNoCase(&info,"name","BB",0).mIndex==1);
    assert(MR::findJMapInfoElementNoCase(&info,"missing","BB",0)==info.end());
    assert(MR::isEqualString("same","same") && !MR::isEqualString("A","a"));
    JMapInfo copy=info, assigned; assigned=info;
    assert(copy==info && assigned==info && copy.mData!=info.mData);
    b.clear(); info=JMapInfo();
    assert(copy.getValue(1,"name",&text) && std::string(text)=="bb");
    assert(assigned.getValue(0,"int",&integer) && integer==-2);
    for (size_t n=0;n<original.size();++n) { JMapInfo bad; assert(!bad.attach(original.data(),n)); }
    const std::pair<size_t,u32> corruptions[]={{0,0xffffffff},{4,0xffffffff},{8,0},{8,0xffffffff},
        {12,0},{12,0xffffffff},{156,6},{16+3*12+8,0x000aff03},{16+3*12+8,0x00012003},
        {16+3*12+8,0x000a0003},{16+3*12+8,0x000c0003},{16+3*12+8,0x00280007}};
    for (const auto [p,v]:corruptions) { auto bad=original; put(bad,p,v); assert(!copy.attach(bad.data(),bad.size())); }
    auto bad=original; std::fill(bad.begin()+208,bad.end(),u8('x')); assert(!copy.attach(bad.data(),bad.size()));
    bad=original; std::fill(bad.begin()+124,bad.begin()+160,u8('x')); assert(!copy.attach(bad.data(),bad.size()));
    assert(copy.getValue(0,"int",&integer) && integer==-2);
#ifdef GALAXY_EMBEDDED_TABLES
    JMapInfo galaxy, story; assert(galaxy.attach(&GalaxyIDBCSV)); assert(story.attach(StoryEventBCSV));
    assert(galaxy.getNumEntries()==55 && galaxy.getNumFields()==7);
    assert(story.getNumEntries()==14 && story.getNumFields()==2);
    const auto castle = story.findElement("name", "ピーチ城浮上後", 0);
    u32 progress = 0;
    assert(castle.isValid() && castle.getValue("progress", &progress) && progress > 0);
    const auto attack = story.findElement("name", "クッパ襲来後", 0);
    assert(attack.isValid() && attack.getValue("progress", &progress) && progress > 0);
    JMapInfo storyCopy = story;
    story = JMapInfo();
    assert(storyCopy.findElement("name", "スピン権利", 0).isValid());
    assert(galaxy.findElement("name","HeavensDoorGalaxy",0).isValid());
#endif
}
size_t walk(JKRMemArchive& archive, const std::string& path) {
    size_t count=0;
    std::unique_ptr<JKRArcFinder> finder(archive.getFirstFile(path.empty() ? "/" : path.c_str()));
    assert(finder);
    for (;finder->mHasMoreFiles;finder->findNextFile()) {
        const std::string name=finder->mName;
        if (name=="." || name=="..") continue;
        if (finder->mFileIsFolder) { count+=walk(archive,path+"/"+name); continue; }
        if (name.ends_with(".canm")) continue;
        JMapInfo table;
        auto* resource=archive.getIdxResource(finder->mDirIndex);
        if (!table.attach(resource)) { std::cerr << "Rejected actual table: " << path << "/" << name << '\n'; std::abort(); }
        for (int row=0;row<table.getNumEntries();++row) for(int field=0;field<table.getNumFields();++field) {
            const auto type=table.mData->mItems[field].mType;
            if(type==1 || type==6) { const char* text; assert(table.getValueFast(row,field,&text)); }
            else if(type==2) { float value; assert(table.getValueFast(row,field,&value)); }
            else { u32 value; assert(table.getValueFast(row,field,&value)); }
        }
        ++count;
    }
    return count;
}
}
int main(int argc,char** argv) {
    check();
    if(argc==2 && std::filesystem::exists(std::filesystem::path(argv[1])/"StageData/HeavensDoorGalaxy.arc")) {
        JKRMemArchive::setNativeResourceRoot(argv[1]); size_t count=0;
        for (const char* name:{"StageData/HeavensDoorGalaxy/HeavensDoorGalaxyScenario.arc", "StageData/HeavensDoorGalaxy.arc", "StageData/EggStarGalaxy.arc"}) {
            auto* archive=JKRMemArchive::mountNativeFile(name,nullptr,JKRArchive::MOUNT_DIRECTION_1);
            count+=walk(*archive,"");
            if(std::string(name).find("Scenario.arc")!=std::string::npos) {
                JMapInfo scenarios; assert(scenarios.attach(archive->getResource("ScenarioData.bcsv")));
                const auto firstScenario=scenarios.findElement("ScenarioNo",s32(1),0);
                s32 star; u32 layers;
                assert(firstScenario.isValid() && firstScenario.getValue("PowerStarId",&star) && star==1);
                assert(firstScenario.getValue("HeavensDoorGalaxy",&layers) && layers==1);
                JMapInfo zones; assert(zones.attach(archive->getResource("ZoneList.bcsv")));
                const char* first; assert(zones.getValue(0,"ZoneName",&first) && std::string(first)=="HeavensDoorGalaxy");
                archive->unmount(); assert(zones.getValue(0,"ZoneName",&first));
            } else archive->unmount();
        }
        assert(count>10); std::cout << "Read all fields from " << count << " actual stage/scenario tables\n";
    }
    std::cout << "BCSV bounds, endian, bitfield, and ownership checks passed\n";
}
