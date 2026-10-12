#include "JSystem/JSupport/JSUMemoryInputStream.hpp"
#include "JSystem/JSupport/JSUMemoryOutputStream.hpp"
#include <array>
#include <cassert>
#include <climits>
#include <stdexcept>

template<class F> void rejected(F f) { bool caught=false; try { f(); } catch(const std::invalid_argument&) { caught=true; } assert(caught); }
int main() {
    const std::array<u8,4> source={0x12,0x34,0x56,0x78};
    std::array<u8,6> destination; destination.fill(0xcc);
    JSUMemoryInputStream input(source.data(),4);
    assert(input.read(destination.data()+1,2)==2 && destination[1]==0x12 && destination[2]==0x34);
    assert(input.read(destination.data()+3,INT_MAX)==2 && destination[3]==0x56 && destination[4]==0x78);
    assert(destination[0]==0xcc && destination[5]==0xcc && input.getPosition()==4 && !input.isGood());
    assert(input.readData(nullptr,1)==0 && input.readData(nullptr,-1)==0);
    assert(input.seek(2,SEEK_FROM_END)==-2 && input.getPosition()==2 && input.isGood());
    assert(input.seek(INT_MAX,SEEK_FROM_POSITION)==2 && input.getPosition()==4);
    assert(input.seek(INT_MIN,SEEK_FROM_POSITION)==-4 && input.getPosition()==0);
    assert(input.seek(INT_MIN,SEEK_FROM_END)==4 && input.getPosition()==4);
    assert(input.seek(INT_MAX,SEEK_FROM_END)==-4 && input.getPosition()==0);
    rejected([&]{input.readData(nullptr,1);}); assert(input.getPosition()==0);
    rejected([&]{input.setBuffer(nullptr,1);}); rejected([&]{input.setBuffer(source.data(),-1);});
    rejected([&]{input.seekPos(0,static_cast<JSUStreamSeekFrom>(99));});
    JSUMemoryInputStream empty(nullptr,0); assert(empty.readData(nullptr,INT_MAX)==0);
    std::array<u8,6> output; output.fill(0xcc);
    JSUMemoryOutputStream writer(output.data()+1,4);
    assert(writer.write(source.data(),INT_MAX)==4 && !writer.isGood());
    for(size_t i=0;i<4;++i) assert(output[i+1]==source[i]);
    assert(output[0]==0xcc && output[5]==0xcc && writer.writeData(nullptr,1)==0);
    assert(writer.seek(INT_MIN,SEEK_FROM_POSITION)==-4 && writer.isGood());
    rejected([&]{writer.writeData(nullptr,1);}); assert(writer.getPosition()==0);
    assert(writer.writeData(source.data(),-1)==0 && writer.getPosition()==0);
    assert(writer.seek(INT_MIN,SEEK_FROM_END)==4 && writer.getPosition()==4);
    assert(writer.seek(INT_MAX,SEEK_FROM_END)==-4 && writer.getPosition()==0);
    rejected([&]{writer.setBuffer(nullptr,1);}); rejected([&]{writer.setBuffer(output.data(),-1);});
}
