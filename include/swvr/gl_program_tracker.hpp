#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace swvr {
// What a per-program GL consumer knows about a program, asked without side
// effects. `none` is a promise: until the consumer's cache is invalidated,
// every per-draw / per-upload call for this program is a no-op.
enum class GlProgramInterest {unknown,none,active};
// Remembers the program the glUseProgram hook last bound on one thread, and
// which consumers proved it irrelevant. The per-draw, per-matrix and
// per-uniform hooks then need no driver query and skip no-op consumer calls.
// Pure bookkeeping: no GL calls, no locks. Use one instance per thread.
template<std::size_t Consumers> class GlProgramTracker {
public:
    using Query=GlProgramInterest(*)(std::size_t consumer,void* context,unsigned program) noexcept;
    // After every hooked glUseProgram. `program` must be GL_CURRENT_PROGRAM read
    // back from the driver: a rejected glUseProgram leaves the old one bound.
    // `binding` changes when a remembered binding can no longer be trusted
    // (context deleted); `caches` changes when a consumer's answer may differ.
    void bind(void* context,unsigned program,std::uint64_t binding,std::uint64_t caches,Query query) noexcept {
        const bool same=context&&context_==context&&program_==program&&binding_==binding&&caches_==caches;
        context_=context;program_=program;binding_=binding;
        if(!same){caches_=caches;evaluate(query);}
    }
    // False: nothing is known about `context` here; call every consumer.
    bool current(void* context,std::uint64_t binding,std::uint64_t caches,Query query) noexcept {
        if(!context||context!=context_)return false;
        if(binding!=binding_){context_=nullptr;return false;}
        if(caches!=caches_){caches_=caches;evaluate(query);}
        return true;
    }
    bool wants(std::size_t consumer) const noexcept {return calls_[consumer]!=Call::skip;}
    // After a consumer call that can create its record of the program. An
    // answer still unknown then stays "call" until the next bind or invalidation.
    void taught(std::size_t consumer,Query query) noexcept {
        if(calls_[consumer]==Call::call_and_ask)calls_[consumer]=decide(query(consumer,context_,program_),true);
    }
    unsigned program() const noexcept {return program_;}
    // True once every 256 calls: compare program() with the driver then.
    bool validation_due() noexcept {return ++checks_==0;}
    void forget() noexcept {context_=nullptr;}
private:
    enum class Call : unsigned char {skip,call,call_and_ask};
    static Call decide(GlProgramInterest interest,bool asked_before) noexcept {
        return interest==GlProgramInterest::none?Call::skip:interest==GlProgramInterest::active||asked_before?Call::call:Call::call_and_ask;
    }
    void evaluate(Query query) noexcept {
        // With no program bound every consumer returns before touching state.
        for(std::size_t i=0;i<Consumers;++i)calls_[i]=program_?decide(query(i,context_,program_),false):Call::skip;
    }
    void* context_{};unsigned program_{};std::uint64_t binding_{},caches_{};
    std::array<Call,Consumers> calls_{};std::uint8_t checks_{};
};
}
