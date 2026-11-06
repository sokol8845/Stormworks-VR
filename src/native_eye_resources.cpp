#include "swvr/native_eye_resources.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <algorithm>

namespace swvr {
static_assert(sizeof(void*)==8,"Native resource layout requires a 64-bit process");
namespace {
template<class T>T read(const unsigned char* memory,std::size_t offset) {
    T value{};std::memcpy(&value,memory+offset,sizeof(value));return value;
}
template<class T>void write(unsigned char* memory,std::size_t offset,T value) {
    std::memcpy(memory+offset,&value,sizeof(value));
}
constexpr std::array<std::size_t,16> resource_offsets{
    0,8,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x78,0x80,0x88,0x90,0x98,0xb0,0xb8};
struct Config {
    std::uint32_t width{},height{},p5{},p6{},p7{},p8{},p12{},p13{},p16{},p17{};
    std::uint8_t p9{},p10{},p11{},p14{},p15{},p18{};
    bool operator==(const Config&) const=default;
};
Config config(std::span<const unsigned char> memory) {
    if(memory.size()!=native_target_package_size) throw std::invalid_argument("Native package must have exactly 0xF0 bytes");
    const auto p=memory.data();
    Config c{read<std::uint32_t>(p,0xc0),read<std::uint32_t>(p,0xc4),
        read<std::uint32_t>(p,0xd0),read<std::uint32_t>(p,0xd4),
        read<std::uint32_t>(p,0xe0),read<std::uint32_t>(p,0xe4),
        read<std::uint32_t>(p,0xcc),read<std::uint32_t>(p,0xdc),
        read<std::uint32_t>(p,0xe8),read<std::uint32_t>(p,0xec),
        p[0xc8],p[0xca],p[0xc9],p[0xd8],p[0xd9],p[0xda]};
    if(!c.width || !c.height || c.width>16384 || c.height>16384 || !c.p8 || c.p8>16384)
        throw std::invalid_argument("Invalid native target dimensions or divisor");
    if(c.p7!=2 || c.p16!=16)
        throw std::invalid_argument("Nondefault native E0/E8 requires a separately verified resize path");
    return c;
}
struct Graph {
    std::vector<void*> resources;
    std::array<void*,2> arrays{};
};
Graph graph(const unsigned char* p,bool complete,const Config& c) {
    Graph result;
    for(const auto offset:resource_offsets) {
        const auto resource=read<void*>(p,offset);
        if(resource) result.resources.push_back(resource);
        else if(complete && (offset!=0x40 || c.p9))
            throw std::runtime_error("Native target package has a missing required resource");
    }
    for(std::size_t container=0;container<2;++container) {
        const auto offset=container?0x60:0x48;
        const auto array=read<unsigned char*>(p,offset);
        const auto capacity=read<std::uint32_t>(p,offset+8);
        const auto begin=read<std::uint32_t>(p,offset+12);
        const auto count=read<std::uint32_t>(p,offset+16);
        if(capacity>64 || count>5 || count>capacity ||
           (capacity && (!array || begin>=capacity)) || (!capacity && (array || begin || count)))
            throw std::runtime_error("Native target container metadata is outside measured bounds");
        if(complete && c.p10 && count!=5)
            throw std::runtime_error("Native target pyramid is incomplete");
        result.arrays[container]=array;
        for(std::uint32_t i=0;i<count;++i) {
            const auto resource=read<void*>(array,((begin+i)%capacity)*sizeof(void*));
            if(resource) result.resources.push_back(resource);
            else if(complete && c.p10) throw std::runtime_error("Native target pyramid has a null resource");
        }
    }
    return result;
}
void invoke(NativePackageInitialize initialize,void* package,void* manager,const Config& c) {
    initialize(package,manager,c.width,c.height,c.p5,c.p6,c.p7,c.p8,
        c.p9,c.p10,c.p11,c.p12,c.p13,c.p14,c.p15,c.p16,c.p17,c.p18);
}
std::uint32_t attachment_count(const unsigned char* package) {
    const auto target=read<const unsigned char*>(package,0);
    const auto count=target?read<std::uint32_t>(target,0x58):0;
    if(count!=2 && count!=3) throw std::runtime_error("Unsupported native G-buffer attachment count");
    return count;
}
}
bool native_target_has_stereo_extent(std::span<const unsigned char> source) {
    const auto settings=config(source);
    return settings.width>1 && settings.height>1;
}
struct NativeEyeResources::Impl {
    struct alignas(16) Package {std::array<unsigned char,native_target_package_size> bytes{};};
    std::array<Package,2> packages{};
    std::array<bool,2> started{},containers_safe{},destroyed{};
    NativeResourceApi api{};void* manager{};Config settings{};
    bool active{},poisoned{};
    std::uint32_t attachments{};
    std::unordered_set<void*> source_resources,source_arrays;
    std::vector<void*> owned;
    std::size_t released{};
    // Collect only distinct resources not present in the source. Even on a
    // failed initializer, source ownership is never inferred from a field copy.
    void collect() {
        bool bad{};
        std::unordered_set<void*> seen;
        std::unordered_set<void*> arrays;
        for(unsigned eye=0;eye<2;++eye) if(started[eye] && !destroyed[eye]) {
            const auto g=graph(packages[eye].bytes.data(),false,settings);
            containers_safe[eye]=true;
            for(auto array:g.arrays) if(array) {
                if(source_arrays.contains(array) || !arrays.insert(array).second) {
                    containers_safe[eye]=false;bad=true;
                    // A shared array must not be freed by either package.
                    for(unsigned previous=0;previous<eye;++previous) containers_safe[previous]=false;
                }
            }
            for(auto resource:g.resources) {
                if(source_resources.contains(resource)) {bad=true;continue;}
                if(!seen.insert(resource).second) {bad=true;continue;}
                bool recorded{};for(auto known:owned) if(known==resource) {recorded=true;break;}
                if(!recorded) owned.push_back(resource);
            }
        }
        if(bad) throw std::runtime_error("Native eye targets alias source or each other");
    }
};
NativeEyeResources::NativeEyeResources():impl_(std::make_unique<Impl>()) {}
NativeEyeResources::~NativeEyeResources()=default;
void NativeEyeResources::create(std::span<const unsigned char> source,void* manager,NativeResourceApi api) {
    auto& s=*impl_;
    if(s.active) throw std::logic_error("Native eye resources already created; close first");
    if(!manager || !api.initialize || !api.resize || !api.release || !api.destroy_containers)
        throw std::invalid_argument("Native resource manager and callbacks are required");
    const auto c=config(source);const auto original=graph(source.data(),true,c);
    const auto attachments=attachment_count(source.data());
    if(attachments==2 && (c.p12==2 || c.p11))
        throw std::runtime_error("Native attachment configuration is inconsistent");
    auto fresh=std::make_unique<Impl>();fresh->settings=c;fresh->manager=manager;fresh->api=api;
    fresh->attachments=attachments;
    fresh->source_resources.insert(original.resources.begin(),original.resources.end());
    for(auto array:original.arrays) if(array) fresh->source_arrays.insert(array);
    impl_=std::move(fresh);auto& state=*impl_;state.active=true;
    bool reconfiguring{};
    try {
        for(unsigned eye=0;eye<2;++eye) {
            auto& data=state.packages[eye].bytes;
            write<std::uint32_t>(data.data(),0xe0,2);write<std::uint32_t>(data.data(),0xe4,2);
            write<std::uint32_t>(data.data(),0xe8,16);write<std::uint32_t>(data.data(),0xec,1);
            state.started[eye]=true;
            invoke(api.initialize,data.data(),manager,c);
            state.collect();
            (void)graph(data.data(),true,c);
            if(attachment_count(data.data())!=3) throw std::runtime_error("Native init did not create three attachments");
            if(attachments==2) {
                // With identical dimensions/E0/E4/settings, measured resize
                // changes only this 3-attachment G-buffer to the 2-attachment
                // variant and releases the old reference itself. Do not release
                // that reference again when this helper closes.
                const auto replaced=read<void*>(data.data(),0);
                std::erase(state.owned,replaced);
                reconfiguring=true;
                invoke(api.resize,data.data(),manager,c);
                reconfiguring=false;
                state.collect();
                if(attachment_count(data.data())!=2)
                    throw std::runtime_error("Native resize did not produce two attachments");
            }
        }
    } catch(...) {
        state.poisoned=true;
        // A partially created native graph can be cleaned explicitly if its
        // metadata is still valid. Never replace the original error.
        // A throwing reconfigure may have released its previous G-buffer but
        // not replaced the pointer yet. Do not reclaim uncertain ownership.
        if(!reconfiguring) try {state.collect();} catch(...) {}
        throw;
    }
}
bool NativeEyeResources::matches_configuration(std::span<const unsigned char> source) const {
    const auto& s=*impl_;
    if(!s.active || s.poisoned) throw std::logic_error("Native eye resources unavailable");
    return config(source)==s.settings && attachment_count(source.data())==s.attachments;
}
bool NativeEyeResources::ready(std::span<const unsigned char> source) {
    auto& s=*impl_;
    if(!s.active || s.poisoned) throw std::logic_error("Native eye resources unavailable");
    try {
        if(config(source)!=s.settings) throw std::runtime_error("Native source target configuration changed");
        const auto original=graph(source.data(),true,s.settings);
        if(attachment_count(source.data())!=s.attachments) throw std::runtime_error("Native source attachments changed");
        // A source rebuild is not an ownership transfer; protect its new objects.
        for(auto resource:original.resources) s.source_resources.insert(resource);
        for(auto array:original.arrays) if(array) s.source_arrays.insert(array);
        s.collect();
        std::unordered_set<std::uint32_t> framebuffer_names;
        for(auto resource:original.resources) {
            const auto name=read<std::uint32_t>(static_cast<unsigned char*>(resource),0x28);
            if(name) framebuffer_names.insert(name);
        }
        bool complete=true;
        for(unsigned eye=0;eye<2;++eye) {
            const auto g=graph(s.packages[eye].bytes.data(),true,s.settings);
            if(attachment_count(s.packages[eye].bytes.data())!=s.attachments)
                throw std::runtime_error("Native eye attachment layout changed");
            for(auto resource:g.resources) {
                const auto name=read<std::uint32_t>(static_cast<unsigned char*>(resource),0x28);
                if(!name) complete=false;
                else if(!framebuffer_names.insert(name).second)
                    throw std::runtime_error("Native eye framebuffer aliases another target");
            }
        }
        return complete;
    } catch(...) {s.poisoned=true;throw;}
}
void* NativeEyeResources::package(unsigned eye) {
    if(eye>=2) throw std::out_of_range("Native eye index");
    if(!impl_->active || impl_->poisoned) throw std::logic_error("Native eye resources unavailable");
    return impl_->packages[eye].bytes.data();
}
bool NativeEyeResources::created() const noexcept {return impl_->active;}
bool NativeEyeResources::failed() const noexcept {return impl_->poisoned;}
void NativeEyeResources::close() {
    auto& s=*impl_;if(!s.active)return;
    try {
        while(s.released<s.owned.size()) {
            const auto resource=s.owned[s.released++];
            if(!s.source_resources.contains(resource))
                s.api.release(static_cast<unsigned char*>(s.manager)+0xc0,resource);
        }
        for(unsigned eye=0;eye<2;++eye) if(s.started[eye] && !s.destroyed[eye] && s.containers_safe[eye]) {
            s.destroyed[eye]=true;
            s.api.destroy_containers(s.packages[eye].bytes.data());
        }
        s.active=false;
    } catch(...) {s.poisoned=true;throw;}
}
}
