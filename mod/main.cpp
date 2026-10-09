#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Psapi.h>

static void MakeRegionWritable(DWORD base, DWORD size)
{
    constexpr DWORD regionSize = 0x10000;
    DWORD end = base + size;
    DWORD extra = (DWORD)base & (regionSize - 1);
    DWORD pageStart = base - extra;

    while (pageStart < end)
    {
        DWORD oldPerms;
        BOOL bSuccess = VirtualProtect((LPVOID)base, regionSize, PAGE_EXECUTE_READWRITE, &oldPerms);
        if (!bSuccess)
        {
            LPVOID message;
            DWORD error = GetLastError();
            FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&message,
                    0, NULL);
            MessageBox(NULL, (LPCTSTR)message, "XrdKeyboard", MB_OK);
            LocalFree(message);
        }
        pageStart += regionSize;
    }
}

extern "C" __declspec(dllexport) unsigned int RunInitThread(void*)
{
    // Prevent re-initialisation if injector run multiple times.
    static bool bInitialised = false;
    if (bInitialised)
    {
        return 1;
    }
    bInitialised = true;

    // Find module base
    const char* exeName = "GuiltyGearXrd.exe";
    HMODULE module = GetModuleHandleA(exeName);
    if (!module)
    {
        return 1;
    }

    HANDLE process = GetCurrentProcess();
    if (!process)
    {
        return 1;
    }

    MODULEINFO info;
    if (!GetModuleInformation(process, module, &info, sizeof(MODULEINFO)))
    {
        return 1;
    }
    DWORD moduleBase = (DWORD)(info.lpBaseOfDll);

    // code at jump point is something like:
    // for (int i = 0; i < NumJoysticks; ++i)
    // {
    //      AddNewJoystickIfAvailable(i);
    // }
    // We change the start to "int i = 1" so that the first controller slot is
    // never used so nothing ever overlaps with keyboard inputs.
    // this is done by replacing a register clear with a jump to some padding
    // instructions between functions that we've replaced with instructions
    // setting the register to 1.

    DWORD functionPadding = moduleBase + 0xc1e794;
    DWORD jumpPoint = moduleBase + 0xc1e801;

    MakeRegionWritable(functionPadding, 7);
    MakeRegionWritable(jumpPoint, 2);

    BYTE* paddingBytes = (BYTE*)(functionPadding);
    BYTE* jumpBytes = (BYTE*)(jumpPoint);

    // Jump to padding bytes
    jumpBytes[0] = 0xeb;
    jumpBytes[1] = 0x91;

    // mov ebx 1
    // then jump to just after the instruction that jumped here
    paddingBytes[0] = 0xbb;
    paddingBytes[1] = 0x01;
    paddingBytes[2] = 0x00;
    paddingBytes[3] = 0x00;
    paddingBytes[4] = 0x00;
    paddingBytes[5] = 0xeb;
    paddingBytes[6] = 0x68;

    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    return TRUE;
}
