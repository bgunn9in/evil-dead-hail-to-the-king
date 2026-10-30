// Included inside Graphics; source text and Maya quads share existing UI shaders.
static void textPageKey(std::string& key,const edguitext::Page& page) {
    const auto put=[&](auto value){key.append(reinterpret_cast<const char*>(&value),sizeof(value));};
    for(int value:{page.bounds.left,page.bounds.top,page.bounds.right,page.bounds.bottom})put(value);
    put(uint8_t(page.background));put(uint8_t(page.clip));put(uint32_t(page.glyphs.size()));
    for(const auto& g:page.glyphs){put(g.x);put(g.y);put(g.code);put(g.color);put(g.w);put(g.h);}
}
void appendTextPage(const edguitext::Page& page,std::vector<Vertex>& vertices,std::vector<InventoryDraw>& draws) {
    if(!font||!fontColumns)throw std::runtime_error("Source text font is unavailable");
    const auto rectangle=[&](int x,int y,int w,int h,float u,float v,float u1,float v1,int texture,uint32_t color) {
        const float l=float(x)/320-1,r=float(x+w)/320-1,t=1-float(y)/240,b=1-float(y+h)/240;
        const unsigned first=unsigned(vertices.size());
        vertices.insert(vertices.end(),{{l,t,0,0,0,0,u,v},{r,t,0,0,0,0,u1,v},{l,b,0,0,0,0,u,v1},
            {l,b,0,0,0,0,u,v1},{r,t,0,0,0,0,u1,v},{r,b,0,0,0,0,u1,v1}});
        const auto tint=unpackTint(color);
        if(!draws.empty()&&draws.back().texture==texture&&draws.back().color==tint&&draws.back().first+draws.back().count==first)draws.back().count+=6;
        else draws.push_back({first,6,texture,tint});
    };
    if(page.glyphs.size()>8192)throw std::runtime_error("Excessive source text page");
    if(page.background)rectangle(page.bounds.left,page.bounds.top,page.bounds.right-page.bounds.left,page.bounds.bottom-page.bounds.top,0,0,1,1,-2,0x80000000u);
    for(const auto& g:page.glyphs) {
        const auto index=edfont::glyph(fontCharset,g.code);if(index==std::string::npos)continue;
        const float u=float((index%fontColumns)*fontWidth)/fontAtlasWidth,v=float((index/fontColumns)*fontHeight)/fontAtlasHeight;
        const auto crop=edguitext::croppedGlyph(page,g);if(!crop.w)continue;
        const float du=float(fontWidth)/fontAtlasWidth,dv=float(fontHeight)/fontAtlasHeight;
        rectangle(crop.x,crop.y,crop.w,crop.h,u+du*crop.u0,v+dv*crop.v0,u+du*crop.u1,v+dv*crop.v1,-1,g.color);
    }
}
SDL_GPUTexture* guiTexture(uint32_t id) {
    const auto found=objectTextures.find(id);if(found!=objectTextures.end())return found->second;
    const auto data=edobjects::Texture::load(uiTexturePath(sourceAssets,id,uiLanguage_));
    if(data.identity!=id)throw std::runtime_error("Source GUI texture identity mismatch");
    auto* image=uploadTexture(data.rgba.data(),data.width,data.height,data.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    try{objectTextures.emplace(id,image);}catch(...){SDL_ReleaseGPUTexture(device,image);throw;}return image;
}
void updateSourceGui(const std::vector<edguitext::Page>& pages,const std::vector<edguipresentation::Sprite>& sprites,
                     const std::vector<edguipresentation::Command>& commands={}) {
    if(pages.size()>250||sprites.size()>1000||commands.size()>1000)throw std::runtime_error("Excessive source GUI presentation");
    std::string key;for(uint32_t count:{uint32_t(pages.size()),uint32_t(sprites.size()),uint32_t(commands.size())})key.append(reinterpret_cast<const char*>(&count),sizeof(count));std::vector<Vertex> vertices;std::vector<InventoryDraw> draws;
    auto sprite=[&](const edguipresentation::Sprite& s){
        if((s.texture&&(s.texture>>24)!=11)||s.vertices.size()%3||s.vertices.size()>1000000)throw std::runtime_error("Invalid original source GUI sprite");
        const auto count=uint32_t(s.vertices.size());key.append(reinterpret_cast<const char*>(&count),sizeof(count));
        if(vertices.size()+s.vertices.size()>1000000)throw std::runtime_error("Excessive source GUI vertices");
        key.append(reinterpret_cast<const char*>(&s.texture),sizeof(s.texture));key.append(reinterpret_cast<const char*>(&s.color),sizeof(s.color));
        key.append(reinterpret_cast<const char*>(s.vertices.data()),s.vertices.size()*sizeof(edmesh::Vertex32));
        if(s.texture)guiTexture(s.texture);const auto first=unsigned(vertices.size());
        for(const auto& v:s.vertices){for(float value:{v.x,v.y,v.z,v.u,v.v})if(!std::isfinite(value))throw std::runtime_error("Non-finite source GUI vertex");vertices.push_back({v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v});}
        draws.push_back({first,unsigned(s.vertices.size()),s.texture?int(s.texture):-2,unpackTint(s.color)});
    };
    auto page=[&](const edguitext::Page& value){if(vertices.size()+value.glyphs.size()*6+(value.background?6:0)>1000000)throw std::runtime_error("Excessive source GUI vertices");textPageKey(key,value);appendTextPage(value,vertices,draws);};
    if(commands.empty()){for(const auto& value:pages)page(value);for(const auto& value:sprites)sprite(value);}
    else for(const auto& command:commands){key.push_back(char(command.value.index()));if(const auto* value=std::get_if<edguitext::Page>(&command.value))page(*value);else sprite(std::get<edguipresentation::Sprite>(command.value));}
    if(vertices.size()>1000000)throw std::runtime_error("Excessive source GUI vertices");
    if(key==sourceGuiKey)return;
    auto* buffer=vertices.empty()?nullptr:uploadBuffer(vertices.data(),unsigned(vertices.size()*sizeof(Vertex)));
    if(sourceGuiBuffer)SDL_ReleaseGPUBuffer(device,sourceGuiBuffer);
    sourceGuiBuffer=buffer;sourceGuiDraws=std::move(draws);sourceGuiKey=std::move(key);
}
