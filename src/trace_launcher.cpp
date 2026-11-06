// Launch only a new local game/test process. No attachment to existing processes.
// Pause at its PE entry point, then load and initialize our tracer outside DllMain.
#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool ok,const char* action) {
    if(!ok) throw std::runtime_error(std::string(action)+" (Win32 "+std::to_string(GetLastError())+")");
}
struct Handle {
    HANDLE value{};
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default; explicit Handle(HANDLE h):value(h){}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
template<class T> T read(HANDLE process,uintptr_t address) {
    T value{};SIZE_T bytes{};
    require(ReadProcessMemory(process,reinterpret_cast<void*>(address),&value,sizeof(value),&bytes)&&bytes==sizeof(value),"Read image header");
    return value;
}
void write_code(HANDLE process,uintptr_t address,unsigned char value) {
    DWORD old{};auto p=reinterpret_cast<void*>(address);
    require(VirtualProtectEx(process,p,1,PAGE_EXECUTE_READWRITE,&old)!=0,"Protect entry point");
    SIZE_T bytes{};const bool ok=WriteProcessMemory(process,p,&value,1,&bytes)&&bytes==1;
    DWORD ignored{};const bool restored=VirtualProtectEx(process,p,1,old,&ignored)!=0;
    require(ok&&restored&&FlushInstructionCache(process,p,1),"Write entry breakpoint");
}
uintptr_t module_base(HANDLE process,const std::filesystem::path& path) {
    std::vector<HMODULE> modules(1024);DWORD needed{};
    require(EnumProcessModulesEx(process,modules.data(),static_cast<DWORD>(modules.size()*sizeof(HMODULE)),&needed,LIST_MODULES_64BIT)!=0,"Enumerate child modules");
    require(needed<=modules.size()*sizeof(HMODULE),"Module list capacity");
    for(size_t i=0;i<needed/sizeof(HMODULE);++i) {
        wchar_t name[32768]{};
        if(GetModuleFileNameExW(process,modules[i],name,32768) && !_wcsicmp(name,path.c_str()))
            return reinterpret_cast<uintptr_t>(modules[i]);
    }
    throw std::runtime_error("Expected module not loaded in child");
}
DWORD remote_call(HANDLE process,uintptr_t function,const std::wstring& argument) {
    const auto bytes=(argument.size()+1)*sizeof(wchar_t);
    auto memory=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    require(memory!=nullptr,"Allocate tracer argument");
    SIZE_T written{};
    if(!WriteProcessMemory(process,memory,argument.c_str(),bytes,&written)||written!=bytes) {
        VirtualFreeEx(process,memory,0,MEM_RELEASE);require(false,"Write tracer argument");
    }
    Handle thread(CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),memory,0,nullptr));
    if(!thread.value){VirtualFreeEx(process,memory,0,MEM_RELEASE);require(false,"Start initialization thread");}
    // On timeout the caller terminates its new child; keep the argument allocated until then.
    require(WaitForSingleObject(thread.value,15000)==WAIT_OBJECT_0,"Wait for initialization thread");
    DWORD code{};require(GetExitCodeThread(thread.value,&code)!=0,"Initialization result");
    VirtualFreeEx(process,memory,0,MEM_RELEASE);return code;
}
void load_tracer(HANDLE process,const std::filesystem::path& dll,const std::filesystem::path& log) {
    const auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    require(load!=nullptr,"Find LoadLibraryW");
    HMODULE owner{};
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(load),&owner)!=0,"Locate loader module");
    wchar_t owner_path[32768]{};require(GetModuleFileNameW(owner,owner_path,32768)!=0,"Loader module path");
    const auto loader_remote=module_base(process,owner_path)+reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner);
    remote_call(process,loader_remote,dll.wstring());
    const auto remote_dll=module_base(process,dll); // Do not truncate a 64-bit HMODULE to thread exit code.
    auto local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    require(local!=nullptr,"Read tracer export");
    const auto start=GetProcAddress(local,"swvr_trace_start");
    const auto rva=reinterpret_cast<uintptr_t>(start)-reinterpret_cast<uintptr_t>(local);
    FreeLibrary(local);require(start!=nullptr,"Tracer start export");
    const auto result=remote_call(process,remote_dll+rva,log.wstring());
    if(result)throw std::runtime_error("Tracer initialization returned "+std::to_string(result));
}
}
int wmain(int argc,wchar_t** argv) {
    PROCESS_INFORMATION pi{};bool created=false,attached=false,started=false;
    try {
        if(argc<3||argc>5){std::cerr<<"Usage: swvr_trace_launcher.exe PATH_TO_stormworks64.exe ABSOLUTE_LOG_PATH [--continue] [--dry-run]\n";return 2;}
        bool continue_autosave{},dry_run{};
        for(int i=3;i<argc;++i){const std::wstring_view option=argv[i];
            if(option==L"--continue"&&!continue_autosave)continue_autosave=true;
            else if(option==L"--dry-run"&&!dry_run)dry_run=true;
            else throw std::invalid_argument("Unknown or duplicate launcher option");}
        const auto exe=std::filesystem::canonical(argv[1]);
        const auto name=exe.filename().wstring();
        if(name!=L"stormworks64.exe" && name!=L"swvr_trace_test.exe")throw std::runtime_error("Expected Stormworks or the project trace test");
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        const auto dll=std::filesystem::canonical(std::filesystem::path(self).parent_path()/"swvr_gl_trace.dll");
        const auto log=std::filesystem::absolute(argv[2]);
        if(std::filesystem::exists(log))throw std::runtime_error("Refusing to overwrite an existing capture; choose a new path");
        if(continue_autosave&&name!=L"stormworks64.exe")throw std::invalid_argument("Continue is only supported for Stormworks");
        std::wstring command=L"\""+exe.wstring()+L"\""+(continue_autosave?L" -continue":L"");
        if(dry_run){std::wcout<<L"Dry run; no process created: "<<command<<std::endl;return 0;}
        std::filesystem::create_directories(log.parent_path());
        // Test host uses the trace already initialized by this launcher.
        if(name==L"swvr_trace_test.exe")SetEnvironmentVariableW(L"SWVR_EXTERNAL_TRACE",log.c_str());
        STARTUPINFOW si{};si.cb=sizeof(si);
        require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS,
                               nullptr,exe.parent_path().c_str(),&si,&pi)!=0,"Create game under startup debugger");
        created=attached=true;
        uintptr_t entry{};unsigned char original{};bool gated=false;
        const auto deadline=GetTickCount64()+30000;
        while(!gated && GetTickCount64()<deadline) {
            DEBUG_EVENT event{};
            if(!WaitForDebugEvent(&event,1000)) {
                require(GetLastError()==ERROR_SEM_TIMEOUT,"Wait startup debug event");continue;
            }
            DWORD continuation=DBG_CONTINUE;
            if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
                const auto base=reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage);
                const auto dos=read<IMAGE_DOS_HEADER>(pi.hProcess,base);
                const auto nt=read<IMAGE_NT_HEADERS64>(pi.hProcess,base+dos.e_lfanew);
                require(dos.e_magic==IMAGE_DOS_SIGNATURE && nt.Signature==IMAGE_NT_SIGNATURE &&
                        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64,"Validate x64 PE image");
                entry=base+nt.OptionalHeader.AddressOfEntryPoint;
                original=read<unsigned char>(pi.hProcess,entry);write_code(pi.hProcess,entry,0xCC);
                if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
            } else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
                if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
            } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
                auto& exception=event.u.Exception.ExceptionRecord;
                if(exception.ExceptionCode==EXCEPTION_BREAKPOINT &&
                   reinterpret_cast<uintptr_t>(exception.ExceptionAddress)==entry) {
                    write_code(pi.hProcess,entry,original);
                    CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
                    require(GetThreadContext(pi.hThread,&context)!=0,"Read startup context");
                    context.Rip=entry;require(SetThreadContext(pi.hThread,&context)!=0,"Restore entry context");
                    require(SuspendThread(pi.hThread)!=DWORD(-1),"Hold entry thread");gated=true;
                } else if(exception.ExceptionCode!=EXCEPTION_BREAKPOINT) continuation=DBG_EXCEPTION_NOT_HANDLED;
            } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
                ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);
                throw std::runtime_error("Child exited before application entry: "+std::to_string(event.u.ExitProcess.dwExitCode));
            }
            require(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,continuation)!=0,"Continue startup event");
        }
        if(!gated)throw std::runtime_error("Startup entry timeout");
        require(DebugActiveProcessStop(pi.dwProcessId)!=0,"Detach startup debugger");attached=false;
        load_tracer(pi.hProcess,dll,log);
        require(ResumeThread(pi.hThread)!=DWORD(-1),"Resume game");started=true;
        std::wcout<<L"Tracer initialized before application entry. PID="<<pi.dwProcessId<<L" log="<<log<<std::endl;
        CloseHandle(pi.hThread);pi.hThread=nullptr;
        // Keep an authoritative process handle until the game exits.
        require(WaitForSingleObject(pi.hProcess,INFINITE)==WAIT_OBJECT_0,"Wait game exit");
        DWORD result{};GetExitCodeProcess(pi.hProcess,&result);CloseHandle(pi.hProcess);pi.hProcess=nullptr;
        std::cout<<"Child exit code: "<<result<<'\n';return result==0?0:1;
    } catch(const std::exception& e) {
        std::cerr<<"ERROR: "<<e.what()<<'\n';
        if(created&&!started)TerminateProcess(pi.hProcess,1);
        if(attached)DebugActiveProcessStop(pi.dwProcessId);
        if(pi.hThread)CloseHandle(pi.hThread);if(pi.hProcess)CloseHandle(pi.hProcess);
        return 1;
    }
}
