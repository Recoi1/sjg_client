#pragma once
#include <RVLFaceLibInternal.h>
#ifdef __cplusplus
extern "C" {
#endif
void RFLPCParseResource(RFLiLoader*);
u32 RFLPCResourceSize(const RFLiLoader*, u32 arc, u16 file);
void* RFLPCLoadResource(void* destination, const RFLiLoader*, u32 arc, u16 file);
void RFLPCConvertTexture(RFLiTexture*, u32 size);
void RFLPCConvertShape(RFLiShapeRes*, void* data, u32 size);
#ifdef __cplusplus
}
#endif
