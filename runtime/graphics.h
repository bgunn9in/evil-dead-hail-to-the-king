#pragma once
#include "inventory_ui.h"
#include "video_settings.h"
#include "source_font.h"
#include "ui_strings.h"
#include "movie_player.h"
#include "gameplay_actors.h"
#include "object_mesh.h"
#include "source_vfx.h"
#include "source_screen_effects.h"
#include "source_gui_presentation.h"
#include "source_gui_draw.h"
#include "dc_scene_profile.h"
#include "source_native_visual.h"
#include "source_native_order.h"
#include "lossless_image.h"
#include <sstream>
#include <functional>
struct ObjectDraw {
    uint32_t model=0,texture=0;
    edobjects::Transform transform;
    int16_t type=0;
    uint32_t tint=0xffffffff;
    uint8_t mode=255;
};
struct ActorDrawTint {uint32_t color=0xffffffff;uint8_t mode=255;};
struct InventoryIcon {
    unsigned itemID=0,texture=0,shape=0,flags=0;
    float uv[4]{},aspect=1;
    std::string name,description;
};
struct InventoryDraw {
    unsigned first=0,count=0;
    int texture=-1;
    std::array<float,4> color{1,1,1,1};
};
struct SceneResources {
    SDL_GPUDevice* device;
    fs::path assets;
    SDL_GPUBuffer *mesh=nullptr,*idle=nullptr,*walk=nullptr;
    SDL_GPUTexture* ash=nullptr;
    std::vector<SDL_GPUTexture*> backgrounds,backgroundDepth;
    SDL_GPUTexture *animatedBackground=nullptr,*animatedDepth=nullptr;
    unsigned animatedTrack=~0u,animatedFrame=~0u;
    unsigned foregroundTrack=~0u,foregroundFrame=~0u;
    unsigned backgroundWidth=640,backgroundHeight=384;
    float backgroundFactor=1;
    bool nativeBackground=false;
    struct DcMask {SDL_GPUTexture* coverage=nullptr;SDL_GPUBuffer* vertices=nullptr;float depth=1;};
    std::vector<std::vector<DcMask>> dcMasks;
    std::vector<DcMask> animatedMasks;
    explicit SceneResources(SDL_GPUDevice* gpu):device(gpu) {}
    SceneResources(const SceneResources&)=delete;
    ~SceneResources() {
        for (auto* buffer:{mesh,idle,walk}) if (buffer) SDL_ReleaseGPUBuffer(device,buffer);
        for (auto* image:backgrounds) SDL_ReleaseGPUTexture(device,image);
        for (auto* image:backgroundDepth) SDL_ReleaseGPUTexture(device,image);
        if (ash) SDL_ReleaseGPUTexture(device,ash);
        for(auto& layers:dcMasks)for(auto& layer:layers){
            if(layer.coverage)SDL_ReleaseGPUTexture(device,layer.coverage);
            if(layer.vertices)SDL_ReleaseGPUBuffer(device,layer.vertices);
        }
        for(auto& layer:animatedMasks){
            if(layer.coverage)SDL_ReleaseGPUTexture(device,layer.coverage);
            if(layer.vertices)SDL_ReleaseGPUBuffer(device,layer.vertices);
        }
        if(animatedBackground)SDL_ReleaseGPUTexture(device,animatedBackground);
        if(animatedDepth)SDL_ReleaseGPUTexture(device,animatedDepth);
    }
};
struct ActorResources {
    SDL_GPUDevice* device=nullptr;
    SDL_GPUBuffer* vertices=nullptr;
    SDL_GPUTransferBuffer* transfer=nullptr;
    unsigned capacity=0;
    explicit ActorResources(SDL_GPUDevice* gpu):device(gpu){}
    ~ActorResources(){if(vertices)SDL_ReleaseGPUBuffer(device,vertices);if(transfer)SDL_ReleaseGPUTransferBuffer(device,transfer);}
};
struct Graphics {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUSampler *sampler = nullptr, *modelSampler = nullptr, *depthSampler = nullptr;
    SDL_GPUGraphicsPipeline *backgroundPipeline = nullptr, *modelPipeline = nullptr, *hudPipeline=nullptr,*iconPipeline=nullptr,
        *objectPipeline=nullptr,*objectOpaquePipeline=nullptr,*particlePipeline=nullptr,*particleDepthPipeline=nullptr,*particleAlphaPipeline=nullptr;
    SDL_GPUBuffer *quad = nullptr,*hudBuffer=nullptr,*inventoryBuffer=nullptr;
    SDL_GPUTexture *target = nullptr, *depth = nullptr,*font=nullptr,*uiWhite=nullptr;
    SDL_GPUTexture *postTarget=nullptr,*lastPresentedTarget=nullptr;
    SDL_GPUGraphicsPipeline* postPipeline=nullptr;
    bool postScanlines=false,postFilm=false;
    bool commonSceneRaster=true;
    SDL_GPUFilter commonSceneFilter=SDL_GPU_FILTER_LINEAR;
    SDL_GPUTexture *commonSceneColor=nullptr,*commonSceneDepth=nullptr;
    struct PostTuning {float scanStrength=.16f,lineCount=240,grainStrength=.04f,grainSize=1,vignette=.065f,flicker=.012f;} postTuning;
    unsigned fontWidth=0,fontHeight=0,fontColumns=0,fontAtlasWidth=256,fontAtlasHeight=256,fontCodepage=0,hudVertices=0;
    std::string fontCharset,hudText;
    unsigned targetWidth = 0, targetHeight = 0;
    std::unique_ptr<SceneResources> resources;
    std::vector<SDL_GPUTexture*> inventoryTextures;
    std::vector<InventoryIcon> inventoryIcons;
    std::vector<InventoryDraw> inventoryDraws;
    std::string inventoryKey;
    bool inventoryLoaded=false;
    SDL_GPUBuffer* sourceGuiBuffer=nullptr;
    std::vector<InventoryDraw> sourceGuiDraws;
    std::string sourceGuiKey;
    SDL_GPUTexture* movieTexture=nullptr;
    SDL_GPUTransferBuffer* movieTransfer=nullptr;
    unsigned movieWidth=0,movieHeight=0,movieFrame=~0u;
    std::uint64_t movieGeneration=0;
    std::map<edactors::Key,std::unique_ptr<ActorResources>> actorBuffers;
    std::map<uint32_t,SDL_GPUTexture*> actorTextures;
    fs::path sourceAssets;
    std::map<uint32_t,edobjects::ObjectMesh> objectModels;
    std::map<uint32_t,SDL_GPUTexture*> objectTextures;
    UiLanguage uiLanguage_=UiLanguage::Russian;
    SDL_GPUTexture* particleTexture=nullptr;
    std::unique_ptr<ActorResources> objectBuffer,particleBuffer;
    bool profileScopeActive=false;
    SDL_GPUGraphicsPipeline* dcMaskPipeline=nullptr;
    std::array<SDL_GPUGraphicsPipeline*,3> nativeObjectPipelines{},nativeActorPipelines{};
    SDL_GPUGraphicsPipeline* nativeActorOpaquePipeline=nullptr;
    SDL_GPUGraphicsPipeline* nativeShadowPipeline=nullptr;

    struct ProfileState {
        std::unique_ptr<SceneResources> resources;
        std::map<edactors::Key,std::unique_ptr<ActorResources>> actorBuffers;
        std::map<uint32_t,SDL_GPUTexture*> actorTextures,objectTextures;
        std::map<uint32_t,edobjects::ObjectMesh> objectModels;
        std::unique_ptr<ActorResources> objectBuffer,particleBuffer;
        fs::path sourceAssets;
        SDL_GPUTexture *particleTexture=nullptr,*font=nullptr,*movieTexture=nullptr;
        SDL_GPUTransferBuffer* movieTransfer=nullptr;
        SDL_GPUBuffer *hudBuffer=nullptr,*inventoryBuffer=nullptr;
        unsigned fontWidth=0,fontHeight=0,fontColumns=0,fontAtlasWidth=256,fontAtlasHeight=256,fontCodepage=0,hudVertices=0,movieWidth=0,movieHeight=0,movieFrame=~0u;
        uint64_t movieGeneration=0;
        std::string fontCharset,hudText,inventoryKey;
        std::vector<SDL_GPUTexture*> inventoryTextures;
        std::vector<InventoryIcon> inventoryIcons;
        std::vector<InventoryDraw> inventoryDraws;
        bool inventoryLoaded=false;
        SDL_GPUBuffer* sourceGuiBuffer=nullptr;
        std::vector<InventoryDraw> sourceGuiDraws;
        std::string sourceGuiKey;
        void swap(Graphics& g) noexcept {
            using std::swap;
            swap(sourceGuiBuffer,g.sourceGuiBuffer);swap(sourceGuiDraws,g.sourceGuiDraws);swap(sourceGuiKey,g.sourceGuiKey);
            swap(resources,g.resources);swap(actorBuffers,g.actorBuffers);swap(actorTextures,g.actorTextures);
            swap(objectTextures,g.objectTextures);swap(objectModels,g.objectModels);swap(objectBuffer,g.objectBuffer);
            swap(particleBuffer,g.particleBuffer);swap(sourceAssets,g.sourceAssets);swap(particleTexture,g.particleTexture);
            swap(font,g.font);swap(movieTexture,g.movieTexture);swap(movieTransfer,g.movieTransfer);
            swap(hudBuffer,g.hudBuffer);swap(inventoryBuffer,g.inventoryBuffer);
            swap(fontWidth,g.fontWidth);swap(fontHeight,g.fontHeight);swap(fontColumns,g.fontColumns);swap(fontAtlasWidth,g.fontAtlasWidth);swap(fontAtlasHeight,g.fontAtlasHeight);swap(fontCodepage,g.fontCodepage);swap(hudVertices,g.hudVertices);
            swap(movieWidth,g.movieWidth);swap(movieHeight,g.movieHeight);swap(movieFrame,g.movieFrame);swap(movieGeneration,g.movieGeneration);
            swap(fontCharset,g.fontCharset);swap(hudText,g.hudText);swap(inventoryKey,g.inventoryKey);
            swap(inventoryTextures,g.inventoryTextures);swap(inventoryIcons,g.inventoryIcons);swap(inventoryDraws,g.inventoryDraws);
            swap(inventoryLoaded,g.inventoryLoaded);
        }
        void release(SDL_GPUDevice* device) noexcept {
            resources.reset();actorBuffers.clear();objectBuffer.reset();particleBuffer.reset();
            for(auto* images:{&actorTextures,&objectTextures}){for(const auto& [id,image]:*images){(void)id;SDL_ReleaseGPUTexture(device,image);}images->clear();}
            for(auto* image:inventoryTextures)SDL_ReleaseGPUTexture(device,image);inventoryTextures.clear();
            for(auto* image:{particleTexture,font,movieTexture})if(image)SDL_ReleaseGPUTexture(device,image);
            particleTexture=font=movieTexture=nullptr;
            if(movieTransfer)SDL_ReleaseGPUTransferBuffer(device,movieTransfer);movieTransfer=nullptr;
            for(auto* buffer:{hudBuffer,inventoryBuffer,sourceGuiBuffer})if(buffer)SDL_ReleaseGPUBuffer(device,buffer);
            hudBuffer=inventoryBuffer=sourceGuiBuffer=nullptr;
        }
    };
    struct ProfileScope {
        Graphics& graphics;ProfileState previous;bool finished=false;
        explicit ProfileScope(Graphics& value):graphics(value) {
            if(!graphics.device||graphics.profileScopeActive)throw std::runtime_error("Invalid or nested GPU profile transaction");
            previous.swap(graphics);graphics.profileScopeActive=true;
        }
        ~ProfileScope() {
            if(finished)return;
            SDL_WaitForGPUIdle(graphics.device);
            previous.swap(graphics);previous.release(graphics.device);graphics.profileScopeActive=false;
        }
        void commit() {
            if(finished)throw std::runtime_error("GPU profile transaction already completed");
            checked(SDL_WaitForGPUIdle(graphics.device),"Wait before committing GPU profile");
            previous.release(graphics.device);graphics.profileScopeActive=false;finished=true;
        }
        ProfileScope(const ProfileScope&)=delete;
        ProfileScope& operator=(const ProfileScope&)=delete;
    };

    ~Graphics() {
        if (device) {
            SDL_WaitForGPUIdle(device);
            resources.reset();
            actorBuffers.clear();for(auto& [type,image]:actorTextures){(void)type;SDL_ReleaseGPUTexture(device,image);}
            objectBuffer.reset();particleBuffer.reset();
            for(auto& [id,image]:objectTextures){(void)id;SDL_ReleaseGPUTexture(device,image);}
            if(particleTexture)SDL_ReleaseGPUTexture(device,particleTexture);
            if (quad) SDL_ReleaseGPUBuffer(device,quad);
            if (hudBuffer) SDL_ReleaseGPUBuffer(device,hudBuffer);
            if (inventoryBuffer) SDL_ReleaseGPUBuffer(device,inventoryBuffer);
            if(sourceGuiBuffer)SDL_ReleaseGPUBuffer(device,sourceGuiBuffer);
            if (movieTransfer) SDL_ReleaseGPUTransferBuffer(device,movieTransfer);
            if (movieTexture) SDL_ReleaseGPUTexture(device,movieTexture);
            for (auto* image:inventoryTextures) SDL_ReleaseGPUTexture(device,image);
            for (auto* image : {target, depth,font,uiWhite,postTarget,commonSceneColor,commonSceneDepth}) if (image) SDL_ReleaseGPUTexture(device, image);
            if(postPipeline)SDL_ReleaseGPUGraphicsPipeline(device,postPipeline);
            if (sampler) SDL_ReleaseGPUSampler(device, sampler);
            if (modelSampler) SDL_ReleaseGPUSampler(device, modelSampler);
            if (depthSampler) SDL_ReleaseGPUSampler(device, depthSampler);
            if (backgroundPipeline) SDL_ReleaseGPUGraphicsPipeline(device, backgroundPipeline);
            if(dcMaskPipeline)SDL_ReleaseGPUGraphicsPipeline(device,dcMaskPipeline);
            for(auto* pipeline:nativeObjectPipelines)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(nativeShadowPipeline)SDL_ReleaseGPUGraphicsPipeline(device,nativeShadowPipeline);
            for(auto* pipeline:nativeActorPipelines)if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(nativeActorOpaquePipeline)SDL_ReleaseGPUGraphicsPipeline(device,nativeActorOpaquePipeline);
            if (modelPipeline) SDL_ReleaseGPUGraphicsPipeline(device, modelPipeline);
            if (hudPipeline) SDL_ReleaseGPUGraphicsPipeline(device,hudPipeline);
            if (iconPipeline) SDL_ReleaseGPUGraphicsPipeline(device,iconPipeline);
            if (objectPipeline) SDL_ReleaseGPUGraphicsPipeline(device,objectPipeline);
            if (objectOpaquePipeline) SDL_ReleaseGPUGraphicsPipeline(device,objectOpaquePipeline);
            if (particlePipeline) SDL_ReleaseGPUGraphicsPipeline(device,particlePipeline);
            if (particleDepthPipeline) SDL_ReleaseGPUGraphicsPipeline(device,particleDepthPipeline);
            if (particleAlphaPipeline) SDL_ReleaseGPUGraphicsPipeline(device,particleAlphaPipeline);
            if (window) SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
        if (window) SDL_DestroyWindow(window);
    }
    SDL_GPUTexture* texture(unsigned width, unsigned height, SDL_GPUTextureFormat format,
                            SDL_GPUTextureUsageFlags usage) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = format; info.usage = usage;
        info.width = width; info.height = height; info.layer_count_or_depth = 1; info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        return checked(SDL_CreateGPUTexture(device, &info), "Create texture");
    }
    void resizeTargets(unsigned width, unsigned height) {
        if (width == targetWidth && height == targetHeight) return;
        checked(SDL_WaitForGPUIdle(device), "Wait before resize");
        auto* newTarget = texture(width, height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                  SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        SDL_GPUTexture* newDepth = nullptr;
        try { newDepth = texture(width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET); }
        catch (...) { SDL_ReleaseGPUTexture(device, newTarget); throw; }
        if (target) SDL_ReleaseGPUTexture(device, target);
        if (depth) SDL_ReleaseGPUTexture(device, depth);
        if(postTarget)SDL_ReleaseGPUTexture(device,postTarget);
        postTarget=nullptr;lastPresentedTarget=nullptr;
        target = newTarget; depth = newDepth; targetWidth = width; targetHeight = height;
        const auto viewport = sceneViewport(width, height);
        SDL_Log("Native render %ux%u; 4:3 scene %ux%u at (%u,%u)", width, height,
                viewport.width, viewport.height, viewport.x, viewport.y);
    }
    SDL_GPUBuffer* uploadBuffer(const void* bytes, unsigned size) {
        SDL_GPUBufferCreateInfo info{}; info.usage = SDL_GPU_BUFFERUSAGE_VERTEX; info.size = size;
        auto* buffer = checked(SDL_CreateGPUBuffer(device, &info), "Create vertex buffer");
        SDL_GPUTransferBuffer* transfer=nullptr;
        SDL_GPUCommandBuffer* command=nullptr;
        try {
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transferInfo.size = size;
        transfer = checked(SDL_CreateGPUTransferBuffer(device, &transferInfo), "Create upload buffer");
        std::memcpy(checked(SDL_MapGPUTransferBuffer(device, transfer, false), "Map upload"), bytes, size);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        command = checked(SDL_AcquireGPUCommandBuffer(device), "Acquire upload command");
        auto* pass = checked(SDL_BeginGPUCopyPass(command), "Begin upload");
        SDL_GPUTransferBufferLocation source{transfer, 0}; SDL_GPUBufferRegion destination{buffer, 0, size};
        SDL_UploadToGPUBuffer(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        auto* submitted=command; command=nullptr;
        checked(SDL_SubmitGPUCommandBuffer(submitted), "Submit upload");
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return buffer;
        } catch (...) {
            if (command) SDL_CancelGPUCommandBuffer(command);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device,transfer);
            SDL_ReleaseGPUBuffer(device,buffer);
            throw;
        }
    }
    SDL_GPUTexture* uploadTexture(const void* sourcePixels, unsigned width, unsigned height,
                                  unsigned sourcePitch, SDL_GPUTextureFormat format) {
        auto* result = texture(width, height, format, SDL_GPU_TEXTUREUSAGE_SAMPLER);
        SDL_GPUTransferBuffer* transfer=nullptr;
        SDL_GPUCommandBuffer* command=nullptr;
        try {
        const unsigned pixelsPerRow = (width + 63) & ~63u;
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transferInfo.size = pixelsPerRow * height * 4;
        transfer = checked(SDL_CreateGPUTransferBuffer(device, &transferInfo), "Create texture upload");
        auto* pixels = static_cast<Uint8*>(checked(SDL_MapGPUTransferBuffer(device, transfer, false), "Map texture"));
        for (unsigned row = 0; row < height; ++row)
            std::memcpy(pixels + row * pixelsPerRow * 4, static_cast<const Uint8*>(sourcePixels) + row * sourcePitch, width * 4);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        command = checked(SDL_AcquireGPUCommandBuffer(device), "Acquire texture upload");
        auto* pass = checked(SDL_BeginGPUCopyPass(command), "Begin texture upload");
        SDL_GPUTextureTransferInfo source{transfer, 0, pixelsPerRow, height};
        SDL_GPUTextureRegion destination{}; destination.texture = result;
        destination.w = width; destination.h = height; destination.d = 1;
        SDL_UploadToGPUTexture(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        auto* submitted=command; command=nullptr;
        checked(SDL_SubmitGPUCommandBuffer(submitted), "Submit texture upload");
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return result;
        } catch (...) {
            if (command) SDL_CancelGPUCommandBuffer(command);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device,transfer);
            SDL_ReleaseGPUTexture(device,result);
            throw;
        }
    }
    SDL_GPUTexture* loadTexture(const fs::path& path, unsigned expectedWidth=0,unsigned expectedHeight=0) {
        const auto rgba=edimage::load(edimage::colorPath(path),expectedWidth,expectedHeight);
        return uploadTexture(rgba->pixels,rgba->w,rgba->h,rgba->pitch,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    }
    SDL_GPUTexture* loadDepth(const fs::path& path,unsigned width=640,unsigned height=384) {
        const auto data = edimage::packedPixels(path,width,height,4);
        if (data.size() != width * height * sizeof(float)) throw std::runtime_error("Invalid depth map size");
        for (size_t offset = 0; offset < data.size(); offset += sizeof(float)) {
            float value; std::memcpy(&value, data.data() + offset, sizeof(value));
            if (!std::isfinite(value) || value < 0 || value > 1) throw std::runtime_error("Invalid depth value");
        }
        return uploadTexture(data.data(), width, height, width * 4, SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
    }
    SDL_GPUShader* shader(const fs::path& path, SDL_GPUShaderStage stage, const char* entry, unsigned samplers,unsigned uniforms) {
        const auto code = readFile(path);
        SDL_GPUShaderCreateInfo info{};
        info.code = code.data(); info.code_size = code.size(); info.entrypoint = entry;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV; info.stage = stage;
        info.num_uniform_buffers = uniforms;
        info.num_samplers = samplers;
        return checked(SDL_CreateGPUShader(device, &info), "Create shader");
    }
    void setPostEffects(bool scanlines,bool film) {postScanlines=scanlines;postFilm=film;}
    void setPostEffects(const VideoSettings& settings) {
        if(!settings.valid())throw std::runtime_error("Invalid display effects settings");
        postTuning={settings.scanStrength,settings.lineCount,settings.grainStrength,settings.grainSize,settings.vignette,settings.flicker};
        setPostEffects(settings.scanlines,settings.film);
    }
    void ensureCommonSceneTargets() {
        if(commonSceneColor)return;
        auto* color=texture(640,480,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
        SDL_GPUTexture* z=nullptr;try{z=texture(640,480,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);}
        catch(...){SDL_ReleaseGPUTexture(device,color);throw;}
        commonSceneColor=color;commonSceneDepth=z;
    }
    void compositeCommonScene(SDL_GPUCommandBuffer* command,unsigned width,unsigned height) {
        SDL_GPUColorTargetInfo clear{};clear.texture=target;clear.load_op=SDL_GPU_LOADOP_CLEAR;clear.store_op=SDL_GPU_STOREOP_STORE;clear.clear_color={0,0,0,1};
        auto* pass=checked(SDL_BeginGPURenderPass(command,&clear,1,nullptr),"Clear common-raster display");SDL_EndGPURenderPass(pass);
        const auto area=sceneViewport(width,height);SDL_GPUBlitInfo blit{};blit.source.texture=commonSceneColor;blit.source.w=640;blit.source.h=480;
        blit.destination.texture=target;blit.destination.x=area.x;blit.destination.y=area.y;blit.destination.w=area.width;blit.destination.h=area.height;
        blit.load_op=SDL_GPU_LOADOP_LOAD;blit.filter=commonSceneFilter;SDL_BlitGPUTexture(command,&blit);
    }
    void presentFrame(SDL_GPUCommandBuffer* command,SDL_GPUTexture* swapchain,unsigned width,unsigned height,
                      const SDL_Rect& content,double seconds) {
        auto* output=target;
        if(postScanlines||postFilm) {
            if(!postTarget)postTarget=texture(width,height,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
            SDL_GPUColorTargetInfo color{};color.texture=postTarget;color.load_op=SDL_GPU_LOADOP_DONT_CARE;color.store_op=SDL_GPU_STOREOP_STORE;
            auto* pass=checked(SDL_BeginGPURenderPass(command,&color,1,nullptr),"Begin display effects");
            SDL_BindGPUGraphicsPipeline(pass,postPipeline);
            const SDL_GPUViewport viewport{0,0,float(width),float(height),0,1};SDL_SetGPUViewport(pass,&viewport);
            const SDL_Rect scissor{0,0,int(width),int(height)};SDL_SetGPUScissor(pass,&scissor);
            SDL_GPUBufferBinding vertices{quad,0};SDL_BindGPUVertexBuffers(pass,0,&vertices,1);
            SDL_GPUTextureSamplerBinding image{target,depthSampler};SDL_BindGPUFragmentSamplers(pass,0,&image,1);
            struct VertexUniform {std::array<float,16> mvp;float light[4];} vertex{};
            vertex.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};vertex.light[3]=1;
            struct PostUniform {float content[4],scanGrain[4],filmTime[4];} pixel{
                {float(content.x),float(content.y),float(content.w),float(content.h)},
                {postScanlines?postTuning.scanStrength:0.f,postTuning.lineCount,postFilm?postTuning.grainStrength:0.f,postTuning.grainSize},
                {postFilm?postTuning.vignette:0.f,postFilm?postTuning.flicker:0.f,float(std::fmod(seconds,3600.0)),0}};
            static_assert(sizeof(PostUniform)==48);
            SDL_PushGPUVertexUniformData(command,0,&vertex,sizeof(vertex));
            SDL_PushGPUFragmentUniformData(command,0,&pixel,sizeof(pixel));SDL_DrawGPUPrimitives(pass,6,1,0,0);
            SDL_EndGPURenderPass(pass);output=postTarget;
        }
        SDL_GPUBlitInfo blit{};blit.source.texture=output;blit.source.w=width;blit.source.h=height;
        blit.destination.texture=swapchain;blit.destination.w=width;blit.destination.h=height;
        blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_NEAREST;SDL_BlitGPUTexture(command,&blit);
        lastPresentedTarget=output;
    }
    using DisplayResolution=VideoResolution;
    DisplayResolution desktopResolution() const {
        const auto id=SDL_GetDisplayForWindow(window);checked(id!=0,"Locate window display");
        const auto* mode=checked(SDL_GetDesktopDisplayMode(id),"Query desktop mode");
        return {unsigned(std::lround(mode->w*mode->pixel_density)),unsigned(std::lround(mode->h*mode->pixel_density))};
    }
    std::vector<DisplayResolution> displayResolutions() const {
        const auto id=SDL_GetDisplayForWindow(window);checked(id!=0,"Locate window display");int count=0;
        auto** modes=checked(SDL_GetFullscreenDisplayModes(id,&count),"Enumerate display modes");
        std::vector<DisplayResolution> result;
        for(int i=0;i<count;++i)if(modes[i]->w>0&&modes[i]->h>0&&std::isfinite(modes[i]->pixel_density)&&modes[i]->pixel_density>0)
            result.push_back({unsigned(std::lround(modes[i]->w*modes[i]->pixel_density)),unsigned(std::lround(modes[i]->h*modes[i]->pixel_density))});
        SDL_free(modes);result.push_back(desktopResolution());
        std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tie(a.width,a.height)<std::tie(b.width,b.height);});
        result.erase(std::unique(result.begin(),result.end(),[](const auto&a,const auto&b){return a.width==b.width&&a.height==b.height;}),result.end());
        return result;
    }
    void refreshSwapchain() {
        checked(SDL_WaitForGPUIdle(device),"Finish frames before display change");
        // Keep the window claimed while a minimized surface defers recreation.
        checked(SDL_SetGPUSwapchainParameters(device,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,SDL_GPU_PRESENTMODE_VSYNC),"Set display vsync");
    }
    void applyDisplayMode(unsigned width,unsigned height,bool fullscreen,std::function<void()> commit={}) {
        if((!width)!=(!height)||width>16384||height>16384||(width&&(width<320||height<240)))
            throw std::runtime_error("Invalid display resolution");
        const bool oldFullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0;
        int oldWidth=0,oldHeight=0;checked(SDL_GetWindowSize(window,&oldWidth,&oldHeight),"Remember window size");bool rememberedWindowed=!oldFullscreen;
        SDL_DisplayMode oldMode{};const auto* previous=SDL_GetWindowFullscreenMode(window);const bool oldExclusive=previous!=nullptr;if(previous)oldMode=*previous;
        SDL_DisplayMode selected{};bool exclusive=false;
        if(width) {
            const auto id=SDL_GetDisplayForWindow(window);checked(id!=0,"Locate selected display");
            const auto* desktop=checked(SDL_GetDesktopDisplayMode(id),"Query selected desktop");
            int count=0;auto** modes=checked(SDL_GetFullscreenDisplayModes(id,&count),"Enumerate selected modes");
            float best=std::numeric_limits<float>::infinity();
            for(int i=0;i<count;++i)if(unsigned(std::lround(modes[i]->w*modes[i]->pixel_density))==width&&
                unsigned(std::lround(modes[i]->h*modes[i]->pixel_density))==height) {
                const float score=std::abs(modes[i]->refresh_rate-desktop->refresh_rate)+(modes[i]->format==desktop->format?0.f:1000.f);
                if(score<best){selected=*modes[i];best=score;exclusive=true;}
            }
            SDL_free(modes);if(!exclusive)throw std::runtime_error("Selected resolution is unavailable on this display");
        }
        try {
            checked(SDL_SetWindowFullscreen(window,false),"Leave fullscreen before changing display");
            checked(SDL_SyncWindow(window),"Settle window before display change");
            if(oldFullscreen){checked(SDL_GetWindowSize(window,&oldWidth,&oldHeight),"Remember restored windowed size");rememberedWindowed=true;}
            checked(SDL_SetWindowFullscreenMode(window,fullscreen&&exclusive?&selected:nullptr),"Select display mode");
            if(!fullscreen&&width) {
                const float density=SDL_GetWindowPixelDensity(window);
                if(!std::isfinite(density)||density<=0)throw std::runtime_error("Invalid window pixel density");
                checked(SDL_SetWindowSize(window,int(std::lround(width/density)),int(std::lround(height/density))),"Set window resolution");
            }
            checked(SDL_SetWindowFullscreen(window,fullscreen),"Apply display fullscreen");checked(SDL_SyncWindow(window),"Settle display mode");SDL_PumpEvents();
            int actualWidth=0,actualHeight=0;checked(SDL_GetWindowSizeInPixels(window,&actualWidth,&actualHeight),"Verify display resolution");
            if(width&&(actualWidth!=int(width)||actualHeight!=int(height)))throw std::runtime_error("Requested display pixel size was not applied");
            refreshSwapchain();
            // Keep rollback active until the caller saves its new preference.
            if(commit)commit();
        } catch(...) {
            SDL_SetWindowFullscreen(window,false);SDL_SetWindowFullscreenMode(window,oldExclusive?&oldMode:nullptr);
            if(rememberedWindowed)SDL_SetWindowSize(window,oldWidth,oldHeight);SDL_SetWindowFullscreen(window,oldFullscreen);SDL_SyncWindow(window);SDL_PumpEvents();
            try{refreshSwapchain();}catch(...){}throw;
        }
    }
    void setFullscreen(bool enabled) {
        SDL_PumpEvents();
        checked(SDL_SetWindowFullscreenMode(window,nullptr),"Set desktop fullscreen mode");
        checked(SDL_SetWindowFullscreen(window,enabled),"Set fullscreen");
        checked(SDL_SyncWindow(window),"Settle fullscreen");
        SDL_PumpEvents();
        refreshSwapchain();
    }
    void initialize(const Scene& scene, const fs::path& assets, int width, int height) {
        if (!width || !height) {
            SDL_Rect bounds{};
            if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds)) {
                const float scale = std::min({1.0f, std::max(320, bounds.w - 64) / 1280.0f, std::max(240, bounds.h - 80) / 960.0f});
                width = int(1280 * scale); height = int(960 * scale);
            } else { width = 1280; height = 960; }
        }
        window = checked(SDL_CreateWindow("Evil Dead — prototype", width, height,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY), "Create window");
        checked(SDL_SetWindowMinimumSize(window, 160, 120), "Set minimum window size");
        checked(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER,"vulkan",SDL_HINT_OVERRIDE),"Select Vulkan renderer");
        device = checked(SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,true,"vulkan"),"Create Vulkan GPU device");
        checked(SDL_ClaimWindowForGPUDevice(device, window), "Claim window");
        checked(SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC), "Set vsync");
        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR; samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
        samplerInfo.address_mode_u = samplerInfo.address_mode_v = samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler = checked(SDL_CreateGPUSampler(device, &samplerInfo), "Create sampler");
        samplerInfo.address_mode_u = samplerInfo.address_mode_v = samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        modelSampler = checked(SDL_CreateGPUSampler(device, &samplerInfo), "Create model sampler");
        samplerInfo.address_mode_u = samplerInfo.address_mode_v = samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.min_filter = samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        depthSampler = checked(SDL_CreateGPUSampler(device, &samplerInfo), "Create depth sampler");
        const fs::path shaders = fs::u8path(SDL_GetBasePath()) / "shaders";
        auto* vertex = shader(shaders / "scene.vert.spv", SDL_GPU_SHADERSTAGE_VERTEX, "VSMain", 0,1);
        auto* fragment = shader(shaders / "scene.frag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, "PSMain", 1,1);
        auto* backgroundFragment = shader(shaders / "scene.bgfrag.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, "PSBackground", 2,1);
        auto* dcMaskFragment=shader(shaders/"scene.maskfrag.spv",SDL_GPU_SHADERSTAGE_FRAGMENT,"PSDCMask",2,1);
        auto* hudFragment=shader(shaders/"scene.hudfrag.spv",SDL_GPU_SHADERSTAGE_FRAGMENT,"PSHud",1,1);
        auto* particleFragment=shader(shaders/"scene.particlefrag.spv",SDL_GPU_SHADERSTAGE_FRAGMENT,"PSParticle",1,1);
        auto* opaqueFragment=shader(shaders/"scene.opaquefrag.spv",SDL_GPU_SHADERSTAGE_FRAGMENT,"PSOpaque",1,1);
        auto* postFragment=shader(shaders/"scene.postfrag.spv",SDL_GPU_SHADERSTAGE_FRAGMENT,"PSPost",1,1);
        SDL_GPUVertexBufferDescription bufferDescription{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        SDL_GPUVertexAttribute attributes[] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12}, {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}};
        SDL_GPUColorTargetDescription color{}; color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex; info.fragment_shader = backgroundFragment;
        info.vertex_input_state = {&bufferDescription, 1, attributes, 3};
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.enable_depth_clip = true;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &color; info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true; info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.depth_stencil_state.enable_depth_test = true; info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        backgroundPipeline = checked(SDL_CreateGPUGraphicsPipeline(device, &info), "Create background pipeline");
        info.fragment_shader = fragment;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        modelPipeline = checked(SDL_CreateGPUGraphicsPipeline(device, &info), "Create mesh pipeline");
        info.fragment_shader=opaqueFragment;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_BACK;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;
        objectOpaquePipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create opaque source object pipeline");
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.fragment_shader=hudFragment;
        info.depth_stencil_state.enable_depth_test=false; info.depth_stencil_state.enable_depth_write=false;
        color.blend_state.enable_blend=true;
        color.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;
        color.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        hudPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create HUD pipeline");
        info.fragment_shader=dcMaskFragment;info.depth_stencil_state.enable_depth_test=true;
        info.depth_stencil_state.enable_depth_write=false;
        dcMaskPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create original DC coverage pipeline");
        info.depth_stencil_state.enable_depth_test=false;
        info.fragment_shader=fragment;
        iconPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create inventory icon pipeline");
        info.depth_stencil_state.enable_depth_test=true;info.depth_stencil_state.enable_depth_write=true;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_BACK;
        objectPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create source object pipeline");
        const auto savedInfo=info;const auto savedColor=color;
        // Original085580: textured SRC_ALPHA/INV_SRC_ALPHA bydefault, strict reciprocalGREATER/depthwrite.
        info.fragment_shader=particleFragment;info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS;
        for(unsigned blend=0;blend<3;++blend){
            color.blend_state.src_color_blendfactor=blend==2?SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor=blend==0?SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.src_alpha_blendfactor=color.blend_state.src_color_blendfactor;color.blend_state.dst_alpha_blendfactor=color.blend_state.dst_color_blendfactor;
            nativeObjectPipelines[blend]=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create native source material pipeline");
        }
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_FRONT;info.fragment_shader=opaqueFragment;color.blend_state.enable_blend=false;
        nativeActorOpaquePipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create native opaque actor pipeline");
        info.fragment_shader=particleFragment;color.blend_state.enable_blend=true;
        for(unsigned blend=0;blend<3;++blend){
            color.blend_state.src_color_blendfactor=blend==2?SDL_GPU_BLENDFACTOR_ZERO:SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            color.blend_state.dst_color_blendfactor=blend==2?SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR:blend==0?SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:SDL_GPU_BLENDFACTOR_ONE;
            color.blend_state.src_alpha_blendfactor=color.blend_state.src_color_blendfactor;color.blend_state.dst_alpha_blendfactor=color.blend_state.dst_color_blendfactor;
            nativeActorPipelines[blend]=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create native actor material pipeline");
        }
        info.fragment_shader=particleFragment;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_FRONT;
        color.blend_state.src_color_blendfactor=color.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color.blend_state.dst_color_blendfactor=color.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        nativeShadowPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create original default flat shadow pipeline");
        info=savedInfo;color=savedColor;
        info.fragment_shader=particleFragment;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.depth_stencil_state.enable_depth_test=false;info.depth_stencil_state.enable_depth_write=false;
        color.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        particlePipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create additive source VFX pipeline");
        // Native world effects test opaque depth; transparent draws keep depth writes disabled.
        info.depth_stencil_state.enable_depth_test=true;
        particleDepthPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create depth-tested native additive VFX pipeline");
        color.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        particleAlphaPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create original Dreamcast alpha VFX pipeline");
        info.fragment_shader=postFragment;info.target_info.has_depth_stencil_target=false;
        info.depth_stencil_state={};color.blend_state={};
        postPipeline=checked(SDL_CreateGPUGraphicsPipeline(device,&info),"Create display effects pipeline");
        SDL_ReleaseGPUShader(device,postFragment);
        SDL_ReleaseGPUShader(device, vertex); SDL_ReleaseGPUShader(device, fragment); SDL_ReleaseGPUShader(device, backgroundFragment);
        SDL_ReleaseGPUShader(device,hudFragment);
        SDL_ReleaseGPUShader(device,particleFragment);
        SDL_ReleaseGPUShader(device,opaqueFragment);
        SDL_ReleaseGPUShader(device,dcMaskFragment);
        const Vertex quadVertices[] = {{-1, 1, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 1, 0},
            {-1, -1, 0, 0, 0, 0, 0, 1}, {-1, -1, 0, 0, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 0, 1, 0}, {1, -1, 0, 0, 0, 0, 1, 1}};
        quad = uploadBuffer(quadVertices, sizeof(quadVertices));
        const Uint8 white[]={255,255,255,255};
        uiWhite=uploadTexture(white,1,1,4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        commit(prepare(scene,assets));
        SDL_Log("Renderer: %s; mesh: %u vertices; cameras: %u", SDL_GetGPUDeviceDriver(device), scene.vertexCount, unsigned(scene.cameras.size()));
    }
    std::unique_ptr<SceneResources> prepare(const Scene& scene,const fs::path& assets) {
        auto pending=std::make_unique<SceneResources>(device);
        pending->assets=assets;
        const auto sizePath=assets/"background_size.txt";
        if(fs::exists(sizePath)) {
            std::ifstream input(sizePath);const auto profile=eddcprofile::background(input);
            pending->backgroundWidth=profile.width;pending->backgroundHeight=profile.height;pending->backgroundFactor=profile.factor;
            pending->nativeBackground=profile.native;
        }
        pending->dcMasks.resize(scene.cameras.size());
        pending->backgrounds.reserve(scene.cameras.size()); pending->backgroundDepth.reserve(scene.cameras.size());
        if(!scene.vertices.empty())pending->mesh=uploadBuffer(scene.vertices.data(),static_cast<unsigned>(scene.vertices.size()*sizeof(Vertex)));
        if (!scene.idle.empty()) pending->idle=uploadBuffer(scene.idle.data(),static_cast<unsigned>(scene.idle.size()));
        if (!scene.walk.empty()) pending->walk=uploadBuffer(scene.walk.data(),static_cast<unsigned>(scene.walk.size()));
        if(scene.vertexCount)pending->ash=loadTexture(assets/"ash.bmp");
        // Upscaled DC colour keeps the original depth and coverage resolutions.
        for (unsigned i=0;i<scene.cameras.size();++i) {
            pending->backgrounds.push_back(loadTexture(assets/("background_"+std::to_string(i)+".bmp"),pending->backgroundWidth,pending->backgroundHeight));
            pending->backgroundDepth.push_back(loadDepth(assets/("background_"+std::to_string(i)+".depth"),
                pending->nativeBackground?320:pending->backgroundWidth,pending->nativeBackground?240:pending->backgroundHeight));
        }
        if(pending->nativeBackground)for(unsigned camera=0;camera<scene.cameras.size();++camera)
            prepareDcMasks(assets,std::to_string(camera),scene.cameras[camera].vp,pending->dcMasks[camera]);
        return pending;
    }
    void prepareDcMasks(const fs::path& assets,const std::string& suffix,const std::array<float,16>& vp,
                        std::vector<SceneResources::DcMask>& target) {
        std::ifstream input(assets/("background_"+suffix+".masks"));
        const auto sourceLayers=eddcprofile::masks(input);
        const auto mapping=eddcprofile::depthMapping(vp);
        for(unsigned n=0;n<sourceLayers.size();++n) {
            const auto& source=sourceLayers[n];
            const float x=source.x,y=source.y,w=source.width,h=source.height,u=source.u,v=source.v;
            const float mapped=mapping.map(source.inverseZ);
            const float left=2*x/640-1,right=2*(x+w)/640-1,top=1-2*y/480,bottom=1-2*(y+h)/480;
            const float bu0=x/640,bv0=y/480,bu1=(x+w)/640,bv1=(y+h)/480;
            const Vertex vertices[]={
                {left,top,mapped,bu0,bv0,0,0,0},{right,top,mapped,bu1,bv0,0,u,0},{left,bottom,mapped,bu0,bv1,0,0,v},
                {left,bottom,mapped,bu0,bv1,0,0,v},{right,top,mapped,bu1,bv0,0,u,0},{right,bottom,mapped,bu1,bv1,0,u,v}};
            target.emplace_back();auto& layer=target.back();layer.depth=mapped;
            layer.vertices=uploadBuffer(vertices,sizeof(vertices));
            const auto bytes=edimage::packedPixels(assets/("mask_"+suffix+"_"+std::to_string(n)+".coverage8"),256,256,1);
            if(bytes.size()!=65536)throw std::runtime_error("Invalid original256x256 coverage bytes");
            std::vector<float> values;values.reserve(65536);
            for(auto value:bytes){
                if(value%85)throw std::runtime_error("Non-native DC two-bit coverage value");
                values.push_back(float(value)/255);
            }
            layer.coverage=uploadTexture(values.data(),256,256,256*4,SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
            SDL_Log("DC layer%u native1/Z %.9g ->GPUdepth %.9g (A %.12g B %.12g)",n,source.inverseZ,mapped,mapping.a,mapping.b);
        }
    }
    void prepareCameraFrame(SceneResources& prepared,const Scene& scene,unsigned track) {
        const unsigned frame=scene.cameraFrame.empty()?0:scene.cameraFrame.at(track);
        const unsigned foreground=scene.foregroundFrame.empty()?0:scene.foregroundFrame.at(track);
        const bool backgroundChanged=frame&&(prepared.animatedTrack!=track||prepared.animatedFrame!=frame);
        const bool foregroundChanged=prepared.nativeBackground&&foreground&&
            (prepared.foregroundTrack!=track||prepared.foregroundFrame!=foreground);
        if(!backgroundChanged&&!foregroundChanged)return;
        SceneResources pending(device);
        if(backgroundChanged) {
            const auto suffix=std::to_string(track)+"_"+std::to_string(frame);
            pending.animatedBackground=loadTexture(prepared.assets/("background_"+suffix+".bmp"),prepared.backgroundWidth,prepared.backgroundHeight);
            pending.animatedDepth=loadDepth(prepared.assets/("background_"+suffix+".depth"),
                prepared.nativeBackground?320:prepared.backgroundWidth,prepared.nativeBackground?240:prepared.backgroundHeight);
        }
        if(foregroundChanged) {
            pending.dcMasks.resize(1);
            prepareDcMasks(prepared.assets,std::to_string(track)+"_"+std::to_string(foreground),scene.cameras[track].vp,pending.dcMasks[0]);
        }
        if(backgroundChanged) {
            std::swap(prepared.animatedBackground,pending.animatedBackground);std::swap(prepared.animatedDepth,pending.animatedDepth);
            prepared.animatedTrack=track;prepared.animatedFrame=frame;
        }
        if(foregroundChanged) {
            prepared.animatedMasks.swap(pending.dcMasks[0]);prepared.foregroundTrack=track;prepared.foregroundFrame=foreground;
        }
    }
    void commit(std::unique_ptr<SceneResources> pending) {
        checked(SDL_WaitForGPUIdle(device),"Wait before committing scene assets");
        resources.swap(pending);
    }
    bool fontHasGlyph(uint8_t code)const{return edfont::glyph(fontCharset,code)!=std::string::npos;}
    void loadFont(const fs::path& root) {
        std::ifstream input(root/"font.txt",std::ios::binary);auto metadata=edfont::Metadata::read(input);
        auto* atlas=loadTexture(root/"font.bmp",int(metadata.atlasWidth),int(metadata.atlasHeight));
        if(font)SDL_ReleaseGPUTexture(device,font);
        font=atlas;fontWidth=metadata.width;fontHeight=metadata.height;fontColumns=metadata.columns;fontCharset=std::move(metadata.charset);
        // Glyph cells and layout stay logical when only the stored atlas is enlarged.
        fontAtlasWidth=metadata.atlasWidth/metadata.atlasScale;fontAtlasHeight=metadata.atlasHeight/metadata.atlasScale;fontCodepage=metadata.codepage;
        hudText.clear();inventoryKey.clear();sourceGuiKey.clear();
    }
    void loadActorTextures(const fs::path& root,const edactors::GameplayActors& actors) {
        std::map<uint32_t,SDL_GPUTexture*> prepared;
        SDL_GPUTexture* atlasImage=nullptr;fs::path preparedAssets=root;
        try {
            for(uint32_t id:actors.textureCatalog()) {
                const auto data=edobjects::Texture::load(root/"actors/textures"/(resourceName(id)+".rgba"));
                if(data.identity!=id)throw std::runtime_error("Source actor texture identity mismatch");
                auto* image=uploadTexture(data.rgba.data(),data.width,data.height,data.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                try{prepared.emplace(id,image);}catch(...){SDL_ReleaseGPUTexture(device,image);throw;}
            }
            const auto atlas=edobjects::Texture::load(root/"vfx/atlas.rgba");
            if(atlas.identity!=0x0816e044)throw std::runtime_error("Source VFX atlas identity mismatch");
            atlasImage=uploadTexture(atlas.rgba.data(),atlas.width,atlas.height,atlas.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        } catch(...) {for(const auto& [type,image]:prepared){(void)type;SDL_ReleaseGPUTexture(device,image);}if(atlasImage)SDL_ReleaseGPUTexture(device,atlasImage);throw;}
        actorTextures.swap(prepared);for(const auto& [type,image]:prepared){(void)type;SDL_ReleaseGPUTexture(device,image);}
        sourceAssets.swap(preparedAssets);
        if(particleTexture)SDL_ReleaseGPUTexture(device,particleTexture);particleTexture=atlasImage;
    }
    static std::string resourceName(uint32_t id) {
        std::ostringstream name;name<<std::hex<<std::setfill('0')<<std::setw(8)<<id;return name.str();
    }
    static std::array<float,4> unpackTint(uint32_t color) {
        return {float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255};
    }
    SDL_GPUTexture* objectTexture(uint32_t id) {
        auto found=objectTextures.find(id);if(found!=objectTextures.end())return found->second;
        const auto texture=edobjects::Texture::load(sourceAssets/"objects/textures"/(resourceName(id)+".rgba"));
        if(texture.identity!=id)throw std::runtime_error("Source object texture identity mismatch");
        auto* image=uploadTexture(texture.rgba.data(),texture.width,texture.height,texture.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        try {objectTextures.emplace(id,image);}catch(...){SDL_ReleaseGPUTexture(device,image);throw;}return image;
    }
    SDL_GPUTexture* actorTexture(uint32_t id) {
        const auto found=actorTextures.find(id);if(found!=actorTextures.end())return found->second;
        const auto data=edobjects::Texture::load(sourceAssets/"actors/textures"/(resourceName(id)+".rgba"));
        if(data.identity!=id)throw std::runtime_error("Source actor texture identity mismatch");
        auto* image=uploadTexture(data.rgba.data(),data.width,data.height,data.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        try {actorTextures.emplace(id,image);}catch(...){SDL_ReleaseGPUTexture(device,image);throw;}return image;
    }
    void uploadDynamic(SDL_GPUCommandBuffer* command,std::unique_ptr<ActorResources>& buffer,
                       const std::vector<edmesh::Vertex32>& vertices) {
        if(vertices.empty())return;
        if(vertices.size()>1000000)throw std::runtime_error("Excessive dynamic source vertices");
        const unsigned bytes=unsigned(vertices.size()*sizeof(edmesh::Vertex32));
        if(!buffer||buffer->capacity<bytes) {
            auto pending=std::make_unique<ActorResources>(device);
            SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_VERTEX;info.size=bytes;
            pending->vertices=checked(SDL_CreateGPUBuffer(device,&info),"Create dynamic source vertices");
            SDL_GPUTransferBufferCreateInfo transfer{};transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;transfer.size=bytes;
            pending->transfer=checked(SDL_CreateGPUTransferBuffer(device,&transfer),"Create dynamic source transfer");
            pending->capacity=bytes;buffer=std::move(pending);
        }
        void* mapped=checked(SDL_MapGPUTransferBuffer(device,buffer->transfer,true),"Map dynamic source vertices");
        std::memcpy(mapped,vertices.data(),bytes);SDL_UnmapGPUTransferBuffer(device,buffer->transfer);
        auto* copy=checked(SDL_BeginGPUCopyPass(command),"Upload dynamic source vertices");
        SDL_GPUTransferBufferLocation source{buffer->transfer,0};SDL_GPUBufferRegion destination{buffer->vertices,0,bytes};
        SDL_UploadToGPUBuffer(copy,&source,&destination,true);SDL_EndGPUCopyPass(copy);
    }
    using ActorVertexStreams=std::map<edactors::Key,const std::vector<edmesh::Vertex32>*>;
    void uploadActors(SDL_GPUCommandBuffer* command,const edactors::GameplayActors& actors,const ActorVertexStreams& streams) {
        bool any=false;
        for(const auto& [key,body]:actors.bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&!streams.at(key)->empty()) {
            const auto& vertices=*streams.at(key);
            if(!actorTextures.count(body.textureID)||vertices.size()>1000000)throw std::runtime_error("Actor/GPU texture or vertex count mismatch");
            const unsigned bytes=unsigned(vertices.size()*sizeof(edmesh::Vertex32));
            auto found=actorBuffers.find(key);
            if(found==actorBuffers.end()||found->second->capacity<bytes) {
                auto pending=std::make_unique<ActorResources>(device);
                SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_VERTEX;info.size=bytes;
                pending->vertices=checked(SDL_CreateGPUBuffer(device,&info),"Create actor vertex stream");
                SDL_GPUTransferBufferCreateInfo transfer{};transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;transfer.size=bytes;
                pending->transfer=checked(SDL_CreateGPUTransferBuffer(device,&transfer),"Create actor transfer");pending->capacity=bytes;
                actorBuffers[key]=std::move(pending);found=actorBuffers.find(key);
            }
            auto& buffer=*found->second;
            void* data=checked(SDL_MapGPUTransferBuffer(device,buffer.transfer,true),"Map skinned actor");
            std::memcpy(data,vertices.data(),bytes);SDL_UnmapGPUTransferBuffer(device,buffer.transfer);any=true;
        }
        if(!any)return;
        auto* pass=checked(SDL_BeginGPUCopyPass(command),"Begin skinned actor upload");
        for(const auto& [key,body]:actors.bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&!streams.at(key)->empty()) {
            auto& buffer=*actorBuffers.find(key)->second;
            SDL_GPUTransferBufferLocation source{buffer.transfer,0};
            SDL_GPUBufferRegion destination{buffer.vertices,0,unsigned(streams.at(key)->size()*sizeof(edmesh::Vertex32))};
            SDL_UploadToGPUBuffer(pass,&source,&destination,true);
        }
        SDL_EndGPUCopyPass(pass);
    }
    void updateHud(const std::string& text) {
        if (text==hudText) return;
        if (!font || text.size()>512) throw std::runtime_error("HUD font is unavailable or text is too long");
        std::vector<Vertex> vertices;
        float x=12,y=480-12-float(1+std::count(text.begin(),text.end(),'\n'))*fontHeight;
        for (char character:text) {
            if (character=='\n') { x=12; y+=fontHeight; continue; }
            if (character==' ') { x+=fontWidth; continue; }
            size_t glyph=edfont::glyph(fontCharset,uint8_t(character),false);
            if (glyph==std::string::npos) glyph=fontCharset.find('?');
            if (glyph==std::string::npos || x+fontWidth>628) { x+=fontWidth; continue; }
            const float left=2*x/640-1,right=2*(x+fontWidth)/640-1;
            const float top=1-2*y/480,bottom=1-2*(y+fontHeight)/480;
            const float u=float((glyph%fontColumns)*fontWidth)/fontAtlasWidth,v=float((glyph/fontColumns)*fontHeight)/fontAtlasHeight;
            const float u1=u+float(fontWidth)/fontAtlasWidth,v1=v+float(fontHeight)/fontAtlasHeight;
            const Vertex a{left,top,0,0,0,0,u,v},b{right,top,0,0,0,0,u1,v};
            const Vertex c{left,bottom,0,0,0,0,u,v1},d{right,bottom,0,0,0,0,u1,v1};
            vertices.insert(vertices.end(),{a,b,c,c,b,d}); x+=fontWidth;
        }
        SDL_GPUBuffer* next=vertices.empty()?nullptr:uploadBuffer(vertices.data(),unsigned(vertices.size()*sizeof(Vertex)));
        if (hudBuffer) SDL_ReleaseGPUBuffer(device,hudBuffer);
        hudBuffer=next; hudVertices=unsigned(vertices.size()); hudText=text;
    }
    void loadInventory(const fs::path& root) {loadInventory(root,uiLanguage_);}
    void loadInventory(const fs::path& root,UiLanguage language) {
        const auto data=readFile(uiInventoryPath(root,language));
        if(data.size()<16)throw std::runtime_error("Invalid inventory metadata");
        const bool localized=std::memcmp(data.data(),"EDINV2\0\0",8)==0;
        if(!localized&&std::memcmp(data.data(),"EDINV1\0\0",8))throw std::runtime_error("Invalid inventory metadata");
        size_t offset=8;
        if(localized&&(take<uint32_t>(data,offset)!=1251||fontCodepage!=1251))throw std::runtime_error("Unsupported declared inventory encoding/font");
        const auto textureCount=take<uint32_t>(data,offset),itemCount=take<uint32_t>(data,offset);
        if(!textureCount||textureCount>8||!itemCount||itemCount>4096) throw std::runtime_error("Invalid inventory counts");
        std::vector<SDL_GPUTexture*> textures;
        std::vector<InventoryIcon> icons;
        std::vector<unsigned> identities;
        textures.reserve(textureCount); icons.reserve(itemCount); identities.reserve(textureCount);
        try {
            for(unsigned i=0;i<textureCount;++i) {
                const auto id=take<uint32_t>(data,offset),width=take<uint32_t>(data,offset),height=take<uint32_t>(data,offset);
                if(!width||!height||width>2048||height>2048||std::find(identities.begin(),identities.end(),id)!=identities.end())
                    throw std::runtime_error("Invalid inventory atlas dimensions/identity");
                identities.push_back(id);
                char filename[64]; SDL_snprintf(filename,sizeof(filename),"inventory_%08x.rgba",id);
                const auto pixels=readFile(root/filename,size_t(width)*height*4);
                if(pixels.size()!=size_t(width)*height*4) throw std::runtime_error("Invalid inventory atlas byte count");
                textures.push_back(uploadTexture(pixels.data(),width,height,width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM));
            }
            auto label=[&](unsigned count)->std::string {
                if(offset>data.size()||count>data.size()-offset) throw std::runtime_error("Truncated inventory label");
                std::string text(reinterpret_cast<const char*>(data.data()+offset),count); offset+=count;
                if(std::any_of(text.begin(),text.end(),[&](unsigned char c){return localized?(!edfont::cp1251Byte(c)&&c!='\n'):(c<32||c>126);}))
                    throw std::runtime_error("Unsupported inventory glyph byte");
                return text;
            };
            for(unsigned i=0;i<itemCount;++i) {
                InventoryIcon icon;
                icon.itemID=take<uint32_t>(data,offset); icon.flags=take<uint32_t>(data,offset);
                icon.texture=take<uint32_t>(data,offset); icon.shape=take<uint32_t>(data,offset);
                for(auto& value:icon.uv) value=takeFloat(data,offset);
                icon.aspect=takeFloat(data,offset);
                const auto name=take<uint32_t>(data,offset),description=take<uint32_t>(data,offset);
                if(icon.itemID>65535||icon.texture>=textureCount||icon.aspect<=0||icon.aspect>=20||
                   icon.uv[0]<0||icon.uv[1]<0||icon.uv[2]>1||icon.uv[3]>1||icon.uv[0]>=icon.uv[2]||icon.uv[1]>=icon.uv[3]||
                   !name||name>256||!description||description>4096||
                   std::any_of(icons.begin(),icons.end(),[&](const auto& old){return old.itemID==icon.itemID;}))
                    throw std::runtime_error("Invalid inventory icon or text range");
                icon.name=label(name); icon.description=label(description); icons.push_back(std::move(icon));
            }
            if(offset!=data.size()) throw std::runtime_error("Unindexed inventory metadata bytes");
        } catch(...) { for(auto* image:textures) SDL_ReleaseGPUTexture(device,image); throw; }
        for(auto* image:inventoryTextures) SDL_ReleaseGPUTexture(device,image);
        inventoryTextures.swap(textures); inventoryIcons.swap(icons); inventoryKey.clear(); inventoryLoaded=true;
    }
    void setUiLanguage(UiLanguage language) {
        if(!validUiLanguage(language))throw std::runtime_error("Unsupported UI language");
        if(language==uiLanguage_)return;
        if(sourceAssets.empty()||!inventoryLoaded){uiLanguage_=language;return;}
        std::map<uint32_t,SDL_GPUTexture*> replacements;
        try {
            for(uint32_t id:{0x0b942034u,0x0b942054u}){
                const auto data=edobjects::Texture::load(uiTexturePath(sourceAssets,id,language));
                if(data.identity!=id)throw std::runtime_error("Source GUI texture identity mismatch");
                auto* image=uploadTexture(data.rgba.data(),data.width,data.height,data.width*4,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
                try{replacements.emplace(id,image);}catch(...){SDL_ReleaseGPUTexture(device,image);throw;}
            }
            loadInventory(sourceAssets,language);
        }catch(...){for(const auto& [id,image]:replacements){(void)id;SDL_ReleaseGPUTexture(device,image);}throw;}
        for(const auto& [id,image]:replacements){(void)image;const auto old=objectTextures.find(id);
            if(old!=objectTextures.end()){SDL_ReleaseGPUTexture(device,old->second);objectTextures.erase(old);}}
        objectTextures.merge(replacements);uiLanguage_=language;sourceGuiKey.clear();hudText.clear();
    }
#include "gui_graphics.inl"
    void updateInventory(const InventoryView& view,bool sourceMessage=false) {
        if(!font||!inventoryLoaded) throw std::runtime_error("Inventory font/assets are unavailable");
        if(view.items.size()>256) throw std::runtime_error("Too many inventory view items");
        const auto t=[&](std::string_view value){return edui::text(value,russianUi(uiLanguage_));};
        std::string key=std::to_string(view.selected)+','+std::to_string(view.health)+','+std::to_string(view.maxHealth);
        key+=view.reading?",reading":",items";
        key+=','+std::to_string(view.combineSlot)+','+std::to_string(view.combineItemID);
        key+=';'+view.notice;
        key+=sourceMessage?",message":",description";
        if(view.documentPage)textPageKey(key,*view.documentPage);
        for(const auto& item:view.items) key+=';'+std::to_string(item.itemID)+','+std::to_string(item.quantity)+','+std::to_string(item.slot);
        if(key==inventoryKey&&inventoryBuffer) return;
        std::vector<Vertex> vertices;
        std::vector<InventoryDraw> draws;
        auto rectangle=[&](float x,float y,float width,float height,float u0,float v0,float u1,float v1) {
            const float l=2*x/640-1,r=2*(x+width)/640-1,t=1-2*y/480,b=1-2*(y+height)/480;
            const Vertex a{l,t,0,0,0,0,u0,v0},c{l,b,0,0,0,0,u0,v1},d{r,b,0,0,0,0,u1,v1},e{r,t,0,0,0,0,u1,v0};
            vertices.insert(vertices.end(),{a,e,c,c,e,d});
        };
        auto panel=[&](float x,float y,float w,float h,std::array<float,4> color) {
            const unsigned start=unsigned(vertices.size()); rectangle(x,y,w,h,0,0,1,1);
            draws.push_back({start,6,-2,color});
        };
        auto text=[&](const std::string& value,float x,float y,float scale,unsigned columns,unsigned lines) {
            const unsigned start=unsigned(vertices.size()); const float origin=x;
            unsigned column=0,line=0;
            for(size_t i=0;i<value.size();++i) {
                const char character=value[i];
                const bool wordStart=character!=' '&&character!='\n'&&(i==0||value[i-1]==' '||value[i-1]=='\n');
                const auto wordEnd=wordStart?value.find_first_of(" \n",i):i;
                const size_t wordSize=wordStart?(wordEnd==std::string::npos?value.size():wordEnd)-i:0;
                const bool wrapWord=wordStart&&column&&wordSize<=columns&&column+wordSize>columns;
                if(character=='\n'||column==columns||wrapWord) {
                    column=0; ++line; x=origin; y+=fontHeight*scale;
                    if(character=='\n') continue;
                }
                if(line>=lines) break;
                if(character==' '&&!column) continue;
                if(character!=' ') {
                    auto glyph=edfont::glyph(fontCharset,uint8_t(character),false); if(glyph==std::string::npos) glyph=fontCharset.find('?');
                    if(glyph!=std::string::npos) {
                        const float u=float((glyph%fontColumns)*fontWidth)/fontAtlasWidth,v=float((glyph/fontColumns)*fontHeight)/fontAtlasHeight;
                        rectangle(x,y,fontWidth*scale,fontHeight*scale,u,v,u+float(fontWidth)/fontAtlasWidth,v+float(fontHeight)/fontAtlasHeight);
                    }
                }
                x+=fontWidth*scale; ++column;
            }
            if(vertices.size()>start) draws.push_back({start,unsigned(vertices.size()-start),-1,{1,1,1,1}});
        };
        auto iconFor=[&](int identity)->const InventoryIcon* {
            for(const auto& icon:inventoryIcons) if(icon.itemID==unsigned(identity)) return &icon;
            return nullptr;
        };
        panel(24,24,592,432,{0.02f,0.025f,0.03f,0.96f});
        text(t("inventory"),44,40,1,32,1);
        if(!view.notice.empty())text(t(view.notice),44,64,0.55f,53,1);
        if(view.health>=0&&view.maxHealth>0)
            text(t("hp ")+std::to_string(view.health)+"/"+std::to_string(view.maxHealth),422,44,0.75f,13,1);
        if(view.reading) {
            if(!view.documentPage)throw std::runtime_error("Document page is unavailable");
            appendTextPage(*view.documentPage,vertices,draws);
            text(t("next page: e/a   back: esc/b"),44,430,0.6f,49,1);
        } else {
        const unsigned selected=view.items.empty()?0:std::min<unsigned>(view.selected,unsigned(view.items.size()-1));
        const unsigned page=selected/12,begin=page*12,end=std::min<unsigned>(begin+12,unsigned(view.items.size()));
        for(unsigned i=begin;i<end;++i) {
            const float x=44+float((i-begin)%4)*138,y=80+float((i-begin)/4)*72;
            const bool first=view.items[i].containerField==99&&view.items[i].slot==view.combineSlot;
            panel(x,y,128,66,i==selected?std::array<float,4>{0.5f,0.31f,0.08f,1}:first?std::array<float,4>{0.08f,0.35f,0.22f,1}:std::array<float,4>{0.13f,0.14f,0.15f,1});
            panel(x+2,y+2,124,62,{0.04f,0.05f,0.06f,1});
            const auto& item=view.items[i];
            if(const auto* icon=iconFor(item.itemID)) {
                const float width=std::min(92.0f,52*icon->aspect),height=width/icon->aspect;
                const unsigned start=unsigned(vertices.size());
                rectangle(x+8+(92-width)*0.5f,y+5+(52-height)*0.5f,width,height,icon->uv[0],icon->uv[1],icon->uv[2],icon->uv[3]);
                draws.push_back({start,6,int(icon->texture),{1,1,1,1}});
            } else text(t("id ")+std::to_string(item.itemID),x+8,y+12,0.65f,9,1);
            const auto quantity=std::to_string(item.quantity);
            text(quantity,x+8,y+43,0.65f,9,1);
        }
        if(view.items.empty()) text(t("empty"),44,100,0.85f,36,1);
        else if(!sourceMessage) {
            const auto& item=view.items[selected]; const auto* icon=iconFor(item.itemID);
            text(icon?icon->name:t("item ")+std::to_string(item.itemID),44,304,0.75f,40,2);
            text(icon?icon->description:t("original icon/name not decoded"),44,344,0.65f,46,4);
        }
        text(t(view.combineSlot>=0?"choose second: e/a   cancel: esc/b":"use/read: e/a   combine: c/x   back: esc/b"),44,430,0.6f,49,1);
        }
        auto* buffer=uploadBuffer(vertices.data(),unsigned(vertices.size()*sizeof(Vertex)));
        if(inventoryBuffer) SDL_ReleaseGPUBuffer(device,inventoryBuffer);
        inventoryBuffer=buffer; inventoryDraws=std::move(draws); inventoryKey=std::move(key);
    }
    bool renderMovie(const MoviePlayer& movie) {
        if(!device||!window||!quad||!iconPipeline) throw std::runtime_error("Movie renderer is unavailable");
        const unsigned sourceWidth=movie.width(),sourceHeight=movie.height();
        const unsigned pitch=(sourceWidth+63)&~63u;
        if(!movieTexture||sourceWidth!=movieWidth||sourceHeight!=movieHeight) {
            auto* image=texture(sourceWidth,sourceHeight,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_SAMPLER);
            SDL_GPUTransferBufferCreateInfo info{}; info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; info.size=pitch*sourceHeight*4;
            auto* transfer=SDL_CreateGPUTransferBuffer(device,&info);
            if(!transfer) { SDL_ReleaseGPUTexture(device,image); throw std::runtime_error(SDL_GetError()); }
            if(movieTexture) SDL_ReleaseGPUTexture(device,movieTexture);
            if(movieTransfer) SDL_ReleaseGPUTransferBuffer(device,movieTransfer);
            movieTexture=image; movieTransfer=transfer; movieWidth=sourceWidth; movieHeight=sourceHeight; movieFrame=~0u;
        }
        auto* command=checked(SDL_AcquireGPUCommandBuffer(device),"Acquire movie frame");
        bool swapchainAcquired=false;
        try {
            SDL_GPUTexture* swapchain=nullptr; Uint32 width=0,height=0;
            checked(SDL_WaitAndAcquireGPUSwapchainTexture(command,window,&swapchain,&width,&height),"Acquire movie swapchain");
            swapchainAcquired=swapchain!=nullptr;
            if(!swapchain||!width||!height) {
                auto* submitted=command; command=nullptr; checked(SDL_SubmitGPUCommandBuffer(submitted),"Submit hidden movie frame"); return false;
            }
            resizeTargets(width,height);
            if(movieGeneration!=movie.generation()||movieFrame!=movie.frameIndex()) {
                movieFrame=~0u;
                auto* pixels=static_cast<Uint8*>(checked(SDL_MapGPUTransferBuffer(device,movieTransfer,true),"Map movie frame"));
                for(unsigned y=0;y<sourceHeight;++y)
                    std::memcpy(pixels+y*pitch*4,movie.frameRGBA().data()+std::size_t(y)*sourceWidth*4,sourceWidth*4);
                SDL_UnmapGPUTransferBuffer(device,movieTransfer);
                auto* copy=checked(SDL_BeginGPUCopyPass(command),"Begin movie upload");
                SDL_GPUTextureTransferInfo source{movieTransfer,0,pitch,sourceHeight};
                SDL_GPUTextureRegion destination{}; destination.texture=movieTexture; destination.w=sourceWidth; destination.h=sourceHeight; destination.d=1;
                SDL_UploadToGPUTexture(copy,&source,&destination,true); SDL_EndGPUCopyPass(copy);
            }
            unsigned viewWidth=width,viewHeight=height;
            if(std::uint64_t(width)*movie.displayHeight()>std::uint64_t(height)*movie.displayWidth())
                viewWidth=unsigned(std::uint64_t(height)*movie.displayWidth()/movie.displayHeight());
            else viewHeight=unsigned(std::uint64_t(width)*movie.displayHeight()/movie.displayWidth());
            const unsigned x=(width-viewWidth)/2,y=(height-viewHeight)/2;
            SDL_GPUColorTargetInfo color{}; color.texture=target; color.load_op=SDL_GPU_LOADOP_CLEAR; color.store_op=SDL_GPU_STOREOP_STORE; color.clear_color={0,0,0,1};
            SDL_GPUDepthStencilTargetInfo depthInfo{}; depthInfo.texture=depth; depthInfo.clear_depth=1;
            depthInfo.load_op=SDL_GPU_LOADOP_CLEAR; depthInfo.store_op=SDL_GPU_STOREOP_DONT_CARE;
            depthInfo.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE; depthInfo.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
            auto* pass=checked(SDL_BeginGPURenderPass(command,&color,1,&depthInfo),"Begin movie render");
            const SDL_GPUViewport viewport{float(x),float(y),float(viewWidth),float(viewHeight),0,1}; SDL_SetGPUViewport(pass,&viewport);
            const SDL_Rect scissor{int(x),int(y),int(viewWidth),int(viewHeight)}; SDL_SetGPUScissor(pass,&scissor);
            struct Uniforms { std::array<float,16> mvp; float light[4]; } uniforms{};
            uniforms.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; uniforms.light[3]=1;
            SDL_BindGPUGraphicsPipeline(pass,iconPipeline);
            SDL_GPUBufferBinding binding{quad,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            SDL_GPUTextureSamplerBinding image{movieTexture,depthSampler}; SDL_BindGPUFragmentSamplers(pass,0,&image,1);
            const auto whiteTint=unpackTint(0xffffffff);SDL_PushGPUFragmentUniformData(command,0,whiteTint.data(),sizeof(whiteTint));
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms)); SDL_DrawGPUPrimitives(pass,6,1,0,0);
            SDL_EndGPURenderPass(pass);
            presentFrame(command,swapchain,width,height,scissor,double(SDL_GetTicksNS())/1e9);
            auto* submitted=command; command=nullptr; checked(SDL_SubmitGPUCommandBuffer(submitted),"Submit movie frame");
            movieGeneration=movie.generation(); movieFrame=movie.frameIndex(); return true;
        } catch(...) {
            if(command) {
                if(swapchainAcquired) SDL_SubmitGPUCommandBuffer(command);
                else SDL_CancelGPUCommandBuffer(command);
            }
            throw;
        }
    }
    bool render(const Scene& scene, unsigned camera, const float position[3], float yaw, double animationTime, bool walking,
                const std::string& hud={},const InventoryView* inventory=nullptr,const edactors::GameplayActors* actors=nullptr,
                const std::vector<ObjectDraw>& objects={},const std::vector<edvfx::Batch>& effects={},
                const std::map<edactors::Key,ActorDrawTint>& actorTints={},
                const edscreen::Overlay& screen={},float projectionShake=0,
                const std::vector<edguitext::Page>& textPages={},const std::vector<edguipresentation::Sprite>& guiSprites={},const std::vector<edguipresentation::Command>& guiCommands={}) {
        if (!resources || camera>=resources->backgrounds.size()) throw std::runtime_error("Scene/GPU asset mismatch");
        if(!std::isfinite(projectionShake)||!std::isfinite(screen.barHeight)||screen.barHeight<0||screen.barHeight>32)
            throw std::runtime_error("Invalid source screen effect snapshot");
        const auto foregroundVP=edscreen::foregroundTransform(scene.cameras[camera].vp,projectionShake);
        prepareCameraFrame(*resources,scene,camera);
        const bool commonRaster=commonSceneRaster&&(resources->nativeBackground||scene.menuContext);
        if (!hud.empty() || !hudText.empty()) updateHud(hud);
        if(inventory&&inventory->open) updateInventory(*inventory,!textPages.empty());
        const std::vector<edguitext::Page> noPages;
        updateSourceGui(inventory&&inventory->reading?noPages:textPages,guiSprites,guiCommands);
        std::map<edactors::Key,std::vector<edmesh::Vertex32>> clippedActors;
        ActorVertexStreams actorVertices;
        if(actors)for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)) {
            actorTexture(body.textureID);const auto* vertices=&body.vertices;
            // shortcut: cached floor plane; use floor triangles if a sloped disappearance needs it.
            if(resources->nativeBackground&&key.type!=0&&body.floorTriangle>=0&&
                std::any_of(body.vertices.begin(),body.vertices.end(),[&](const auto& v){return v.y < -body.localY;}))
                vertices=&clippedActors.emplace(key,ednativevisual::clipAboveFloor(body.vertices,-body.localY)).first->second;
            actorVertices.emplace(key,vertices);
        }
        struct ObjectBatch{SDL_GPUTexture* texture;unsigned first,count;bool alpha;uint32_t tint;uint8_t mode;int16_t type;};
        std::vector<ObjectBatch> objectBatches;
        std::vector<edmesh::Vertex32> objectVertices,particleVertices,shadowVertices;
        const auto& sourceCamera=scene.cameras[camera];
        for(const auto& item:objects) {
            auto found=objectModels.find(item.model);
            if(found==objectModels.end()) {
                auto model=edobjects::ObjectMesh::load(sourceAssets/"objects/models"/(resourceName(item.model)+".bin"));
                if(model.identity!=item.model)throw std::runtime_error("Source object model identity mismatch");
                found=objectModels.emplace(item.model,std::move(model)).first;
            }
            const auto vertices=found->second.drawVertices(item.transform);
            if(objectVertices.size()+vertices.size()>1000000)throw std::runtime_error("Excessive world object vertices");
            objectBatches.push_back({objectTexture(item.texture),unsigned(objectVertices.size()),unsigned(vertices.size()),edobjects::alphaBlended(item.type),item.tint,item.mode,item.type});
            for(const auto& vertex:vertices)objectVertices.push_back(vertex.geometry);
            if(resources->nativeBackground&&!ednativevisual::specialObject(item.type))ednativevisual::appendProjectedShadow(shadowVertices,
                ednativevisual::objectShadow(found->second,item.transform),sourceCamera.eye3,sourceCamera.right3,sourceCamera.up3);
        }
        if(resources->nativeBackground&&actors)for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&!actorVertices.at(key)->empty()&&
            scene.queries.visible(key.type==0?1:2,camera,{body.pose.x,body.pose.y,body.pose.z}))
            ednativevisual::appendProjectedShadow(shadowVertices,ednativevisual::actorShadow(*actorVertices.at(key),body.localY,{{body.pose.x,body.pose.y,body.pose.z}},body.pose.yaw),
                sourceCamera.eye3,sourceCamera.right3,sourceCamera.up3);
        const unsigned shadowFirst=unsigned(objectVertices.size()),shadowCount=unsigned(shadowVertices.size());
        if(objectVertices.size()+shadowVertices.size()>1000000)throw std::runtime_error("Excessive native world/shadow vertices");
        objectVertices.insert(objectVertices.end(),shadowVertices.begin(),shadowVertices.end());
        for(const auto& batch:effects) {
            if(batch.texture!=0x0816e044||!particleTexture||particleVertices.size()+batch.vertices.size()>edvfx::MaxVertices||
               (batch.blend!=edvfx::Blend::Additive&&batch.blend!=edvfx::Blend::Alpha))
                throw std::runtime_error("Source VFX/GPU atlas or vertex count mismatch");
            particleVertices.insert(particleVertices.end(),batch.vertices.begin(),batch.vertices.end());
        }
        struct NativeActorBatch {edactors::Key key;uint32_t texture;std::array<float,16> mvp;std::array<float,4> tint;ednativevisual::Material material;};
        std::vector<NativeActorBatch> nativeActors;
        std::vector<ednativeorder::Draw> nativeDraws;
        if(resources->nativeBackground){
            if(actors)for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&!actorVertices.at(key)->empty()&&
                scene.queries.visible(key.type==0?1:2,camera,{body.pose.x,body.pose.y,body.pose.z})){
                const auto entry=actorTints.find(key);const auto appearance=entry==actorTints.end()?ActorDrawTint{}:entry->second;
                const auto material=ednativevisual::actorMaterial(appearance.mode,appearance.color);if(material.opaque)continue;
                const float sine=std::sin(body.pose.yaw),cosine=std::cos(body.pose.yaw);
                const std::array<float,16> model{-cosine,0,sine,0,0,1,0,0,sine,0,cosine,0,body.pose.x,body.pose.y,body.pose.z,1};
                auto tint=unpackTint(appearance.color);tint[3]=material.alpha;
                const auto mvp=multiply(foregroundVP,model);const unsigned batch=unsigned(nativeActors.size());
                nativeActors.push_back({key,body.textureID,mvp,tint,material});
                const auto& vertices=*actorVertices.at(key);
                ednativeorder::append(nativeDraws,ednativeorder::Kind::Actor,batch,0,vertices.data(),unsigned(vertices.size()),mvp);
            }
            for(unsigned i=0;i<objectBatches.size();++i){const auto& batch=objectBatches[i];
                ednativeorder::append(nativeDraws,ednativeorder::Kind::Object,i,batch.first,objectVertices.data()+batch.first,batch.count,foregroundVP);}
            if(shadowCount)ednativeorder::append(nativeDraws,ednativeorder::Kind::Shadow,0,shadowFirst,objectVertices.data()+shadowFirst,shadowCount,foregroundVP);
            unsigned first=0;for(unsigned i=0;i<effects.size();++i){const auto& batch=effects[i];const unsigned count=unsigned(batch.vertices.size());
                if(batch.nativeRenderer)ednativeorder::append(nativeDraws,ednativeorder::Kind::Particle,i,first,particleVertices.data()+first,count,foregroundVP);first+=count;}
            ednativeorder::sortAndCoalesce(nativeDraws);
        }
        auto* command = checked(SDL_AcquireGPUCommandBuffer(device), "Acquire frame");
        bool swapchainAcquired=false;
        try {
        SDL_GPUTexture* swapchain = nullptr; Uint32 width = 0, height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain, &width, &height)) {
            SDL_CancelGPUCommandBuffer(command);command=nullptr;throw std::runtime_error(SDL_GetError());
        }
        swapchainAcquired=swapchain!=nullptr;
        if (!swapchain) {auto* submitted=command;command=nullptr;checked(SDL_SubmitGPUCommandBuffer(submitted),"Submit empty frame");return false;}
        resizeTargets(width, height);if(commonRaster)ensureCommonSceneTargets();
        if(actors)uploadActors(command,*actors,actorVertices);
        uploadDynamic(command,objectBuffer,objectVertices);uploadDynamic(command,particleBuffer,particleVertices);
        SDL_GPUColorTargetInfo color{}; color.texture = commonRaster?commonSceneColor:target; color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
        color.clear_color = {0, 0, 0, 1};
        SDL_GPUDepthStencilTargetInfo depthInfo{}; depthInfo.texture = commonRaster?commonSceneDepth:depth; depthInfo.clear_depth = 1;
        depthInfo.load_op = SDL_GPU_LOADOP_CLEAR; depthInfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE; depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = checked(SDL_BeginGPURenderPass(command, &color, 1, &depthInfo), "Begin frame");
        const auto area = commonRaster?SceneViewport{0,0,640,480}:sceneViewport(width, height);
        const SDL_GPUViewport viewport{float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
        SDL_SetGPUViewport(pass, &viewport);
        const SDL_Rect scissor{int(area.x), int(area.y), int(area.width), int(area.height)};
        SDL_SetGPUScissor(pass, &scissor);
        struct Uniforms { std::array<float, 16> mvp; float light[4]; } uniforms{};
        uniforms.mvp = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; uniforms.light[3] = 1;
        if(projectionShake)uniforms.mvp=edscreen::backgroundTransform(projectionShake);
        SDL_BindGPUGraphicsPipeline(pass, backgroundPipeline);
        SDL_GPUBufferBinding binding{quad, 0}; SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        const bool animated=!scene.cameraFrame.empty()&&scene.cameraFrame[camera]!=0;
        SDL_GPUTextureSamplerBinding backgroundBindings[] = {{animated?resources->animatedBackground:resources->backgrounds[camera], sampler},
            {animated?resources->animatedDepth:resources->backgroundDepth[camera], depthSampler}};
        SDL_BindGPUFragmentSamplers(pass, 0, backgroundBindings, 2);
        SDL_PushGPUVertexUniformData(command, 0, &uniforms, sizeof(uniforms));
        const float backgroundColor[]={resources->backgroundFactor,resources->backgroundFactor,resources->backgroundFactor,1};
        SDL_PushGPUFragmentUniformData(command,0,backgroundColor,sizeof(backgroundColor));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
        SDL_GPUTextureSamplerBinding textureBinding{};
        const auto whiteTint=unpackTint(0xffffffff);SDL_PushGPUFragmentUniformData(command,0,whiteTint.data(),sizeof(whiteTint));
        if(actors) {
            SDL_BindGPUGraphicsPipeline(pass,modelPipeline);
            uniforms.light[0]=0;uniforms.light[1]=resources->nativeBackground?0:.25f;uniforms.light[2]=0;uniforms.light[3]=resources->nativeBackground?1:.75f;
            for(const auto& [key,body]:actors->bodies())if(body.nativeSlot>=0&&(body.flags&1)&&!(body.flags&8)&&!actorVertices.at(key)->empty()&&
                scene.queries.visible(key.type==0?1:2,camera,{body.pose.x,body.pose.y,body.pose.z})) {
                const float sine=std::sin(body.pose.yaw),cosine=std::cos(body.pose.yaw);
                const std::array<float,16> model{-cosine,0,sine,0,0,1,0,0,sine,0,cosine,0,body.pose.x,body.pose.y,body.pose.z,1};
                uniforms.mvp=multiply(foregroundVP,model);
                const auto entry=actorTints.find(key);const auto appearance=entry==actorTints.end()?ActorDrawTint{}:entry->second;
                auto tint=unpackTint(appearance.color);
                if(resources->nativeBackground){
                    const auto material=ednativevisual::actorMaterial(appearance.mode,appearance.color);if(!material.opaque)continue;tint[3]=material.alpha;
                    SDL_BindGPUGraphicsPipeline(pass,material.opaque?nativeActorOpaquePipeline:nativeActorPipelines[material.blend==ednativevisual::Blend::DestinationInverse?2:size_t(material.blend)]);
                    if(material.opaque)SDL_PushGPUFragmentUniformData(command,0,tint.data(),sizeof(tint));
                    else {const float parameters[]={tint[0],tint[1],tint[2],tint[3],-1,1,0,0};SDL_PushGPUFragmentUniformData(command,0,parameters,sizeof(parameters));}
                }else{SDL_BindGPUGraphicsPipeline(pass,modelPipeline);SDL_PushGPUFragmentUniformData(command,0,tint.data(),sizeof(tint));}
                binding={actorBuffers.find(key)->second->vertices,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
                textureBinding={actorTextures.find(body.textureID)->second,modelSampler};SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
                SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
                SDL_DrawGPUPrimitives(pass,unsigned(actorVertices.at(key)->size()),1,0,0);
            }
        } else if(scene.vertexCount) {
        const float s = scene.scale, sine = std::sin(yaw), cosine = std::cos(yaw);
        std::array<float, 16> model{-cosine*s, 0, sine*s, 0, 0, s, 0, 0, sine*s, 0, cosine*s, 0,
                                   position[0], position[1], position[2], 1};
        uniforms.mvp = multiply(foregroundVP, model);
        uniforms.light[0] = 0; uniforms.light[1] = 0.25f; uniforms.light[2] = 0; uniforms.light[3] = 0.75f;
        SDL_BindGPUGraphicsPipeline(pass, modelPipeline);
        SDL_GPUBuffer* animation = walking ? resources->walk : resources->idle;
        const unsigned frameCount = walking ? scene.walkFrames : scene.idleFrames;
        const unsigned frame = frameCount ? static_cast<unsigned>(animationTime * scene.animationFPS) % frameCount : 0;
        binding = {animation ? animation : resources->mesh, 0}; SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        textureBinding={resources->ash, modelSampler}; SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
        SDL_PushGPUVertexUniformData(command, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, scene.vertexCount, 1, frame * scene.vertexCount, 0);
        }
        uniforms.mvp=foregroundVP;
        uniforms.light[0]=uniforms.light[1]=uniforms.light[2]=0;uniforms.light[3]=1;
        SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
        if(!resources->nativeBackground&&!objectVertices.empty()) {
            binding={objectBuffer->vertices,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            for(const auto& batch:objectBatches) {
                SDL_BindGPUGraphicsPipeline(pass,batch.alpha?objectPipeline:objectOpaquePipeline);
                const auto tint=unpackTint(batch.tint);SDL_PushGPUFragmentUniformData(command,0,tint.data(),sizeof(tint));
                textureBinding={batch.texture,modelSampler};SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
                SDL_DrawGPUPrimitives(pass,batch.count,1,batch.first,0);
            }
        }
        const bool separateForeground=resources->nativeBackground?
            (!scene.foregroundFrame.empty()&&scene.foregroundFrame.at(camera)!=0):animated;
        const auto& foregroundMasks=separateForeground?resources->animatedMasks:resources->dcMasks[camera];
        auto drawParticles=[&](float nearExclusive,float farInclusive,int nativeOnly){
            if(particleVertices.empty())return;
            SDL_BindGPUGraphicsPipeline(pass,particlePipeline);
            binding={particleBuffer->vertices,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            textureBinding={particleTexture,modelSampler};SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
            uniforms.mvp=foregroundVP;SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            unsigned first=0;for(const auto& batch:effects){
                const unsigned count=unsigned(batch.vertices.size());
                if(nativeOnly<0||bool(batch.nativeRenderer)==bool(nativeOnly)){
                    SDL_BindGPUGraphicsPipeline(pass,batch.blend==edvfx::Blend::Alpha?particleAlphaPipeline:
                        batch.nativeRenderer?particleDepthPipeline:particlePipeline);
                    const float parameters[]={batch.tint[0],batch.tint[1],batch.tint[2],batch.tint[3],nearExclusive,farInclusive,0,0};
                    SDL_PushGPUFragmentUniformData(command,0,parameters,sizeof(parameters));
                    SDL_DrawGPUPrimitives(pass,count,1,first,0);
                }first+=count;
            }
        };
        auto drawNative=[&](float nearExclusive,float farInclusive){
            bool bound=false;ednativeorder::Kind kind{};unsigned batchIndex=0;
            for(const auto& draw:nativeDraws){
                if(!bound||kind!=draw.kind||batchIndex!=draw.batch){
                    kind=draw.kind;batchIndex=draw.batch;bound=true;std::array<float,4> tint{};
                    uniforms.mvp=foregroundVP;
                    if(kind==ednativeorder::Kind::Actor){const auto& batch=nativeActors[batchIndex];tint=batch.tint;uniforms.mvp=batch.mvp;
                        SDL_BindGPUGraphicsPipeline(pass,nativeActorPipelines[batch.material.blend==ednativevisual::Blend::DestinationInverse?2:size_t(batch.material.blend)]);
                        binding={actorBuffers.find(batch.key)->second->vertices,0};textureBinding={actorTextures.find(batch.texture)->second,modelSampler};
                    }else if(kind==ednativeorder::Kind::Object){const auto& batch=objectBatches[batchIndex];
                        const auto material=ednativevisual::material(batch.mode,ednativevisual::specialObject(batch.type));tint=unpackTint(batch.tint);
                        if(material.white)tint[0]=tint[1]=tint[2]=1;tint[3]=material.alpha;
                        SDL_BindGPUGraphicsPipeline(pass,nativeObjectPipelines[size_t(material.blend)]);
                        binding={objectBuffer->vertices,0};textureBinding={batch.texture,modelSampler};
                    }else if(kind==ednativeorder::Kind::Shadow){tint={0,0,0,.20000000298023224f};
                        SDL_BindGPUGraphicsPipeline(pass,nativeShadowPipeline);binding={objectBuffer->vertices,0};textureBinding={uiWhite,modelSampler};
                    }else{const auto& batch=effects[batchIndex];tint=batch.tint;
                        SDL_BindGPUGraphicsPipeline(pass,batch.blend==edvfx::Blend::Alpha?particleAlphaPipeline:particleDepthPipeline);
                        binding={particleBuffer->vertices,0};textureBinding={particleTexture,modelSampler};
                    }
                    const float parameters[]={tint[0],tint[1],tint[2],tint[3],nearExclusive,farInclusive,0,0};
                    SDL_PushGPUFragmentUniformData(command,0,parameters,sizeof(parameters));
                    SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
                    SDL_BindGPUVertexBuffers(pass,0,&binding,1);SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
                }
                SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
            }
        };
        auto drawMask=[&](const SceneResources::DcMask& layer){
            SDL_BindGPUGraphicsPipeline(pass,dcMaskPipeline);
            uniforms.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            if(projectionShake)uniforms.mvp=edscreen::backgroundTransform(projectionShake);
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            const float nativeColor[]={.875f,.875f,.875f,1};SDL_PushGPUFragmentUniformData(command,0,nativeColor,sizeof(nativeColor));
            binding={layer.vertices,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            SDL_GPUTextureSamplerBinding masks[]={{backgroundBindings[0].texture,sampler},{layer.coverage,modelSampler}};
            SDL_BindGPUFragmentSamplers(pass,0,masks,2);SDL_DrawGPUPrimitives(pass,6,1,0,0);
        };
        // Native translucent actors/objects/shadows/particles share one far-to-near stream.
        // Foreground planes split it per fragment; transparent coverage never writes depth.
        if(resources->nativeBackground){
            std::vector<const SceneResources::DcMask*> ordered;for(const auto& layer:foregroundMasks)ordered.push_back(&layer);
            std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->depth>b->depth;});
            float farBoundary=1;for(const auto* layer:ordered){drawNative(layer->depth,farBoundary);drawMask(*layer);farBoundary=layer->depth;}
            drawNative(-1,farBoundary);drawParticles(-1,1,0);
        }else{
            for(const auto& layer:foregroundMasks)drawMask(layer);
            drawParticles(-1,1,-1);
        }
        uniforms.mvp=foregroundVP;SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
        if (hudVertices&&!(inventory&&inventory->open)) {
            SDL_BindGPUGraphicsPipeline(pass,hudPipeline);
            binding={hudBuffer,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            textureBinding={font,depthSampler}; SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
            uniforms.mvp={1,0,0,0,0,1,0,0,0,0,1,0,2.0f/640,-2.0f/480,0,1};
            const float shadow[]={0,0,0,0.9f},white[]={1,1,1,1};
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            SDL_PushGPUFragmentUniformData(command,0,shadow,sizeof(shadow));
            SDL_DrawGPUPrimitives(pass,hudVertices,1,0,0);
            uniforms.mvp[12]=uniforms.mvp[13]=0;
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            SDL_PushGPUFragmentUniformData(command,0,white,sizeof(white));
            SDL_DrawGPUPrimitives(pass,hudVertices,1,0,0);
        }
        if(inventory&&inventory->open&&inventoryBuffer) {
            uniforms.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            uniforms.light[0]=uniforms.light[1]=uniforms.light[2]=0; uniforms.light[3]=1;
            binding={inventoryBuffer,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            for(const auto& draw:inventoryDraws) {
                SDL_BindGPUGraphicsPipeline(pass,draw.texture<0?hudPipeline:iconPipeline);
                auto* image=draw.texture==-1?font:(draw.texture==-2?uiWhite:inventoryTextures[draw.texture]);
                textureBinding={image,depthSampler}; SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
                const auto& tint=draw.texture<0?draw.color:whiteTint;
                SDL_PushGPUFragmentUniformData(command,0,tint.data(),sizeof(tint));
                SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
            }
        }
        if(sourceGuiBuffer) {
            uniforms.mvp={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            binding={sourceGuiBuffer,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
            for(const auto& draw:sourceGuiDraws) {
                SDL_BindGPUGraphicsPipeline(pass,draw.texture<0?hudPipeline:iconPipeline);
                auto* image=draw.texture==-1?font:draw.texture==-2?uiWhite:guiTexture(uint32_t(draw.texture));
                textureBinding={image,depthSampler};SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
                SDL_PushGPUFragmentUniformData(command,0,draw.color.data(),sizeof(draw.color));
                SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
            }
        }
        if(screen.fade||(screen.bars&&screen.barHeight>0)) {
            SDL_BindGPUGraphicsPipeline(pass,hudPipeline);
            binding={quad,0};SDL_BindGPUVertexBuffers(pass,0,&binding,1);
            textureBinding={uiWhite,depthSampler};SDL_BindGPUFragmentSamplers(pass,0,&textureBinding,1);
            uniforms.light[0]=uniforms.light[1]=uniforms.light[2]=0;uniforms.light[3]=1;
            auto blackQuad=[&](const std::array<float,16>& matrix,float alpha) {
                uniforms.mvp=matrix;const float tint[]={0,0,0,alpha};
                SDL_PushGPUVertexUniformData(command,0,&uniforms,sizeof(uniforms));
                SDL_PushGPUFragmentUniformData(command,0,tint,sizeof(tint));
                SDL_DrawGPUPrimitives(pass,6,1,0,0);
            };
            if(screen.fade)blackQuad({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},float(screen.blackAlpha)/255.f);
            if(screen.bars&&screen.barHeight>0) {
                const float size=screen.barHeight/480.f;
                blackQuad({1,0,0,0,0,size,0,0,0,0,1,0,0,1-size,0,1},1);
                blackQuad({1,0,0,0,0,size,0,0,0,0,1,0,0,-1+size,0,1},1);
            }
        }
        SDL_EndGPURenderPass(pass);
        if(commonRaster)compositeCommonScene(command,width,height);
        const auto displayArea=sceneViewport(width,height);const SDL_Rect displayContent{int(displayArea.x),int(displayArea.y),int(displayArea.width),int(displayArea.height)};
        presentFrame(command,swapchain,width,height,displayContent,double(SDL_GetTicksNS())/1e9);
        auto* submitted=command;command=nullptr;checked(SDL_SubmitGPUCommandBuffer(submitted), "Submit frame");
        return true;
        } catch(...) {
            if(command){if(swapchainAcquired)SDL_SubmitGPUCommandBuffer(command);else SDL_CancelGPUCommandBuffer(command);}
            throw;
        }
    }
    void screenshot(const fs::path& path) {
        const unsigned pixelsPerRow = (targetWidth + 63) & ~63u;
        SDL_GPUTransferBufferCreateInfo info{}; info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; info.size = pixelsPerRow * targetHeight * 4;
        auto* transfer = checked(SDL_CreateGPUTransferBuffer(device, &info), "Create screenshot download");
        auto* command = checked(SDL_AcquireGPUCommandBuffer(device), "Acquire screenshot");
        auto* pass = checked(SDL_BeginGPUCopyPass(command), "Begin screenshot");
        SDL_GPUTextureRegion source{}; source.texture = lastPresentedTarget?lastPresentedTarget:target; source.w = targetWidth; source.h = targetHeight; source.d = 1;
        SDL_GPUTextureTransferInfo destination{transfer, 0, pixelsPerRow, targetHeight};
        SDL_DownloadFromGPUTexture(pass, &source, &destination); SDL_EndGPUCopyPass(pass);
        auto* fence = checked(SDL_SubmitGPUCommandBufferAndAcquireFence(command), "Submit screenshot");
        checked(SDL_WaitForGPUFences(device, true, &fence, 1), "Wait for screenshot");
        void* pixels = checked(SDL_MapGPUTransferBuffer(device, transfer, false), "Map screenshot");
        auto* surface = checked(SDL_CreateSurfaceFrom(targetWidth, targetHeight, SDL_PIXELFORMAT_RGBA32, pixels, pixelsPerRow * 4), "Create screenshot surface");
        const bool saved = SDL_SaveBMP(surface, path.u8string().c_str());
        SDL_DestroySurface(surface); SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUFence(device, fence); SDL_ReleaseGPUTransferBuffer(device, transfer);
        checked(saved, "Save screenshot");
        SDL_Log("Screenshot: %s", path.u8string().c_str());
    }
};

