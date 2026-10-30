#include "native_input_queue.h"
#include <iostream>
#include <stdexcept>
using namespace ednativeinput;
static void require(bool yes){if(!yes)throw std::runtime_error("Native FIFO policy differs");}
int main(){try{
    Queue q;for(unsigned n=0;n<33;++n)require(q.append({int(n),uint16_t(n+1),true}));require(!q.append({99,1,true})&&q.records.size()==33);q.clear();
    q.append(physical(15,1,true));q.append(physical(15,1,true));q.append(physical(16,1,false));q.append(physical(17,4,true));q.compact();require(q.records.size()==4);
    q.append(physical(19,8,true));q.compact();require(q.records.size()==4&&q.records[0].action==15&&q.records[1].action==16&&q.records[2].action==17&&q.records[3].action==19);q.clear();
    q.append(physical(16,1,false));q.append(physical(15,1,true));q.append(physical(17,4,true));q.append(physical(19,8,true));q.append(physical(140,2,true));q.compact();require(q.records.size()==4&&q.records[0].action==16&&q.records[1].action==17);q.clear();
    q.append(physical(24,0x400,true));q.append(physical(110,0x800,true));q.append(physical(112,0x2000,true));q.append(physical(15,1,true));q.append(physical(17,4,true));q.compact();require(q.records.size()==3&&q.records[0].code==0&&q.records[0].action==24);q.clear();
    q.append(physical(7,0x10,true));q.append(physical(3,0x40,true));q.append(physical(15,1,true));q.append(physical(110,0x800,true));
    unsigned count=0;bool more=true;while(more&&!q.records.empty()){const auto record=q.pop();more=direction(record);++count;}
    require(count==3&&q.records.size()==1&&q.records[0].action==110);q.clear();
    q.append({1,0xabcd,true});q.append({2,0xabcd,false});q.append({3,0x10,true});q.append({4,0x20,true});q.append({5,0x40,true});q.compact();require(q.records.size()==3);
    std::cout<<"Original native FIFO33/low-byte metadata/>4stable dedup/release dominance/direction prefix+one nondirection: passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
