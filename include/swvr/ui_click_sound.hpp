#pragma once
#include <memory>
namespace swvr {
// One short, quiet PCM click on the default Windows audio output. Never waits
// on audio from the render thread. Only the owned waveOut device is closed.
class UiClickSound {
public:UiClickSound();~UiClickSound();void play()noexcept;bool failed()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
