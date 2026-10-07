// Temporary Windows arm64 crash diagnostic: calls luaL_newstate/lua_close and symbolizes any fault.
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

typedef void* (*newstate_fn)(void);
typedef void (*close_fn)(void*);

static HMODULE mod;
static newstate_fn newstate;
static close_fn close_state;

static void describe(const char* label, DWORD64 addr)
{
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buffer;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line = {sizeof(line)};
    DWORD ldisp = 0;
    printf("  %s 0x%llx (luau+0x%llx)", label, addr, addr - (DWORD64)mod);
    if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym))
        printf(" %s+0x%llx", sym->Name, disp);
    if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &ldisp, &line))
        printf(" [%s:%lu]", line.FileName, line.LineNumber);
    printf("\n");
}

static int filter(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    CONTEXT* c = ep->ContextRecord;
    printf("exception 0x%08lX\n", r->ExceptionCode);
    for (DWORD i = 0; i < r->NumberParameters; i++)
        printf("  info[%lu]=0x%llx\n", i, (unsigned long long)r->ExceptionInformation[i]);
    describe("pc", (DWORD64)r->ExceptionAddress);
#if defined(_M_ARM64)
    describe("lr", c->Lr);
    printf("  sp=0x%llx fp=0x%llx\n", c->Sp, c->Fp);
    for (int i = 0; i < 29; i++)
        printf("  x%d=0x%llx\n", i, c->X[i]);
    // Code bytes around pc
    unsigned int* pc = (unsigned int*)r->ExceptionAddress;
    for (int i = -8; i <= 4; i++)
        printf("  %s%p: %08x\n", i == 0 ? "=>" : "  ", (void*)(pc + i), pc[i]);
#endif
#if defined(_M_ARM64)
    CONTEXT unwind = *c;
    printf("fault stack:\n");
    for (int i = 0; i < 20 && unwind.Pc; i++)
    {
        describe("fault frame", unwind.Pc);
        DWORD64 imagebase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(unwind.Pc, &imagebase, NULL);
        if (!fn)
        {
            if (unwind.Pc == unwind.Lr)
                break;
            unwind.Pc = unwind.Lr;
        }
        else
        {
            PVOID handlerdata;
            DWORD64 establisher;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imagebase, unwind.Pc, fn, &unwind, &handlerdata, &establisher, NULL);
        }
    }
#endif
    void* frames[32];
    USHORT n = RtlCaptureStackBackTrace(0, 32, frames, NULL);
    printf("stack:\n");
    for (USHORT i = 0; i < n; i++)
        describe("frame", (DWORD64)frames[i]);
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS* ep)
{
    filter(ep);
    ExitProcess(3);
    return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI exercise(void* unused)
{
    (void)unused;
    __try
    {
        for (int i = 0; i < 100; i++)
        {
            void* L = newstate();
            if (!L)
                return 4;
            close_state(L);
        }
        printf("worker ok\n");
    }
    __except (filter(GetExceptionInformation()))
    {
        return 3;
    }
    return 0;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(unhandled);
    if (argc < 2)
        return 1;
    mod = LoadLibraryA(argv[1]);
    if (!mod)
    {
        printf("LoadLibrary failed %lu\n", GetLastError());
        return 2;
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    printf("loaded %s at %p\n", argv[1], (void*)mod);
    newstate = (newstate_fn)GetProcAddress(mod, "luaL_newstate");
    close_state = (close_fn)GetProcAddress(mod, "lua_close");
    printf("luaL_newstate at luau+0x%llx\n", (unsigned long long)((char*)newstate - (char*)mod));
    fflush(stdout);
    __try
    {
        void* L = newstate();
        printf("state %p\n", L);
        close_state(L);
        printf("main ok\n");
        HANDLE worker = CreateThread(NULL, 0, exercise, NULL, 0, NULL);
        WaitForSingleObject(worker, INFINITE);
        DWORD result;
        GetExitCodeThread(worker, &result);
        CloseHandle(worker);
        if (result != 0)
            return (int)result;
        printf("unloading DLL\n");
        FreeLibrary(mod);
        printf("unload ok\n");
        SymCleanup(GetCurrentProcess());
        printf("ok\n");
    }
    __except (filter(GetExceptionInformation()))
    {
        return 3;
    }
    return 0;
}
