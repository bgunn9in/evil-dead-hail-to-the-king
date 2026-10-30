#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <string>
#include "source_gui_text.h"

struct InventoryItemView { int itemID=0,quantity=0,slot=-1,containerField=99; };
struct InventoryView {
    bool open=false;
    std::vector<InventoryItemView> items;
    unsigned selected=0;
    int health=-1,maxHealth=0;
    bool reading=false;
    std::optional<edguitext::Page> documentPage;
    int combineSlot=-1,combineItemID=-1;
    std::string notice;
};
inline int selectedInventorySlot(const InventoryView& view) {
    return view.items.empty()?-1:view.items[std::min<std::size_t>(view.selected,view.items.size()-1)].slot;
}
inline void moveInventorySelection(InventoryView& view,int delta) {
    if (view.items.empty()) { view.selected=0; return; }
    const int count=int(view.items.size()),current=int(std::min<std::size_t>(view.selected,view.items.size()-1));
    if (delta==1||delta==-1) view.selected=unsigned((current+delta+count)%count);
    else if (delta==4||delta==-4) {
        const int rows=(count+3)/4,row=(current/4+(delta>0?1:-1)+rows)%rows;
        view.selected=unsigned(std::min(row*4+current%4,count-1));
    } else view.selected=unsigned(current);
}
struct InventoryNavigator {
    int held=0;
    float remaining=0;
    void reset() { held=0; remaining=0; }
    int update(float x,float y,float dt) {
        if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(dt)||dt<0) { reset(); return 0; }
        const int direction=std::max(std::abs(x),std::abs(y))<0.5f?0:
            (std::abs(x)>std::abs(y)?(x>0?1:-1):(y>0?4:-4));
        if (!direction) { reset(); return 0; }
        if (direction!=held) { held=direction; remaining=0.35f; return direction; }
        remaining-=dt;
        if (remaining>0) return 0;
        remaining=0.12f;
        return direction;
    }
};
