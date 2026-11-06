#include "swvr/native_eye_resources.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message) {
    bool caught{};try{f();}catch(const std::exception&){caught=true;}require(caught,message);
}
template<class T>T get(const unsigned char* p,std::size_t at) {T v{};std::memcpy(&v,p+at,sizeof(v));return v;}
template<class T>void put(unsigned char* p,std::size_t at,T value) {std::memcpy(p+at,&value,sizeof(value));}
constexpr std::array<std::size_t,16> slots{0,8,0x10,0x18,0x20,0x28,0x30,0x38,0x40,0x78,0x80,0x88,0x90,0x98,0xb0,0xb8};
struct Fixture {
    using Resource=std::array<unsigned char,0x70>;
    std::array<unsigned char,0xf0> source{};
    std::array<unsigned char,0x200> manager{};
    std::vector<std::unique_ptr<Resource>> objects;
    std::vector<void**> source_arrays;
    std::unordered_set<void**> own_arrays;
    std::vector<void*> source_objects,own_objects;
    std::unordered_set<void*> released;
    unsigned init_calls{},destroy_calls{},resize_calls{},release_calls{};
    bool alias_source{},alias_array{},partial_failure{},throw_release{};
    Fixture(bool optional=true,bool pyramid=true) {
        auto p=source.data();put<std::uint32_t>(p,0xc0,1280);put<std::uint32_t>(p,0xc4,720);
        put<std::uint32_t>(p,0xd0,3);put<std::uint32_t>(p,0xd4,4);
        put<std::uint32_t>(p,0xe0,2);put<std::uint32_t>(p,0xe4,4);
        put<std::uint32_t>(p,0xcc,2);put<std::uint32_t>(p,0xdc,7);
        put<std::uint32_t>(p,0xe8,16);put<std::uint32_t>(p,0xec,1);
        p[0xc8]=optional?1:0;p[0xca]=pyramid?1:0;p[0xc9]=1;p[0xd8]=1;p[0xd9]=0;p[0xda]=1;
        populate(p,true);
    }
    ~Fixture(){for(auto array:source_arrays)delete[] array;for(auto array:own_arrays)delete[] array;}
    void* resource(bool original) {
        auto value=std::make_unique<Resource>();auto pointer=value->data();
        if(original)put<std::uint32_t>(pointer,0x28,static_cast<std::uint32_t>(objects.size()+1));
        objects.push_back(std::move(value));
        (original?source_objects:own_objects).push_back(pointer);return pointer;
    }
    void populate(unsigned char* p,bool original) {
        for(auto at:slots)if(at!=0x40 || p[0xc8])put<void*>(p,at,resource(original));
        put<std::uint32_t>(get<unsigned char*>(p,0),0x58,3);
        if(p[0xca])for(auto offset:{0x48,0x60}) {
            auto array=new void*[8]{};
            for(unsigned i=0;i<5;++i)array[(2+i)%8]=resource(original);
            put<void**>(p,offset,array);put<std::uint32_t>(p,offset+8,8);
            put<std::uint32_t>(p,offset+12,2);put<std::uint32_t>(p,offset+16,5);
            if(original)source_arrays.push_back(array);
            else own_arrays.insert(array);
        }
    }
    void realize() {std::uint32_t name=1000;for(auto resource:own_objects)put(static_cast<unsigned char*>(resource),0x28,name++);}
};
Fixture* current{};
void initialize(void* output,void* manager,std::uint32_t width,std::uint32_t height,
    std::uint32_t p5,std::uint32_t p6,std::uint32_t p7,std::uint32_t p8,
    std::uint8_t p9,std::uint8_t p10,std::uint8_t p11,std::uint32_t p12,std::uint32_t p13,
    std::uint8_t p14,std::uint8_t p15,std::uint32_t p16,std::uint32_t p17,std::uint8_t p18) {
    auto& f=*current;++f.init_calls;require(manager==f.manager.data(),"manager identity");
    auto p=static_cast<unsigned char*>(output);auto src=f.source.data();
    require(reinterpret_cast<std::uintptr_t>(p)%16==0,"package alignment");
    require(get<void*>(p,0x48)==nullptr && get<void*>(p,0x60)==nullptr,"fresh containers");
    require(get<std::uint32_t>(p,0xe0)==2 && get<std::uint32_t>(p,0xe4)==2 &&
        get<std::uint32_t>(p,0xe8)==16 && get<std::uint32_t>(p,0xec)==1,"constructor defaults");
    require(width==get<std::uint32_t>(src,0xc0) && height==get<std::uint32_t>(src,0xc4) &&
        p5==3 && p6==4 && p7==2 && p8==4,"first ABI arguments");
    require(p9==src[0xc8] && p10==src[0xca] && p11==src[0xc9] && p12==get<std::uint32_t>(src,0xcc) && p13==7 &&
        p14==1 && p15==0 && p16==16 && p17==1 && p18==1,"remaining ABI arguments");
    // Mimic scalar writes, never copy source resource/container ownership.
    std::memcpy(p+0xc0,src+0xc0,0x30);
    if(f.partial_failure && f.init_calls==2) {put<void*>(p,0,f.resource(false));throw std::runtime_error("partial initialization");}
    f.populate(p,false);
    if(f.alias_source) put<void*>(p,0,f.source_objects[0]);
    if(f.alias_array) {
        const auto old=get<void**>(p,0x48);f.own_arrays.erase(old);delete[] old;
        put<void**>(p,0x48,get<void**>(src,0x48));
    }
}
void release(void* list,void* resource);
void resize(void* output,void* manager,std::uint32_t width,std::uint32_t height,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,
    std::uint8_t,std::uint8_t,std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t,std::uint8_t,
    std::uint32_t,std::uint32_t,std::uint8_t) {
    auto& f=*current;++f.resize_calls;
    require(manager==f.manager.data() && width==1280 && height==720,"same-size reconfigure ABI");
    auto p=static_cast<unsigned char*>(output);
    const auto previous=get<void*>(p,0);
    release(f.manager.data()+0xc0,previous);
    const auto replacement=f.resource(false);put<void*>(p,0,replacement);
    put<std::uint32_t>(static_cast<unsigned char*>(replacement),0x58,2);
}
void release(void* list,void* resource) {
    auto& f=*current;require(list==f.manager.data()+0xc0,"pending-list offset");require(resource!=nullptr,"no null release");
    for(auto source:f.source_objects)require(resource!=source,"never release source object");
    require(f.released.insert(resource).second,"never double release");++f.release_calls;
    if(f.throw_release) {f.throw_release=false;throw std::runtime_error("release uncertainty");}
}
void destroy(void* package) {
    auto& f=*current;auto p=static_cast<unsigned char*>(package);++f.destroy_calls;
    for(auto offset:{0x48,0x60}) {
        auto array=get<void**>(p,offset);
        for(auto source:f.source_arrays)require(array!=source,"never free source array");
        f.own_arrays.erase(array);
        delete[] array;std::memset(p+offset,0,20);
    }
}
swvr::NativeResourceApi api{initialize,resize,release,destroy};
void normal(bool optional,bool pyramid) {
    Fixture fixture(optional,pyramid);current=&fixture;swvr::NativeEyeResources targets;
    targets.create(fixture.source,fixture.manager.data(),api);
    require(targets.created() && !targets.failed() && fixture.init_calls==2 && fixture.resize_calls==0,"create two independent packages");
    require(targets.package(0)!=targets.package(1) && targets.package(0)!=fixture.source.data(),"separate CPU packages");
    require(!targets.ready(fixture.source),"pending FBOs not ready");
    fixture.realize();require(targets.ready(fixture.source),"realized FBOs ready");
    rejects([&]{targets.package(2);},"eye index bounds");
    targets.close();targets.close();
    require(!targets.created() && fixture.destroy_calls==2 && fixture.release_calls==fixture.own_objects.size(),"all owned references and containers cleaned once");
}
}
int main() {
    try {
        normal(true,true);normal(false,false);
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;
            t.create(f.source,f.manager.data(),api);f.realize();
            require(swvr::native_target_has_stereo_extent(f.source),"normal source supports stereo");
            const auto left=t.package(0),right=t.package(1);
            for(const auto dims:std::array<std::array<std::uint32_t,2>,3>{{{1,1},{1,720},{1280,1}}}) {
                put(f.source.data(),0xc0,dims[0]);put(f.source.data(),0xc4,dims[1]);
                require(!swvr::native_target_has_stereo_extent(f.source),"transient tiny target must suspend stereo before split or resource recreation");
            }
            put<std::uint32_t>(f.source.data(),0xc0,1280);put<std::uint32_t>(f.source.data(),0xc4,720);
            require(swvr::native_target_has_stereo_extent(f.source) && t.matches_configuration(f.source) && t.ready(f.source),"restored source resumes the existing resource generation");
            require(t.package(0)==left && t.package(1)==right && f.init_calls==2 && !f.release_calls && !f.destroy_calls,"suspension must retain both owned eye packages");
            t.close();require(f.release_calls==f.own_objects.size() && f.destroy_calls==2,"close suspended/resumed packages exactly once");
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;
            put<std::uint32_t>(f.source.data(),0xe0,3);
            rejects([&]{t.create(f.source,f.manager.data(),api);},"nondefault E0 rejected");
            require(f.init_calls==0 && !t.created(),"validate before native mutation");
            rejects([&]{t.create(std::span(f.source).first(30),f.manager.data(),api);},"source size bounds");
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;
            put<std::uint32_t>(get<unsigned char*>(f.source.data(),0),0x58,4);
            rejects([&]{t.create(f.source,f.manager.data(),api);},"four actual attachments rejected");
            require(f.init_calls==0,"reject unsupported attachment layout before init");
            put<std::uint32_t>(get<unsigned char*>(f.source.data(),0),0x58,3);
            put<std::uint32_t>(f.source.data(),0xcc,0);f.source[0xc9]=0;
            t.create(f.source,f.manager.data(),api);
            require(f.resize_calls==0,"match actual three-attachment source without unnecessary resize");t.close();
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;
            put<std::uint32_t>(get<unsigned char*>(f.source.data(),0),0x58,2);
            put<std::uint32_t>(f.source.data(),0xcc,0);f.source[0xc9]=0;
            t.create(f.source,f.manager.data(),api);
            require(f.resize_calls==2 && f.release_calls==2,"reconfigure releases old G-buffer once per eye");
            f.realize();require(t.ready(f.source),"two-attachment packages realized");t.close();
            require(f.release_calls==f.own_objects.size(),"reconfigured resources released exactly once");
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;t.create(f.source,f.manager.data(),api);f.realize();
            put<std::uint32_t>(static_cast<unsigned char*>(f.own_objects[0]),0x28,1);
            rejects([&]{t.ready(f.source);},"source FBO alias rejected");require(t.failed(),"failed closed");t.close();
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;t.create(f.source,f.manager.data(),api);
            put<std::uint32_t>(f.source.data(),0xc0,1024);
            require(!t.matches_configuration(f.source) && !t.failed(),"configuration change can be handled before readiness check");
            rejects([&]{t.ready(f.source);},"source resize rejected");t.close();
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;t.create(f.source,f.manager.data(),api);
            require(t.matches_configuration(f.source),"initial config matches");
            put<std::uint32_t>(f.source.data(),0xc0,1024);
            require(!t.matches_configuration(f.source),"resize detected");
            t.close();t.create(f.source,f.manager.data(),api);f.realize();
            require(t.matches_configuration(f.source) && t.ready(f.source),"recreated targets match changed source");
            t.close();require(f.release_calls==f.own_objects.size() && f.destroy_calls==4,"both generations released once");
        }
        {
            Fixture f;current=&f;f.alias_source=true;swvr::NativeEyeResources t;
            rejects([&]{t.create(f.source,f.manager.data(),api);},"source resource alias rejected");
            t.close();require(t.failed() && f.release_calls+1==f.own_objects.size(),"aliased source protected; overwritten unknown object not claimed");
        }
        {
            Fixture f;current=&f;f.partial_failure=true;swvr::NativeEyeResources t;
            rejects([&]{t.create(f.source,f.manager.data(),api);},"partial init reported");t.close();
            require(f.release_calls==f.own_objects.size() && f.destroy_calls==2,"partial graph cleaned");
        }
        {
            Fixture f;current=&f;f.alias_array=true;swvr::NativeEyeResources t;
            rejects([&]{t.create(f.source,f.manager.data(),api);},"source container alias rejected");t.close();
            require(f.destroy_calls==0,"uncertain container ownership never freed");
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;
            put<std::uint32_t>(f.source.data(),0x58,6);
            rejects([&]{t.create(f.source,f.manager.data(),api);},"container count beyond measured five rejected");
            require(f.init_calls==0,"container validation before native calls");
        }
        {
            Fixture f;current=&f;swvr::NativeEyeResources t;t.create(f.source,f.manager.data(),api);f.throw_release=true;
            rejects([&]{t.close();},"uncertain release reported");t.close();
            require(f.release_calls==f.own_objects.size(),"throwing release never retried");
        }
        {
            Fixture f(false,false);current=&f;
            {swvr::NativeEyeResources t;t.create(f.source,f.manager.data(),api);}
            require(f.release_calls==0 && f.destroy_calls==0,"destructor has no native calls");
        }
        std::cout<<"Native eye package ownership tests passed (fake native functions, no game).\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
