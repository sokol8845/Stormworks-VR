#include "native_hand_buttons_fixture.hpp"
#include "swvr/native_hand_buttons.hpp"
#include "swvr/native_button_retirement.hpp"
#include "swvr/native_button_request.hpp"
#include "swvr/hand_event_registry.hpp"
#include "swvr/comfort.hpp"
#include <MinHook.h>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

using namespace swvr;
using namespace swvr::hand_buttons_fixture;
namespace {
using U=std::uintptr_t;
using Selection=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,const int*);
using Origin=void*(*)(void*,double*,std::uint8_t,std::uint8_t);
using Loop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using ShortLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*);
using Dispatch=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*);
using Action=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,int,int);
using Pop=bool(*)(void*,void**);
using Dtor=void*(*)(void*,std::uint32_t);
using Clear=void(*)(void*);
using Delete=void(*)(void*,std::size_t);
using Serializer=void(*)(void*,void*);
using ServerDispatch=void(*)(void*,void*,void*,void*,void*);
using Deactivate=void(*)(void*,std::uint64_t,void*,const int*);
using Replace=U(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t);
struct State;
State* current{};
struct Hook {U rva{};void* detour{};void* original{};};
enum class Allocation { own_key,foreign_key,foreign_pause,own_request,foreign_request,parent };
struct NativeAllocation {Allocation kind{};std::size_t bytes{};};
struct Buffer {bool temporary{},foreign{};std::size_t bytes{};};
struct State {
    Image image;
    HandInteractionBridge bridge{HandInteractionOptions{.both_hands=true}};
    unsigned test_hand=1,left_presses{};
    unsigned observer_phase{},observer_begins{},observer_ends{};
    std::vector<unsigned char> owner=std::vector<unsigned char>(0xcf000),simulation=std::vector<unsigned char>(0x4000),
        data=std::vector<unsigned char>(0x1000),pause=std::vector<unsigned char>(0x100),
        button=std::vector<unsigned char>(0x300),definition=std::vector<unsigned char>(0xdb0),
        section=std::vector<unsigned char>(0x260),vehicle=std::vector<unsigned char>(0x180);
    std::array<unsigned char,64> opaque{};
    std::vector<unsigned char> world=std::vector<unsigned char>(0xbba00);
    std::array<unsigned char,0x98> actor_node{};
    U actor_holder{};
    std::array<U,3> section_slots{};
    std::vector<Hook> hooks;
    std::unordered_map<void*,NativeAllocation> allocations;
    std::unordered_map<void*,Buffer> buffers;
    std::deque<void*> events;
    std::vector<NativeButtonRequestLayout*> requests;
    std::vector<std::string> delivered,delegated;
    std::array<double,3> native_head{1000000000.125,3,2000000000.25};
    std::array<float,3> native_forward{-.0f,.6f,.8f};
    std::array<float,3> saved_hit{-.0f,1.25f,-2.5f},hover_hit=saved_hit;
    std::array<std::int32_t,3> grid{3,-4,5};
    std::string payload{"vr\0release",10};
    HandInteractionSnapshot selected_sample{};
    U select_call{},origin_call{},loop_call{},short_loop_call{},dispatch_call{},long_dispatch_call{},action_call{};
    U pop_first{},pop_more{},long_pop_first{},long_pop_more{},serializer_call{},server_call{},write_clear_call{},typed_clear_call{};
    U inner_clear_call{},raw_delete_call{},parent_delete_call{};
    U replace_call{},deactivate_call{},source_reset_call{},source_destroy_call{};bool replace_in_loop{};int next_character{};
    unsigned transitions{};U held_at_transition{};
    std::uint64_t frames{};
    unsigned alloc_keys{},free_keys{},alloc_requests{},free_requests{},alloc_payloads{},free_payloads{};
    unsigned press_actions{},phase0_actions{},short_clicks{},foreign_actions{},parent_serializers{},request_serializers{},server_dispatches{},getters{};
    unsigned native_dispatches{},ray_uses{},unexpected_hooks{},native_helper_calls{};
    int character{},logic_counter{};
    bool expected_skip{},envelope_active{},consume_events=true,head_correction{};
    Ring* outer_live{};
    U parent_live{};
    State(){require(!current,"Nested integration fixture");current=this;initialize_objects();}
    ~State() {
        // Failure cleanup belongs to the fixture only; successful paths assert
        // these maps empty before reaching this destructor.
        for(const auto& [p,_]:buffers)::operator delete[](p);
        for(const auto& [p,_]:allocations)VirtualFree(p,0,MEM_RELEASE);
        current=nullptr;
    }
    U owner_ptr()const{return reinterpret_cast<U>(owner.data());}
    U controller()const{return reinterpret_cast<U>(simulation.data())+0x38;}
    U data_ptr()const{return reinterpret_cast<U>(data.data());}
    U client()const{return data_ptr()+0x280;}
    U button_ptr()const{return reinterpret_cast<U>(button.data());}
    void initialize_objects() {
        put(owner.data(),0,image.base()+0xb0f4a0);put(owner.data(),0xce190,reinterpret_cast<U>(simulation.data()));
        put(owner.data(),0x350,reinterpret_cast<U>(simulation.data()));put(owner.data(),0xce150,reinterpret_cast<U>(pause.data()));
        put(simulation.data(),0,image.base()+0xb1a988);put(reinterpret_cast<void*>(controller()),0,reinterpret_cast<U>(world.data()));
        actor_holder=reinterpret_cast<U>(&character);
        put(world.data(),0xbb9f8,reinterpret_cast<U>(actor_node.data()));put(actor_node.data(),0,int{4});
        put(actor_node.data(),0x90,reinterpret_cast<U>(&actor_holder));
        put(simulation.data(),0x3010,std::uint8_t{1});put(data.data(),0,image.base()+0xb17cc0);
        put(reinterpret_cast<void*>(client()),0x338,std::uint16_t{7});
        put(pause.data(),0,image.base()+0xb07c10);put(button.data(),0,image.base()+0xb75658);
        put(button.data(),0x58,reinterpret_cast<U>(definition.data()));put(definition.data(),0xda4,std::uint32_t{0});
        put(button.data(),0x18,grid);put(button.data(),0x28,reinterpret_cast<U>(section.data()));
        put(section.data(),0x250,reinterpret_cast<U>(vehicle.data()));
        section_slots={0,reinterpret_cast<U>(section.data()),0};
        put(vehicle.data(),0,reinterpret_cast<U>(section_slots.data()));put(vehicle.data(),8,std::uint32_t{3});
        put(vehicle.data(),0xc,std::uint32_t{1});put(vehicle.data(),0x10,std::uint32_t{1});
        put(vehicle.data(),0x178,std::uint32_t{51});put(reinterpret_cast<void*>(controller()),0xaa0,native_forward);
        put(reinterpret_cast<void*>(controller()),0x520,button_ptr());put(reinterpret_cast<void*>(controller()),0x528,saved_hit);
    }
    void* hook(U rva)const {
        for(const auto& h:hooks)if(h.rva==rva)return h.detour;
        throw std::runtime_error("Missing integration hook");
    }
    void* allocation(Allocation kind,std::size_t bytes) {
        auto* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);require(p!=nullptr,"Native-double allocation failed");
        require(allocations.emplace(p,NativeAllocation{kind,bytes}).second,"Duplicate native allocation");
        std::memset(p,0xa5,bytes);
        if(kind==Allocation::own_key)++alloc_keys;
        if(kind==Allocation::own_request)++alloc_requests;
        return p;
    }
    void* buffer(std::size_t bytes,bool temporary,bool foreign=false) {
        auto* p=::operator new[](bytes);require(buffers.emplace(p,Buffer{temporary,foreign,bytes}).second,"Duplicate native payload allocation");
        if(!foreign)++alloc_payloads;return p;
    }
    static void* allocate(std::size_t bytes) {
        auto& s=*current;++s.native_helper_calls;
        require(bytes==16 || bytes==72,"Unexpected native allocation size");
        return s.allocation(bytes==16?Allocation::own_key:Allocation::own_request,bytes);
    }
    static void reserve(NativeButtonString* output,std::uint32_t bytes) {
        auto& s=*current;++s.native_helper_calls;
        require(output && !output->data && !output->length && !output->capacity && bytes>=2,"Native string reserve ABI");
        std::uint32_t capacity=32;while(capacity<bytes)capacity*=2;
        output->data=reinterpret_cast<U>(s.buffer(capacity,false));output->capacity=capacity;
    }
    static void free_buffer(void* pointer) {
        auto& s=*current;++s.native_helper_calls;const auto it=s.buffers.find(pointer);
        require(it!=s.buffers.end(),"Payload was freed twice or not allocated by the native double");
        if(!it->second.foreign)++s.free_payloads;
        s.buffers.erase(it);::operator delete[](pointer);
    }
    static void native_delete(void* pointer,std::size_t bytes) {
        auto& s=*current;++s.native_helper_calls;const auto it=s.allocations.find(pointer);
        require(it!=s.allocations.end(),"Native object was deleted twice or was not owned by the fixture");
        const auto kind=it->second.kind;
        if(kind==Allocation::own_request || kind==Allocation::foreign_request)require(bytes==16 || bytes==72,"Wrong request delete ABI size");
        else require(bytes==16,"Wrong event/parent delete size");
        if(kind==Allocation::own_key)++s.free_keys;
        if(kind==Allocation::own_request)++s.free_requests;
        s.allocations.erase(it);require(VirtualFree(pointer,0,MEM_RELEASE)!=0,"Cannot free native-double object");
    }
    static void push(void* queue,void* pointer) {
        auto& s=*current;++s.native_helper_calls;
        const auto it=s.allocations.find(pointer);require(it!=s.allocations.end(),"Push did not receive a live native allocation");
        if(reinterpret_cast<U>(queue)==s.owner_ptr()+0xce6b8) {
            require(it->second.kind==Allocation::own_key && read<int>(pointer,8)==(s.test_hand?20:19) && read<int>(pointer,12)==1,"Owned queue event is not action20/phase1");
            s.events.push_back(pointer);return;
        }
        require(reinterpret_cast<U>(queue)==s.client()+0x340 && it->second.kind==Allocation::own_request,"Wrong live release request queue");
        auto* request=static_cast<NativeButtonRequestLayout*>(pointer);s.check_release(*request);
        s.requests.push_back(request);
    }
    void check_release(const NativeButtonRequestLayout& request)const {
        require(request.vtable==image.base()+0xb0fec0 && request.type==10 && request.metadata==0 && request.pressed==0,
            "Real release builder produced a wrong request header");
        require(request.vehicle_id==51 && request.section_index==0 && request.component_grid==grid,"Release captured wrong vehicle/section/grid");
        require(std::memcmp(request.local_hit.data(),saved_hit.data(),sizeof(saved_hit))==0 && request.skip_on_button_press==static_cast<std::uint8_t>(expected_skip),
            "Release used new hover hit or wrong normal/cancel flag");
        require(request.payload.length==payload.size() && request.payload.capacity>request.payload.length &&
            std::memcmp(reinterpret_cast<const void*>(request.payload.data),payload.data(),payload.size())==0 &&
            reinterpret_cast<const char*>(request.payload.data)[payload.size()]==0,"Real release payload copy changed");
    }
    static NativeButtonString* getter(void* target,NativeButtonString* output,const float* saved) {
        auto& s=*current;++s.native_helper_calls;++s.getters;
        require(reinterpret_cast<U>(target)==s.button_ptr() && output && !output->data && !output->length && !output->capacity &&
            saved && std::memcmp(saved,s.saved_hit.data(),sizeof(s.saved_hit))==0,"Release getter did not use the actual held button/saved hit");
        auto* memory=static_cast<char*>(s.buffer(s.payload.size()+1,true));std::memcpy(memory,s.payload.data(),s.payload.size());memory[s.payload.size()]=0;
        output->data=reinterpret_cast<U>(memory);output->length=static_cast<std::uint32_t>(s.payload.size());output->capacity=output->length+1;return output;
    }
    static void* identity(void* object){return object;}
    static bool available(void*,const void*){return true;}
    static int key_type(void*){return 23;}
    static int pause_type(void*){return 1;}
    static void reset_button(void* button){put(button,0x278,std::uint16_t{});}
    static void* origin(void* character,double* destination,std::uint8_t,std::uint8_t) {
        auto& s=*current;require(character==&s.character && destination,"Original Origin ABI changed");
        std::memcpy(destination,s.native_head.data(),sizeof(s.native_head));return destination;
    }
    static void selection(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,std::uint64_t a5,const int* a6) {
        auto& s=*current;
        require(reinterpret_cast<U>(controller)==s.controller() && a2==s.client() && a3==s.owner_ptr()+0xce600 &&
            a4==s.data_ptr() && a5==s.owner_ptr()+0xce5d0 && reinterpret_cast<U>(a6)==s.owner_ptr()+0x358,"Selection live ABI changed");
        std::array<double,3> ray_origin{};
        require(reinterpret_cast<Origin>(s.origin_call)(&s.character,ray_origin.data(),0,1)==ray_origin.data(),"Origin RAX changed");
        if(s.selected_sample.valid_ray) {
            require(ray_origin==s.selected_sample.ray.origin,"Real native_hand_ray did not supply the bound world hand origin");
            const auto direction=read<std::array<float,3>>(controller,0xaa0);
            for(unsigned i=0;i<3;++i)require(std::abs(direction[i]-s.selected_sample.ray.direction[i])<1e-6,"Real hand direction did not reach Selection");
            ++s.ray_uses;
        } else require(ray_origin==s.native_head,"Invalid tracking did not restore native head ray");
        // Only this original native double selects the target, never the test
        // calling a backend callback directly or writing a certificate.
        put(controller,0x520,s.button_ptr());put(controller,0x528,s.hover_hit);
    }
    static void* key_dtor(void* event,std::uint32_t flags) {
        const auto original=event;
        if(flags&1)reinterpret_cast<Delete>(current->image.base()+0x95c2ec)(event,16);
        return original;
    }
    static bool pop(void* queue,void** slot) {
        auto& s=*current;require(reinterpret_cast<U>(queue)==s.owner_ptr()+0xce6b8 && slot,"Pop queue/slot ABI changed");
        if(!s.consume_events)return false;
        if(s.events.empty())return false;
        if(*slot)destroy_event(*slot);
        *slot=s.events.front();s.events.pop_front();
        const auto kind=s.allocations.at(*slot).kind;
        s.delivered.push_back(kind==Allocation::foreign_pause?"pause":kind==Allocation::foreign_key?"foreign99":"own20");return true;
    }
    static void destroy_event(void* event) {
        const auto vt=read<U>(event);const auto fn=read<U>(reinterpret_cast<const void*>(vt));
        require(reinterpret_cast<Dtor>(fn)(event,1)==event,"Scalar event destructor changed its returned identity");
    }
    static void action(void* controller,std::uint64_t opaque,std::uint64_t client,std::uint64_t a4,std::uint64_t a5,int id,int phase) {
        auto& s=*current;
        require(reinterpret_cast<U>(controller)==s.controller() && opaque==reinterpret_cast<U>(s.opaque.data()) &&
            client==s.client() && a4==s.owner_ptr()+0xce5d0 && a5==s.owner_ptr()+0xce600,"Action live ABI changed");
        if((id==20||id==19) && phase==0){++s.phase0_actions;++s.short_clicks;return;}
        if((id==20||id==19) && phase==1) {
            require(read<unsigned char>(controller,0x49c)==0 && read<U>(controller,0x520)==s.button_ptr(),"Native press double was called behind pause/wrong target");
            ++s.press_actions;if(id==19)++s.left_presses;
            const auto hand=id==19?HandAction::q:HandAction::e;
            put(controller,hand_held_offset(hand),s.button_ptr());put(controller,hand_down_offset(hand),std::uint8_t{1});
            put(controller,hand_hit_offset(hand),read<std::array<float,3>>(controller,0x528));return;
        }
        require(id==99 && phase==1,"Unexpected foreign native action");++s.foreign_actions;
    }
    static void dispatch(void* controller,std::uint64_t data,std::uint64_t opaque,std::uint64_t client,
        std::uint64_t a5,std::uint64_t a6,void* event) {
        auto& s=*current;++s.native_dispatches;
        require(reinterpret_cast<U>(controller)==s.controller() && data==s.data_ptr() && opaque==reinterpret_cast<U>(s.opaque.data()) &&
            client==s.client() && a5==s.owner_ptr()+0xce5d0 && a6==s.owner_ptr()+0xce600,"Dispatcher seven-argument ABI changed");
        if(read<U>(event)==s.image.base()+0xb59cf0) {
            s.delegated.push_back("pause");put(controller,0x49c,read<unsigned char>(event,8));return;
        }
        require(read<U>(event)==s.image.base()+0xbb2110,"Native dispatcher saw an unknown event");
        const auto id=read<int>(event,8);s.delegated.push_back(id==99?"foreign99":"own20");
        reinterpret_cast<Action>(s.action_call)(controller,opaque,client,a5,a6,id,read<int>(event,12));
    }
    static void drain(bool short_loop) {
        auto& s=*current;void* event{};bool first=true;
        for(;;) {
            const auto pop_call=short_loop?(first?s.pop_first:s.pop_more):(first?s.long_pop_first:s.long_pop_more);
            if(!reinterpret_cast<Pop>(pop_call)(reinterpret_cast<void*>(s.owner_ptr()+0xce6b8),&event))break;
            first=false;
            reinterpret_cast<Dispatch>(short_loop?s.dispatch_call:s.long_dispatch_call)(reinterpret_cast<void*>(s.controller()),s.data_ptr(),
                reinterpret_cast<U>(s.opaque.data()),s.client(),s.owner_ptr()+0xce5d0,s.owner_ptr()+0xce600,event);
        }
        if(event)destroy_event(event);
    }
    static void loop(void* owner,std::uint64_t opaque,std::uint64_t data,std::uint64_t client) {
        auto& s=*current;require(reinterpret_cast<U>(owner)==s.owner_ptr() && opaque==reinterpret_cast<U>(s.opaque.data()) &&
            data==s.data_ptr() && client==s.client(),"Long logic loop ABI changed");
        if(s.replace_in_loop){s.replace_in_loop=false;
            require(reinterpret_cast<Replace>(s.replace_call)(s.world.data(),opaque,reinterpret_cast<void*>(client),7,s.saved_hit.data(),0x11223344)==0x12345678,
                "Character replacement lost its returned node identity");}
        drain(false);
    }
    static void short_loop(void* owner,std::uint64_t opaque,std::uint64_t data,std::uint64_t client,int* output) {
        auto& s=*current;require(output==&s.logic_counter,"Short logic loop stack argument changed");++*output;
        require(reinterpret_cast<U>(owner)==s.owner_ptr() && opaque==reinterpret_cast<U>(s.opaque.data()) && data==s.data_ptr() && client==s.client(),
            "Short logic loop ABI changed");drain(true);
    }
    static std::vector<U> ring_values(void* ring) {
        const auto value=read<Ring>(ring);std::vector<U> result;
        require(value.count<=value.capacity && (!value.count||value.data),"Malformed fixture ring");
        for(std::uint32_t i=0;i<value.count;++i)result.push_back(read<U>(reinterpret_cast<void*>(value.data),
            ((value.begin+i)%value.capacity)*sizeof(U)));
        return result;
    }
    static void clear_ring(void* ring){put(ring,0,Ring{});}
    static void request_serializer(void* pointer,void*) {
        auto& s=*current;++s.request_serializers;
        const auto it=s.allocations.find(pointer);require(it!=s.allocations.end(),"Serializer read a dead request");
        if(it->second.kind==Allocation::own_request)s.check_release(*static_cast<NativeButtonRequestLayout*>(pointer));
    }
    static void parent_serializer(void* parent,void* buffer) {
        auto& s=*current;++s.parent_serializers;
        require(reinterpret_cast<U>(parent)==s.parent_live && read<U>(parent)==s.image.base()+0xb89760 && read<int>(parent,8)==3 &&
            read<unsigned char>(buffer,0x14)==1,"Type3 WRITE serializer ABI/mode changed");
        for(const auto request:ring_values(static_cast<unsigned char*>(parent)+0x18))
            reinterpret_cast<Serializer>(read<U>(reinterpret_cast<void*>(read<U>(reinterpret_cast<void*>(request)))))(reinterpret_cast<void*>(request),buffer);
    }
    static void server_dispatch(void* owner,void* opaque,void* client,void* parent,void* data) {
        auto& s=*current;++s.server_dispatches;
        require(reinterpret_cast<U>(owner)==s.owner_ptr() && opaque==s.opaque.data() && reinterpret_cast<U>(client)==s.client() &&
            reinterpret_cast<U>(data)==s.data_ptr() && reinterpret_cast<U>(parent)==s.parent_live,"Type3 localhost dispatcher ABI changed");
        for(const auto request:ring_values(static_cast<unsigned char*>(parent)+0x18)) {
            const auto it=s.allocations.find(reinterpret_cast<void*>(request));require(it!=s.allocations.end(),"Server dispatcher read a dead request");
            if(it->second.kind==Allocation::own_request)s.check_release(*reinterpret_cast<NativeButtonRequestLayout*>(request));
        }
    }
    static void raw_clear(void* queue) {
        auto& s=*current;
        if(reinterpret_cast<U>(queue)==s.client()+0x340) {
            for(const auto request:ring_values(queue))reinterpret_cast<Delete>(s.raw_delete_call)(reinterpret_cast<void*>(request),16);
            clear_ring(queue);return;
        }
        require(s.envelope_active,"Clear called outside the fixture's native parent lifetime");
        if(queue==s.outer_live) {
            // Native WRITE disposal deletes the parent only. The REAL retirement
            // hook owns the missing request/payload cleanup after this returns.
            for(const auto parent:ring_values(queue))reinterpret_cast<Delete>(s.raw_delete_call)(reinterpret_cast<void*>(parent),16);
        } else {
            require(reinterpret_cast<U>(queue)==s.parent_live+0x18,"Wrong localhost inner request queue");
            for(const auto request:ring_values(queue))reinterpret_cast<Delete>(s.raw_delete_call)(reinterpret_cast<void*>(request),16);
        }
        clear_ring(queue);
    }
    static void typed_clear(void* queue) {
        auto& s=*current;require(queue==s.outer_live,"Wrong localhost parent queue");
        for(const auto parent:ring_values(queue)) {
            reinterpret_cast<Clear>(s.inner_clear_call)(reinterpret_cast<void*>(parent+0x18));
            reinterpret_cast<Delete>(s.parent_delete_call)(reinterpret_cast<void*>(parent),16);
        }
        clear_ring(queue);
    }
    static void deactivate(void* simulation,std::uint64_t opaque,void* data,const int* state) {
        auto& s=*current;require(simulation==s.simulation.data() && opaque==reinterpret_cast<U>(s.opaque.data()) && data==s.data.data() &&
            reinterpret_cast<U>(state)==s.owner_ptr()+0x358,"Deactivate ABI changed");
        require(s.observer_phase==1,"lifecycle observer did not begin before native deactivation");s.observer_phase=2;
        ++s.transitions;s.held_at_transition=read<U>(reinterpret_cast<void*>(s.controller()),hand_held_offset(s.test_hand?HandAction::e:HandAction::q));
        require(!s.held_at_transition && !read<unsigned char>(reinterpret_cast<void*>(s.controller()),hand_down_offset(s.test_hand?HandAction::e:HandAction::q)),"Owned hold reached native deactivation before release");
        s.actor_holder=0;
    }
    static U replace_character(void* world,std::uint64_t opaque,void* client,std::uint16_t player,const void* pose,std::uint64_t connection) {
        auto& s=*current;require(world==s.world.data() && opaque==reinterpret_cast<U>(s.opaque.data()) && reinterpret_cast<U>(client)==s.client() &&
            player==7 && pose==s.saved_hit.data() && connection==0x11223344,"Character replacement lost a register/stack argument");
        require(s.observer_phase==1,"lifecycle observer did not begin before native replacement");s.observer_phase=2;
        ++s.transitions;s.held_at_transition=read<U>(reinterpret_cast<void*>(s.controller()),hand_held_offset(s.test_hand?HandAction::e:HandAction::q));
        require(!s.held_at_transition,"Owned hold reached native replacement before release");s.actor_holder=reinterpret_cast<U>(&s.next_character);return 0x12345678;
    }
    static bool lifecycle_begin(void* object,U owner,U context) noexcept {
        auto& s=*static_cast<State*>(object);
        if(owner!=s.owner_ptr()||context!=s.client()||s.observer_phase)return false;
        s.observer_phase=1;++s.observer_begins;return true;
    }
    static void lifecycle_end(void* object,bool success) noexcept {
        auto& s=*static_cast<State*>(object);
        if(success&&s.observer_phase==2)++s.observer_ends;s.observer_phase=0;
    }
    static void* original(U rva) {
        switch(rva) {
        case 0x74f260:return reinterpret_cast<void*>(selection);case 0x4e33a0:return reinterpret_cast<void*>(origin);
        case 0x86a150:return reinterpret_cast<void*>(loop);case 0x86cc30:return reinterpret_cast<void*>(short_loop);
        case 0x790850:return reinterpret_cast<void*>(dispatch);case 0x78c7a0:return reinterpret_cast<void*>(action);
        case 0x695e70:return reinterpret_cast<void*>(key_dtor);case 0x23ca90:return reinterpret_cast<void*>(parent_serializer);
        case 0x855ab0:return reinterpret_cast<void*>(server_dispatch);case 0x8d3bb0:return reinterpret_cast<void*>(raw_clear);
        case 0x89d030:return reinterpret_cast<void*>(typed_clear);case 0x95c2ec:return reinterpret_cast<void*>(native_delete);
        case 0x7aca70:return reinterpret_cast<void*>(deactivate);case 0x5f2950:return reinterpret_cast<void*>(replace_character);
        default:return nullptr;
        }
    }
    void connect() {
        for(const auto& h:hooks)image.tail_jump(h.rva,h.detour);
        for(const auto& [rva,fn]:std::vector<std::pair<U,void*>>{{0x95c6f8,reinterpret_cast<void*>(allocate)},
            {0x17ca10,reinterpret_cast<void*>(reserve)},{0x9b6750,reinterpret_cast<void*>(free_buffer)},
            {0x236ed0,reinterpret_cast<void*>(push)},{0x32b470,reinterpret_cast<void*>(getter)},
            {0x231bd0,reinterpret_cast<void*>(request_serializer)},{0x694230,reinterpret_cast<void*>(pop)},
            {0xcb690,reinterpret_cast<void*>(identity)},{0x1413b0,reinterpret_cast<void*>(available)},
            {0x35d4e0,reinterpret_cast<void*>(reset_button)},{0x37c4d0,reinterpret_cast<void*>(key_type)}})image.tail_jump(rva,fn);
        put(image.data,0xb59cf0,reinterpret_cast<U>(key_dtor));put(image.data,0xb59cf0+8,reinterpret_cast<U>(pause_type));
        select_call=image.add_caller(0x745a24,6,hook(0x74f260));origin_call=image.add_caller(0x74f38d,4,hook(0x4e33a0));
        loop_call=image.add_caller(0x84cd83,4,hook(0x86a150));short_loop_call=image.add_caller(0x84cb35,5,hook(0x86cc30));
        dispatch_call=image.add_caller(0x86cd3d,7,hook(0x790850));long_dispatch_call=image.add_caller(0x86a76c,7,hook(0x790850));
        action_call=image.add_caller(0x7933b2,7,hook(0x78c7a0));
        pop_first=image.add_caller(0x86ccef,2,reinterpret_cast<void*>(pop));pop_more=image.add_caller(0x86cd4e,2,reinterpret_cast<void*>(pop));
        long_pop_first=image.add_caller(0x86a728,2,reinterpret_cast<void*>(pop));long_pop_more=image.add_caller(0x86a77c,2,reinterpret_cast<void*>(pop));
        serializer_call=image.add_caller(0x23cd40,2,hook(0x23ca90));server_call=image.add_caller(0x86a7ff,5,hook(0x855ab0));
        write_clear_call=image.add_caller(0x240898,1,hook(0x8d3bb0));typed_clear_call=image.add_caller(0x86a817,1,hook(0x89d030));
        inner_clear_call=image.add_caller(0x89d0a8,1,hook(0x8d3bb0));raw_delete_call=image.add_caller(0x8d3bee,2,hook(0x95c2ec));
        parent_delete_call=image.add_caller(0x89d0cf,2,hook(0x95c2ec));
        replace_call=image.add_caller(0x852d06,6,hook(0x5f2950));
        deactivate_call=image.add_caller(0x84c6f1,4,hook(0x7aca70));
        source_reset_call=image.add_caller(0x24005b,1,hook(0x8d3bb0));source_destroy_call=image.add_caller(0x23eb68,1,hook(0x8d3bb0));
        image.register_unwind();
    }
    void frame(bool pinch,bool focused=true) {
        XrRuntimeFrame f{};f.id=++frames;f.predicted_display_time_ns=static_cast<std::int64_t>(frames)*10000000;
        f.space_revision=1;f.running=f.should_render=true;f.focused=focused;
        f.position_valid=f.position_tracked=f.orientation_valid=f.orientation_tracked=true;
        auto& hand=f.hands[test_hand];hand.active=true;
        for(auto& joint:hand.joints)joint.position_valid=joint.position_tracked=true;
        hand.joints[xr_index_tip].pose.position={.25f,0,-.5f};hand.joints[xr_index_proximal].pose.position={.2f,0,-.3f};
        hand.joints[xr_thumb_tip].pose.position={pinch?.26f:.33f,0,-.5f};
        const Matrix4d anchor{1,0,0,0,0,1,0,0,0,0,1,0,native_head[0],native_head[1],native_head[2],1};
        if(head_correction){
            const std::array<Pose,2> neutral{Pose{{},{-.032f,0,0}},Pose{{},{.032f,0,0}}};
            auto eyes=neutral;for(auto& eye:eyes)eye.position.x+=.4f+.005f*static_cast<float>(frames);
            f.eyes[0].pose=eyes[0];f.eyes[1].pose=eyes[1];
            const auto view=head_comfort_view(Pose{},neck_position(Pose{},neutral),eyes,{},true,false);
            require(view.limited,"Native click regression did not activate head correction");
            bridge.publish(f,HandCalibration{Pose{},f.space_revision},view.origin,anchor);
        }else bridge.publish(f,Pose{},anchor);
    }
    void select() {
        selected_sample=bridge.inspect();reinterpret_cast<Selection>(select_call)(reinterpret_cast<void*>(controller()),client(),
            owner_ptr()+0xce600,data_ptr(),owner_ptr()+0xce5d0,reinterpret_cast<const int*>(owner_ptr()+0x358));
        require(read<std::array<float,3>>(reinterpret_cast<void*>(controller()),0xaa0)==native_forward,"Selection did not restore native direction");
    }
    void tick(bool long_loop=false) {
        if(long_loop)reinterpret_cast<Loop>(loop_call)(owner.data(),reinterpret_cast<U>(opaque.data()),data_ptr(),client());
        else reinterpret_cast<ShortLoop>(short_loop_call)(owner.data(),reinterpret_cast<U>(opaque.data()),data_ptr(),client(),&logic_counter);
    }
    void retire(bool localhost) {
        require(requests.size()==1,"Expected exactly one real release request before retirement");
        auto* own=requests.front();requests.clear();const auto own_payload=own->payload.data;
        auto* foreign=static_cast<NativeButtonRequestLayout*>(allocation(Allocation::foreign_request,72));
        *foreign={};foreign->vtable=image.base()+0xb0fec0;foreign->type=10;
        auto* foreign_payload=buffer(16,false,true);foreign->payload={reinterpret_cast<U>(foreign_payload),7,16};
        std::memcpy(foreign_payload,"foreign",8);
        auto* parent=allocation(Allocation::parent,0x38);put(parent,0,image.base()+0xb89760);put(parent,8,std::uint32_t{3});
        std::array<U,2> children{reinterpret_cast<U>(own),reinterpret_cast<U>(foreign)};
        put(parent,0x18,Ring{reinterpret_cast<U>(children.data()),2,0,2});
        std::array<U,1> parents{reinterpret_cast<U>(parent)};Ring outer{reinterpret_cast<U>(parents.data()),1,0,1};
        parent_live=reinterpret_cast<U>(parent);outer_live=&outer;envelope_active=true;
        const auto before=native_button_retirement_stats();const auto deletes_before=free_requests;
        if(localhost) {
            reinterpret_cast<ServerDispatch>(server_call)(owner.data(),opaque.data(),reinterpret_cast<void*>(client()),parent,data.data());
            reinterpret_cast<Clear>(typed_clear_call)(&outer);
        } else {
            std::array<unsigned char,0x20> output{};output[0x14]=1;
            reinterpret_cast<Serializer>(serializer_call)(parent,output.data());reinterpret_cast<Clear>(write_clear_call)(&outer);
        }
        const auto after=native_button_retirement_stats();
        require(after.retained_records==0 && after.live_records==0 && !after.quarantined && !after.unknown_deletes,
            "Real retirement did not terminally release its ownership records");
        require((localhost?after.localhost_retired:after.write_retired)==(localhost?before.localhost_retired:before.write_retired)+1 &&
            free_requests==deletes_before+1,"Wrong terminal path or duplicate native request deletion");
        require(!allocations.contains(own) && !buffers.contains(reinterpret_cast<void*>(own_payload)) && !allocations.contains(parent),
            "Own request/payload or native parent survived proven retirement");
        require(buffers.contains(foreign_payload),"Retirement freed another owner's payload");
        if(localhost)require(!allocations.contains(foreign),"Original localhost inner clear did not delete its foreign child");
        else {require(allocations.contains(foreign),"WRITE retirement deleted a foreign request");native_delete(foreign,72);}
        free_buffer(foreign_payload);require(outer.count==0,"Native clear did not finish the outer queue");
        envelope_active=false;outer_live=nullptr;parent_live=0;
    }
    void pipeline(bool focus_loss,bool localhost,bool shutdown=false) {
        const auto before=native_hand_buttons_stats();hover_hit=saved_hit;
        frame(false);select();tick(localhost);frame(true);select();tick(localhost);
        const auto accepted=native_hand_buttons_stats();
        require(accepted.events_queued==before.events_queued+1 && accepted.presses_accepted==before.presses_accepted+1 &&
            accepted.own_hold && accepted.pending_events==0 && !accepted.blocked,"Real bridge/ray/tick/owned-dispatch did not accept one press");
        require(read<U>(reinterpret_cast<void*>(controller()),hand_held_offset(test_hand?HandAction::e:HandAction::q))==button_ptr() &&
            read<unsigned char>(reinterpret_cast<void*>(controller()),hand_down_offset(test_hand?HandAction::e:HandAction::q))==1,"Original action did not own the held target");
        require(alloc_keys==free_keys && events.empty(),"Owned key event did not complete its native destruction handoff");
        hover_hit={9,-8,7};expected_skip=focus_loss||shutdown;frame(shutdown,!focus_loss);
        if(focus_loss||shutdown) {
            const auto calls=native_helper_calls;reset_native_hand_buttons();
            require(native_hand_buttons_stats().own_hold && native_helper_calls==calls && requests.empty(),
                "Session cancellation ran native cleanup outside a live logic scope");
        }
        select();tick(localhost);
        const auto released=native_hand_buttons_stats();
        require(released.release_requests==before.release_requests+1 && !released.own_hold && !released.blocked && !released.faults,
            "Real capture/builder/enqueue did not finish local hold cleanup");
        require(!read<U>(reinterpret_cast<void*>(controller()),hand_held_offset(test_hand?HandAction::e:HandAction::q)) && !read<unsigned char>(reinterpret_cast<void*>(controller()),hand_down_offset(test_hand?HandAction::e:HandAction::q)),
            "Dedicated cleanup left local held/action state down");
        require(!phase0_actions && !short_clicks,"Cleanup invoked native phase0/short-click instead of a dedicated request");
        require(native_button_retirement_stats().live_records==1,"Real wrapped enqueue did not retain native request ownership");
        if(shutdown) {
            const auto calls=native_helper_calls;reset_native_hand_buttons();
            require(native_button_retirement_stats().retained_records==1 && native_helper_calls==calls,
                "Shutdown forgot a submitted release before proven retirement");
            tick(localhost);require(requests.size()==1,"Shutdown duplicated its release while retirement remained pending");
        }
        retire(localhost);
        require(alloc_requests==free_requests && alloc_payloads==free_payloads && allocations.empty() && buffers.empty(),
            "Pipeline did not balance exactly its native events, requests and payloads");
    }
    void queued_shutdown() {
        const auto before=native_hand_buttons_stats();const auto foreign_before=foreign_actions;
        frame(false);select();tick();frame(true);select();consume_events=false;tick();
        require(events.size()==1 && native_hand_buttons_stats().pending_events>0,"Shutdown integration did not queue an owned press");
        const auto own=events.front();const auto calls=native_helper_calls;
        bridge.reset();reset_native_hand_buttons();reset_native_hand_buttons();
        require(allocations.contains(own) && native_hand_buttons_stats().pending_events>0 && native_helper_calls==calls,
            "Shutdown freed or forgot a queued event outside native destruction");
        auto* foreign=allocation(Allocation::foreign_key,16);put(foreign,0,image.base()+0xbb2110);put(foreign,8,99);put(foreign,12,1);events.push_back(foreign);
        delivered.clear();delegated.clear();consume_events=true;tick();const auto after=native_hand_buttons_stats();
        require(after.presses_accepted==before.presses_accepted && after.presses_rejected==before.presses_rejected+1 &&
            !after.pending_events && !after.own_hold && !after.blocked && !after.faults,
            "Shutdown pending press executed or left an unresolved event after its native destructor");
        require(delivered==std::vector<std::string>{"own20","foreign99"} && delegated==std::vector<std::string>{"foreign99"} &&
            foreign_actions==foreign_before+1 && allocations.empty() && buffers.empty() && !native_button_retirement_stats().retained_records,
            "Shutdown changed FIFO/foreign passthrough or reported idle before all ownership drained");
    }
    void lifecycle(bool deactivation) {
        const auto before=native_hand_buttons_stats();const auto retired_before=native_button_retirement_stats();
        hover_hit=saved_hit;frame(false);select();tick();frame(true);select();tick();expected_skip=true;
        if(deactivation)reinterpret_cast<Deactivate>(deactivate_call)(simulation.data(),reinterpret_cast<U>(opaque.data()),data.data(),reinterpret_cast<const int*>(owner_ptr()+0x358));
        else {replace_in_loop=true;tick(true);}
        const auto after=native_hand_buttons_stats();
        require(after.lifecycle_releases==before.lifecycle_releases+1 && after.release_requests==before.release_requests+1 &&
            !after.own_hold && !after.blocked && !after.faults && requests.size()==1 && !held_at_transition,
            "Real lifecycle hooks did not release before native context mutation");
        std::array<U,1> children{reinterpret_cast<U>(requests.front())};void* source=reinterpret_cast<void*>(client()+0x340);
        put(source,0,Ring{reinterpret_cast<U>(children.data()),1,0,1});
        reinterpret_cast<Clear>(deactivation?source_destroy_call:source_reset_call)(source);requests.clear();
        const auto retired=native_button_retirement_stats();
        require(retired.retained_records==0 && !retired.quarantined &&
            (deactivation?retired.source_destroy_retired==retired_before.source_destroy_retired+1:retired.source_reset_retired==retired_before.source_reset_retired+1) &&
            alloc_requests==free_requests && alloc_payloads==free_payloads && allocations.empty() && buffers.empty(),
            "Lifecycle source-Q teardown did not free only its payload after the original native request delete");
        actor_holder=reinterpret_cast<U>(&character);
    }
    void pause_during_hold() {
        const auto before=native_hand_buttons_stats();hover_hit=saved_hit;
        frame(false);select();tick();frame(true);select();tick();
        auto* pause_event=allocation(Allocation::foreign_pause,16);put(pause_event,0,image.base()+0xb59cf0);put(pause_event,8,std::uint8_t{1});events.push_back(pause_event);
        tick();require(native_hand_buttons_stats().own_hold,"Fixture did not apply pause after the initial logic service");
        expected_skip=true;tick();const auto after=native_hand_buttons_stats();
        require(after.presses_accepted==before.presses_accepted+1 && after.release_requests==before.release_requests+1 &&
            !after.own_hold && !after.blocked && !after.faults && !phase0_actions && native_button_retirement_stats().retained_records==1,
            "Pause did not cancel a held button on the next live logic scope");
        retire(false);put(reinterpret_cast<void*>(controller()),0x49c,std::uint8_t{});
    }
    void pause_fifo() {
        const auto before=native_hand_buttons_stats();const auto old_press_actions=press_actions;const auto old_foreign=foreign_actions;
        hover_hit=saved_hit;frame(false);select();tick();frame(true);select();
        auto* pause_event=allocation(Allocation::foreign_pause,16);put(pause_event,0,image.base()+0xb59cf0);put(pause_event,8,std::uint8_t{1});
        auto* foreign=allocation(Allocation::foreign_key,16);put(foreign,0,image.base()+0xbb2110);put(foreign,8,99);put(foreign,12,1);
        events.push_back(pause_event);events.push_back(foreign);delivered.clear();delegated.clear();tick();
        const auto after=native_hand_buttons_stats();
        require(after.events_queued==before.events_queued+1 && after.presses_rejected==before.presses_rejected+1 &&
            after.presses_accepted==before.presses_accepted && !after.own_hold && !after.pending_events && !after.faults,
            "Pause before own press was not checked at real logical dispatch");
        require(delivered==std::vector<std::string>{"pause","foreign99","own20"} &&
            delegated==std::vector<std::string>{"pause","foreign99"},"FIFO order or foreign passthrough changed");
        require(press_actions==old_press_actions && foreign_actions==old_foreign+1 && !read<U>(reinterpret_cast<void*>(controller()),0x558),
            "Owned pause-blocked press reached native action, or foreign action was suppressed");
        require(allocations.empty() && buffers.empty() && alloc_keys==free_keys,"Rejected owned press did not finish native event lifetime");
        put(reinterpret_cast<void*>(controller()),0x49c,std::uint8_t{});
    }
};
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    auto& s=*current;const auto rva=reinterpret_cast<U>(target)-s.image.base();
    for(const auto& hook:s.hooks)if(hook.rva==rva){++s.unexpected_hooks;return MH_ERROR_ALREADY_CREATED;}
    const auto native=State::original(rva);if(!native){++s.unexpected_hooks;return MH_ERROR_NOT_EXECUTABLE;}
    s.hooks.push_back({rva,detour,native});*original=native;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID target) {
    auto& s=*current;const auto rva=reinterpret_cast<U>(target)-s.image.base();
    const auto it=std::find_if(s.hooks.begin(),s.hooks.end(),[&](const auto& hook){return hook.rva==rva;});
    if(it==s.hooks.end())return MH_ERROR_NOT_CREATED;s.hooks.erase(it);return MH_OK;
}
int main() {
    try {
        State fixture;const auto directory=std::filesystem::current_path()/"artifacts";std::filesystem::create_directories(directory);
        const auto trace=directory/("hand-buttons-integration-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",L"1");SetEnvironmentVariableW(L"SWVR_HAND_RAY",L"1");
        require(install_native_button_retirement(fixture.image.base(),true,trace)==NativeButtonRetirementStatus::installed,"Real retirement installation failed");
        require(install_native_hand_buttons(fixture.image.base(),true,trace,fixture.bridge)==NativeHandButtonsStatus::installed,"Real hand-buttons installation failed");
        require(set_native_hand_lifecycle_callbacks({&fixture,State::lifecycle_begin,State::lifecycle_end}),"Lifecycle observer registration failed");
        require(!set_native_hand_lifecycle_callbacks({&fixture,State::lifecycle_begin,State::lifecycle_end}),"Duplicate observer replaced a live registration");
        require(install_native_hand_ray(fixture.image.base(),true,trace,fixture.bridge,native_hand_buttons_callbacks())==NativeHandRayStatus::installed,
            "Real ray/callback installation failed");
        require(fixture.hooks.size()==14 && !fixture.unexpected_hooks && !fixture.native_helper_calls,
            "Installers duplicated hooks or invoked native helpers before fixture handoff");
        fixture.connect();fixture.head_correction=true;fixture.pipeline(false,false);fixture.pipeline(true,true);
        fixture.head_correction=false;
        fixture.pipeline(false,false,true);fixture.pipeline(false,true,true);
        fixture.queued_shutdown();fixture.pause_during_hold();fixture.lifecycle(true);fixture.lifecycle(false);fixture.pause_fifo();
        require(fixture.observer_begins==2&&fixture.observer_ends==2&&fixture.observer_phase==0,"Shared observer did not bracket exactly the native transitions");
        fixture.test_hand=0;fixture.bridge.reset();fixture.pipeline(false,false);fixture.pipeline(true,true);
        fixture.lifecycle(true);fixture.lifecycle(false);
        require(fixture.left_presses==4,"Left index pinch did not execute the native Q scenarios");
        require(fixture.observer_begins==4&&fixture.observer_ends==4,"Left lifecycle did not bracket native transitions");
        const auto before_physical=native_hand_buttons_stats();
        put(reinterpret_cast<void*>(fixture.controller()),0x4c0,std::uint8_t{1});
        fixture.frame(false);fixture.select();fixture.tick();fixture.frame(true);fixture.select();fixture.tick();
        put(reinterpret_cast<void*>(fixture.controller()),0x4c0,std::uint8_t{});
        fixture.frame(true);fixture.select();fixture.tick();
        require(native_hand_buttons_stats().presses_accepted==before_physical.presses_accepted&&!native_hand_buttons_stats().own_hold,"Physical Q conflict was stolen or replayed after release");
        fixture.frame(false);fixture.select();fixture.tick();
        const auto left_before_rearm=fixture.left_presses;
        fixture.test_hand=0;fixture.bridge.reset();fixture.pipeline(false,false);fixture.pipeline(true,true);
        require(fixture.left_presses==left_before_rearm+2,"Left Q did not rearm for exactly two presses after physical input");
        const auto buttons=native_hand_buttons_stats();const auto retirement=native_button_retirement_stats();
        require(buttons.presses_accepted==13 && buttons.presses_rejected==2 && buttons.release_requests==13 && buttons.lifecycle_releases==4 && !buttons.faults &&
            !buttons.own_hold && !buttons.pending_events && !retirement.retained_records && retirement.write_retired==5 &&
            retirement.localhost_retired==4 && retirement.source_reset_retired==2 && retirement.source_destroy_retired==2 &&
            !retirement.quarantined && !fixture.phase0_actions && !fixture.short_clicks,
            "Final integration service counters disagree with the actual pipeline");
        flush_native_hand_buttons();flush_native_hand_ray();
        SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",nullptr);SetEnvironmentVariableW(L"SWVR_HAND_RAY",nullptr);
        std::cout<<"Native hand-buttons integration passed: real bridge/ray/buttons/capture/bindings/builder/retirement; "
            "normal WRITE + focus-loss localhost, shutdown hold/pending/retirement, native deactivate/character replacement + source-Q reset/destroy, pause + FIFO, foreign passthrough, balanced allocations, no phase0.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
