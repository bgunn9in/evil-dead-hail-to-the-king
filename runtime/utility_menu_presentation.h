#pragma once
#include "source_gui_draw.h"
#include "utility_menu.h"

namespace edutility {
class Presenter {
    edguipresentation::Sprite frame;
    static constexpr int LineHeight=22,BodyTop=88,BodyLines=11;
    // Leave the text cell crossing the original book's internal rail empty.
    static int bodyY(unsigned slot) { return BodyTop+int(slot)*LineHeight+(slot>=5?LineHeight:0); }
    static constexpr uint32_t Selected=0xffffff00u,Unselected=0xff606060u;
    static void validate(const std::string& text) {
        if(text.size()>32766)throw std::runtime_error("Utility menu line exceeds text bound");
        for(uint8_t c:text)if(!edfont::cp1251Byte(c)||c=='$'||c=='@')
            throw std::runtime_error("Utility menu requires literal bounded font bytes");
    }
    static edguitext::Page line(const std::string& text,int y,uint32_t color,double seconds) {
        validate(text);
        return edguitext::marquee(text,{54,y,586,y+LineHeight},2,color,seconds);
    }
public:
    explicit Presenter(const std::filesystem::path& root) {
        const auto mesh=edobjects::ObjectMesh::load(root/"gui/all-models/0b142188.mesh");
        if(mesh.identity!=0x0b142188u||mesh.partFlags.size()!=8)
            throw std::runtime_error("Original book frame model differs");
        // InvBookText.initialize1191 binds these four original frame parts to054.
        frame.item=-1;frame.texture=0x0b942054u;
        float l=1000,t=1000,r=-1000,b=-1000;
        for(const auto& source:mesh.vertices)if(source.part>=2&&source.part<=5) {
            const float x=320+20*source.geometry.x,y=240-20*source.geometry.y;
            l=std::min(l,x);r=std::max(r,x);t=std::min(t,y);b=std::max(b,y);
        }
        if(!(l<r&&t<b))throw std::runtime_error("Original book frame bounds are empty");
        for(const auto& source:mesh.vertices)if(source.part>=2&&source.part<=5) {
            auto v=source.geometry;
            const float x=28+(320+20*v.x-l)*584/(r-l),y=20+(240-20*v.y-t)*440/(b-t);
            v.x=x/320-1;v.y=1-y/240;v.z=.5f;v.nx=v.ny=0;v.nz=1;
            frame.vertices.push_back(v);
        }
        if(frame.vertices.empty()||frame.vertices.size()>10000)
            throw std::runtime_error("Original book frame vertex bound differs");
    }
    std::vector<edguipresentation::Command> sample(const Menu& menu,double seconds=0)const {
        if(menu.rows.size()>128||menu.details.size()>6||menu.footer.size()>2||
           (!menu.rows.empty()&&menu.focus>=menu.rows.size()))
            throw std::runtime_error("Utility menu exceeds bounded native layout");
        const unsigned tail=unsigned(menu.details.size())+(!menu.details.empty()?1u:0u);
        if(tail>=BodyLines)throw std::runtime_error("Utility menu has no row window");
        const unsigned visible=std::min(unsigned(menu.rows.size()),unsigned(BodyLines)-tail);
        const unsigned begin=menu.rows.size()<=visible?0:std::min(menu.focus>visible/2?menu.focus-visible/2:0,
                                                               unsigned(menu.rows.size())-visible);
        std::vector<edguipresentation::Command> result{{-1,frame}};
        auto add=[&](const std::string& text,int y,uint32_t color){
            if(!text.empty())result.push_back({-1,line(text,y,color,seconds)});
        };
        add(menu.title,42,Selected);
        unsigned slot=0;
        for(unsigned n=begin;n<begin+visible;++n,++slot)
            add(menu.rows[n].text,bodyY(slot),menu.rows[n].selected?Selected:Unselected);
        if(!menu.details.empty())++slot;
        for(const auto& row:menu.details){add(row.text,bodyY(slot++),row.selected?Selected:Unselected);}
        // Keep the original book frame's horizontal footer rail clear.
        int y=menu.notice.empty()?392:380;
        if(!menu.notice.empty()){auto notice=menu.notice;for(char& c:notice)if(c=='\n'||c=='\r'||c=='\t')c=' ';
            add(notice,y,Selected);y+=LineHeight;}
        if(!menu.notice.empty()&&menu.footer.size()==2){
            validate(menu.footer[0]);validate(menu.footer[1]);
            auto help=edguitext::pagedNotice(menu.footer[0]+"\n"+menu.footer[1],{54,y,586,440},Unselected,seconds);
            result.push_back({-1,std::move(help)});
        }else for(const auto& footer:menu.footer){add(footer,y,Unselected);y+=LineHeight;}
        return result;
    }
    std::vector<edguipresentation::Command> notice(const std::string& text,double seconds=0)const {
        if(text.empty())return {};
        auto literal=text;for(char& c:literal)if(c=='\r')c='\n';else if(c=='\t')c=' ';
        auto check=literal;std::replace(check.begin(),check.end(),'\n',' ');validate(check);
        auto page=edguitext::pagedNotice(literal,{54,396,586,440},Selected,seconds);
        return {{-1,std::move(page)}};
    }
};
} // namespace edutility
