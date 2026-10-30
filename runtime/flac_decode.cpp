#include "flac_decode.h"
#include <fstream>
#include <memory>
#include <stdexcept>
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_IMPLEMENTATION
#include "third_party/dr_flac.h"

namespace edflac {
std::vector<uint8_t> readMusic(const std::filesystem::path& path,size_t expectedPcmBytes){
    if(!expectedPcmBytes||expectedPcmBytes%4||expectedPcmBytes>16*1024*1024)throw std::runtime_error("Invalid original music PCM length");
    std::ifstream input(path,std::ios::binary|std::ios::ate);const auto size=input.tellg();
    if(!input||size<42||size>32*1024*1024)throw std::runtime_error("Invalid bounded FLAC file");
    std::vector<uint8_t> encoded(static_cast<size_t>(size));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(encoded.data()),std::streamsize(encoded.size())))throw std::runtime_error("Truncated FLAC file");
    struct Close{void operator()(drflac* value)const{drflac_close(value);}};
    std::unique_ptr<drflac,Close> decoder(drflac_open_memory(encoded.data(),encoded.size(),nullptr));
    if(!decoder||decoder->channels!=2||decoder->sampleRate!=44100||decoder->bitsPerSample!=16||decoder->totalPCMFrameCount!=expectedPcmBytes/4)
        throw std::runtime_error("FLAC changed original music format/duration");
    std::vector<uint8_t> pcm(expectedPcmBytes);
    if(drflac_read_pcm_frames_s16(decoder.get(),expectedPcmBytes/4,reinterpret_cast<drflac_int16*>(pcm.data()))!=expectedPcmBytes/4)
        throw std::runtime_error("Incomplete FLAC PCM decode");
    drflac_int16 extra[2];if(drflac_read_pcm_frames_s16(decoder.get(),1,extra))throw std::runtime_error("Excess FLAC PCM frames");
    return pcm;
}
}
