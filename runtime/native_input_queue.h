#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ednativeinput {
struct Record {int action=0;uint16_t code=0;bool pressed=false;};
inline Record physical(int action,unsigned mask,bool pressed){return {action,uint8_t(mask),pressed};}
inline bool direction(const Record& record){return record.code==0x10||record.code==0x20||record.code==0x40||record.code==0x80;}
struct Queue {
    static constexpr unsigned Capacity=33;
    std::vector<Record> records;
    bool append(Record record){if(records.size()>=Capacity)return false;records.push_back(record);return true;}
    void compact(){
        if(records.size()<=4)return;
        std::array<bool,Capacity> removed{};
        for(size_t i=0;i+1<records.size();++i){
            if(removed[i]||records[i].code==0xabcd)continue;
            for(size_t j=i+1;j<records.size();++j)
                if(!removed[j]&&records[j].code==records[i].code&&
                   (records[i].pressed==records[j].pressed||!records[i].pressed))removed[j]=true;
        }
        size_t kept=0;for(size_t i=0;i<records.size();++i)if(!removed[i]&&records[i].code!=0xabcd)records[kept++]=records[i];
        records.resize(kept);
    }
    Record pop(){const Record record=records.front();records.erase(records.begin());return record;}
    void clear(){records.clear();}
};
}
