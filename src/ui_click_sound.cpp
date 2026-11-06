#include "swvr/ui_click_sound.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <cmath>
namespace swvr {
struct UiClickSound::Impl {
    HWAVEOUT device{};WAVEHDR header{};std::array<short,480> samples{};bool failed{},prepared{};
    bool open(){if(device)return true;if(failed)return false;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=24000;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=48000;
        for(unsigned i=0;i<samples.size();++i){const double t=static_cast<double>(i)/samples.size();samples[i]=static_cast<short>(2400*std::sin(i*6.28318530718*1050/24000)*std::sin(t*3.14159265359)*std::exp(-t*3));}
        if(waveOutOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){device=nullptr;failed=true;return false;}
        header.lpData=reinterpret_cast<LPSTR>(samples.data());header.dwBufferLength=static_cast<DWORD>(samples.size()*sizeof(short));
        prepared=waveOutPrepareHeader(device,&header,sizeof(header))==MMSYSERR_NOERROR;failed=!prepared;return prepared;
    }
    ~Impl(){if(device){waveOutReset(device);if(prepared)waveOutUnprepareHeader(device,&header,sizeof(header));waveOutClose(device);}}
};
UiClickSound::UiClickSound():impl_(std::make_unique<Impl>()){}UiClickSound::~UiClickSound()=default;
void UiClickSound::play()noexcept{auto& s=*impl_;if(s.failed||!s.open()||(s.header.dwFlags&WHDR_INQUEUE))return;if(waveOutWrite(s.device,&s.header,sizeof(s.header))!=MMSYSERR_NOERROR)s.failed=true;}
bool UiClickSound::failed()const noexcept{return impl_->failed;}
}
