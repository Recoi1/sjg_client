#pragma once
#include "Game/Screen/LayoutActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
class MiiSelectIcon;
class FileSelectModel;
class NativeCharacterSelect : public LayoutActor {
public:
    NativeCharacterSelect();
    void open(int slot, bool mario);
    void control() override;
    void calcAnim() override;
    void draw() const override;
    void drawPortrait(unsigned character,float x,float y,float scale);
    void drawHead(unsigned character) const;
    MiiSelectIcon* icons[4]{};
    FileSelectModel* heads[4]{};
    TPos3f hosts[4];
    TVec2f lastPointer;
    unsigned phase = 0, age = 0;
};
