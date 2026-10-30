// The source script creates the menu actor pose; there is no authored spawn marker.
Scene loadMenuScene(const fs::path& directory) {
    Scene scene;scene.menuContext=true;scene.episode=true;
    std::ifstream input(directory/"menu.txt");std::string magic,extra;unsigned cameras=0;
    input>>magic>>scene.id>>cameras;
    if(!input||magic!="EDMENU1"||(scene.id!="aa01"&&scene.id!="bb01")||!cameras||cameras>256||(input>>extra))
        throw std::runtime_error("Invalid original menu context");
    const auto native=readFile(directory/"scene.native");scene.cameraTracks=edcamera::readSourceTracks(native);
    if(scene.cameraTracks.size()!=cameras)throw std::runtime_error("Menu camera count differs from source");
    scene.cameraFrames=edcamera::readCameraFrames(readFile(directory/"camera-frames.bin",16*1024*1024),scene.cameraTracks);
    scene.cameras.resize(cameras);scene.cameraFrame.assign(cameras,0);
    for(unsigned camera=0;camera<cameras;++camera)setCameraFrame(scene,camera,0);
    loadFloor(scene,directory);loadRules(scene,directory);loadNativeScene(scene,directory,cameras);
    if(!scene.scripting.markers.empty()||!scene.nativePortals.empty()||!scene.triggers.empty())
        throw std::runtime_error("Unexpected walking data in original menu context");
    scene.portalData.scene=scene.id;
    scene.portalData.gridOrigin={scene.triggerGrid.minX,scene.triggerGrid.minZ};
    scene.portalData.gridExtent={scene.triggerGrid.extentX,scene.triggerGrid.extentZ};
    return scene;
}
