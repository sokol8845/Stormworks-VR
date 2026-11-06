#include "swvr/head_mask_sources.hpp"
#include "swvr/head_mask_shader.hpp"
#include <cstring>

namespace swvr {
namespace {
constexpr unsigned fragment_shader=0x8B30,shader_type=0x8B4F,compile_status=0x8B81;
constexpr std::size_t max_source=16*1024*1024,max_backups=256,max_backup_bytes=64*1024*1024;
}
bool HeadMaskShaderSources::submit(std::uintptr_t context,unsigned shader,int count,const char* const* strings,
                                   const int* lengths,const HeadMaskShaderApi& api,TraceNote note) {
    if(count<=0 || count>1024 || !strings || !api.source) return false;
    int type{};if(api.get_shader) api.get_shader(shader,shader_type,&type);
    if(type==static_cast<int>(fragment_shader)) {
        std::string original;
        bool supported=true;
        for(int i=0;i<count;++i) {
            if(!strings[i]) {supported=false;break;}
            const auto length=(lengths && lengths[i]>=0)?static_cast<std::size_t>(lengths[i]):strnlen_s(strings[i],max_source);
            if(length>=max_source || original.size()+length>max_source) {supported=false;break;}
            original.append(strings[i],length);
        }
        if(supported) {
            auto patch=patch_head_mask_fragment(original);
            if(patch.patched) {
                bool backed_up{};
                {
                    std::lock_guard lock(mutex_);
                    const Key key{context,shader};
                    if(backups_.size()<max_backups && bytes_+original.size()<=max_backup_bytes) {
                        auto [entry,inserted]=backups_.try_emplace(key,original);
                        if(!inserted) {
                            auto replacement=original;
                            bytes_-=entry->second.size();entry->second.swap(replacement);
                        }
                        bytes_+=original.size();backed_up=true;
                    }
                }
                if(backed_up) {
                    const auto data=patch.source.c_str();const auto size=static_cast<int>(patch.source.size());
                    api.source(shader,1,&data,&size);++patched_;
                    if(note) note("head_mask_shader","known_fragment_patched_in_memory");
                    return true;
                }
            }
        }
    }
    forget(context,shader);
    return false;
}
void HeadMaskShaderSources::compiled(std::uintptr_t context,unsigned shader,const HeadMaskShaderApi& api,TraceNote note) {
    std::string original;
    {
        std::lock_guard lock(mutex_);
        const auto found=backups_.find({context,shader});
        if(found==backups_.end()) return;
        original=found->second;
    }
    int ok{};if(api.get_shader) api.get_shader(shader,compile_status,&ok);
    if(!ok && api.source && api.compile) {
        const auto data=original.c_str();const auto size=static_cast<int>(original.size());
        api.source(shader,1,&data,&size);api.compile(shader);
        ++fallbacks_;if(note) note("head_mask_shader","compile_failed_restored_original");
    }
}
void HeadMaskShaderSources::forget(std::uintptr_t context,unsigned shader) {
    std::lock_guard lock(mutex_);
    const auto found=backups_.find({context,shader});
    if(found!=backups_.end()) {bytes_-=found->second.size();backups_.erase(found);}
}
void HeadMaskShaderSources::context_destroyed(std::uintptr_t context) {
    std::lock_guard lock(mutex_);
    for(auto it=backups_.begin();it!=backups_.end();) {
        if(it->first.first==context) {bytes_-=it->second.size();it=backups_.erase(it);}
        else ++it;
    }
}
unsigned HeadMaskShaderSources::patched() const noexcept {return patched_.load();}
unsigned HeadMaskShaderSources::fallbacks() const noexcept {return fallbacks_.load();}
std::size_t HeadMaskShaderSources::backups() const {std::lock_guard lock(mutex_);return backups_.size();}
}
