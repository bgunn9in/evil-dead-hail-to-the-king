#pragma once
namespace locale_save_test {
struct CatalogFixture {
    fs::path path;
    explicit CatalogFixture(const fs::path& source):path(fs::temp_directory_path()/
        ("evil-dead-locale-"+std::to_string(SDL_GetPerformanceCounter()))) {
        if(fs::exists(path))throw std::runtime_error("Locale fixture already exists");
        fs::create_directories(path/"gui");
        for(const char* name:{"text.bin","layout.bin"})fs::copy_file(source/"gui"/name,path/"gui"/name);
    }
    void translated(uint8_t first=0xcf)const {
        const auto source=edobjects::detail::read(path/"gui/text.bin",8*1024*1024);
        edobjects::detail::Reader input{source};input.magic("EGTEXT2\0");const auto count=input.get<uint32_t>();
        statebytes::Writer output;for(uint8_t c:std::string("EGTEXT3\0",8))output.put<uint8_t>(c);
        output.put<uint32_t>(1251);output.put(count);
        for(unsigned n=0;n<count;++n){const auto scope=input.get<uint32_t>(),id=input.get<uint32_t>(),size=input.get<uint32_t>();
            output.put(scope);output.put(id);output.put<uint32_t>(size+3);
            for(uint8_t c:{first,uint8_t(0xf0),uint8_t(32)})output.put(c);
            for(unsigned k=0;k<size;++k)output.put(input.get<uint8_t>());}
        if(input.cursor!=source.size())throw std::runtime_error("Locale fixture text ranges disagree");
        std::ofstream file(path/"gui/text-ru.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(output.bytes.data()),std::streamsize(output.bytes.size()));
        if(!file)throw std::runtime_error("Cannot write locale fixture");
    }
    ~CatalogFixture(){std::error_code error;
        for(const char* name:{"text-ru.bin","text.bin","layout.bin"})fs::remove(path/"gui"/name,error);
        fs::remove(path/"gui",error);fs::remove(path,error);}
};
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Locale save: ")+message);}
inline void check(World& world,const fs::path& root) {
    CatalogFixture fixture(root);const auto original=world.saveStateBytes();auto originalCatalog=std::move(world.textCatalog);
    world.textCatalog=std::make_unique<edguitext::Catalog>(fixture.path);
    edguitext::restartTextCursors(world.sourceGui,world.game.vm);world.sampleSourceTexts();
    world.applyInventoryItem();world.sampleSourceTexts();world.sampleSourceTexts();
    require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset>0,"fixture did not reach a later English page");
    world.applyInventoryItem();require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).flags&0x200u,"pending source page was not queued");
    const auto english=world.saveStateBytes(),vm=world.game.vm.snapshot(),actors=world.actors->snapshot();
    fixture.translated();world.textCatalog=std::make_unique<edguitext::Catalog>(fixture.path);
    require(world.sourceTexts().textRevision()!=0,"translated revision is missing");
    world.restoreStateBytes(english);
    require(world.inventoryView.reading&&SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset==0,
        "locale change retained an English byte cursor");
    require(!(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).flags&0x200u),"locale change retained a pending page advance");
    require(world.game.vm.snapshot()==vm&&world.actors->snapshot()==actors,"locale migration changed gameplay state");
    const auto russian=world.saveStateBytes();world.restoreStateBytes(russian);require(world.saveStateBytes()==russian,"same-revision load differs");
    World cold(root,nullptr,presentMovieHeadless,true);cold.textCatalog=std::make_unique<edguitext::Catalog>(fixture.path);
    cold.restoreStateBytes(russian);require(cold.saveStateBytes()==russian,"cold localized load differs");
    world.applyInventoryItem();world.sampleSourceTexts();world.sampleSourceTexts();const auto revised=world.saveStateBytes();
    fixture.translated(0xd0);world.textCatalog=std::make_unique<edguitext::Catalog>(fixture.path);world.restoreStateBytes(revised);
    require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset==0,"new translation revision retained an old byte cursor");
    auto legacy=english;statebytes::Reader header{legacy};require(header.text()=="EDPLAY1","fixture header differs");
    const auto versionAt=header.cursor;for(unsigned n=0;n<4;++n)legacy.at(versionAt+n)=uint8_t(18u>>(n*8));legacy.resize(legacy.size()-8);
    world.restoreStateBytes(legacy);require(SourceGui::at(world.sourceGui.items,world.documentReaderItem()).textOffset==0,"legacy English cursor was not migrated");
    fs::remove(fixture.path/"gui/text-ru.bin");world.textCatalog=std::make_unique<edguitext::Catalog>(fixture.path);
    world.restoreStateBytes(legacy);require(world.saveStateBytes()==english,"legacy English progress changed");
    world.textCatalog=std::move(originalCatalog);world.restoreStateBytes(original);
    require(world.saveStateBytes()==original,"locale fixture did not restore its original World");
    std::cout<<"Locale saves: same-revision/cold exact, English18->Russian cursor restart, English18->English progress retained\n";
}
}
