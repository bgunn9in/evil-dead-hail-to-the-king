#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace savefile {
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
constexpr std::size_t DefaultLimit=16*1024*1024,HeaderBytes=24;
class Error:public std::runtime_error { public:using std::runtime_error::runtime_error; };
namespace detail {
inline std::uint32_t checksum(const Bytes& data,std::size_t start=0) {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=start;i<data.size();++i) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
    }
    return ~crc;
}
inline void append32(Bytes& bytes,std::uint32_t value) {
    for(unsigned i=0;i<4;++i) bytes.push_back(std::uint8_t(value>>(8*i)));
}
inline std::uint32_t word(const Bytes& bytes,std::size_t offset) {
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i) value|=std::uint32_t(bytes[offset+i])<<(8*i);
    return value;
}
inline void limit(std::size_t maximum) {
    if(!maximum||maximum>64*1024*1024) throw Error("Invalid save byte limit");
}
inline void filename(const std::string& name) {
    if(name.empty()||name.size()>64||name=="."||name==".."||name.back()=='.'||
       std::any_of(name.begin(),name.end(),[](unsigned char c){
           return !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-');
       })) throw Error("Save name must be a portable basename");
    std::string stem=name.substr(0,name.find('.'));
    for(auto& c:stem) if(c>='a'&&c<='z') c=char(c-'a'+'A');
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
       (stem.size()==4&&(stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT")&&stem[3]>='1'&&stem[3]<='9'))
        throw Error("Reserved save basename");
}
inline bool reparse(const fs::path& path) {
#ifdef _WIN32
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) throw Error("Cannot inspect save path");
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
inline fs::path directory(const fs::path& input) {
    if(input.empty()) throw Error("Save directory is empty");
    for(const auto& component:input) if(component=="..") throw Error("Save directory contains a parent traversal");
    const auto path=fs::absolute(input).lexically_normal();
    if(path==path.root_path()) throw Error("Save directory must be an application preference directory");
    auto current=path.root_path();
    for(const auto& component:path.relative_path()) {
        current/=component;
        if(reparse(current)||!fs::is_directory(current)) throw Error("Save directory contains a link or non-directory");
    }
    return path;
}
struct Handle {
#ifdef _WIN32
    HANDLE value=INVALID_HANDLE_VALUE;
    bool valid()const{return value!=INVALID_HANDLE_VALUE;}
    bool close(){const auto old=value;value=INVALID_HANDLE_VALUE;return old==INVALID_HANDLE_VALUE||CloseHandle(old)!=0;}
#else
    int value=-1;
    bool valid()const{return value>=0;}
    bool close(){const int old=value;value=-1;return old<0||::close(old)==0;}
#endif
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){close();}
};
inline Bytes rawRead(const fs::path& path,std::size_t maximum) {
    Handle file;
    std::uint64_t size=0;
#ifdef _WIN32
    file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER length{};
    if(!file.valid()||!GetFileInformationByHandle(file.value,&info)||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
       !GetFileSizeEx(file.value,&length)||length.QuadPart<0) throw Error("Cannot read a regular save file");
    size=std::uint64_t(length.QuadPart);
#else
    file.value=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat info{};
    if(!file.valid()||::fstat(file.value,&info)||!S_ISREG(info.st_mode)||info.st_size<0)
        throw Error("Cannot read a regular save file");
    size=std::uint64_t(info.st_size);
#endif
    if(size<HeaderBytes||size>maximum+HeaderBytes) throw Error("Save file exceeds its byte limit or is truncated");
    Bytes bytes(static_cast<std::size_t>(size));
    std::size_t offset=0;
    while(offset<bytes.size()) {
#ifdef _WIN32
        DWORD got=0;
        if(!ReadFile(file.value,bytes.data()+offset,DWORD(bytes.size()-offset),&got,nullptr)||!got)
            throw Error("Save read failed or was truncated");
#else
        const auto got=::read(file.value,bytes.data()+offset,bytes.size()-offset);
        if(got<0&&errno==EINTR) continue;
        if(got<=0) throw Error("Save read failed or was truncated");
#endif
        offset+=std::size_t(got);
    }
    return bytes;
}
inline void envelope(const Bytes& bytes,std::size_t maximum,bool verifyChecksum) {
    constexpr std::array<std::uint8_t,8> magic{'E','D','S','A','V','E','1',0};
    if(bytes.size()<HeaderBytes||!std::equal(magic.begin(),magic.end(),bytes.begin())||word(bytes,8)!=1||
       word(bytes,12)>maximum||word(bytes,12)!=bytes.size()-HeaderBytes||word(bytes,20)!=0)
        throw Error("Unknown or malformed save envelope");
    if(verifyChecksum&&word(bytes,16)!=checksum(bytes,HeaderBytes)) throw Error("Save checksum mismatch");
}
inline bool existing(const fs::path& target,std::size_t maximum) {
    std::error_code error;
    const auto status=fs::symlink_status(target,error);
    if(status.type()==fs::file_type::not_found) return false;
    if(error||!fs::is_regular_file(status)||reparse(target)) throw Error("Save target is not a regular file");
#ifdef _WIN32
    if(GetFileAttributesW(target.c_str())&FILE_ATTRIBUTE_READONLY) throw Error("Save target is read-only");
#else
    constexpr auto writable=fs::perms::owner_write|fs::perms::group_write|fs::perms::others_write;
    if((status.permissions()&writable)==fs::perms::none) throw Error("Save target is read-only");
#endif
    // A corrupt payload may be repaired; foreign formats are never overwritten.
    envelope(rawRead(target,maximum),maximum,false);
    return true;
}
inline Bytes encode(const Bytes& payload,std::size_t maximum) {
    limit(maximum);
    if(payload.size()>maximum) throw Error("Save payload exceeds its byte limit");
    Bytes bytes{'E','D','S','A','V','E','1',0};
    bytes.reserve(HeaderBytes+payload.size());
    append32(bytes,1); append32(bytes,std::uint32_t(payload.size()));
    append32(bytes,checksum(payload)); append32(bytes,0);
    bytes.insert(bytes.end(),payload.begin(),payload.end());
    return bytes;
}
}

inline Bytes read(const fs::path& preferenceDirectory,const std::string& name,std::size_t maximum=DefaultLimit) {
    detail::limit(maximum); detail::filename(name);
    const auto bytes=detail::rawRead(detail::directory(preferenceDirectory)/name,maximum);
    detail::envelope(bytes,maximum,true);
    return Bytes(bytes.begin()+HeaderBytes,bytes.end());
}

inline void atomicWrite(const fs::path& preferenceDirectory,const std::string& name,
                        const Bytes& payload,std::size_t maximum=DefaultLimit) {
    // Caller supplies its SDL_GetPrefPath directory, never a game/resource path.
    const auto bytes=detail::encode(payload,maximum);
    detail::filename(name);
    const auto directory=detail::directory(preferenceDirectory),target=directory/name;
    detail::existing(target,maximum);
    std::random_device random;
    fs::path temporary;
    detail::Handle file;
    bool owned=false;
    try {
        for(unsigned attempt=0;attempt<16&&!owned;++attempt) {
            temporary=directory/(name+".tmp."+std::to_string(random())+"-"+std::to_string(random())+
                "-"+std::to_string(random())+"-"+std::to_string(random()));
#ifdef _WIN32
            file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr);
            if(!file.valid()&&GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)
                throw Error("Cannot create a new save temporary file");
#else
            file.value=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(!file.valid()&&errno!=EEXIST) throw Error("Cannot create a new save temporary file");
#endif
            owned=file.valid();
        }
        if(!owned) throw Error("Cannot allocate an exclusively owned save temporary file");
        std::size_t offset=0;
        while(offset<bytes.size()) {
#ifdef _WIN32
            DWORD written=0;
            if(!WriteFile(file.value,bytes.data()+offset,DWORD(bytes.size()-offset),&written,nullptr)||!written)
                throw Error("Save temporary write failed");
#else
            const auto written=::write(file.value,bytes.data()+offset,bytes.size()-offset);
            if(written<0&&errno==EINTR) continue;
            if(written<=0) throw Error("Save temporary write failed");
#endif
            offset+=std::size_t(written);
        }
#ifdef _WIN32
        if(!FlushFileBuffers(file.value)) throw Error("Cannot flush save temporary file");
#else
        if(::fsync(file.value)) throw Error("Cannot flush save temporary file");
#endif
        if(!file.close()) throw Error("Cannot close save temporary file");
        detail::directory(directory);
        const bool replace=detail::existing(target,maximum);
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)))
            throw Error("Cannot atomically replace the previous save");
#else
        (void)replace;
        if(::rename(temporary.c_str(),target.c_str())) throw Error("Cannot atomically replace the previous save");
#endif
        owned=false;
#ifndef _WIN32
        // Rename is the commit point; a directory flush must not report rollback after commit.
        detail::Handle parent;
        parent.value=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(parent.valid()) (void)::fsync(parent.value);
#endif
    } catch(...) {
        file.close();
        if(owned) { std::error_code ignored; fs::remove(temporary,ignored); }
        throw;
    }
}
}
