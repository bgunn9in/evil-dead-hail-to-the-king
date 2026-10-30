#include "source_gui.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using Bytes=std::vector<uint8_t>;
static Bytes fixture(){statebytes::Writer out;for(char c:std::string("EGUIUV1\0",8))out.put<uint8_t>(uint8_t(c));out.put<uint32_t>(3);
 for(unsigned model=0;model<3;++model){out.put<uint32_t>(0x0b000001+model);out.put<uint32_t>(1);out.put<uint32_t>(4);out.put<uint32_t>(6);
  for(unsigned vertex=0;vertex<4;++vertex){out.put<float>(float(model)*.25f+float(vertex)*.01f);out.put<float>(float(vertex)*.02f);}
  for(uint16_t index:{0,1,2,0,2,3})out.put(index);}return out.bytes;}
static Bytes legacy(const SourceGui& gui){statebytes::Writer out;out.text("EDGUI3");out.put<int32_t>(gui.stereo);out.put<int32_t>(gui.masterVolume);out.put<int32_t>(gui.activeSystem);
 auto table=[&](const auto& records){out.put<uint32_t>(uint32_t(records.size()));for(const auto& r:records){out.put(r.flags);out.put(r.pad);out.put(r.x);out.put(r.y);out.put(r.textOffset);out.put<int32_t>(r.owner);out.put<int32_t>(r.ordinal);out.put<int32_t>(r.selected);for(int n:r.links)out.put<int32_t>(n);out.put<uint32_t>(uint32_t(r.properties.size()));for(const auto& [key,args]:r.properties){out.text(key);out.put<uint32_t>(uint32_t(args.size()));for(int n:args)out.put<int32_t>(n);}}};table(gui.systems);table(gui.items);return out.bytes;}
int main(int argc,char** argv){try{if(argc!=3)throw jpc::Error("Pass original JPC and real GUI UV sidecar");const auto package=jpc::Package::load(argv[1]);jpc::Interpreter vm(package);SourceGui gui;
 auto call=[&](const std::string& name,std::initializer_list<int> args){std::vector<jpc::Value> values;for(int n:args)values.push_back(jpc::Value::integer(n));return gui.native(vm,name,values);};
 const int sys=call("guiSysCreate",{1}).integer(),item=call("guiItemCreate",{17,sys}).integer();
 assert((gui.items[item].properties.at("guiItemSetColor")==std::vector<int32_t>{128,128,128,128}));
 call("guiItemSetRenderFunc",{item,4});call("guiItemSetRenderFunc",{item,1});call("guiItemSetRenderFunc",{item,-1});assert(gui.items[item].properties.at("guiItemSetRenderFunc")==std::vector<int32_t>{4});
 call("guiItemSetMayaInfo",{item,0x0b000001,0x0b940001,257});assert(gui.items[item].properties.at("guiItemSetMayaInfo")[2]==1);
 call("guiItemSetPartID",{item,255});call("guiItemSetMayaInfoModelID",{item,0x0b940002});assert((gui.items[item].properties.at("guiItemSetMayaInfo")==std::vector<int32_t>{0x0b000001,0x0b940002,-1}));
 call("guiItemSetMayaInfo",{item,0x0b000002,0x0b940003,0});assert((gui.items[item].properties.at("guiItemSetMayaInfo")==std::vector<int32_t>{0x0b000002,0x0b940003,0}));
 call("guiItemSetMayaVal",{item,1,258});call("guiItemSetMayaVal",{item,0,-1});call("guiItemSetMayaVal",{item,2,65});assert((gui.items[item].properties.at("guiItemSetMayaVal")==std::vector<int32_t>{255,2,65}));
 assert(gui.selectionPulse==255&&!gui.pulseDescending);gui.finishRender();assert(gui.selectionPulse==255&&gui.pulseDescending);gui.finishRender();assert(gui.selectionPulse==251);for(unsigned n=0;n<31;++n)gui.finishRender();assert(gui.selectionPulse==128&&!gui.pulseDescending);gui.finishRender();assert(gui.selectionPulse==132);
 auto before=gui.snapshot();bool bad=false;try{call("guiItemSetMayaVal",{item,3,7});}catch(const jpc::Error&){bad=true;}assert(bad&&gui.snapshot()==before);
 gui.uvLayout=std::make_shared<const edguiuv::Layout>(edguiuv::Layout::decode(fixture()));gui.uvFingerprint=gui.uvLayout->fingerprint;
 gui.copyUVs({0x0b000001,0},{0x0b000002,0});gui.copyUVs({0x0b000002,0},{0x0b000003,0});assert((gui.uvSource({0x0b000001,0})==edguiuv::Key{0x0b000002,0}));assert((gui.uvSource({0x0b000002,0})==edguiuv::Key{0x0b000003,0}));
 gui.copyUVs({0x0b000003,0},{0x0b000001,0});assert((gui.uvSource({0x0b000003,0})==edguiuv::Key{0x0b000002,0}));
 before=gui.snapshot();gui.copyUVs({0x0b000001,0},{0x0bffffff,0});assert(gui.snapshot()==before); // Missing source native no-op.
 SourceGui restored;restored.uvLayout=gui.uvLayout;restored.restore(before);assert(restored.snapshot()==before);
 restored.copyUVs({0x0b000001,0},{0x0b000002,0});gui.copyUVs({0x0b000001,0},{0x0b000002,0});assert(restored.snapshot()==gui.snapshot());
 auto broken=before;broken.pop_back();const auto stable=restored.snapshot();bad=false;try{restored.restore(broken);}catch(const std::exception&){bad=true;}assert(bad&&restored.snapshot()==stable);
 broken=before;broken[broken.size()-4]=255;bad=false;try{restored.restore(broken);}catch(const std::exception&){bad=true;}assert(bad&&restored.snapshot()==stable);
 auto malformed=fixture();malformed.back()=255;bad=false;try{edguiuv::Layout::decode(malformed);}catch(const std::exception&){bad=true;}assert(bad);
 SourceGui old;old.items[0].properties["guiItemSetMayaInfo"]={0x0b000001,0x0b940001,0};old.items[0].properties["guiItemSetPartID"]={2};old.items[0].properties["guiItemSetMayaInfoTextureID"]={0x0b940002};old.items[0].properties["guiItemSetMayaVal"]={1,255};
 SourceGui migrated;migrated.restore(legacy(old));assert((migrated.items[0].properties.at("guiItemSetMayaInfo")==std::vector<int32_t>{0x0b000001,0x0b940002,2}));assert((migrated.items[0].properties.at("guiItemSetMayaVal")==std::vector<int32_t>{0,255,0}));
 auto preservedMaya=gui.items[item].properties.at("guiItemSetMayaInfo"),preservedBytes=gui.items[item].properties.at("guiItemSetMayaVal");call("guiItemSetDataID",{item,77});call("guiItemSetFlag",{item,0});assert(call("guiItemCreate",{17,sys}).integer()==item);assert(gui.items[item].properties.at("guiItemSetMayaInfo")==preservedMaya&&gui.items[item].properties.at("guiItemSetMayaVal")==preservedBytes);assert(gui.items[item].properties.at("guiItemSetDataID")==std::vector<int32_t>{77}&&gui.items[item].properties.at("guiItemSetRenderFunc")==std::vector<int32_t>{0});
 SourceGui wrongLayout;auto wrongBytes=fixture();wrongBytes[28]^=1;wrongLayout.uvLayout=std::make_shared<const edguiuv::Layout>(edguiuv::Layout::decode(wrongBytes));const auto unchanged=wrongLayout.snapshot();bad=false;try{wrongLayout.restore(before);}catch(const std::exception&){bad=true;}assert(bad&&wrongLayout.snapshot()==unchanged);
 const bool levelB=std::any_of(package.classes.begin(),package.classes.end(),[](const auto& c){return c.name=="LevelB";});
 // A retains its95 native models/624parts plus the explicit32-part debug inventory union model.
 SourceGui actual;actual.loadUVs(argv[2]);assert(actual.uvLayout->models.size()==(levelB?102u:96u));size_t parts=0;for(const auto& model:actual.uvLayout->models)parts+=model.second.size();assert(parts==(levelB?647u:656u));
 actual.copyUVs({0x0b14218c,0},{0x0b14224c,14});const auto saved=actual.snapshot();SourceGui cold;cold.loadUVs(argv[2]);cold.restore(saved);assert(cold.snapshot()==saved);
 std::cout<<"Native GUI effective setter order/default128/render1/bytewrap/UV value-copy/save4/cold/future/legacy+atomic negatives; actual"<<actual.uvLayout->models.size()<<"models/"<<parts<<"parts PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
