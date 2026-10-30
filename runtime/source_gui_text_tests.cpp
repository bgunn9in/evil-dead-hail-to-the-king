#include "source_gui_text.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    try {
        if(argc<2||argc>3)throw jpc::Error("Pass original levelA/levelB.jpc and optional GUI asset root");
        const auto package=jpc::Package::load(argv[1]);jpc::Interpreter vm(package);SourceGui gui;
        const auto I=[](int n){return jpc::Value::integer(n);};
        const int system=gui.native(vm,"guiSysCreate",{I(2)}).integer();
        const int item=gui.native(vm,"guiItemCreate",{I(83),I(system)}).integer();
        gui.native(vm,"guiItemSetDataID",{I(item),I(int32_t(0xf1870800u))});
        const auto before=gui.items[size_t(item)];
        for(int ignored:{0,1,-17,2147483647}) {
            gui.native(vm,"guiItemAdvanceText",{I(item),I(ignored)});
            assert(gui.items[size_t(item)].flags==(before.flags|0x200u));
            assert(gui.items[size_t(item)].properties==before.properties);
        }
        const auto bytes=gui.snapshot();SourceGui copy;copy.restore(bytes);
        assert(copy.snapshot()==bytes&&(copy.items[size_t(item)].flags&0x200u));
        assert(package.methodIndex("ed/base/External","guiItemAdvanceText","(II)V")>=0);
        for(const auto& method:package.methods)if(package.classes[size_t(method.owner)].name=="ed/base/External"&&
            (SourceGui::setter(method.name)||method.name=="guiSysCreate"||method.name=="guiItemCreate"||method.name=="guiSysGetFlag")) {
            const auto name=method.name;vm.bind("ed/base/External",name,method.signature,[&,name](auto& machine,auto,const auto& args){return gui.native(machine,name,args);});
        }
        const auto box=vm.makeObject(package.classIndex("GameTextBox"));
        vm.invoke(package.methodIndex("GameTextBox","<init>","()V"),box);vm.invoke(package.methodIndex("GameTextBox","initialize","()V"),box);
        const int boxItem=vm.arrayValues(vm.field(box,835).reference()).at(0).integer();
        vm.invoke(package.methodIndex("GameTextBox","setTextAssetID","(I)V"),box,{I(int32_t(0xf1870800u))});
        assert(gui.items[size_t(boxItem)].textOffset==0&&!(gui.items[size_t(boxItem)].flags&0x200u));
        gui.items[size_t(boxItem)].textOffset=7;
        vm.invoke(package.methodIndex("GameTextBox","setTextAssetID","(I)V"),box,{I(int32_t(0xf1870800u))});
        assert(gui.items[size_t(boxItem)].textOffset==7&&(gui.items[size_t(boxItem)].flags&0x200u));
        vm.invoke(package.methodIndex("GameTextBox","setTextAssetID","(I)V"),box,{I(int32_t(0x11990a96u))});
        assert(gui.items[size_t(boxItem)].textOffset==0); // New resource executes actual40BBF0 path.
        // Native4096C0 accepts whole words, preserving the whitespace before
        // the first unshown word as its next-page pointer.
        const auto first=edguitext::layout("ONE TWO THREE FOUR",0,{0,0,84,44});
        assert(first.glyphs.size()==6&&first.nextOffset==7&&first.glyphs[0].x==0&&first.glyphs[3].y==22);
        const auto second=edguitext::layout("ONE TWO THREE FOUR",first.nextOffset,{0,0,84,44});
        assert(second.glyphs.size()==9&&second.nextOffset==0&&second.glyphs[0].code=='T');
        const auto breaks=edguitext::layout(" A@B\nC",0,{0,0,140,66});
        assert(breaks.glyphs.size()==3&&breaks.glyphs[0].x==0&&breaks.glyphs[1].y==22&&breaks.glyphs[2].y==44);
        const auto control=edguitext::layout("$A032048128{$C X",0,{0,0,140,22});
        assert(control.glyphs.size()==2&&control.glyphs[0].color==0xff203080u&&control.glyphs[1].color==0xffffffffu&&control.glyphs[1].x==28);
        assert(edguitext::layout("^",0,{0,0,140,22}).glyphs[0].code=='\''); //409CC0 mov al,27h.
        assert(edguitext::layout("OVERSIZEDWORD",0,{0,0,42,22}).glyphs.empty()); // Original no-progress witness.
        const auto split=edguitext::layout("OVERSIZEDWORD",0,{0,0,42,22},0,0xffffffff,false,true);
        assert(split.glyphs.size()==3&&split.nextOffset==3);
        std::string rebuilt;uint32_t cursor=0;
        do{const auto part=edguitext::layout("$A255000000OVERSIZEDWORD",cursor,{0,0,42,22},0,0xffffffff,false,true);
            for(const auto& glyph:part.glyphs)rebuilt+=char(glyph.code);
            assert(!part.nextOffset||part.nextOffset>cursor);cursor=part.nextOffset;}while(cursor);
        assert(rebuilt=="OVERSIZEDWORD");
        const auto scrolling=edguitext::marquee("ABCDEFGHIJK",{0,0,42,22},0,0xffffff00,1.25);
        bool partial=false;for(const auto& glyph:scrolling.glyphs){const auto crop=edguitext::croppedGlyph(scrolling,glyph);
            assert(!crop.w||(crop.x>=0&&crop.x+crop.w<=42));partial|=crop.w>0&&crop.w<14;}
        assert(partial);
        gui.items[size_t(item)].textOffset=7;gui.native(vm,"guiItemAdvanceText",{I(item),I(1)});
        edguitext::commitAdvance(gui.items[size_t(item)],second);assert(gui.items[size_t(item)].textOffset==0&&!(gui.items[size_t(item)].flags&0x200));
        gui.items[size_t(item)].textOffset=7;gui.native(vm,"guiItemSetDataID",{I(item),I(int32_t(0xf1870800u))});assert(gui.items[size_t(item)].textOffset==0);
        if(argc==3) {
            edguitext::Catalog assets(argv[2]);const auto rect=edguitext::window(assets.bounds(0x0b14208c,2),gui.items[size_t(item)],gui.systems[size_t(system)]);
            assert(rect.left==40&&rect.top==336&&rect.right==600&&rect.bottom==424);
            const bool b=std::any_of(package.classes.begin(),package.classes.end(),[](const auto& c){return c.name=="LevelB";});
            const uint32_t scene=b?0x62670a00u:0x61680200u,identity=b?0x31a65000u:0xf1870800u;
            assert(!assets.lookup(scene,identity).empty());
            const auto russianText=assets.lookup(scene,identity);const auto russianRevision=assets.textRevision();
            assets.setLanguage(UiLanguage::English);
            assert(!assets.localized()&&assets.textRevision()==0&&assets.lookup(scene,identity)==assets.originalText(scene,identity));
            bool invalidLanguage=false;try{assets.setLanguage(UiLanguage(255));}catch(const jpc::Error&){invalidLanguage=true;}
            assert(invalidLanguage&&assets.language()==UiLanguage::English);
            assets.setLanguage(UiLanguage::Russian);
            assert(assets.lookup(scene,identity)==russianText&&assets.textRevision()==russianRevision);
            if(!b){
                // Original DC AH01/AH02: "The still has run dry." Declared RU catalog is CP1251.
                const std::string expected=assets.localized()?
                    "\xd1\xe0\xec\xee\xe3\xee\xed\x20\xe2\x20\xe0\xef\xef\xe0\xf0\xe0\xf2\xe5\x20\xe7\xe0\xea\xee\xed\xf7\xe8\xeb\xf1\xff\x2e":
                    "The still has run dry.";
                assert(assets.lookup(scene,identity)==expected);
                // These two original DC allocations contain the same page-return reminder.
                assert(!assets.lookup(0x61630100,0x91820800).empty());
                assert(assets.lookup(0x61630100,0x91820800)==assets.lookup(0x616b0100,0x91820800));
            }
            gui.native(vm,"guiItemSetMayaInfo",{I(item),I(0x0b14208c),I(0x0b942050),I(2)});
            gui.native(vm,"guiItemSetRenderFunc",{I(item),I(3)});gui.native(vm,"guiItemSetDataID",{I(item),I(int32_t(identity))});
            gui.native(vm,"guiSysAddFlag",{I(system),I(1)});gui.native(vm,"guiItemAdvanceText",{I(item),I(1)});
            const auto page=edguitext::sample(gui,assets,scene,item,0);assert(!page.page.glyphs.empty()&&page.page.background&&!(gui.items[size_t(item)].flags&0x200));
            assert(page.page.glyphs[0].x==48&&page.page.glyphs[0].y==344);
            gui.items[size_t(item)].textOffset=32767;bool rejected=false;try{assets.validate(gui,scene);}catch(const jpc::Error&){rejected=true;}assert(rejected);
            gui.native(vm,"guiSysRemoveFlag",{I(system),I(1)});assets.validate(gui,scene); // Legitimate hidden old-scene cursor is not resolved.
            rejected=false;try{assets.validate(gui,scene,{},item);}catch(const jpc::Error&){rejected=true;}assert(rejected);
            gui.items[size_t(item)].textOffset=0;assets.validate(gui,scene,{},item);
        }
        std::cout<<"Original guiItemAdvanceText40B760: ignored argument2, deferred0x200 page request, idempotence and exact snapshot passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
