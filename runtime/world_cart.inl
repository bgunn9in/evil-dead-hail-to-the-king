// Included inside World after cart_adapter.h. The source natives only enqueue
// work; Root processes it after leaving the VM and owns the World/file hooks.
fs::path sourceCartDirectory;
std::unique_ptr<edcart::Adapter> sourceCart;

void configureSourceCart(const fs::path& directory) {
    const auto checked=savefile::detail::directory(directory);
    if(checked==sourceCartDirectory)return;
    if(sourceCart&&sourceCart->pending())throw jpc::Error("Cannot change the save directory during a Cart operation");
    sourceCart.reset();sourceCartDirectory=checked;
}
edcart::Adapter& sourceCartAdapter() {
    if(!sourceCart) {
        if(sourceCartDirectory.empty())throw jpc::Error("Source Cart preference directory has not been configured");
        sourceCart=std::make_unique<edcart::Adapter>(sourceCartDirectory);
    }
    return *sourceCart;
}
std::optional<edcart::State> sourceCartCheckpoint() const {
    if(!sourceCart)return {};return sourceCart->checkpoint();
}
void restoreSourceCartCheckpoint(const std::optional<edcart::State>& state) {
    if(!state){sourceCart.reset();return;}sourceCartAdapter().restore(*state);
}
int sourceCartAccess(unsigned slot=0) {
    if(slot>=8)throw jpc::Error("Source Cart slot is outside0..7");
    auto& cart=sourceCartAdapter();
    const auto state=cart.checkpoint();
    // MenSaveLoad.setMode calls begin(), then accessCard(0) before another
    // source frame. One real catalog scan satisfies both accepted requests.
    if(state.pending&&state.pending->operation==edcart::Operation::Access)return 1;
    return cart.queue(edcart::Operation::Access,slot);
}
jpc::Value sourceCartNative(const std::string& name,const std::vector<jpc::Value>& args) {
    const auto I=[](int value){return jpc::Value::integer(value);};
    if(name=="cartCountValidSlots")return I(game.profile.dreamcast?8:edcart::Adapter::countValidSlots());
    if(name=="cartGetCardFreeBlocks")return I(game.profile.dreamcast&&args.at(0).integer()!=0?-1:edcart::Adapter::freeBlocks());
    if(name=="cartIsCardPresent")return I(edcart::Adapter::present());
    if(name=="cartBeep")return {}; //406F50 ->40A7A0 RET on PC.
    if(name=="cartFormat")throw jpc::Error("Original card format is unavailable for modern save files");
    if(name=="cartRefresh")throw jpc::Error("Unused unregistered PC Cart refresh is unsupported");
    if(name=="cartEnd") {
        //406F30 closes the service; catalog bytes survive. Only work which has
        // not reached process() can be canceled from a native callback.
        if(sourceCart){auto state=sourceCart->checkpoint();state.pending.reset();state.completed.reset();sourceCart->restore(state);}return {};
    }
    if(name=="cartCountValidCardSlots")return I(game.profile.dreamcast?1:sourceCart?sourceCart->countValidCardSlots():8);
    if(name=="cartError")return I(sourceCart?sourceCart->error():0);
    if(name=="cartLoadGameDone"||name=="cartSaveGameDone"||name=="cartAccessCardDone"||name=="cartDeleteGameDone")return I(sourceCart?sourceCart->done():0);
    if(name=="cartBegin") {
        auto& cart=sourceCartAdapter();if(!cart.pending()&&!cart.done())sourceCartAccess();return {};
    }
    const int signedSlot=args.at(0).integer();
    if(signedSlot<0||signedSlot>=8)throw jpc::Error("Source Cart slot is outside0..7");
    const unsigned slot=unsigned(signedSlot);
    if(name=="cartCheckValidSlot")return I(sourceCart?sourceCart->checkValidSlot(slot):0);
    if(name=="cartAccessCard")return I(sourceCartAccess(slot));
    auto& cart=sourceCartAdapter();
    if(name=="cartLoadGame")return I(cart.queue(edcart::Operation::Load,slot));
    if(name=="cartSaveGame")return I(cart.queue(edcart::Operation::Save,slot));
    if(name=="cartDeleteGame") {
        // The only JDC caller is MenSaveLoad.itemUse overwrite preparation.
        // Modern atomic replacement needs no VMU erase; retain the old file until Save commits.
        if(game.profile.dreamcast)return I(cart.pending()?0:1);
        return I(cart.queue(edcart::Operation::Delete,slot));
    }
    throw jpc::Error("Unsupported source Cart native: "+name);
}
template<class Binder>void bindCartNatives(Binder& bind) {
    const std::pair<const char*,const char*> abi[]={
        {"cartCountValidSlots","()I"},{"cartCountValidCardSlots","()I"},{"cartFormat","()I"},{"cartError","()I"},
        {"cartSaveGameDone","(I)I"},{"cartLoadGameDone","(I)I"},{"cartSaveGame","(I)I"},{"cartBegin","()V"},
        {"cartLoadGame","(I)I"},{"cartDeleteGameDone","(I)I"},{"cartGetCardFreeBlocks","(I)I"},
        {"cartDeleteGame","(I)I"},{"cartBeep","()V"},{"cartRefresh","()V"},{"cartEnd","()V"},
        {"cartAccessCardDone","(I)I"},{"cartAccessCard","(I)I"},{"cartIsCardPresent","()I"},{"cartCheckValidSlot","(I)I"}};
    for(const auto& [name,signature]:abi) {
        game.package.methodIndex("ed/base/External",name,signature);
        const std::string key=name;
        bind(key,[this,key](auto&,auto,const auto& args){return sourceCartNative(key,args);});
    }
}
std::optional<std::string> sourceCartText(uint32_t id) const {
    if((id&0xffff0000u)!=0xffff0000u)return {};
    const unsigned mode=(id>>8)&255u,slot=id&255u;
    if(mode==1) {
        // Modern Dreamcast compatibility: one verified directory/card, eight modern records.
        // Native SH4 has eight controller positions and24 game records; no VMU is emulated.
        if(game.profile.dreamcast){if(slot>=8)throw jpc::Error("Source Cart card slot is outside0..7");return slot?std::string{}:edui::text("save files",sourceTexts().localized());}
        const auto& texts=sourceTexts();const auto scope=episode_portal_detail::sceneId(nativeScene().id);
        if(texts.localized()&&texts.contains(scope,edcart::Adapter::cardTextResource()))return texts.lookup(scope,edcart::Adapter::cardTextResource());
        const auto& strings=sourceMenuStrings();const auto found=strings.find(edcart::Adapter::cardTextResource());
        if(found==strings.end())throw jpc::Error("Original Cart card label is unavailable");return found->second;
    }
    if(mode!=2)return std::string{};
    if(slot>=8)throw jpc::Error("Source Cart text slot is outside0..7");
    if(!sourceCart||sourceCart->checkValidSlot(slot)!=2)return game.profile.dreamcast?edui::text("empty",sourceTexts().localized()):std::string{};
    const auto& preview=sourceCart->catalog(slot).preview;
    const uint64_t seconds=uint64_t(preview.seconds);
    const auto two=[](uint64_t value){return (value<10?"0":"")+std::to_string(value);};
    return preview.room+" "+two(seconds/3600)+":"+two(seconds/60%60)+":"+two(seconds%60);
}
