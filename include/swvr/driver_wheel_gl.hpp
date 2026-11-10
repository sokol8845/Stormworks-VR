#pragma once
#include "swvr/gl_program_tracker.hpp"
#include "swvr/native_driver_seat.hpp"
#include "swvr/hand_steering.hpp"
#include <memory>

namespace swvr {
struct DriverWheelGlStats {std::uint64_t eyes{},identified_eyes{},draws{},depth_draws{};};
class GlDriverWheel {
public:
    GlDriverWheel();~GlDriverWheel();
    void configure(void*(*resolver)(const char*),std::vector<Vec3> points);
    void matrix_uploaded(int location,int count,bool transpose,const float*);
    void program_linked(unsigned program);
    void context_destroyed(void* context);
    void begin_eye(DriverSeatSnapshot,SteeringCommand,Matrix4d expected_camera,std::uint64_t frame);
    void camera_rebased(const Matrix4d&);
    void end_eye() noexcept;
    class Draw {
    public:
        Draw()=default;Draw(const Draw&)=delete;Draw& operator=(const Draw&)=delete;
        Draw(Draw&&) noexcept;~Draw();
    private:
        friend class GlDriverWheel;explicit Draw(GlDriverWheel* owner):owner_(owner){};GlDriverWheel* owner_{};
    };
    Draw before_draw();
    // Side-effect free. `none`: the program is not a patched wheel shader, so
    // before_draw() and matrix_uploaded() return at once; a host may skip both.
    GlProgramInterest interest(void* context,unsigned program) const;
    DriverWheelGlStats stats() const;
private:
    void restore() noexcept;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
