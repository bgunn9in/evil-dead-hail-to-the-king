#include "inventory_ui.h"
#undef NDEBUG
#include <cassert>

int main() {
    InventoryView view;
    assert(selectedInventorySlot(view)==-1);
    for(int i=0;i<5;++i) view.items.push_back({119,1,10+i});
    view.selected=3; moveInventorySelection(view,4); assert(view.selected==4);
    moveInventorySelection(view,-4); assert(view.selected==0);
    moveInventorySelection(view,-1); assert(view.selected==4);
    moveInventorySelection(view,1); assert(view.selected==0);
    view.selected=999; assert(selectedInventorySlot(view)==14);
    moveInventorySelection(view,0); assert(view.selected==4);
    InventoryNavigator navigation;
    assert(navigation.update(0.3f,0,0.01f)==0);
    assert(navigation.update(1,0,0.01f)==1);
    assert(navigation.update(1,0,0.2f)==0);
    assert(navigation.update(1,0,0.16f)==1);
    assert(navigation.update(1,0,0.05f)==0);
    assert(navigation.update(0,-1,0)==-4);
    InventoryView containers;containers.items={{119,1,0,99},{40,1,0,98}};
    moveInventorySelection(containers,1);assert(selectedInventorySlot(containers)==0&&containers.items[containers.selected].containerField==98);
    moveInventorySelection(containers,-1);assert(containers.items[containers.selected].containerField==99);
    assert(navigation.update(0,0,0)==0);
    assert(navigation.update(0,-1,0)==-4);
}
