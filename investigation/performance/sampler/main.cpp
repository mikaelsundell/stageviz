#include <windows.h>
#include <dbghelp.h>
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char **argv)
{
    if (argc!=3) return 2;
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,std::stoul(argv[1]));
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,std::stoul(argv[2]));
    if (!process || !thread) { std::cerr << "Open error " << GetLastError(); return 3; }
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    if (!SymInitialize(process,"C:\\git\\github\\stageviz\\deploy.Release",TRUE)) return 4;
    for (int sample=0;sample<12;++sample) {
        std::vector<DWORD64> addresses;
        CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
        LARGE_INTEGER start{},end{},frequency{};
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
        if (SuspendThread(thread)==DWORD(-1)) return 5;
        if (GetThreadContext(thread,&context)) {
            STACKFRAME64 frame{};
            frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;
            frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;
            frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;
            addresses.push_back(context.Rip);
            for (int depth=0;depth<24;++depth) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)) break;
                if (!frame.AddrPC.Offset) break;
                addresses.push_back(frame.AddrPC.Offset);
            }
        }
        ResumeThread(thread);
        QueryPerformanceCounter(&end);
        std::cout << "SAMPLE " << sample << " suspended_ms=" << 1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart << '\n';
        for (DWORD64 address:addresses) {
            IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);
            SymGetModuleInfo64(process,address,&module);
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};
            SYMBOL_INFO *symbol=reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;
            DWORD64 displacement=0;
            bool named=SymFromAddr(process,address,&displacement,symbol)!=FALSE;
            std::cout << "  " << module.ModuleName << "+0x" << std::hex << address-module.BaseOfImage << " " << (named?symbol->Name:"?") << "+0x" << displacement << std::dec << '\n';
        }
        std::cout.flush();
        Sleep(150);
    }
    SymCleanup(process);CloseHandle(thread);CloseHandle(process);
    return 0;
}
