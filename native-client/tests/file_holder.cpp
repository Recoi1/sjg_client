#include <cassert>
#include <cstring>
// Run the real publisher with a consumer scheduled before its send returns.
#define DVDConvertPathToEntrynum testEntryNum
#define OSInitMessageQueue testInitQueue
#define OSSendMessage testSendMessage
#define OSReceiveMessage testReceiveMessage
#include "../Petari/src/Game/System/FileHolder.cpp"
#undef DVDConvertPathToEntrynum
#undef OSInitMessageQueue
#undef OSSendMessage
#undef OSReceiveMessage
namespace { FileHolderFileEntry* consumer; unsigned reads=0; }
extern "C" {
s32 testEntryNum(const char*) { return 1; }
void testInitQueue(OSMessageQueue* queue,OSMessage* storage,s32 count) {
    std::memset(queue,0,sizeof(*queue)); queue->msgArray=storage; queue->msgCount=count;
}
BOOL testReceiveMessage(OSMessageQueue* queue,OSMessage*,s32) {
    assert(queue->usedCount==1); queue->usedCount=0; ++reads; return true;
}
BOOL testSendMessage(OSMessageQueue* queue,OSMessage,s32) {
    queue->usedCount=1; consumer->waitReadDone(); return true;
}
}
int main() {
    u8 bytes{}; FileHolderFileEntry entry("archive",nullptr,&bytes); consumer=&entry;
    entry.setContext(&bytes,nullptr); assert(reads==1 && entry.mContext==&bytes);
    entry.waitReadDone(); entry.waitReadDone(); assert(reads==1);
}
