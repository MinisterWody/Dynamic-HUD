// Dynamic HUD 1.0.
// Native F4SE plugin for per-element HUD visibility control, Power Armor HUD control, modded-HUD
// discovery/control, and context-aware Button Hints suppression.
// Standalone custom menus use Fallout's native IMenu display gate; HUDMenu/native-host children use
// the managed-GFx fallback. Both paths reuse the same visibility policy engine.
// Controls 30 normal HUDMenu objects plus seven logical Power Armor HUD groups. Power Armor groups use
// Bethesda's native NiAVObject::SetAppCulled visibility primitive on named live PADashDials01.nif scene objects.
// First Person / Third Person Show While triggers use PlayerCharacter::IMovementPlayerControlsFilter::IsInThirdPerson().
//
// VATS and Radiation Exposure use native game state. Radiation Exposure is driven by Bethesda's own
// HUDRadiationMeter CurrentRadsDisplayMagnitude pending-update semantics: valid positive updates latch
// exposure ON and explicit invalid updates latch exposure OFF. Bethesda still owns rendering/fades.
//
// Architecture
// ------------
// Dynamic HUD controls native visibility permission/request state for all 30 normal HUDMenu components and
// overlays native culling policy on seven logical Power Armor scene-graph groups.
// Bethesda remains responsible for rendering, content population, animation, and fades. Control OFF is
// true passthrough. Automatic mode evaluates the Show While reasons plus the per-object hotkey latch;
// non-Automatic mode uses ALWAYS HIDE / ALWAYS SHOW with the hotkey as the only dynamic trigger.
// Hide Delay applies only when an already-controlled object transitions into hidden state.
//
// Hotkeys
// -------
// MCM keyinput values are configuration only. Settings are parsed off the gameplay input path and cached
// in RAM. One BSInputEventUser-compatible native handler compares each keypress against those cached
// bindings. Objects that share an identical key+modifier combination are synchronized to one target latch
// state in the same callback, so shared bindings cannot drift out of phase.
//
// Compatibility / safety model
// ----------------------------
// runtime -> ABI family -> semantic Address Library IDs -> structural identity validation -> capability.
// Hook installation is transactional; failed contracts roll back or remain forwarding/fail-open. The four
// special components (Compass, Health, AP, Crosshair) remain hardened discovery sentinels; the other 26
// components use the generic native visibility gate after their vtable contracts pass.

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using s32 = int;
using s64 = long long;
using SIZE_T = unsigned long long;
using DWORD = unsigned long;
using HANDLE = void*;
using HMODULE = void*;
using BOOL = int;

struct FileTimeLite { DWORD lowDateTime; DWORD highDateTime; };
struct Win32FindDataA {
    DWORD fileAttributes;
    FileTimeLite creationTime;
    FileTimeLite lastAccessTime;
    FileTimeLite lastWriteTime;
    DWORD fileSizeHigh;
    DWORD fileSizeLow;
    DWORD reserved0;
    DWORD reserved1;
    char fileName[260];
    char alternateFileName[14];
};
static_assert(sizeof(Win32FindDataA)==320,"WIN32_FIND_DATAA layout mismatch");

enum class DebugLogLevel : s32 { Off=0, Errors=1, Verbose=2 };
static volatile long g_debugLogLevel=(long)DebugLogLevel::Errors;
static bool debug_errors_enabled(){return __atomic_load_n(&g_debugLogLevel,__ATOMIC_ACQUIRE)>=(long)DebugLogLevel::Errors;}
static bool debug_verbose_enabled(){return __atomic_load_n(&g_debugLogLevel,__ATOMIC_ACQUIRE)>=(long)DebugLogLevel::Verbose;}
static DebugLogLevel normalize_debug_level(s32 v){return v<=0?DebugLogLevel::Off:(v>=2?DebugLogLevel::Verbose:DebugLogLevel::Errors);}
static const char*debug_level_name(DebugLogLevel v){return v==DebugLogLevel::Off?"Off":(v==DebugLogLevel::Verbose?"Verbose":"Errors");}

#define WINAPI __stdcall
#define TRUE 1
#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE 0x10u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_EXECUTE_WRITECOPY 0x80u
#define PAGE_NOACCESS 0x01u
#define PAGE_GUARD 0x100u
#define MEM_COMMIT 0x1000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_READ 0x80000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define ERROR_FILE_EXISTS 80u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_FILE_NOT_FOUND 2u
#define ERROR_PATH_NOT_FOUND 3u
#define INVALID_HANDLE_VALUE ((HANDLE)(-1LL))
#define IMAGE_SCN_MEM_EXECUTE 0x20000000u
#define IMAGE_SCN_MEM_READ 0x40000000u
#define CSIDL_PERSONAL 0x0005
#define SHGFP_TYPE_CURRENT 0u

extern "C" {
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(const char* name);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(HMODULE module, char* path, DWORD size);
__declspec(dllimport) DWORD WINAPI GetLastError(void);
__declspec(dllimport) DWORD WINAPI GetFileAttributesA(const char* path);
__declspec(dllimport) BOOL WINAPI VirtualProtect(void* address, SIZE_T size, DWORD newProtect, DWORD* oldProtect);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE process, const void* address, SIZE_T size);
__declspec(dllimport) SIZE_T WINAPI VirtualQuery(const void* address, void* buffer, SIZE_T length);
__declspec(dllimport) HANDLE WINAPI CreateFileA(const char* name, DWORD access, DWORD share, void* sec, DWORD creation, DWORD flags, HANDLE templ);
__declspec(dllimport) HANDLE WINAPI CreateFileW(const wchar_t* name, DWORD access, DWORD share, void* sec, DWORD creation, DWORD flags, HANDLE templ);
__declspec(dllimport) BOOL WINAPI CreateDirectoryW(const wchar_t* path,void* securityAttributes);
__declspec(dllimport) BOOL WINAPI CreateDirectoryA(const char* path,void* securityAttributes);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE file, const void* data, DWORD bytes, DWORD* written, void* overlapped);
__declspec(dllimport) BOOL WINAPI FlushFileBuffers(HANDLE file);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE file, void* data, DWORD bytes, DWORD* read, void* overlapped);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE h);
__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) BOOL WINAPI ReadProcessMemory(HANDLE process, const void* base, void* buffer, SIZE_T size, SIZE_T* bytesRead);
__declspec(dllimport) BOOL WINAPI QueryPerformanceCounter(s64* value);
__declspec(dllimport) BOOL WINAPI QueryPerformanceFrequency(s64* value);
__declspec(dllimport) u32 WINAPI GetPrivateProfileIntA(const char* section,const char* key,s32 defaultValue,const char* fileName);
__declspec(dllimport) DWORD WINAPI GetPrivateProfileStringA(const char* section,const char* key,const char* defaultValue,char* returnedString,DWORD size,const char* fileName);
__declspec(dllimport) HANDLE WINAPI FindFirstChangeNotificationA(const char* path,BOOL watchSubtree,DWORD notifyFilter);
__declspec(dllimport) BOOL WINAPI FindNextChangeNotification(HANDLE changeHandle);
__declspec(dllimport) BOOL WINAPI FindCloseChangeNotification(HANDLE changeHandle);
__declspec(dllimport) HANDLE WINAPI FindFirstFileA(const char* fileName,Win32FindDataA* findData);
__declspec(dllimport) BOOL WINAPI FindNextFileA(HANDLE findFile,Win32FindDataA* findData);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE findFile);
__declspec(dllimport) DWORD WINAPI WaitForSingleObject(HANDLE handle,DWORD milliseconds);
__declspec(dllimport) HANDLE WINAPI CreateThread(void* attributes,SIZE_T stackSize,DWORD (WINAPI *startRoutine)(void*),void* parameter,DWORD creationFlags,DWORD* threadId);
__declspec(dllimport) void WINAPI Sleep(DWORD milliseconds);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int virtualKey);
__declspec(dllimport) long WINAPI SHGetFolderPathW(void* hwndOwner,int csidl,HANDLE token,DWORD flags,wchar_t* path);
}

extern "C" int _fltused = 0;
extern "C" void* memcpy(void* dst, const void* src, SIZE_T n){u8*d=(u8*)dst;const u8*s=(const u8*)src;for(SIZE_T i=0;i<n;++i)d[i]=s[i];return dst;}
extern "C" void* memset(void* dst, int c, SIZE_T n){u8*d=(u8*)dst;for(SIZE_T i=0;i<n;++i)d[i]=(u8)c;return dst;}
extern "C" long _InterlockedCompareExchange(volatile long* destination,long exchange,long comparand);
extern "C" long long _InterlockedIncrement64(volatile long long* addend);
extern "C" long _InterlockedExchange(volatile long* target,long value);
extern "C" void* _InterlockedExchangePointer(void* volatile* target,void* value);
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedIncrement64)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchangePointer)

static constexpr u32 kPluginVersion = 1u;
static constexpr u32 kAddressIndependence_AddressLibrary_1_10_980 = 1u << 1;
static constexpr u32 kAddressIndependence_AddressLibrary_1_11_137 = 1u << 2;
static constexpr u32 kStructureIndependence_1_10_980Layout = 1u << 1;
static constexpr u32 kStructureIndependence_1_11_137Layout = 1u << 2;

struct F4SEInterface {
    u32 f4seVersion;
    u32 runtimeVersion;
    u32 editorVersion;
    u32 isEditor;
    void* (*QueryInterface)(u32 id);
    u32 (*GetPluginHandle)(void);
    u32 (*GetReleaseIndex)(void);
    const void* (*GetPluginInfo)(const char* name);
    const char* (*GetSaveFolderName)(void);
};

struct PluginInfo {
    u32 infoVersion;
    const char* name;
    u32 version;
};

static constexpr u32 kInterface_Messaging = 1u;
struct F4SEMessagingInterface {
    struct Message { const char* sender; u32 type; u32 dataLen; void* data; };
    using EventCallback = void (*)(Message* message);
    u32 interfaceVersion;
    bool (*RegisterListener)(u32 listener,const char* sender,EventCallback handler);
    bool (*Dispatch)(u32 sender,u32 messageType,void* data,u32 dataLen,const char* receiver);
    void* (*GetEventDispatcher)(u32 dispatcherId);
};
static constexpr u32 kMessage_PreLoadGame=2u;
static constexpr u32 kMessage_PostLoadGame=3u;
static constexpr u32 kMessage_PreSaveGame=4u;
static constexpr u32 kMessage_PostSaveGame=5u;
static constexpr u32 kMessage_NewGame=8u;
static constexpr u32 kMessage_GameLoaded=9u;
static constexpr u32 kMessage_GameDataReady=10u;

struct F4SEPluginVersionData {
    u32 dataVersion;
    u32 pluginVersion;
    char name[256];
    char author[256];
    u32 addressIndependence;
    u32 structureIndependence;
    u32 compatibleVersions[16];
    u32 seVersionRequired;
    u32 reservedNonBreaking;
    u32 reservedBreaking;
    u8 reserved[512];
};

extern "C" __declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version = {
    1,
    kPluginVersion,
    "DynamicHUD",
    "MinisterWody",
    kAddressIndependence_AddressLibrary_1_10_980 | kAddressIndependence_AddressLibrary_1_11_137,
    kStructureIndependence_1_10_980Layout | kStructureIndependence_1_11_137Layout,
    {0},
    0,0,0,{0}
};

struct Range { u8* begin; u8* end; };
struct ImageInfo {
    u8* base;
    u32 size;
    Range readable[32]; u32 readableCount;
    Range executable[16]; u32 executableCount;
};
struct MemoryBasicInformation {
    void* baseAddress;
    void* allocationBase;
    u32 allocationProtect;
    u16 partitionId;
    u16 pad16;
    SIZE_T regionSize;
    u32 state;
    u32 protect;
    u32 type;
    u32 pad2C;
};
static_assert(sizeof(MemoryBasicInformation)==0x30,"MEMORY_BASIC_INFORMATION x64 layout mismatch");
struct CompleteObjectLocator64 {
    u32 signature;
    u32 offset;
    u32 cdOffset;
    u32 typeDescriptorRVA;
    u32 classDescriptorRVA;
    u32 selfRVA;
};
struct AddressMapping { u64 id; u64 offset; };
static_assert(sizeof(AddressMapping)==16,"AddressMapping layout mismatch");
struct SmallHUDArrayView {
    u32 capFlags;
    u32 pad;
    union { void* heap; u8 local[0x100]; } storage;
    u32 size;
    u32 pad2;
};
static_assert(sizeof(SmallHUDArrayView)==0x110,"Small HUD array layout mismatch");

static HMODULE g_module=0;
static HANDLE g_process=0;
static HANDLE g_log=INVALID_HANDLE_VALUE;
// Logging architecture: gameplay threads only enqueue prepared text into a fixed
// in-memory ring. A dedicated writer thread owns normal WriteFile traffic. The file
// lock is therefore never taken by ordinary HUD/state/policy logging. Lifecycle
// boundaries may explicitly drain + flush the queue, matching PLH's safe-boundary
// logging discipline.
static volatile long g_logFileLock=0;
static volatile long g_logQueueLock=0;
static volatile long g_logWriterStarted=0;
static volatile long g_logSynchronousFallback=0;
static constexpr u32 kLogQueueCapacity=512u;
static constexpr u32 kLogRecordCapacity=2048u;
struct LogRecord { u32 length; char text[kLogRecordCapacity]; };
static LogRecord g_logQueue[kLogQueueCapacity]={};
static u32 g_logQueueRead=0u,g_logQueueWrite=0u,g_logQueueCount=0u;
static volatile long long g_logQueued=0;
static volatile long long g_logDropped=0;
static volatile long long g_logWritten=0;
static volatile long g_started=0;
static volatile long g_settingsLock=0;
static volatile long g_settingsPending=0;
static volatile long g_settingsWatcherStarted=0;
static volatile long g_lifecycleSuspend=0;
static volatile long g_lifecycleResetRequested=0;
static __forceinline long atomic_load_long(const volatile long* value){return __atomic_load_n(value,__ATOMIC_ACQUIRE);}
static __forceinline void atomic_store_long(volatile long* value,long next){__atomic_store_n(value,next,__ATOMIC_RELEASE);}
static __forceinline long atomic_exchange_long(volatile long* value,long next){return __atomic_exchange_n(value,next,__ATOMIC_ACQ_REL);}
static __forceinline long long atomic_load_long_long(const volatile long long* value){return __atomic_load_n(value,__ATOMIC_ACQUIRE);}
static __forceinline long lifecycle_suspend_load_acquire(){return atomic_load_long(&g_lifecycleSuspend);}
static __forceinline void lifecycle_suspend_store_release(long value){atomic_store_long(&g_lifecycleSuspend,value);}
static __forceinline long lifecycle_suspend_exchange(long value){return atomic_exchange_long(&g_lifecycleSuspend,value);}

static bool g_perfTelemetryEnabled=true;
struct PerfBucket {u64 calls;u64 totalTicks;u64 maxTicks;};
static PerfBucket g_perfPluginPre={};
static PerfBucket g_perfPluginSteady={};
static PerfBucket g_perfPluginMaintenance={};
static PerfBucket g_perfPluginDetailed={};
static PerfBucket g_perfSettingsCommit={};
static PerfBucket g_perfBindingValidation={};
static PerfBucket g_perfDiscovery={};
static PerfBucket g_perfStatePolicy={};
static PerfBucket g_perfStateSample={};
static PerfBucket g_perfStateCore={};
static PerfBucket g_perfActorValues={};
static PerfBucket g_perfCrosshair={};
static PerfBucket g_perfPolicyEval={};
static PerfBucket g_perfDelayedGates={};
static PerfBucket g_perfOriginalAdvance={};
static PerfBucket g_perfPAFull={};
static PerfBucket g_perfPAPre={};
static PerfBucket g_perfPAPost={};
static PerfBucket g_perfPAAwait={};
static PerfBucket g_perfPABind={};
static PerfBucket g_perfCustomBefore={};
static PerfBucket g_perfCustomAfter={};
static PerfBucket g_perfButtonHintsTotal={};
static PerfBucket g_perfButtonHintsClassify={};
static PerfBucket g_perfButtonHintsGFx={};
static u64 g_perfButtonHintsHookCalls=0,g_perfButtonHintsClassifyCalls=0,g_perfButtonHintsTransitions=0;
static u64 g_perfSafeReadCalls=0,g_perfSafeReadBytes=0,g_perfSafeReadFailures=0,g_perfVirtualQueryCalls=0,g_perfReadProcessCalls=0;
static u64 g_perfGateCalls=0,g_perfGateManaged=0,g_perfGateVeto=0;
static u64 g_perfAPRequestCalls=0,g_perfAPRequestAllows=0,g_perfAPRequestDenies=0;
static u64 g_perfHealthRequestCalls=0,g_perfHealthRequestAllows=0,g_perfHealthRequestDenies=0;
static u64 g_perfSettingsCommits=0;
static u64 g_perfWindowStartFrame=0,g_perfSampledFrames=0,g_perfDetailedFrames=0;
static bool g_perfSampleThisFrame=false; // detailed phase profiling only
static s64 perf_qpc();
static void perf_add(PerfBucket&b,s64 start,s64 end);
static ImageInfo g_image={};
static AddressMapping g_addressChunk[4096]={};

static SIZE_T str_len(const char*s){SIZE_T n=0;if(!s)return 0;while(s[n])++n;return n;}
static bool str_eq(const char*a,const char*b){if(!a||!b)return false;SIZE_T i=0;for(;;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}}
static char ascii_lower(char c){return c>='A'&&c<='Z'?(char)(c+('a'-'A')):c;}
static bool str_ieq(const char*a,const char*b){if(!a||!b)return false;for(;;){char ca=ascii_lower(*a++),cb=ascii_lower(*b++);if(ca!=cb)return false;if(!ca)return true;}}
static int str_icompare(const char*a,const char*b){if(!a)a="";if(!b)b="";for(;;){char ca=ascii_lower(*a++),cb=ascii_lower(*b++);if(ca<cb)return -1;if(ca>cb)return 1;if(!ca)return 0;}}
static bool copy_text(char*out,SIZE_T cap,const char*src){if(!out||cap<1u)return false;SIZE_T p=0;while(src&&src[p]&&p+1u<cap){out[p]=src[p];++p;}out[p]=0;return !src||!src[p];}
static void write_log_bytes_sync(const char*data,u32 length){if(g_log==INVALID_HANDLE_VALUE||!data||!length)return;
    while(_InterlockedCompareExchange(&g_logFileLock,1,0)!=0){}u32 off=0;
    while(off<length){DWORD w=0;
    DWORD want=length-off;
    if(!WriteFile(g_log,data+off,want,&w,0)||w==0)break;
    off+=w;
    }_InterlockedExchange(&g_logFileLock,0);
    _InterlockedIncrement64(&g_logWritten);
    }
static bool pop_log_record(LogRecord*out){if(!out)return false;
    bool ok=false;
    while(_InterlockedCompareExchange(&g_logQueueLock,1,0)!=0){}if(g_logQueueCount){*out=g_logQueue[g_logQueueRead];
    g_logQueueRead=(g_logQueueRead+1u)%kLogQueueCapacity;
    --g_logQueueCount;
    ok=true;
    }_InterlockedExchange(&g_logQueueLock,0);
    return ok;
    }
static void drain_log_queue_sync(){LogRecord r={};while(pop_log_record(&r))write_log_bytes_sync(r.text,r.length);}
static void flush_log_file_sync(){if(g_log==INVALID_HANDLE_VALUE)return;
    while(_InterlockedCompareExchange(&g_logFileLock,1,0)!=0){}FlushFileBuffers(g_log);
    _InterlockedExchange(&g_logFileLock,0);
    }
static DWORD WINAPI log_writer_thread(void*){LogRecord r={};
    for(;;){bool wrote=false;
    while(pop_log_record(&r)){write_log_bytes_sync(r.text,r.length);
    wrote=true;
    }Sleep(wrote?1u:25u);
    }}
static bool start_log_writer(){if(g_log==INVALID_HANDLE_VALUE)return false;
    if(_InterlockedCompareExchange(&g_logWriterStarted,1,0)!=0)return true;
    DWORD tid=0;
    HANDLE thread=CreateThread(0,0,&log_writer_thread,0,0,&tid);
    if(!thread||thread==INVALID_HANDLE_VALUE){_InterlockedExchange(&g_logWriterStarted,0);
    _InterlockedExchange(&g_logSynchronousFallback,1);
    return false;
    }CloseHandle(thread);
    return true;
    }
static void log_raw(const char*s){if(g_log==INVALID_HANDLE_VALUE||!s)return;
    SIZE_T full=str_len(s);
    if(!full)return;
    if(_InterlockedCompareExchange(&g_logSynchronousFallback,0,0)!=0){u32 len=(u32)(full>0xFFFFFFFFULL?0xFFFFFFFFULL:full);
    write_log_bytes_sync(s,len);
    return;
    }u32 len=(u32)(full>=(SIZE_T)kLogRecordCapacity?(SIZE_T)kLogRecordCapacity-1ULL:full);
    while(_InterlockedCompareExchange(&g_logQueueLock,1,0)!=0){}if(g_logQueueCount>=kLogQueueCapacity){_InterlockedExchange(&g_logQueueLock,0);
    _InterlockedIncrement64(&g_logDropped);
    return;
    }LogRecord&r=g_logQueue[g_logQueueWrite];
    r.length=len;
    for(u32 i=0;i<len;++i)r.text[i]=s[i];
    r.text[len]=0;
    g_logQueueWrite=(g_logQueueWrite+1u)%kLogQueueCapacity;
    ++g_logQueueCount;
    _InterlockedExchange(&g_logQueueLock,0);
    _InterlockedIncrement64(&g_logQueued);
    }
// Capacity-aware fixed-buffer logging helpers. The old helpers assumed every destination was ~2 KiB,
// even when callers supplied much smaller stack buffers. These routines always reserve one byte for NUL.
static void append_char_cap(char* out,SIZE_T cap,SIZE_T* pos,char value){
    if(!out||!pos||cap==0u)return;
    if(*pos+1u<cap)out[(*pos)++]=value;
    out[*pos<cap?*pos:cap-1u]=0;
}
static void append_text_cap(char* out,SIZE_T cap,SIZE_T* pos,const char* text){
    if(!out||!pos||cap==0u||!text)return;
    while(*text&&*pos+1u<cap)out[(*pos)++]=*text++;
    out[*pos<cap?*pos:cap-1u]=0;
}
static void append_u64_cap(char* out,SIZE_T cap,SIZE_T* pos,u64 value){
    char tmp[32];int count=0;
    do{tmp[count++]=(char)('0'+(value%10ULL));value/=10ULL;}while(value&&count<31);
    while(count)append_char_cap(out,cap,pos,tmp[--count]);
}
static void append_s64_cap(char* out,SIZE_T cap,SIZE_T* pos,s64 value){
    if(value<0){append_char_cap(out,cap,pos,'-');u64 mag=(u64)(-(value+1))+1ULL;append_u64_cap(out,cap,pos,mag);}
    else append_u64_cap(out,cap,pos,(u64)value);
}
static void append_hex64_cap(char* out,SIZE_T cap,SIZE_T* pos,u64 value){
    static const char* hex="0123456789ABCDEF";
    append_char_cap(out,cap,pos,'0');append_char_cap(out,cap,pos,'x');
    for(int i=15;i>=0;--i)append_char_cap(out,cap,pos,hex[(value>>(i*4))&15ULL]);
}
static void line_end_cap(char* out,SIZE_T cap,SIZE_T* pos){
    append_char_cap(out,cap,pos,'\r');append_char_cap(out,cap,pos,'\n');
}
template<SIZE_T N> static void append_text(char (&out)[N],SIZE_T* pos,const char* text){append_text_cap(out,N,pos,text);}
template<SIZE_T N> static void append_u64(char (&out)[N],SIZE_T* pos,u64 value){append_u64_cap(out,N,pos,value);}
template<SIZE_T N> static void append_s64(char (&out)[N],SIZE_T* pos,s64 value){append_s64_cap(out,N,pos,value);}
template<SIZE_T N> static void append_hex64(char (&out)[N],SIZE_T* pos,u64 value){append_hex64_cap(out,N,pos,value);}
template<SIZE_T N> static void line_end(char (&out)[N],SIZE_T* pos){line_end_cap(out,N,pos);}
static __declspec(noinline) void log_line(const char*s){if(!s)return;
    char b[kLogRecordCapacity];
    SIZE_T p=0;
    while(s[p]&&p+3u<kLogRecordCapacity){b[p]=s[p];
    ++p;
    }b[p++]='\r';
    b[p++]='\n';
    b[p]=0;
    log_raw(b);
    }
static void log_hex(const char*prefix,u64 v){char b[320];SIZE_T p=0;append_text(b,&p,prefix);append_hex64(b,&p,v);line_end(b,&p);log_raw(b);}
static void error_log_line(const char*s){if(debug_errors_enabled())log_line(s);}
static void verbose_log_line(const char*s){if(debug_verbose_enabled())log_line(s);}
static void error_log_raw(const char*s){if(debug_errors_enabled())log_raw(s);}
static void verbose_log_raw(const char*s){if(debug_verbose_enabled())log_raw(s);}
static void verbose_log_hex(const char*prefix,u64 v){if(debug_verbose_enabled())log_hex(prefix,v);}

static bool build_own_file_path(const char*name,char*out,SIZE_T cap){
    if(!name||!out||cap<8||!g_module)return false;
    DWORD n=GetModuleFileNameA(g_module,out,(DWORD)cap);if(!n||n>=cap)return false;
    SIZE_T cut=n;while(cut>0&&out[cut-1]!='\\'&&out[cut-1]!='/')--cut;
    SIZE_T i=0;while(name[i]&&cut+i+1<cap){out[cut+i]=name[i];++i;}if(name[i])return false;out[cut+i]=0;return true;
}

static bool append_path_text_w(wchar_t*out,SIZE_T cap,SIZE_T*pos,const wchar_t*text){
    if(!out||!pos||!text||cap==0u)return false;
    SIZE_T i=0;while(text[i]){if(*pos+1u>=cap)return false;out[(*pos)++]=text[i++];}
    out[*pos]=0;return true;
}
static bool build_f4se_log_path(const wchar_t*name,wchar_t*out,SIZE_T cap){
    if(!name||!out||cap<64u)return false;
    wchar_t documents[260]={};
    if(SHGetFolderPathW(0,CSIDL_PERSONAL,0,SHGFP_TYPE_CURRENT,documents)!=0)return false;
    SIZE_T p=0;
    if(!append_path_text_w(out,cap,&p,documents))return false;
    if(!append_path_text_w(out,cap,&p,L"\\My Games"))return false;
    CreateDirectoryW(out,0);
    if(!append_path_text_w(out,cap,&p,L"\\Fallout4"))return false;
    CreateDirectoryW(out,0);
    if(!append_path_text_w(out,cap,&p,L"\\F4SE"))return false;
    CreateDirectoryW(out,0);
    if(!append_path_text_w(out,cap,&p,L"\\"))return false;
    return append_path_text_w(out,cap,&p,name);
}

static bool build_game_file_path(const char*relative,char*out,SIZE_T cap){
    if(!relative||!out||cap<8)return false;
    DWORD n=GetModuleFileNameA(0,out,(DWORD)cap);if(!n||n>=cap)return false;
    SIZE_T cut=n;while(cut>0&&out[cut-1]!='\\'&&out[cut-1]!='/')--cut;
    SIZE_T i=0;while(relative[i]&&cut+i+1<cap){out[cut+i]=relative[i];++i;}if(relative[i])return false;out[cut+i]=0;return true;
}

static bool is_readable_process_range(const void*a,SIZE_T n){if(!a||!n)return false;
    if(g_perfTelemetryEnabled)++g_perfVirtualQueryCalls;
    MemoryBasicInformation mbi={};
    if(VirtualQuery(a,&mbi,sizeof(mbi))!=sizeof(mbi))return false;
    if(mbi.state!=MEM_COMMIT||(mbi.protect&(PAGE_NOACCESS|PAGE_GUARD))!=0)return false;
    u64 start=(u64)a,base=(u64)mbi.baseAddress;
    if(start<base||mbi.regionSize==0||base>~0ULL-mbi.regionSize||start>~0ULL-n)return false;
    return start+n<=base+mbi.regionSize;
    }
static bool is_writable_process_range(const void*a,SIZE_T n){if(!a||!n)return false;
    if(g_perfTelemetryEnabled)++g_perfVirtualQueryCalls;
    MemoryBasicInformation mbi={};
    if(VirtualQuery(a,&mbi,sizeof(mbi))!=sizeof(mbi))return false;
    if(mbi.state!=MEM_COMMIT||(mbi.protect&(PAGE_NOACCESS|PAGE_GUARD))!=0)return false;
    u32 writable=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
    if((mbi.protect&writable)==0u)return false;
    u64 start=(u64)a,base=(u64)mbi.baseAddress;
    if(start<base||mbi.regionSize==0||base>~0ULL-mbi.regionSize||start>~0ULL-n)return false;
    return start+n<=base+mbi.regionSize;
    }
static bool is_executable_process_address(const void*a){if(!a)return false;
    if(g_perfTelemetryEnabled)++g_perfVirtualQueryCalls;
    MemoryBasicInformation mbi={};
    if(VirtualQuery(a,&mbi,sizeof(mbi))!=sizeof(mbi))return false;
    if(mbi.state!=MEM_COMMIT||(mbi.protect&(PAGE_NOACCESS|PAGE_GUARD))!=0)return false;
    u32 executable=PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
    return (mbi.protect&executable)!=0u;
    }
static bool safe_write_u8(void*a,u8 value){if(!is_writable_process_range(a,1u))return false;*((volatile u8*)a)=value;return true;}
static bool safe_read_bytes(const void*a,void*out,SIZE_T n){bool perf=g_perfTelemetryEnabled;
    if(perf){++g_perfSafeReadCalls;
    g_perfSafeReadBytes+=(u64)n;
    }if(!a||!out||!n||!is_readable_process_range(a,n)){if(perf)++g_perfSafeReadFailures;
    return false;
    }SIZE_T got=0;
    if(perf)++g_perfReadProcessCalls;
    bool ok=ReadProcessMemory(g_process,a,out,n,&got)&&got==n;
    if(perf&&!ok)++g_perfSafeReadFailures;
    return ok;
    }
template<class T> static bool safe_read(const void*a,T*out){return safe_read_bytes(a,out,sizeof(T));}
static bool in_range(u64 a,const Range&r){return a>=(u64)r.begin&&a<(u64)r.end;}
static bool is_in_image(u64 a){return g_image.base&&a>=(u64)g_image.base&&a<(u64)(g_image.base+g_image.size);}
static bool is_readable_image(u64 a){for(u32 i=0;i<g_image.readableCount;++i)if(in_range(a,g_image.readable[i]))return true;return false;}
static bool is_executable_image(u64 a){for(u32 i=0;i<g_image.executableCount;++i)if(in_range(a,g_image.executable[i]))return true;return false;}

static bool parse_image(HMODULE module){
    if(!module)return false;u8*base=(u8*)module;u32 peOff=0;if(!safe_read(base+0x3C,&peOff))return false;
    u32 sig=0;if(!safe_read(base+peOff,&sig)||sig!=0x00004550u)return false;u16 sections=0,sizeOpt=0;
    if(!safe_read(base+peOff+6,&sections)||!safe_read(base+peOff+20,&sizeOpt))return false;u8*opt=base+peOff+24;u16 magic=0;
    if(!safe_read(opt,&magic)||magic!=0x20Bu)return false;u32 sizeImage=0;if(!safe_read(opt+0x38,&sizeImage)||!sizeImage)return false;
    g_image.base=base;g_image.size=sizeImage;g_image.readableCount=0;g_image.executableCount=0;u8*sec=opt+sizeOpt;
    for(u16 i=0;i<sections&&i<96;++i,sec+=40){u32 virtualSize=0,virtualAddress=0,rawSize=0,ch=0;
        if(!safe_read(sec+8,&virtualSize)||!safe_read(sec+12,&virtualAddress)||!safe_read(sec+16,&rawSize)||!safe_read(sec+36,&ch))return false;
        u32 span=virtualSize>rawSize?virtualSize:rawSize;if(!span)continue;u8*begin=base+virtualAddress;u8*end=begin+span;if(end>base+sizeImage)end=base+sizeImage;
        if((ch&IMAGE_SCN_MEM_READ)&&g_image.readableCount<32)g_image.readable[g_image.readableCount++]={begin,end};
        if((ch&IMAGE_SCN_MEM_EXECUTE)&&g_image.executableCount<16)g_image.executable[g_image.executableCount++]={begin,end};
    }
    return g_image.readableCount>0&&g_image.executableCount>0;
}

static bool bytes_equal(const u8*a,const char*b,SIZE_T n){for(SIZE_T i=0;i<n;++i)if(a[i]!=(u8)b[i])return false;return true;}
static u8* find_ascii_in_readable(const char*needle){SIZE_T n=str_len(needle);
    if(!n)return 0;
    for(u32 r=0;r<g_image.readableCount;++r){Range rg=g_image.readable[r];
    SIZE_T sz=(SIZE_T)(rg.end-rg.begin);
    if(sz<n)continue;
    for(SIZE_T i=0;i+n<=sz;++i)if(rg.begin[i]==(u8)needle[0]&&bytes_equal(rg.begin+i,needle,n))return rg.begin+i;
    }return 0;
    }
static u8* find_u32_in_readable(u32 wanted,u8*after){for(u32 r=0;r<g_image.readableCount;++r){Range rg=g_image.readable[r];
    if(after&&rg.end<=after)continue;
    u8*start=rg.begin;
    if(after&&after>=rg.begin&&after<rg.end)start=after+1;
    for(u8*p=start;p+4<=rg.end;++p){u32 v=0;
    memcpy(&v,p,4);
    if(v==wanted)return p;
    }}return 0;
    }
static u8* find_u64_in_readable(u64 wanted,u8*after){for(u32 r=0;r<g_image.readableCount;++r){Range rg=g_image.readable[r];
    if(after&&rg.end<=after)continue;
    u8*start=rg.begin;
    if(after&&after>=rg.begin&&after<rg.end)start=after+1;
    for(u8*p=start;p+8<=rg.end;++p){u64 v=0;
    memcpy(&v,p,8);
    if(v==wanted)return p;
    }}return 0;
    }
static bool validate_vtable_slots(void**vt,u32 requiredExecutableSlots){if(!vt||!requiredExecutableSlots||requiredExecutableSlots>64u||!is_readable_image((u64)vt))return false;
    for(u32 i=0;i<requiredExecutableSlots;++i){void*fn=0;
    if(!safe_read(vt+i,&fn)||!fn||!is_executable_image((u64)fn))return false;
    }return true;
    }
static void** find_primary_vtable_by_rtti_slots(const char*decoratedName,u32 requiredExecutableSlots){
    u8*name=find_ascii_in_readable(decoratedName);if(!name||name<g_image.base+16)return 0;u8*typeDesc=name-16;u32 typeRVA=(u32)(typeDesc-g_image.base);u8*scan=0;
    for(u32 attempts=0;attempts<256;++attempts){u8*hit=find_u32_in_readable(typeRVA,scan);
        if(!hit)return 0;
        scan=hit;
        if(hit<g_image.base+12)continue;
        u8*colAddr=hit-12;
        CompleteObjectLocator64 col={};
        if(!safe_read(colAddr,&col))continue;
        if(col.signature!=1||col.offset!=0||col.typeDescriptorRVA!=typeRVA||col.selfRVA!=(u32)(colAddr-g_image.base))continue;u8*ref=0;
        for(u32 refs=0;refs<128;++refs){ref=find_u64_in_readable((u64)colAddr,ref);
            if(!ref)break;
            void**vt=(void**)(ref+8);
            if(validate_vtable_slots(vt,requiredExecutableSlots))return vt;
            }
    }
    return 0;
}
static void** find_primary_vtable_by_rtti(const char*decoratedName){return find_primary_vtable_by_rtti_slots(decoratedName,7u);}
static bool copy_rtti_name(const void*object,char*out,SIZE_T cap){if(!out||cap<2){return false;
    }out[0]=0;
    if(!object)return false;
    void**vt=0;
    if(!safe_read(object,&vt)||!vt)return false;
    u64 colVA=0;
    if(!safe_read(((u64*)vt)-1,&colVA)||!is_in_image(colVA))return false;
    CompleteObjectLocator64 col={};
    if(!safe_read((void*)colVA,&col)||col.signature!=1)return false;
    u64 td=(u64)g_image.base+col.typeDescriptorRVA;
    if(!is_in_image(td+16))return false;
    for(SIZE_T i=0;i+1<cap;++i){char c=0;
    if(!safe_read((void*)(td+16+i),&c))break;
    out[i]=c;
    if(!c)return true;
    out[i+1]=0;
    }out[cap-1]=0;
    return out[0]!=0;
    }
static bool patch_ptr(void**slot,void*value,void**oldOut){if(!slot||!value||!is_readable_process_range(slot,sizeof(void*)))return false;
    void*old=0;
    if(!safe_read(slot,&old))return false;
    DWORD prot=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&prot))return false;
    _InterlockedExchangePointer((void* volatile*)slot,value);
    FlushInstructionCache(g_process,slot,sizeof(void*));
    DWORD ign=0;
    if(!VirtualProtect(slot,sizeof(void*),prot,&ign)){_InterlockedExchangePointer((void* volatile*)slot,old);
    FlushInstructionCache(g_process,slot,sizeof(void*));
    DWORD ign2=0;
    VirtualProtect(slot,sizeof(void*),prot,&ign2);
    return false;
    }if(oldOut)*oldOut=old;
    return true;
    }
static bool restore_ptr_if_owned(void**slot,void*expectedCurrent,void*original){if(!slot||!expectedCurrent||!original)return false;
    void*current=0;
    if(!safe_read(slot,&current)||current!=expectedCurrent)return false;
    void*replaced=0;
    return patch_ptr(slot,original,&replaced)&&replaced==expectedCurrent;
    }

struct RuntimeVersionParts { u32 major;u32 minor;u32 patch;u32 build; };
struct HUDAddressIDs {
    u64 playerSingleton;
    u64 actorIsSneaking;
    u64 actorSprintState;
    u64 actorInPowerArmor;
    u64 crosshairEventSource;
    u64 compassShouldBeVisible;
    u64 healthShouldBeVisible;
    u64 actionPointsShouldBeVisible;
    u64 crosshairShouldBeVisible;
    u64 hudAdvanceMovie;
    u64 menuControlsSingleton;
    u64 memoryManagerSingleton;
    u64 memoryManagerAllocate;
    u64 memoryManagerFree;
    // Optional production VATS capability. A missing mapping fails only VATS-selected policies open.
    u64 uiSingleton;
    u64 uiIsMenuOpen;
    u64 fixedStringCtor;
    // Optional vanilla MessageBoxMenu capability used only to report one-shot Custom HUD discovery results.
    u64 messageMenuManagerSingleton;
    u64 messageMenuManagerCreate;
    // Optional PowerArmorGeometry singleton used by the PA scene-group backend; never affects the normal HUD backend.
    u64 powerArmorGeometrySingleton;
    // Optional Scaleform APIs used by Custom HUD discovery and runtime control.
    u64 gfxObjectRelease;
    u64 gfxGetMember;
    u64 gfxSetMember;
    u64 gfxInvoke;
};
struct RuntimeProfile { const char* familyName; HUDAddressIDs ids; };

static constexpr RuntimeProfile kProfileOG={"OG",{
    303410ULL,1173627ULL,715442ULL,1176757ULL,1231665ULL,893789ULL,754327ULL,380119ULL,387983ULL,494688ULL,
    520890ULL,343176ULL,652767ULL,1582181ULL,
    548587ULL,1065114ULL,640273ULL,959572ULL,89563ULL,1365745ULL,
    856221ULL,1517430ULL,1360149ULL,655847ULL}};
static constexpr RuntimeProfile kProfileNG={"NG",{
    2690919ULL,2207655ULL,2231469ULL,2219437ULL,2694517ULL,2220160ULL,2221461ULL,2219880ULL,2220249ULL,2248853ULL,
    2689089ULL,2193197ULL,2267872ULL,2267874ULL,
    2689028ULL,2284747ULL,2268666ULL,2689087ULL,2249456ULL,2694441ULL,
    2286229ULL,2285936ULL,2286589ULL,2286101ULL}};
static constexpr RuntimeProfile kProfileAE={"AE",{
    4798212ULL,2207655ULL,2231469ULL,2219437ULL,4801808ULL,2220160ULL,2221461ULL,2219880ULL,2220249ULL,2248853ULL,
    4796375ULL,4471522ULL,2267872ULL,2267874ULL,
    4796314ULL,2284747ULL,2268666ULL,4796373ULL,2249456ULL,4801732ULL,
    2286229ULL,4494126ULL,2286589ULL,2286101ULL}};

static u32 g_runtimeVersion=0;
static const RuntimeProfile* g_profile=0;
static RuntimeVersionParts decode_runtime(u32 packed){return {(packed>>24)&0xFFu,(packed>>16)&0xFFu,(packed>>4)&0x0FFFu,packed&0x0Fu};}
static const RuntimeProfile* select_runtime_profile(u32 packed){RuntimeVersionParts v=decode_runtime(packed);
    if(v.major==1u&&v.minor==11u)return &kProfileAE;
    if(v.major==1u&&v.minor==10u)return v.patch>=980u?&kProfileNG:&kProfileOG;
    return 0;
    }
template<SIZE_T N> static void append_runtime(char (&b)[N],SIZE_T* p,u32 v){
    RuntimeVersionParts x=decode_runtime(v);
    append_u64(b,p,x.major);append_char_cap(b,N,p,'.');append_u64(b,p,x.minor);append_char_cap(b,N,p,'.');
    append_u64(b,p,x.patch);append_char_cap(b,N,p,'.');append_u64(b,p,x.build);
}
static bool build_al_filename(u32 packed,char* out,SIZE_T cap){
    if(!out||cap<32u)return false;
    SIZE_T p=0;RuntimeVersionParts v=decode_runtime(packed);
    append_text_cap(out,cap,&p,"version-");append_u64_cap(out,cap,&p,v.major);append_char_cap(out,cap,&p,'-');
    append_u64_cap(out,cap,&p,v.minor);append_char_cap(out,cap,&p,'-');append_u64_cap(out,cap,&p,v.patch);
    append_char_cap(out,cap,&p,'-');append_u64_cap(out,cap,&p,v.build);append_text_cap(out,cap,&p,".bin");
    return p+1u<cap;
}

enum class AddressKind : u32 { ReadableData=0, ExecutableCode=1 };
struct HUDAddressTable {
    u64 playerSingleton;
    u64 actorIsSneaking;
    u64 actorSprintState;
    u64 actorInPowerArmor;
    u64 crosshairEventSource;
    u64 compassShouldBeVisible;
    u64 healthShouldBeVisible;
    u64 actionPointsShouldBeVisible;
    u64 crosshairShouldBeVisible;
    u64 hudAdvanceMovie;
    u64 menuControlsSingleton;
    u64 memoryManagerSingleton;
    u64 memoryManagerAllocate;
    u64 memoryManagerFree;
    // Production VATS trigger capabilities; optional relative to the base HUD backend.
    u64 uiSingleton;
    u64 uiIsMenuOpen;
    u64 fixedStringCtor;
    u64 messageMenuManagerSingleton;
    u64 messageMenuManagerCreate;
    u64 powerArmorGeometrySingleton;
    u64 gfxObjectRelease;
    u64 gfxGetMember;
    u64 gfxSetMember;
    u64 gfxInvoke;
    bool scanCompleted;
};
static HUDAddressTable g_addresses={};
struct AddressSpec { u64 id;u64* address;const char*label;AddressKind kind;bool required; };

static __declspec(noinline) bool load_address_table(){
    if(g_addresses.scanCompleted)return true;if(!g_profile||!g_image.base)return false;
    const HUDAddressIDs& ids=g_profile->ids;
    AddressSpec specs[]={
        {ids.playerSingleton,&g_addresses.playerSingleton,"PlayerCharacter::Singleton",AddressKind::ReadableData,true},
        {ids.actorIsSneaking,&g_addresses.actorIsSneaking,"Actor::IsSneaking",AddressKind::ExecutableCode,true},
        {ids.actorSprintState,&g_addresses.actorSprintState,"ActorState::DoGetSprinting/vslot1F",AddressKind::ExecutableCode,true},
        {ids.actorInPowerArmor,&g_addresses.actorInPowerArmor,"PowerArmor::ActorInPowerArmor",AddressKind::ExecutableCode,false},
        {ids.crosshairEventSource,&g_addresses.crosshairEventSource,"PlayerCrosshairModeEvent::EventSource",AddressKind::ReadableData,true},
        {ids.compassShouldBeVisible,&g_addresses.compassShouldBeVisible,"HUDCompass::ShouldBeVisible",AddressKind::ExecutableCode,true},
        {ids.healthShouldBeVisible,&g_addresses.healthShouldBeVisible,"HUDPlayerHealthMeter::ShouldBeVisible",AddressKind::ExecutableCode,true},
        {ids.actionPointsShouldBeVisible,&g_addresses.actionPointsShouldBeVisible,"HUDActionPointMeter::ShouldBeVisible",AddressKind::ExecutableCode,true},
        {ids.crosshairShouldBeVisible,&g_addresses.crosshairShouldBeVisible,"HUDCrosshair::ShouldBeVisible",AddressKind::ExecutableCode,true},
        {ids.hudAdvanceMovie,&g_addresses.hudAdvanceMovie,"HUDMenu::AdvanceMovie",AddressKind::ExecutableCode,true},
        {ids.menuControlsSingleton,&g_addresses.menuControlsSingleton,"MenuControls::Singleton",AddressKind::ReadableData,true},
        {ids.memoryManagerSingleton,&g_addresses.memoryManagerSingleton,"MemoryManager::GetSingleton",AddressKind::ExecutableCode,true},
        {ids.memoryManagerAllocate,&g_addresses.memoryManagerAllocate,"MemoryManager::Allocate",AddressKind::ExecutableCode,true},
        {ids.memoryManagerFree,&g_addresses.memoryManagerFree,"MemoryManager::Free",AddressKind::ExecutableCode,true},
        {ids.uiSingleton,&g_addresses.uiSingleton,"UI::Singleton",AddressKind::ReadableData,false},
        {ids.uiIsMenuOpen,&g_addresses.uiIsMenuOpen,"UI::IsMenuOpen",AddressKind::ExecutableCode,false},
        {ids.fixedStringCtor,&g_addresses.fixedStringCtor,"BSFixedString::ctor",AddressKind::ExecutableCode,false},
        {ids.messageMenuManagerSingleton,&g_addresses.messageMenuManagerSingleton,"MessageMenuManager::Singleton",AddressKind::ReadableData,false},
        {ids.messageMenuManagerCreate,&g_addresses.messageMenuManagerCreate,"MessageMenuManager::Create",AddressKind::ExecutableCode,false},
        {ids.powerArmorGeometrySingleton,&g_addresses.powerArmorGeometrySingleton,"PowerArmorGeometry::Singleton",AddressKind::ReadableData,false},
        {ids.gfxObjectRelease,&g_addresses.gfxObjectRelease,"GFx::Value::ObjectRelease",AddressKind::ExecutableCode,false},
        {ids.gfxGetMember,&g_addresses.gfxGetMember,"GFx::Value::GetMember",AddressKind::ExecutableCode,false},
        {ids.gfxSetMember,&g_addresses.gfxSetMember,"GFx::Value::SetMember",AddressKind::ExecutableCode,false},
        {ids.gfxInvoke,&g_addresses.gfxInvoke,"GFx::Value::Invoke",AddressKind::ExecutableCode,false}
    };
    constexpr u32 count=(u32)(sizeof(specs)/sizeof(specs[0]));
    u32 requiredCount=0,wantedCount=0;for(u32 i=0;i<count;++i){if(specs[i].required)++requiredCount;if(specs[i].id)++wantedCount;}
    char fileName[64];if(!build_al_filename(g_runtimeVersion,fileName,sizeof(fileName))){error_log_line("[AddressLibrary] FAILED filename construction.");return false;}
    char path[1024];if(!build_own_file_path(fileName,path,sizeof(path))){error_log_line("[AddressLibrary] FAILED plugin-directory path construction.");return false;}
    char lb[512];SIZE_T lp=0;append_text(lb,&lp,"[AddressLibrary] database=");append_text(lb,&lp,fileName);line_end(lb,&lp);verbose_log_raw(lb);
    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
        if(f==INVALID_HANDLE_VALUE){error_log_line("[AddressLibrary] FAILED to open matching database beside plugin DLL.");
        return false;
        }
    u64 entries=0;
        DWORD got=0;
        if(!ReadFile(f,&entries,8,&got,0)||got!=8||entries==0||entries>10000000ULL){CloseHandle(f);
        error_log_line("[AddressLibrary] invalid database header.");
        return false;
        }
    u64 offsets[count]={};bool found[count]={};u32 foundRequired=0,foundWanted=0;u64 remaining=entries;bool readOK=true;
    while(remaining&&foundWanted<wantedCount){u32 take=(u32)(remaining>4096ULL?4096ULL:remaining);
        DWORD bytes=take*(DWORD)sizeof(AddressMapping);
        got=0;
        if(!ReadFile(f,g_addressChunk,bytes,&got,0)||got!=bytes){readOK=false;
        break;
        }
        for(u32 i=0;i<take&&foundWanted<wantedCount;++i){for(u32 r=0;r<count;++r){if(!specs[r].id||found[r])continue;
            if(g_addressChunk[i].id==specs[r].id){found[r]=true;
            offsets[r]=g_addressChunk[i].offset;
            ++foundWanted;
            if(specs[r].required)++foundRequired;
            break;
            }}}remaining-=take;
            }
    CloseHandle(f);
    if(!readOK&&foundRequired<requiredCount){error_log_line("[AddressLibrary] truncated/read-failed database before all required backend addresses were found.");return false;}
    if(!readOK)error_log_line("[Optional] Address Library scan truncated/read-failed after required backend addresses were complete.");
    u32 requiredFound=0,requiredUsable=0;
    for(u32 r=0;r<count;++r){
        if(!specs[r].id){if(!specs[r].required)continue;}
        char b[768];SIZE_T p=0;
        if(specs[r].required)append_text(b,&p,"[AddressLibrary] ");else append_text(b,&p,"[Optional] address ");
        append_text(b,&p,specs[r].label);append_text(b,&p," id=");append_u64(b,&p,specs[r].id);append_text(b,&p," offset=");append_hex64(b,&p,offsets[r]);
        bool usable=false;
        if(!found[r])append_text(b,&p,specs[r].required?" MISSING":" MISSING/INVALID");
        else {if(specs[r].required)++requiredFound;if(!offsets[r]||offsets[r]>=g_image.size)append_text(b,&p,specs[r].required?" REJECTED_OUT_OF_IMAGE":" MISSING/INVALID");
            else {u64 a=(u64)g_image.base+offsets[r];
                bool kindOK=specs[r].kind==AddressKind::ExecutableCode?is_executable_image(a):is_readable_image(a);
                append_text(b,&p," address=");
                append_hex64(b,&p,a);
                append_text(b,&p,kindOK?" PASS":" FAIL_KIND");
                if(kindOK){*specs[r].address=a;
                usable=true;
                if(specs[r].required)++requiredUsable;
                }}}
        line_end(b,&p);
        if(specs[r].required){if(usable)verbose_log_raw(b);else error_log_raw(b);}else log_raw(b);
    }
    g_addresses.scanCompleted=true;
    char sb[384];
        SIZE_T sp=0;
        append_text(sb,&sp,"[AddressLibrary] summary found=");
        append_u64(sb,&sp,requiredFound);
        append_text(sb,&sp," usable=");
        append_u64(sb,&sp,requiredUsable);
        append_text(sb,&sp," required=");
        append_u64(sb,&sp,requiredCount);
        line_end(sb,&sp);
        if(requiredUsable==requiredCount)verbose_log_raw(sb);
        else error_log_raw(sb);
    return true;
}

struct ContractResult { const char*label;const char*rtti;u32 slot;u64 expected;void**vtable;bool pass; };
static __declspec(noinline) bool validate_contract(ContractResult* c){if(!c||!c->rtti||!c->expected)return false;
    c->vtable=find_primary_vtable_by_rtti(c->rtti);
    void*observed=0;
    if(c->vtable)safe_read(c->vtable+c->slot,&observed);
    c->pass=c->vtable&&observed==(void*)c->expected;
    char b[900];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] contract=");
        append_text(b,&p,c->label);
        append_text(b,&p," rtti=");
        append_text(b,&p,c->rtti);
        append_text(b,&p," slot=");
        append_hex64(b,&p,c->slot);
        append_text(b,&p," vtable=");
        append_hex64(b,&p,(u64)c->vtable);
        append_text(b,&p," observed=");
        append_hex64(b,&p,(u64)observed);
        append_text(b,&p," expected=");
        append_hex64(b,&p,c->expected);
        append_text(b,&p,c->pass?" PASS":" FAIL");
        line_end(b,&p);
        if(c->pass)verbose_log_raw(b);
        else error_log_raw(b);
        return c->pass;
        }

static void** g_hudVtable=0;
static void* g_origAdvance=0;
static void** g_buttonBarVtable=0;
static void* g_origButtonBarAdvance=0;
static void* g_buttonBarExpectedAdvance=0;
static bool g_buttonBarContractPass=false;
static bool g_buttonHintsCapable=false;
enum class ButtonHintsContext:u32 { Unknown=0u,PipBoy=1u,Workbench=2u,VATS=3u,Menus=4u };
struct ButtonHintsRuntimeState {
    void* menuObject;
    bool appliedHidden;
    bool failureLogged;
    ButtonHintsContext context;
    char contextMenu[96];
};
static ButtonHintsRuntimeState g_buttonHintsRuntime={};
static bool g_componentContractsPass=false;
static bool g_sprintContractPass=false;
static bool g_hudAdvanceContractPass=false;
static void** g_componentVtables[4]={0,0,0,0};
static void** g_actorStateVtable=0;
static u64 g_advanceCount=0;
static void* g_lastHud=0;
static bool g_arrayResolved=false;
static u32 g_arrayOffset=0;
static u32 g_arrayCount=0;
static u32 g_arrayCapacity=0;
static bool g_arrayLocal=false;
static u32 g_componentArrayIndex[4]={0,0,0,0};
static void** g_arrayData=0;
static bool g_playerLogged=false;
static void* g_validatedPlayer=0;
static void* g_rejectedPlayer=0;
static const char* g_playerContractFailureStage="none";
static s64 g_qpcFreq=0,g_qpcStart=0;

static constexpr u32 kManagedCount=4u;
enum ManagedElementId : u32 { kCompass=0u,kHealth=1u,kActionPoints=2u,kCrosshair=3u };
// The four special components are discovery/identity sentinels only. Runtime policy state for all
// 30 objects lives exclusively in g_controlStates; keeping policy state here would create two truths.
struct ManagedElement {
    const char* name;
    const char* rtti;
    u64 expectedShould;
    void* object;
};
static ManagedElement g_elements[kManagedCount]={
    {"Compass",".?AVHUDCompass@@",0,0},
    {"Health",".?AVHUDPlayerHealthMeter@@",0,0},
    {"ActionPoints",".?AVHUDActionPointMeter@@",0,0},
    {"Crosshair",".?AVHUDCrosshair@@",0,0}
};
enum PolicyReason : u32 {
    kReasonWeapon=1u,kReasonAim=2u,kReasonSprint=4u,kReasonSneak=8u,kReasonCombat=16u,kReasonVATS=32u,
    kReasonLowHealth=64u,kReasonLowAP=128u,kReasonActivate=256u,kReasonCommand=512u,kReasonHotkey=1024u,kReasonRadiation=2048u,kReasonPowerArmor=4096u,
    kReasonFirstPerson=8192u,kReasonThirdPerson=16384u,kReasonWeaponHolstered=32768u,
    kReasonAlwaysHide=0x10000000u,kReasonAlwaysShow=0x20000000u,kReasonTelemetryFailOpen=0x40000000u
};
struct ConfigurableTriggerMetadata { u32 bit; const char* settingKey; const char* diagnosticName; };
static constexpr ConfigurableTriggerMetadata kConfigurableTriggers[]={
    {kReasonWeapon,"bWhenWeaponDrawn","WEAPON"},
    {kReasonWeaponHolstered,"bWhenWeaponHolstered","WEAPON_HOLSTERED"},
    {kReasonAim,"bWhenAiming","AIM"},
    {kReasonSprint,"bWhenSprinting","SPRINT"},
    {kReasonSneak,"bWhenSneaking","SNEAK"},
    {kReasonCombat,"bWhenCombat","COMBAT"},
    {kReasonVATS,"bWhenVATS","VATS"},
    {kReasonFirstPerson,"bWhenFirstPerson","FIRST_PERSON"},
    {kReasonThirdPerson,"bWhenThirdPerson","THIRD_PERSON"},
    {kReasonLowHealth,"bWhenLowHealth","LOW_HP"},
    {kReasonLowAP,"bWhenLowAP","LOW_AP"},
    {kReasonActivate,"bWhenActivate","ACTIVATE"},
    {kReasonCommand,"bWhenCommand","COMMAND"},
    {kReasonRadiation,"bWhenRadiation","RADIATION"},
    {kReasonPowerArmor,"bWhenPowerArmor","POWER_ARMOR"}
};
static constexpr u32 kConfigurableTriggerCount=(u32)(sizeof(kConfigurableTriggers)/sizeof(kConfigurableTriggers[0]));
static constexpr u32 configurable_trigger_mask(){
    u32 mask=0u;
    for(u32 i=0;i<kConfigurableTriggerCount;++i)mask|=kConfigurableTriggers[i].bit;
    return mask;
}
static constexpr u32 kAllConfigurableTriggers=configurable_trigger_mask();
static constexpr bool configurable_trigger_bits_unique(){
    for(u32 i=0;i<kConfigurableTriggerCount;++i){
        for(u32 j=i+1u;j<kConfigurableTriggerCount;++j){
            if(kConfigurableTriggers[i].bit==kConfigurableTriggers[j].bit)return false;
        }
    }
    return true;
}
static_assert(kConfigurableTriggerCount==15u,"Unexpected configurable trigger count");
static_assert(configurable_trigger_bits_unique(),"Configurable trigger reason bits must be unique");
static_assert(kAllConfigurableTriggers==0xFBFFu,"Configurable trigger mask changed; review sampling/policy paths");
static constexpr u32 kNormalHUDObjectCount=30u;
static constexpr u32 kPAGroupCount=7u;
static constexpr u32 kHUDObjectCount=kNormalHUDObjectCount+kPAGroupCount;
static constexpr u32 kFirstPAGroupSlot=kNormalHUDObjectCount;
static constexpr u32 kPAGroupCompassIndex=2u;
static constexpr u32 kSlotPACompass=kFirstPAGroupSlot+kPAGroupCompassIndex;
static_assert(kManagedCount==4u,"Special HUD component count changed");
static_assert(kNormalHUDObjectCount==30u,"Normal HUD inventory size changed");
static_assert(kPAGroupCount==7u,"Power Armor group count changed");
static_assert(kHUDObjectCount==37u,"Logical HUD object count changed");
static_assert(kFirstPAGroupSlot==kNormalHUDObjectCount,"Power Armor groups must remain appended after normal HUD objects");
static_assert(kSlotPACompass==32u,"Power Armor compass slot/id relationship changed");
static constexpr u32 kHotkeyModShift=0x1u,kHotkeyModCtrl=0x2u,kHotkeyModAlt=0x4u,kHotkeyModMask=0x7u;
static constexpr u32 kHotkeyPackedBound=0x40000000u;
enum ControlMode : u32 { kControlOff=0u,kControlHide=1u,kControlHideShow=2u };
enum PermanentVisibility : u32 { kAlwaysHide=0u,kAlwaysShow=1u };
struct HotkeyBinding { u32 keycode;u32 modifiers;bool bound; };
struct ElementSettings { u32 controlMode;bool automatic;u32 permanentVisibility;u32 hideDelaySeconds;u32 triggerMask; };
struct GFxValueLite {
    void* objectInterface;
    s32 type;
    u32 pad0C;
    union { u32 u32v;s32 s32v;double number;bool boolean;const char* string;const char** managedString;void* object; } data;
    void* unk18;
};
static_assert(sizeof(GFxValueLite)==0x20,"GFxValueLite ABI mismatch");
static constexpr u32 kCustomElementCap=64u;
struct CustomElementSettings {
    char fileName[260];
    char name[128];
    char menu[96];
    char path[512];
    char source[260];
    ElementSettings policy;
    HotkeyBinding hotkey;
};
struct HUDSettings { bool enabled;
    bool hideButtonHints;
    bool hideButtonHintsPipBoy;
    bool hideButtonHintsWorkbench;
    bool hideButtonHintsVATS;
    bool hideButtonHintsMenus;
    u32 lowHealthThresholdPct;
    u32 lowAPThresholdPct;
    DebugLogLevel debugLog;
    bool performanceTelemetry;
    ElementSettings elements[kHUDObjectCount];
    HotkeyBinding hotkeys[kHUDObjectCount];
    HotkeyBinding discoveryHotkey;
    };
struct CustomSettingsSnapshot { u32 count; CustomElementSettings entries[kCustomElementCap]; };
static CustomSettingsSnapshot g_customSettings={};
static CustomSettingsSnapshot g_pendingCustomSettings={};
static CustomSettingsSnapshot g_customReadScratch={};
static CustomSettingsSnapshot g_customCommitScratch={};
static CustomElementSettings g_customSortScratch={};
struct ObjectControlState { volatile long suppress;
    volatile long reasonMask;
    volatile long policyInitialized;
    volatile long gateClosed;
    volatile long hidePending;
    volatile long long hideDeadlineUs;
    };
static ObjectControlState g_controlStates[kHUDObjectCount]={};
struct CustomRuntimeState {
    void* lastObject;
    GFxValueLite cachedTarget;
    bool cachedTargetValid;
    bool haveNatural;
    bool naturalVisible;
    bool appliedSuppression;
    bool restoreFailed;
    bool resolveLogged;
    u32 resolveFailureStreak;
    u64 nextResolveFrame;
    u64 nextValidationFrame;
    // Standalone custom menus are controlled at the native IMenu display boundary.
    // HUDMenu/native host menus remain on the proven child-level GFx fallback.
    void* menuObject;
    bool menuGateApplied;
    bool menuHaveNatural;
    bool menuNaturalCanBeVisible;
    u32 menuFailureStreak;
    u32 menuLastFailureReason;
    u64 menuNextAttemptFrame;
};
static ObjectControlState g_customControlStates[kCustomElementCap]={};
static CustomRuntimeState g_customRuntimeStates[kCustomElementCap]={};
static volatile long g_customHotkeyPacked[kCustomElementCap]={};
static volatile long g_customHotkeyActive[kCustomElementCap]={};
static bool g_customControlCapable=false;
static u64 g_customRuntimeBinds=0u;
static u64 g_customRuntimeResolveFailures=0u;
static u64 g_customRuntimeVisibleWrites=0u;
static u64 g_customRuntimeValidations=0u;
static u64 g_customRuntimeValidationFailures=0u;
static u64 g_customMenuGateHides=0u;
static u64 g_customMenuGateReleases=0u;
static u64 g_customMenuGateFailures=0u;
static u64 g_customMenuStaleDrops=0u;
static u64 g_customMenuAbsentRetries=0u;
static constexpr u32 kSlotCrosshair=12u;
static constexpr u32 kSlotHealth=16u;
static constexpr u32 kSlotCompass=19u;
static constexpr u32 kSlotActionPoints=23u;
static constexpr const char* kSettingSections[kHUDObjectCount]={
    "Messages","ObjectiveUpdates","QuestUpdates","QuestVaultBoy","FloatingQuestMarkers","TutorialText","XPMeter","EnemyHealth","Stealth","ConditionBoy",
    "DirectionalHitIndicators","ExplosiveIndicators","Crosshair","HitIndicator","QuickContainer","Rollover","Health","LocationText","Radiation","Compass",
    "CriticalMeter","PerkVaultBoy","Subtitles","ActionPoints","Ammo","GrenadeAmmo","Fatigue","Flashlight","ActiveEffects","PALowBattery",
    "PAAmmoDisplay","PAArmorCondition","PACompass","PADashboard","PADashboardLights","PAGauges","PAVisorShadowEffects"
};
static_assert((u32)(sizeof(kSettingSections)/sizeof(kSettingSections[0]))==kHUDObjectCount,"Settings section table must cover every logical HUD object");

static HUDSettings make_default_settings(){
    HUDSettings s={};
    s.enabled=true;
    s.hideButtonHints=false;
    s.hideButtonHintsPipBoy=true;
    s.hideButtonHintsWorkbench=true;
    s.hideButtonHintsVATS=true;
    s.hideButtonHintsMenus=false;
    s.lowHealthThresholdPct=25u;
    s.lowAPThresholdPct=25u;
    s.debugLog=DebugLogLevel::Errors;
    s.performanceTelemetry=false;
    for(u32 i=0;i<kHUDObjectCount;++i){
        s.elements[i].controlMode=kControlOff;
        s.elements[i].automatic=true;
        s.elements[i].permanentVisibility=kAlwaysHide;
        s.elements[i].hideDelaySeconds=0u;
        s.elements[i].triggerMask=0u;
    }
    s.elements[kSlotCrosshair].controlMode=kControlHide;
    s.elements[kSlotCrosshair].hideDelaySeconds=10u;
    s.elements[kSlotCrosshair].triggerMask=kReasonWeapon|kReasonAim|kReasonCombat|kReasonActivate|kReasonCommand;
    s.elements[kSlotHealth].controlMode=kControlHideShow;
    s.elements[kSlotHealth].triggerMask=kReasonWeapon|kReasonAim|kReasonCombat|kReasonLowHealth;
    s.elements[kSlotCompass].controlMode=kControlHideShow;
    s.elements[kSlotCompass].hideDelaySeconds=5u;
    s.elements[kSlotCompass].triggerMask=kReasonSprint|kReasonSneak|kReasonCombat;
    s.elements[kSlotActionPoints].controlMode=kControlHideShow;
    s.elements[kSlotActionPoints].hideDelaySeconds=5u;
    s.elements[kSlotActionPoints].triggerMask=kReasonLowAP;
    return s;
}
static HUDSettings g_settings={};
static HUDSettings g_pendingSettings={};
static volatile long g_hotkeyPacked[kHUDObjectCount]={};
static volatile long g_customDiscoveryHotkeyPacked=0;
static volatile long g_hotkeyActive[kHUDObjectCount]={};
static volatile long g_hotkeyPolicyDirty=0;
static volatile long g_hotkeyInputRegistered=0;
static volatile long g_customDiscoveryRequested=0;
static void* g_menuControls=0;
static u32 g_requiredTriggerMask=0u;
static u64 g_policyChanges=0;
static u64 g_stateSamples=0;
static u64 g_stateChanges=0;
static u64 g_stateFailures=0;
static bool g_policyFailOpen=false;
static bool g_haveRuntimeState=false;
static u64 g_avDiscoveryAttempts=0;
static u64 g_avReadFailures=0;
static u32 g_avDiscoveryAttemptedMask=0u;
static void* g_healthAVInfo=0;
static void* g_actionPointsAVInfo=0;
static void** g_crosshairValueSinkVtable=0;
static void* g_crosshairValueSink=0;
static u32 g_crosshairValueSinkOffset=0;
static bool g_crosshairSinkDiscoveryAttempted=false;
static constexpr u64 kSummaryEveryAdvances=600ULL;
static constexpr u64 kPerfSampleStride=16ULL;
static constexpr u64 kPerfDetailedStride=256ULL;

// Proven player/runtime structure contracts used by the hot sampler.
static constexpr u32 kPlayerMovementStateOffset=0x128u;
static constexpr u32 kPlayerActorStateOffset=0x134u;
static constexpr u32 kActorValueOwnerOffset=0x58u;
static constexpr u32 kActorCombatVslot=0xFEu;
static constexpr u32 kActorStateWeaponShift=1u;
static constexpr u32 kActorStateWeaponMask=0x7u;
static constexpr u32 kActorStateGunShift=14u;
static constexpr u32 kActorStateGunMask=0xFu;
static constexpr u32 kWeaponStateDrawn=3u;
static constexpr u32 kGunStateSighted=6u;
static constexpr u32 kGunStateFireSighted=8u;

enum RuntimeSourceMask : u32 {
    kSourceSprint=1u<<0,
    kSourceSneak=1u<<1,
    kSourceCombat=1u<<2,
    kSourceCrosshair=1u<<3,
    kSourceVATS=1u<<4,
    kSourceRadiation=1u<<5,
    kSourcePowerArmor=1u<<6,
    kSourcePerspective=1u<<7
};
enum ActorValueSampleMask : u32 {
    kActorValueHealth=1u<<0,
    kActorValueActionPoints=1u<<1
};
static constexpr u32 kCrosshairModeCommand=7u;
static constexpr u32 kCrosshairModeActivate=8u;
static constexpr u32 kCrosshairSinkModeOffset=0x08u;
static constexpr u32 kCrosshairSinkValidOffset=0x0Cu;
static constexpr u32 kInvalidCrosshairMode=0xFFFFFFFFu;

// HUDRadiationMeter's embedded CurrentRadsDisplayMagnitude event sink/cache.
static constexpr u32 kRadiationComponentSlot=18u; // logical object id 19
static constexpr u32 kRadiationSinkOffset=0x328u;
static constexpr u32 kRadiationValueOffset=0x330u;
static constexpr u32 kRadiationValidOffset=0x334u;
static constexpr u32 kRadiationDirtyOffset=0x338u;

typedef bool (*ActorBoolFn)(void*);
typedef float (*ActorValueGetFn)(void*,const void*);
// Supported OG/NG/AE runtime-family layouts embed IMovementPlayerControlsFilter at PlayerCharacter +0x4C0.
// Virtual slot 06 is IsInThirdPerson(). The runtime contract is validated lazily and independently so a
// perspective-only ABI failure cannot disable the existing trigger telemetry.
static constexpr u32 kPlayerPerspectiveSubobjectOffset=0x4C0u;
static constexpr u32 kIsInThirdPersonVslot=6u;
static void* g_perspectiveValidatedPlayer=0;
static void* g_perspectiveRejectedPlayer=0;
static ActorBoolFn g_isInThirdPersonFn=0;
typedef float (*ActorValueModifierFn)(void*,u32,const void*);
typedef bool (*ShouldBeVisibleFn)(void*);
typedef void (*AdvanceFn)(void*,float,u64);
static ActorBoolFn g_combatFn=0;
static ActorValueGetFn g_avCurrentFn=0;
static ActorValueGetFn g_avPermanentFn=0;
static ActorValueModifierFn g_avModifierFn=0;
static ShouldBeVisibleFn g_origShould[kManagedCount]={0,0,0,0};
static void* g_gateHooks[kManagedCount]={0,0,0,0};
enum class HookBackendState:u32{Uninitialized=0,Ready=1,Failed=2};
static HookBackendState g_hookBackendState=HookBackendState::Uninitialized;
// Separate native Power Armor compass component. Its primary vtable slot 7 is the same
// HUDComponentBase::ShouldBeVisible predicate used by generic HUD components, but the instance is
// not part of the normal 30-object HUDMenu array. The class-level hook therefore owns the Scaleform
// side of logical object #33 while the PA scene adapter owns its 3D/NIF side.
static void** g_paCompassVtable=0;
static ShouldBeVisibleFn g_origPACompassShould=0;
static bool g_paCompassContractPass=false;
static bool g_paCompassHookInstalled=false;
static bool g_paLifecycleActive=false;

// Full-control generic hooks are derived directly from the normal HUD descriptor table.
// The four legacy/special component hooks remain separate because they are also hardened discovery sentinels.
static constexpr u32 kGenericHookTargetCount=kNormalHUDObjectCount-kManagedCount;
static bool g_genericNativeRequestCapability=false;
static u32 g_genericPersistentRequestOffset=0;
static bool g_componentInventoryReady=false;

struct HUDComponent {
    u32 id;
    const char* name;
    const char* rtti;
    s32 managedIndex;
    bool derived;
    void** vtable;
    ShouldBeVisibleFn original;
    void* object;
    u32 arrayIndex;
    bool contractPass;
    bool hookInstalled;
};
static HUDComponent g_hudComponents[kNormalHUDObjectCount]={
    {1u,"Messages",".?AVHUDMessages@@",-1,false,0,0,0,0,false,false},
    {2u,"Objective Updates",".?AVHUDObjectiveUpdates@@",-1,false,0,0,0,0,false,false},
    {3u,"Quest Updates",".?AVHUDQuestUpdates@@",-1,false,0,0,0,0,false,false},
    {4u,"Quest Vault Boy",".?AVHUDQuestVaultBoy@@",-1,false,0,0,0,0,false,false},
    {5u,"Floating Quest Markers",".?AVHUDFloatingQuestMarkers@@",-1,false,0,0,0,0,false,false},
    {6u,"Tutorial Text",".?AVHUDTutorialText@@",-1,false,0,0,0,0,false,false},
    {7u,"XP Meter",".?AVHUDExperienceMeter@@",-1,false,0,0,0,0,false,false},
    {8u,"Enemy Health Meter",".?AVHUDEnemyHealthMeter@@",-1,false,0,0,0,0,false,false},
    {9u,"Stealth Meter",".?AVHUDStealthMeter@@",-1,false,0,0,0,0,false,false},
    {10u,"Vault Boy Condition",".?AVFlashVaultBoyCondition@@",-1,false,0,0,0,0,false,false},
    {11u,"Directional Hit Indicators",".?AVDirectionalHitIndicators@@",-1,false,0,0,0,0,false,false},
    {12u,"Explosive Indicators",".?AVExplosiveIndicators@@",-1,false,0,0,0,0,false,false},
    {13u,"Crosshair",".?AVHUDCrosshair@@",(s32)kCrosshair,false,0,0,0,0,false,false},
    {14u,"Hit Indicator",".?AVFlashHitIndicator@@",-1,false,0,0,0,0,false,false},
    {15u,"Quick Container",".?AVHUDQuickContainer@@",-1,false,0,0,0,0,false,false},
    {16u,"Rollover Widget",".?AVHUDRollover@@",-1,false,0,0,0,0,false,false},
    {17u,"Health Meter",".?AVHUDPlayerHealthMeter@@",(s32)kHealth,true,0,0,0,0,false,false},
    {18u,"Location Text",".?AVHUDLocationText@@",-1,false,0,0,0,0,false,false},
    {19u,"Radiation Meter",".?AVHUDRadiationMeter@@",-1,false,0,0,0,0,false,false},
    {20u,"Compass",".?AVHUDCompass@@",(s32)kCompass,true,0,0,0,0,false,false},
    {21u,"Critical Meter",".?AVHUDCriticalMeter@@",-1,false,0,0,0,0,false,false},
    {22u,"Perk Vault Boy",".?AVHUDPerkVaultBoy@@",-1,false,0,0,0,0,false,false},
    {23u,"Subtitles",".?AVHUDSubtitleText@@",-1,false,0,0,0,0,false,false},
    {24u,"Action Points",".?AVHUDActionPointMeter@@",(s32)kActionPoints,true,0,0,0,0,false,false},
    {25u,"Ammo Count",".?AVHUDAmmoCounter@@",-1,false,0,0,0,0,false,false},
    {26u,"Explosive / Grenade Ammo Count",".?AVHUDExplosiveAmmoCounter@@",-1,false,0,0,0,0,false,false},
    {27u,"Fatigue Warning",".?AVHUDFatigueWarning@@",-1,true,0,0,0,0,false,false},
    {28u,"Flashlight Widget",".?AVHUDFlashLightWidget@@",-1,false,0,0,0,0,false,false},
    {29u,"Active Effects",".?AVHUDActiveEffectsDisplay@@",-1,false,0,0,0,0,false,false},
    {30u,"Power Armor Low Battery Warning",".?AVHUDPowerArmorLowBatterWarningText@@",-1,false,0,0,0,0,false,false}
};
static_assert((u32)(sizeof(g_hudComponents)/sizeof(g_hudComponents[0]))==kNormalHUDObjectCount,"HUD component table must match normal HUD inventory");

static const char* kPAGroupDisplayNames[kPAGroupCount]={
    "Power Armor - Ammo Display",
    "Power Armor - Armor Condition",
    "Power Armor - Compass",
    "Power Armor - Dashboard",
    "Power Armor - Dashboard Lights",
    "Power Armor - Gauges",
    "Power Armor - Visor / Shadow Effects"
};
static const char* hud_object_name(u32 slot){if(slot<kNormalHUDObjectCount)return g_hudComponents[slot].name;
    u32 group=slot-kFirstPAGroupSlot;
    return group<kPAGroupCount?kPAGroupDisplayNames[group]:"Unknown HUD Object";
    }
static HUDComponent* component_by_rtti(const char*name){if(!name)return 0;
    for(u32 i=0;i<kNormalHUDObjectCount;++i)if(str_eq(name,g_hudComponents[i].rtti))return &g_hudComponents[i];
    return 0;
    }

static u32 clamp_setting(u32 value,u32 lo,u32 hi){return value<lo?lo:(value>hi?hi:value);}
static bool setting_bool(const char*file,const char*section,const char*key,bool current){return GetPrivateProfileIntA(section,key,current?1:0,file)!=0;}
static u32 setting_u32(const char*file,const char*section,const char*key,u32 current,u32 lo,u32 hi){return clamp_setting(GetPrivateProfileIntA(section,key,(s32)current,file),lo,hi);
    }
static bool try_setting_s32(const char*file,const char*section,const char*key,s32*out){if(!file||!section||!key||!out)return false;
    const s32 missing=-123456789;
    s32 raw=(s32)GetPrivateProfileIntA(section,key,missing,file);
    if(raw==missing)return false;
    *out=raw;
    return true;
    }
static DebugLogLevel setting_debug_level(const char*file,DebugLogLevel current){const s32 missing=-123456789;
    s32 raw=(s32)GetPrivateProfileIntA("General","iDebugLog",missing,file);
    if(raw!=missing)return normalize_debug_level(raw);
    s32 legacy=(s32)GetPrivateProfileIntA("General","bDebugLogging",missing,file);
    if(legacy!=missing)return legacy?DebugLogLevel::Verbose:DebugLogLevel::Off;
    return current;
    }
static void read_element_settings(const char*file,const char*section,ElementSettings*out){if(!file||!section||!out)return;
    s32 rawControl=0,rawMode=0,rawAutomatic=0,rawAlways=0;
    bool haveControl=try_setting_s32(file,section,"bControl",&rawControl);
    bool haveMode=try_setting_s32(file,section,"iMode",&rawMode);
    bool haveAutomatic=try_setting_s32(file,section,"bAutomatic",&rawAutomatic);
    bool haveAlways=try_setting_s32(file,section,"iAlways",&rawAlways);
    if(haveControl||haveMode||haveAutomatic||haveAlways){
        bool controlled=haveControl?(rawControl!=0):(out->controlMode!=kControlOff);
        u32 automaticMode=(out->controlMode==kControlHideShow)?1u:0u;if(haveMode)automaticMode=clamp_setting((u32)(rawMode<0?0:rawMode),0u,1u);
        if(haveAutomatic)out->automatic=(rawAutomatic!=0);
        if(haveAlways)out->permanentVisibility=clamp_setting((u32)(rawAlways<0?0:rawAlways),kAlwaysHide,kAlwaysShow);
        if(!controlled)out->controlMode=kControlOff;
        else if(out->automatic)out->controlMode=automaticMode?kControlHideShow:kControlHide;
        else out->controlMode=(out->permanentVisibility==kAlwaysShow)?kControlHideShow:kControlHide;
    }else{
        s32 legacy=0;if(try_setting_s32(file,section,"iControl",&legacy))out->controlMode=clamp_setting((u32)(legacy<0?0:legacy),kControlOff,kControlHideShow);
    }
    out->hideDelaySeconds=setting_u32(file,section,"iHideDelay",out->hideDelaySeconds,0u,60u);
    u32 mask=out->triggerMask;
    for(u32 i=0;i<kConfigurableTriggerCount;++i){
        const ConfigurableTriggerMetadata& trigger=kConfigurableTriggers[i];
        bool on=setting_bool(file,section,trigger.settingKey,(mask&trigger.bit)!=0);
        if(on)mask|=trigger.bit;else mask&=~trigger.bit;
    }
    out->triggerMask=mask&kAllConfigurableTriggers;
}
static bool is_hotkey_shift_code(u32 code){return code==0x10u||code==0xA0u||code==0xA1u;}
static bool is_hotkey_ctrl_code(u32 code){return code==0x11u||code==0xA2u||code==0xA3u;}
static bool is_hotkey_alt_code(u32 code){return code==0x12u||code==0xA4u||code==0xA5u;}
static bool parse_decimal_u32(const char**cursor,u32*out){
    if(!cursor||!*cursor||!out)return false;
        const char*p=*cursor;
        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
        if(*p<'0'||*p>'9')return false;
        u32 v=0u;
        while(*p>='0'&&*p<='9'){u32 digit=(u32)(*p-'0');
        if(v>10000u)return false;
        v=v*10u+digit;
        ++p;
        }while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
        *cursor=p;
        *out=v;
        return true;
}
static bool parse_keyinput_binding(const char*value,HotkeyBinding*out){
    if(!value||!out)return false;HotkeyBinding next={0u,0u,false};const char*p=value;u32 first=0u;if(!parse_decimal_u32(&p,&first))return false;
    if(first==0u){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;if(*p)return false;*out=next;return true;}
    if(first>255u)return false;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
    if(!*p){next.keycode=first;next.bound=true;*out=next;return true;}
    if(*p!=',')return false;++p;u32 second=0u;if(!parse_decimal_u32(&p,&second))return false;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')++p;
    if(!*p&&second<=kHotkeyModMask){next.keycode=first;next.modifiers=second;next.bound=true;*out=next;return true;}
    // Defensive fallback for any original-MCM variant that serializes physical modifier key codes.
    next={0u,0u,false};
        u32 codes[8]={};
        u32 count=0u;
        const char*q=value;
        for(;;){if(count>=8u||!parse_decimal_u32(&q,&codes[count]))return false;
        ++count;
        while(*q==' '||*q=='\t'||*q=='\r'||*q=='\n')++q;
        if(!*q)break;
        if(*q!=',')return false;
        ++q;
        }
    for(u32 i=0;i<count;++i){u32 code=codes[i];
        if(code==0u)continue;
        if(code>255u)return false;
        if(is_hotkey_shift_code(code))next.modifiers|=kHotkeyModShift;
        else if(is_hotkey_ctrl_code(code))next.modifiers|=kHotkeyModCtrl;
        else if(is_hotkey_alt_code(code))next.modifiers|=kHotkeyModAlt;
        else{if(next.keycode&&next.keycode!=code)return false;
        next.keycode=code;
        }}
    if(next.keycode==0u){if(next.modifiers!=0u)return false;*out=next;return true;}next.bound=true;*out=next;return true;
}
static void read_hotkey_setting_key(const char*file,const char*section,const char*key,HotkeyBinding*out){
    if(!file||!section||!key||!out)return;
        static const char*kMissing="{DYNAMICHUD_KEYINPUT_MISSING_7D5B}";
        char value[160]={};
        DWORD n=GetPrivateProfileStringA(section,key,kMissing,value,(DWORD)sizeof(value),file);
        if(n==0u&&value[0]==0)return;
        if(str_eq(value,kMissing))return;
        HotkeyBinding next={};
        if(parse_keyinput_binding(value,&next)){*out=next;
        return;
        }if(debug_errors_enabled()){char b[520];
        SIZE_T pos=0;
        append_text(b,&pos,"[Hotkeys] ignored invalid keyinput section=");
        append_text(b,&pos,section);
        append_text(b,&pos," key=");
        append_text(b,&pos,key);
        append_text(b,&pos," value=\"");
        append_text(b,&pos,value);
        append_text(b,&pos,"\"");
        line_end(b,&pos);
        log_raw(b);
        }
}
static void read_hotkey_setting(const char*file,const char*section,HotkeyBinding*out){read_hotkey_setting_key(file,section,"sHotkey",out);}
static bool read_profile_text(const char*file,const char*section,const char*key,char*out,SIZE_T cap){
    if(!file||!section||!key||!out||cap<2u)return false;
    static const char*kMissing="{DYNAMICHUD_CUSTOM_MISSING_A12F}";
    char temp[768]={};DWORD use=(DWORD)(cap<sizeof(temp)?cap:sizeof(temp));
    DWORD n=GetPrivateProfileStringA(section,key,kMissing,temp,use,file);
    if(n==0u&&temp[0]==0)return false;if(str_eq(temp,kMissing))return false;
    SIZE_T begin=0,end=str_len(temp);while(begin<end&&(temp[begin]==' '||temp[begin]=='\t'))++begin;
    while(end>begin&&(temp[end-1]==' '||temp[end-1]=='\t'))--end;
    SIZE_T len=end-begin;if(len+1u>cap)return false;for(SIZE_T i=0;i<len;++i)out[i]=temp[begin+i];out[len]=0;return len!=0u;
}
static bool custom_ini_name(const char*name){if(!name)return false;SIZE_T n=str_len(name);return n>4u&&ascii_lower(name[n-4])=='.'&&ascii_lower(name[n-3])=='i'&&ascii_lower(name[n-2])=='n'&&ascii_lower(name[n-1])=='i';}
static bool custom_path_valid(const char*path){if(!path||!path[0])return false;const char*root="root1";u32 i=0;while(root[i]&&path[i]==root[i])++i;return !root[i]&&(path[i]==0||path[i]=='.');}
static bool custom_target_duplicate(const CustomSettingsSnapshot*out,const char*menu,const char*path){if(!out)return false;for(u32 i=0;i<out->count;++i)if(str_ieq(out->entries[i].menu,menu)&&str_ieq(out->entries[i].path,path))return true;return false;}
static bool read_custom_element_ini(const char*full,const char*fileName,CustomElementSettings*out){
    if(!full||!fileName||!out)return false;CustomElementSettings next={};
    next.policy.controlMode=kControlOff;next.policy.automatic=true;next.policy.permanentVisibility=kAlwaysHide;next.policy.hideDelaySeconds=0u;next.policy.triggerMask=0u;
    if(!read_profile_text(full,"CustomHUD","sMenu",next.menu,sizeof(next.menu))||!read_profile_text(full,"CustomHUD","sPath",next.path,sizeof(next.path))||!custom_path_valid(next.path)){
        if(debug_errors_enabled()){char b[900];SIZE_T p=0;append_text(b,&p,"[CustomHUD] ignored invalid custom INI target file=");append_text(b,&p,fileName);append_text(b,&p," (requires sMenu and root1... sPath)");line_end(b,&p);log_raw(b);}return false;
    }
    read_profile_text(full,"CustomHUD","sName",next.name,sizeof(next.name));if(!next.name[0])copy_text(next.name,sizeof(next.name),fileName);
    read_profile_text(full,"CustomHUD","sSourceSWF",next.source,sizeof(next.source));
    copy_text(next.fileName,sizeof(next.fileName),fileName);
    read_element_settings(full,"CustomHUD",&next.policy);read_hotkey_setting(full,"CustomHUD",&next.hotkey);
    *out=next;return true;
}
static void sort_custom_entries(CustomSettingsSnapshot*out){if(!out)return;for(u32 i=1;i<out->count;++i){g_customSortScratch=out->entries[i];u32 j=i;while(j>0u&&str_icompare(out->entries[j-1u].fileName,g_customSortScratch.fileName)>0){out->entries[j]=out->entries[j-1u];--j;}out->entries[j]=g_customSortScratch;}}
static __declspec(noinline) void custom_read_found_ini(CustomSettingsSnapshot*out,const char*fileName){if(!out||!fileName||out->count>=kCustomElementCap)return;char rel[768]={};SIZE_T rp=0;append_text_cap(rel,sizeof(rel),&rp,"Data\\MCM\\Config\\DynamicHUD\\CustomElements\\");append_text_cap(rel,sizeof(rel),&rp,fileName);char full[1024]={};if(!build_game_file_path(rel,full,sizeof(full)))return;CustomElementSettings&next=out->entries[out->count];memset(&next,0,sizeof(next));if(!read_custom_element_ini(full,fileName,&next))return;if(custom_target_duplicate(out,next.menu,next.path)){if(debug_errors_enabled()){char b[900];SIZE_T p=0;append_text(b,&p,"[CustomHUD] duplicate target ignored file=");append_text(b,&p,fileName);append_text(b,&p," menu=");append_text(b,&p,next.menu);append_text(b,&p," path=");append_text(b,&p,next.path);line_end(b,&p);log_raw(b);}memset(&next,0,sizeof(next));return;}++out->count;}
static __declspec(noinline) void read_custom_elements_from_disk(CustomSettingsSnapshot*out){
    if(!out)return;memset(out,0,sizeof(*out));char pattern[1024]={};if(!build_game_file_path("Data\\MCM\\Config\\DynamicHUD\\CustomElements\\*.ini",pattern,sizeof(pattern)))return;
    Win32FindDataA fd={};HANDLE h=FindFirstFileA(pattern,&fd);if(h==INVALID_HANDLE_VALUE){DWORD err=GetLastError();if(err!=ERROR_FILE_NOT_FOUND&&err!=ERROR_PATH_NOT_FOUND&&debug_errors_enabled())error_log_line("[CustomHUD] CustomElements enumeration failed.");return;}
    bool truncated=false;
    do{if((fd.fileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0u||!custom_ini_name(fd.fileName))continue;if(out->count>=kCustomElementCap){truncated=true;continue;}custom_read_found_ini(out,fd.fileName);}while(FindNextFileA(h,&fd));
    FindClose(h);sort_custom_entries(out);
    if(truncated&&debug_errors_enabled())error_log_line("[CustomHUD] CustomElements cap reached (64); additional INIs are ignored/fail-open.");
    if(debug_verbose_enabled()){u32 controlled=0u;for(u32 i=0;i<out->count;++i)if(out->entries[i].policy.controlMode!=kControlOff)++controlled;char b[520];SIZE_T p=0;append_text(b,&p,"[CustomHUD] INIs loaded=");append_u64(b,&p,out->count);append_text(b,&p," controlled=");append_u64(b,&p,controlled);line_end(b,&p);log_raw(b);}
}

// Standard MCM hotkeys are not ModSetting values. Original MCM stores active registrations globally in
// Data\MCM\Settings\Keybinds.json. Keep that file parsing on the existing settings/watcher path and publish
// only the resulting key+modifier pair to gameplay input.
static constexpr SIZE_T kMCMKeybindJsonCap=256u*1024u;
static char g_mcmKeybindJson[kMCMKeybindJsonCap]={};

static __forceinline void json_skip_ws(const char**cursor,const char*end){
    if(!cursor||!*cursor)return;const char*p=*cursor;
    while(p<end&&(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'))++p;
    *cursor=p;
}
static bool json_parse_string(const char**cursor,const char*end,char*out,SIZE_T cap){
    if(!cursor||!*cursor||!out||cap<1u)return false;const char*p=*cursor;json_skip_ws(&p,end);
    if(p>=end||*p!='"')return false;++p;SIZE_T n=0u;
    while(p<end){
        char c=*p++;
        if(c=='"'){out[n]=0;*cursor=p;return true;}
        if(c=='\\'){
            if(p>=end)return false;char esc=*p++;
            if(esc=='"'||esc=='\\'||esc=='/')c=esc;
            else if(esc=='b')c='\b';else if(esc=='f')c='\f';else if(esc=='n')c='\n';
            else if(esc=='r')c='\r';else if(esc=='t')c='\t';
            else if(esc=='u'){
                if((SIZE_T)(end-p)<4u)return false;
                for(u32 i=0;i<4u;++i){char h=p[i];bool hex=(h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F');if(!hex)return false;}
                p+=4;c='?';
            }else return false;
        }else if((u8)c<0x20u)return false;
        if(n+1u<cap)out[n++]=c;
    }
    return false;
}
static bool json_skip_value(const char**cursor,const char*end,u32 depth);
static bool json_skip_object(const char**cursor,const char*end,u32 depth){
    const char*p=*cursor;json_skip_ws(&p,end);if(p>=end||*p!='{')return false;++p;json_skip_ws(&p,end);
    if(p<end&&*p=='}'){*cursor=p+1;return true;}
    for(;;){
        char key[96]={};if(!json_parse_string(&p,end,key,sizeof(key)))return false;json_skip_ws(&p,end);
        if(p>=end||*p!=':')return false;++p;
        if(!json_skip_value(&p,end,depth+1u))return false;json_skip_ws(&p,end);
        if(p>=end)return false;if(*p=='}'){*cursor=p+1;return true;}if(*p!=',')return false;++p;
    }
}
static bool json_skip_array(const char**cursor,const char*end,u32 depth){
    const char*p=*cursor;json_skip_ws(&p,end);if(p>=end||*p!='[')return false;++p;json_skip_ws(&p,end);
    if(p<end&&*p==']'){*cursor=p+1;return true;}
    for(;;){
        if(!json_skip_value(&p,end,depth+1u))return false;json_skip_ws(&p,end);
        if(p>=end)return false;if(*p==']'){*cursor=p+1;return true;}if(*p!=',')return false;++p;
    }
}
static bool json_skip_value(const char**cursor,const char*end,u32 depth){
    if(!cursor||!*cursor||depth>24u)return false;const char*p=*cursor;json_skip_ws(&p,end);if(p>=end)return false;
    if(*p=='{'){bool ok=json_skip_object(&p,end,depth);if(ok)*cursor=p;return ok;}
    if(*p=='['){bool ok=json_skip_array(&p,end,depth);if(ok)*cursor=p;return ok;}
    if(*p=='"'){char sink[2]={};bool ok=json_parse_string(&p,end,sink,sizeof(sink));if(ok)*cursor=p;return ok;}
    if((SIZE_T)(end-p)>=4u&&p[0]=='t'&&p[1]=='r'&&p[2]=='u'&&p[3]=='e'){*cursor=p+4;return true;}
    if((SIZE_T)(end-p)>=5u&&p[0]=='f'&&p[1]=='a'&&p[2]=='l'&&p[3]=='s'&&p[4]=='e'){*cursor=p+5;return true;}
    if((SIZE_T)(end-p)>=4u&&p[0]=='n'&&p[1]=='u'&&p[2]=='l'&&p[3]=='l'){*cursor=p+4;return true;}
    const char*start=p;if(*p=='-')++p;bool digit=false;
    while(p<end&&*p>='0'&&*p<='9'){digit=true;++p;}
    if(p<end&&*p=='.'){++p;while(p<end&&*p>='0'&&*p<='9'){digit=true;++p;}}
    if(p<end&&(*p=='e'||*p=='E')){++p;if(p<end&&(*p=='+'||*p=='-'))++p;bool expDigit=false;while(p<end&&*p>='0'&&*p<='9'){expDigit=true;++p;}if(!expDigit)return false;}
    if(!digit||p==start)return false;*cursor=p;return true;
}
static bool json_parse_s32(const char**cursor,const char*end,s32*out){
    if(!cursor||!*cursor||!out)return false;const char*p=*cursor;json_skip_ws(&p,end);bool neg=false;if(p<end&&*p=='-'){neg=true;++p;}
    if(p>=end||*p<'0'||*p>'9')return false;s64 value=0;
    while(p<end&&*p>='0'&&*p<='9'){value=value*10+(*p-'0');if(value>2147483648LL)return false;++p;}
    if(neg)value=-value;if(value<-2147483648LL||value>2147483647LL)return false;*out=(s32)value;*cursor=p;return true;
}
static bool json_parse_mcm_keybind_object(const char**cursor,const char*end,HotkeyBinding*out,bool*matched){
    if(!cursor||!*cursor||!out||!matched)return false;const char*p=*cursor;json_skip_ws(&p,end);if(p>=end||*p!='{')return false;++p;
    char modName[96]={},id[160]={};bool haveMod=false,haveId=false,haveKey=false,haveMods=false;s32 keycode=0,modifiers=0;
    json_skip_ws(&p,end);if(p<end&&*p=='}'){*cursor=p+1;return true;}
    for(;;){
        char key[96]={};if(!json_parse_string(&p,end,key,sizeof(key)))return false;json_skip_ws(&p,end);if(p>=end||*p!=':')return false;++p;
        if(str_eq(key,"modName")){if(!json_parse_string(&p,end,modName,sizeof(modName)))return false;haveMod=true;}
        else if(str_eq(key,"id")){if(!json_parse_string(&p,end,id,sizeof(id)))return false;haveId=true;}
        else if(str_eq(key,"keycode")){if(!json_parse_s32(&p,end,&keycode))return false;haveKey=true;}
        else if(str_eq(key,"modifiers")){if(!json_parse_s32(&p,end,&modifiers))return false;haveMods=true;}
        else if(!json_skip_value(&p,end,1u))return false;
        json_skip_ws(&p,end);if(p>=end)return false;if(*p=='}'){++p;break;}if(*p!=',')return false;++p;
    }
    *cursor=p;
    if(haveMod&&haveId&&str_eq(modName,"DynamicHUD")&&str_eq(id,"DiscoverModdedHUDElements")){
        *matched=true;HotkeyBinding next={};
        if(!haveKey||keycode<=0){*out=next;return true;}
        if(keycode>255||!haveMods||modifiers<0||((u32)modifiers&~kHotkeyModMask)!=0u){
            if(debug_errors_enabled())error_log_line("[CustomHUD] ignored invalid MCM discovery binding in Data\\MCM\\Settings\\Keybinds.json.");
            *out=next;return true;
        }
        next.keycode=(u32)keycode;next.modifiers=(u32)modifiers;next.bound=true;*out=next;
    }
    return true;
}
static bool parse_mcm_discovery_hotkey_json(const char*data,SIZE_T length,HotkeyBinding*out,bool*found){
    if(!data||!out||!found)return false;*out={};*found=false;const char*p=data;const char*end=data+length;json_skip_ws(&p,end);
    if(p>=end||*p!='{')return false;++p;json_skip_ws(&p,end);if(p<end&&*p=='}')return true;
    for(;;){
        char key[96]={};if(!json_parse_string(&p,end,key,sizeof(key)))return false;json_skip_ws(&p,end);if(p>=end||*p!=':')return false;++p;
        if(str_eq(key,"keybinds")){
            json_skip_ws(&p,end);if(p>=end||*p!='[')return false;++p;json_skip_ws(&p,end);
            if(p<end&&*p!=']'){
                for(;;){
                    json_skip_ws(&p,end);
                    if(p<end&&*p=='{'){
                        if(!json_parse_mcm_keybind_object(&p,end,out,found))return false;
                    }else if(!json_skip_value(&p,end,1u))return false;
                    json_skip_ws(&p,end);if(p>=end)return false;if(*p==']')break;if(*p!=',')return false;++p;
                }
            }
            if(p>=end||*p!=']')return false;++p;
        }else if(!json_skip_value(&p,end,1u))return false;
        json_skip_ws(&p,end);if(p>=end)return false;if(*p=='}'){++p;break;}if(*p!=',')return false;++p;
    }
    json_skip_ws(&p,end);return p==end;
}
static bool read_mcm_discovery_hotkey(const char*file,HotkeyBinding*out){
    if(!file||!out)return false;*out={};HANDLE f=CreateFileA(file,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    if(f==INVALID_HANDLE_VALUE)return true;DWORD got=0;bool ok=ReadFile(f,g_mcmKeybindJson,(DWORD)(kMCMKeybindJsonCap-1u),&got,0)!=0;
    if(!ok){CloseHandle(f);if(debug_errors_enabled())error_log_line("[CustomHUD] could not read MCM Keybinds.json; discovery binding left unbound.");return false;}
    if(got==(DWORD)(kMCMKeybindJsonCap-1u)){char extra=0;DWORD extraGot=0;if(ReadFile(f,&extra,1u,&extraGot,0)&&extraGot!=0u){CloseHandle(f);if(debug_errors_enabled())error_log_line("[CustomHUD] MCM Keybinds.json exceeds parser safety cap; discovery binding left unbound.");return false;}}
    CloseHandle(f);g_mcmKeybindJson[got]=0;bool found=false;HotkeyBinding next={};
    if(!parse_mcm_discovery_hotkey_json(g_mcmKeybindJson,(SIZE_T)got,&next,&found)){if(debug_errors_enabled())error_log_line("[CustomHUD] malformed MCM Keybinds.json; discovery binding left unbound.");return false;}
    *out=found?next:HotkeyBinding{};
    return true;
}
static void apply_settings_file(HUDSettings* out,const char* file){
    if(!out||!file)return;
    out->enabled=setting_bool(file,"General","bEnabled",out->enabled);
    out->hideButtonHints=setting_bool(file,"General","bHideButtonHints",out->hideButtonHints);
    out->hideButtonHintsPipBoy=setting_bool(file,"General","bHideButtonHintsPipBoy",out->hideButtonHintsPipBoy);
    out->hideButtonHintsWorkbench=setting_bool(file,"General","bHideButtonHintsWorkbench",out->hideButtonHintsWorkbench);
    out->hideButtonHintsVATS=setting_bool(file,"General","bHideButtonHintsVATS",out->hideButtonHintsVATS);
    out->hideButtonHintsMenus=setting_bool(file,"General","bHideButtonHintsMenus",out->hideButtonHintsMenus);
    out->lowHealthThresholdPct=setting_u32(file,"General","iLowHealthThreshold",out->lowHealthThresholdPct,0u,100u);
    out->lowAPThresholdPct=setting_u32(file,"General","iLowAPThreshold",out->lowAPThresholdPct,0u,100u);
    out->debugLog=setting_debug_level(file,out->debugLog);
    out->performanceTelemetry=setting_bool(file,"General","bPerformanceTelemetry",out->performanceTelemetry);
    for(u32 i=0;i<kHUDObjectCount;++i){
        read_element_settings(file,kSettingSections[i],&out->elements[i]);
        read_hotkey_setting(file,kSettingSections[i],&out->hotkeys[i]);
    }
}
static u32 count_bound_hotkeys(const HUDSettings&s){u32 n=0;for(u32 i=0;i<kHUDObjectCount;++i)if(s.hotkeys[i].bound)++n;return n;}
static HUDSettings read_settings_from_disk(){
    HUDSettings result=make_default_settings();
    char path[1024];
    if(build_game_file_path("Data\\MCM\\Config\\DynamicHUD\\settings.ini",path,sizeof(path)))apply_settings_file(&result,path);
    if(build_game_file_path("Data\\MCM\\Settings\\DynamicHUD.ini",path,sizeof(path)))apply_settings_file(&result,path);
    if(build_game_file_path("Data\\MCM\\Settings\\Keybinds.json",path,sizeof(path)))read_mcm_discovery_hotkey(path,&result.discoveryHotkey);
    if(debug_verbose_enabled()){
        char b[420];SIZE_T pos=0;
        append_text(b,&pos,"[Hotkeys] keyinput settings parsed; bound HUD objects=");
        append_u64(b,&pos,count_bound_hotkeys(result));
        append_text(b,&pos," discovery=");
        if(result.discoveryHotkey.bound){append_u64(b,&pos,result.discoveryHotkey.keycode);append_text(b,&pos,"/");append_u64(b,&pos,result.discoveryHotkey.modifiers);}
        else append_text(b,&pos,"unbound");
        line_end(b,&pos);log_raw(b);
    }
    return result;
}

static bool element_settings_equal(const ElementSettings&a,const ElementSettings&b){
    return a.controlMode==b.controlMode&&
           a.automatic==b.automatic&&
           a.permanentVisibility==b.permanentVisibility&&
           a.hideDelaySeconds==b.hideDelaySeconds&&
           a.triggerMask==b.triggerMask;
}
static bool hotkey_binding_equal(const HotkeyBinding&a,const HotkeyBinding&b){return a.keycode==b.keycode&&a.modifiers==b.modifiers&&a.bound==b.bound;}
static bool custom_element_settings_equal(const CustomElementSettings&a,const CustomElementSettings&b){return str_eq(a.fileName,b.fileName)&&str_eq(a.name,b.name)&&str_eq(a.menu,b.menu)&&str_eq(a.path,b.path)&&str_eq(a.source,b.source)&&element_settings_equal(a.policy,b.policy)&&hotkey_binding_equal(a.hotkey,b.hotkey);}
static bool custom_settings_equal(const CustomSettingsSnapshot&a,const CustomSettingsSnapshot&b){if(a.count!=b.count)return false;for(u32 i=0;i<a.count;++i)if(!custom_element_settings_equal(a.entries[i],b.entries[i]))return false;return true;}
static bool settings_equal(const HUDSettings& a,const HUDSettings& b){
    if(a.enabled!=b.enabled||a.hideButtonHints!=b.hideButtonHints||a.hideButtonHintsPipBoy!=b.hideButtonHintsPipBoy||
       a.hideButtonHintsWorkbench!=b.hideButtonHintsWorkbench||a.hideButtonHintsVATS!=b.hideButtonHintsVATS||a.hideButtonHintsMenus!=b.hideButtonHintsMenus||
       a.lowHealthThresholdPct!=b.lowHealthThresholdPct||a.lowAPThresholdPct!=b.lowAPThresholdPct||
       a.debugLog!=b.debugLog||a.performanceTelemetry!=b.performanceTelemetry||
       !hotkey_binding_equal(a.discoveryHotkey,b.discoveryHotkey))return false;
    for(u32 i=0;i<kHUDObjectCount;++i){
        if(!element_settings_equal(a.elements[i],b.elements[i])||!hotkey_binding_equal(a.hotkeys[i],b.hotkeys[i]))return false;
    }
    return true;
}
static u32 required_trigger_mask(const HUDSettings& s,const CustomSettingsSnapshot& custom){
    if(!s.enabled)return 0u;
    u32 mask=0u;
    for(u32 i=0;i<kHUDObjectCount;++i){
        if(s.elements[i].controlMode!=kControlOff&&s.elements[i].automatic)mask|=s.elements[i].triggerMask;
    }
    for(u32 i=0;i<custom.count;++i)if(custom.entries[i].policy.controlMode!=kControlOff&&custom.entries[i].policy.automatic)mask|=custom.entries[i].policy.triggerMask;
    return mask&kAllConfigurableTriggers;
}
static __forceinline bool hotkey_active(u32 slot){return slot<kHUDObjectCount&&_InterlockedCompareExchange(&g_hotkeyActive[slot],0,0)!=0;}
static __forceinline bool custom_hotkey_active(u32 slot){return slot<kCustomElementCap&&_InterlockedCompareExchange(&g_customHotkeyActive[slot],0,0)!=0;}
static u32 pack_raw_hotkey_binding(const HotkeyBinding& h){
    if(!h.bound||h.keycode==0u||h.keycode>255u||(h.modifiers&~kHotkeyModMask)!=0u)return 0u;
    return kHotkeyPackedBound|(h.keycode&0xFFu)|((h.modifiers&0xFFu)<<8);
}
static u32 pack_hotkey_binding(const HUDSettings& s,u32 slot){
    if(slot>=kHUDObjectCount||!s.enabled||s.elements[slot].controlMode==kControlOff)return 0u;
    return pack_raw_hotkey_binding(s.hotkeys[slot]);
}
static u32 pack_custom_hotkey_binding(const HUDSettings&s,const CustomSettingsSnapshot&custom,u32 slot){if(slot>=custom.count||slot>=kCustomElementCap||!s.enabled||custom.entries[slot].policy.controlMode==kControlOff)return 0u;return pack_raw_hotkey_binding(custom.entries[slot].hotkey);}
// The settings watcher may do disk I/O, but gameplay input never does. This function publishes parsed
// bindings into the atomic RAM cache consumed by hotkey_on_button(). Rebinding resets only affected latches.
static void publish_hotkey_bindings(const HUDSettings& s,const CustomSettingsSnapshot&custom,bool resetAll){
    bool changed=false;
    for(u32 i=0;i<kHUDObjectCount;++i){
        long next=(long)pack_hotkey_binding(s,i);
        long old=_InterlockedExchange(&g_hotkeyPacked[i],next);
        if(resetAll||old!=next||next==0){
            if(_InterlockedExchange(&g_hotkeyActive[i],0)!=0)changed=true;
        }
        if(old!=next)changed=true;
    }
    for(u32 i=0;i<kCustomElementCap;++i){long next=(long)pack_custom_hotkey_binding(s,custom,i);long old=_InterlockedExchange(&g_customHotkeyPacked[i],next);if(resetAll||old!=next||next==0){if(_InterlockedExchange(&g_customHotkeyActive[i],0)!=0)changed=true;}if(old!=next)changed=true;}
    long discoveryNext=(long)pack_raw_hotkey_binding(s.discoveryHotkey);
    long discoveryOld=_InterlockedExchange(&g_customDiscoveryHotkeyPacked,discoveryNext);
    if(discoveryOld!=discoveryNext&&debug_verbose_enabled()){
        char b[320];SIZE_T p=0;append_text(b,&p,"[CustomHUD] MCM discovery hotkey=");
        if(discoveryNext&kHotkeyPackedBound){append_u64(b,&p,(u32)discoveryNext&0xFFu);append_text(b,&p," modifiers=");append_u64(b,&p,((u32)discoveryNext>>8)&0xFFu);}
        else append_text(b,&p,"unbound");
        line_end(b,&p);log_raw(b);
    }
    if(changed||resetAll)_InterlockedExchange(&g_hotkeyPolicyDirty,1);
}
static void reset_hotkey_trigger_states(){
    bool changed=false;
    for(u32 i=0;i<kHUDObjectCount;++i){
        if(_InterlockedExchange(&g_hotkeyActive[i],0)!=0)changed=true;
    }
    for(u32 i=0;i<kCustomElementCap;++i)if(_InterlockedExchange(&g_customHotkeyActive[i],0)!=0)changed=true;
    if(changed)_InterlockedExchange(&g_hotkeyPolicyDirty,1);
}
static __declspec(noinline) void log_settings_snapshot(const char* prefix,const HUDSettings& s,const CustomSettingsSnapshot&custom){
    if(!debug_verbose_enabled())return;
    u32 off=0,hide=0,hideShow=0,automatic=0,alwaysHide=0,alwaysShow=0;
    for(u32 i=0;i<kHUDObjectCount;++i){
        const ElementSettings& e=s.elements[i];
        if(e.controlMode==kControlHide)++hide;
        else if(e.controlMode==kControlHideShow)++hideShow;
        else ++off;
        if(e.controlMode==kControlOff)continue;
        if(e.automatic)++automatic;
        else if(e.controlMode==kControlHideShow&&e.permanentVisibility==kAlwaysShow)++alwaysShow;
        else ++alwaysHide;
    }
    char b[2200];SIZE_T p=0;
    append_text(b,&p,prefix);append_text(b,&p," enabled=");append_u64(b,&p,s.enabled?1u:0u);
    append_text(b,&p," hideButtonHints=");append_u64(b,&p,s.hideButtonHints?1u:0u);
    append_text(b,&p," hintContexts={pipboy:");append_u64(b,&p,s.hideButtonHintsPipBoy?1u:0u);
    append_text(b,&p,",workbench:");append_u64(b,&p,s.hideButtonHintsWorkbench?1u:0u);
    append_text(b,&p,",vats:");append_u64(b,&p,s.hideButtonHintsVATS?1u:0u);
    append_text(b,&p,",menus:");append_u64(b,&p,s.hideButtonHintsMenus?1u:0u);append_text(b,&p,"}");
    append_text(b,&p," lowHP=");append_u64(b,&p,s.lowHealthThresholdPct);
    append_text(b,&p," lowAP=");append_u64(b,&p,s.lowAPThresholdPct);
    append_text(b,&p," debug=");append_text(b,&p,debug_level_name(s.debugLog));
    append_text(b,&p," perf=");append_u64(b,&p,s.performanceTelemetry?1u:0u);
    append_text(b,&p," hotkeys=");append_u64(b,&p,count_bound_hotkeys(s));
    append_text(b,&p," discoveryHotkey=");append_text(b,&p,s.discoveryHotkey.bound?"bound":"unbound");
    append_text(b,&p," customINIs=");append_u64(b,&p,custom.count);u32 customControlled=0u;for(u32 ci=0;ci<custom.count;++ci)if(custom.entries[ci].policy.controlMode!=kControlOff)++customControlled;append_text(b,&p," customControlled=");append_u64(b,&p,customControlled);
    append_text(b,&p," required=");append_hex64(b,&p,required_trigger_mask(s,custom));
    append_text(b,&p," controls={off:");append_u64(b,&p,off);
    append_text(b,&p,",hide:");append_u64(b,&p,hide);
    append_text(b,&p,",hideShow:");append_u64(b,&p,hideShow);
    append_text(b,&p,"} policies={auto:");append_u64(b,&p,automatic);
    append_text(b,&p,",alwaysHide:");append_u64(b,&p,alwaysHide);
    append_text(b,&p,",alwaysShow:");append_u64(b,&p,alwaysShow);append_text(b,&p,"}");
    for(u32 i=0;i<kHUDObjectCount;++i){
        const ElementSettings& e=s.elements[i];if(e.controlMode==kControlOff)continue;
        append_text(b,&p," #");append_u64(b,&p,i+1u);append_text(b,&p,"={mode:");append_u64(b,&p,e.controlMode);
        append_text(b,&p,",auto:");append_u64(b,&p,e.automatic?1u:0u);
        append_text(b,&p,",always:");append_u64(b,&p,e.permanentVisibility);
        append_text(b,&p,",delay:");append_u64(b,&p,e.hideDelaySeconds);
        append_text(b,&p,",mask:");append_hex64(b,&p,e.triggerMask);append_text(b,&p,"}");
    }
    line_end(b,&p);log_raw(b);
}
static void custom_release_all_owned(const CustomSettingsSnapshot&settings,const char*reason);
static void stage_settings(const HUDSettings&v,const CustomSettingsSnapshot&custom){while(_InterlockedCompareExchange(&g_settingsLock,1,0)!=0){}g_pendingSettings=v;memcpy(&g_pendingCustomSettings,&custom,sizeof(custom));
    atomic_store_long(&g_settingsPending,1);
    _InterlockedExchange(&g_settingsLock,0);
    }
static void reset_policies_for_settings_change(){for(u32 i=0;i<kHUDObjectCount;++i){ObjectControlState&c=g_controlStates[i];
    c.suppress=0;
    c.reasonMask=0;
    c.policyInitialized=0;
    c.hidePending=0;
    c.hideDeadlineUs=0;
    c.gateClosed=0;
    }for(u32 i=0;i<kCustomElementCap;++i){ObjectControlState&c=g_customControlStates[i];c.suppress=0;c.reasonMask=0;c.policyInitialized=0;c.hidePending=0;c.hideDeadlineUs=0;c.gateClosed=0;}memset(g_customRuntimeStates,0,sizeof(g_customRuntimeStates));g_haveRuntimeState=0;
    g_policyFailOpen=false;
    }
// File watching/staging occurs on a worker thread. The HUD/AdvanceMovie thread is the only place that
// commits a new settings snapshot into live policy state, so policy/hotkey state changes stay serialized.
static __declspec(noinline) void consume_pending_settings(){
    if(atomic_load_long(&g_settingsPending)==0)return;
    HUDSettings next=make_default_settings();bool customChanged=false;
    while(_InterlockedCompareExchange(&g_settingsLock,1,0)!=0){}
    next=g_pendingSettings;memcpy(&g_customCommitScratch,&g_pendingCustomSettings,sizeof(g_customCommitScratch));customChanged=!custom_settings_equal(g_customSettings,g_customCommitScratch);
    atomic_store_long(&g_settingsPending,0);
    _InterlockedExchange(&g_settingsLock,0);
    if(settings_equal(g_settings,next)&&!customChanged)return;

    bool wasVerbose=g_settings.debugLog==DebugLogLevel::Verbose;
    bool willVerbose=next.debugLog==DebugLogLevel::Verbose;
    if(wasVerbose)log_settings_snapshot("[Settings] applied",next,g_customCommitScratch);
    custom_release_all_owned(g_customSettings,"settings change");
    g_settings=next;memcpy(&g_customSettings,&g_customCommitScratch,sizeof(g_customSettings));
    g_requiredTriggerMask=required_trigger_mask(g_settings,g_customSettings);
    atomic_store_long(&g_debugLogLevel,(long)g_settings.debugLog);
    g_perfTelemetryEnabled=g_settings.performanceTelemetry;
    if(customChanged)for(u32 i=0;i<kCustomElementCap;++i)_InterlockedExchange(&g_customHotkeyActive[i],0);
    publish_hotkey_bindings(g_settings,g_customSettings,false);
    ++g_perfSettingsCommits;
    reset_policies_for_settings_change();
    if(!wasVerbose&&willVerbose)log_settings_snapshot("[Settings] applied",g_settings,g_customSettings);
}
static constexpr DWORD kWaitObject0=0u;
    static constexpr DWORD kInfiniteWait=0xFFFFFFFFu;
    static constexpr DWORD kFileNotifyChangeFileName=0x1u;
    static constexpr DWORD kFileNotifyChangeSize=0x8u;
    static constexpr DWORD kFileNotifyChangeLastWrite=0x10u;
static DWORD WINAPI settings_watcher_thread(void* context){
    HANDLE h=(HANDLE)context;
    if(!h||h==INVALID_HANDLE_VALUE)return 0;
    for(;;){
        if(WaitForSingleObject(h,kInfiniteWait)!=kWaitObject0)break;
        Sleep(50u);
        HUDSettings next=read_settings_from_disk();read_custom_elements_from_disk(&g_customReadScratch);stage_settings(next,g_customReadScratch);
        if(!FindNextChangeNotification(h))break;
    }
    FindCloseChangeNotification(h);
    return 0;
}
static bool start_settings_watcher(){
    if(_InterlockedCompareExchange(&g_settingsWatcherStarted,1,0)!=0)return true;
    char path[1024];
    if(!build_game_file_path("Data\\MCM",path,sizeof(path))){
        _InterlockedExchange(&g_settingsWatcherStarted,0);
        error_log_line("[Settings] live MCM sync unavailable: Data\\MCM path construction failed.");
        return false;
    }
    HANDLE change=FindFirstChangeNotificationA(path,TRUE,kFileNotifyChangeFileName|kFileNotifyChangeSize|kFileNotifyChangeLastWrite);
    if(!change||change==INVALID_HANDLE_VALUE){
        _InterlockedExchange(&g_settingsWatcherStarted,0);
        error_log_line("[Settings] live MCM sync unavailable: change notification could not be created.");
        return false;
    }
    DWORD tid=0;
    HANDLE thread=CreateThread(0,0,&settings_watcher_thread,change,0,&tid);
    if(!thread||thread==INVALID_HANDLE_VALUE){
        FindCloseChangeNotification(change);
        _InterlockedExchange(&g_settingsWatcherStarted,0);
        error_log_line("[Settings] live MCM sync unavailable: watcher thread creation failed.");
        return false;
    }
    CloseHandle(thread);
    verbose_log_line("[Settings] MCM directory watcher active; setting-file I/O isolated from HUD/gameplay paths.");
    return true;
}

// Native hotkey input stays entirely on the cached gameplay-input path.
// MCM keyinput controls only capture/store per-object key-code strings in DynamicHUD.ini;
// one BSInputEventUser-compatible object owns gameplay execution inside the DLL.
// These offsets are Fallout 4 ABI contracts used by the supported Address Library/runtime families.
// If the singleton/array contract fails at runtime, hotkeys are disabled rather than patched.
// Current product contract is keyboard + optional Shift/Ctrl/Alt. Mouse/gamepad and modifier-only bindings
// are intentionally outside the current hotkey contract.
static constexpr u32 kMenuControlsHandlersOffset=0x18u;
static constexpr u32 kMenuControlsRemapModeOffset=0x82u;
static constexpr u32 kKeyboardInputDevice=0u;
static constexpr u32 kMaxKeyboardKeyCode=255u;
struct RawButtonEvent {
    void*vtable;u32 device;u32 eventType;void*next;u8 unknown18[0x18];u32 keyCode;u32 pad34;float value;float heldDownSecs;
};
static_assert(sizeof(RawButtonEvent)==0x40,"FO4 ButtonEvent ABI mismatch");
static_assert(__builtin_offsetof(RawButtonEvent,keyCode)==0x30u,"FO4 ButtonEvent keyCode offset mismatch");
static_assert(__builtin_offsetof(RawButtonEvent,value)==0x38u,"FO4 ButtonEvent value offset mismatch");
static_assert(__builtin_offsetof(RawButtonEvent,heldDownSecs)==0x3Cu,"FO4 ButtonEvent heldDownSecs offset mismatch");
struct RawBSTArray { void**data;u32 capacity;u32 pad0C;u32 size;u32 pad14; };
static_assert(sizeof(RawBSTArray)==0x18,"BSTArray pointer layout mismatch");
struct RawInputUser { void**vtable;u8 inputEventHandlingEnabled;u8 pad09[7]; };
static_assert(sizeof(RawInputUser)==0x10,"BSInputEventUser ABI mismatch");
using MemoryManagerGetFn=void* (__fastcall*)();
using MemoryManagerAllocFn=void* (__fastcall*)(void*,u32,u32,u32);
using MemoryManagerFreeFn=void (__fastcall*)(void*,void*,u32);

static void log_hotkey_toggle(u32 slot,bool active,u32 key,u32 mods){
    if(!debug_verbose_enabled()||slot>=kHUDObjectCount)return;
    char b[520];SIZE_T p=0;
    append_text(b,&p,"[Hotkeys] trigger id=");append_u64(b,&p,slot+1u);
    append_text(b,&p," name=");append_text(b,&p,hud_object_name(slot));
    append_text(b,&p," state=");append_text(b,&p,active?"ACTIVE":"INACTIVE");
    append_text(b,&p," key=");append_u64(b,&p,key);
    append_text(b,&p," modifiers=");append_u64(b,&p,mods);
    line_end(b,&p);log_raw(b);
}
static bool set_hotkey_state(u32 slot,bool active,u32 key,u32 mods){if(slot>=kHUDObjectCount)return false;
    long next=active?1:0;
    long old=_InterlockedExchange(&g_hotkeyActive[slot],next);
    if(old!=next)log_hotkey_toggle(slot,active,key,mods);
    return old!=next;
    }
static bool set_custom_hotkey_state(u32 slot,bool active,u32 key,u32 mods){if(slot>=kCustomElementCap)return false;long next=active?1:0;long old=_InterlockedExchange(&g_customHotkeyActive[slot],next);if(old!=next&&debug_verbose_enabled()){char b[700];SIZE_T p=0;append_text(b,&p,"[CustomHUD] hotkey index=");append_u64(b,&p,slot);append_text(b,&p," name=");append_text(b,&p,slot<g_customSettings.count?g_customSettings.entries[slot].name:"<removed>");append_text(b,&p," state=");append_text(b,&p,active?"ACTIVE":"INACTIVE");append_text(b,&p," key=");append_u64(b,&p,key);append_text(b,&p," modifiers=");append_u64(b,&p,mods);line_end(b,&p);log_raw(b);}return old!=next;}
static u32 current_hotkey_modifiers(){
    u32 mods=0u;
    if((GetAsyncKeyState(0x10)&0x8000)!=0)mods|=kHotkeyModShift;
    if((GetAsyncKeyState(0x11)&0x8000)!=0)mods|=kHotkeyModCtrl;
    if((GetAsyncKeyState(0x12)&0x8000)!=0)mods|=kHotkeyModAlt;
    // Windows keys are intentionally outside kHotkeyModMask, preventing an accidental exact match.
    if(((GetAsyncKeyState(0x5B)|GetAsyncKeyState(0x5C))&0x8000)!=0)mods|=0x80u;
    return mods;
}
static void* __fastcall hotkey_input_dtor(void*self,u32){return self;}
static bool __fastcall hotkey_should_handle(void* self,const void*){
    if(!self||lifecycle_suspend_load_acquire()!=0)return false;
    RawInputUser* user=(RawInputUser*)self;
    if(!user->inputEventHandlingEnabled||!g_menuControls)return false;
    // MCM remapping temporarily owns keyboard capture; never toggle HUD objects while a key is being assigned.
    return *((volatile u8*)g_menuControls+kMenuControlsRemapModeOffset)==0u;
}
static void __fastcall hotkey_input_noop(void*,const void*){}
static void __fastcall hotkey_on_button(void*,const RawButtonEvent* event){
    if(!event||event->device!=kKeyboardInputDevice)return;
    u32 key=event->keyCode;
    if(key==0u||key>kMaxKeyboardKeyCode)return;
    if(event->value==0.0f||event->heldDownSecs!=0.0f)return; // initial press only
    u32 mods=current_hotkey_modifiers();

    // Discovery uses a normal MCM hotkey registration. MCM owns assignment/conflict handling; this native
    // handler only consumes the cached registration during gameplay. A matching discovery press is reserved
    // for discovery so it cannot also toggle a Dynamic HUD duplicate-bind keyinput action.
    u32 discoveryPacked=(u32)_InterlockedCompareExchange(&g_customDiscoveryHotkeyPacked,0,0);
    if((discoveryPacked&kHotkeyPackedBound)!=0u&&(discoveryPacked&0xFFu)==key&&((discoveryPacked>>8)&0xFFu)==mods){
        if(_InterlockedExchange(&g_customDiscoveryRequested,1)==0)log_line("[CustomHUD] one-shot discovery requested by standard MCM hotkey.");
        return;
    }

    // One physical combination may intentionally belong to many HUD objects. Determine one target state
    // for the entire matching set, then apply it to every member in this same callback. If any member was
    // reset/out of phase, the first press heals the group to ACTIVE; only an all-active group toggles OFF.
    u32 matches=0u;bool anyInactive=false;
    for(u32 i=0;i<kHUDObjectCount;++i){u32 packed=(u32)_InterlockedCompareExchange(&g_hotkeyPacked[i],0,0);if((packed&kHotkeyPackedBound)==0u)continue;if((packed&0xFFu)!=key||((packed>>8)&0xFFu)!=mods)continue;++matches;if(!hotkey_active(i))anyInactive=true;}
    for(u32 i=0;i<kCustomElementCap;++i){u32 packed=(u32)_InterlockedCompareExchange(&g_customHotkeyPacked[i],0,0);if((packed&kHotkeyPackedBound)==0u)continue;if((packed&0xFFu)!=key||((packed>>8)&0xFFu)!=mods)continue;++matches;if(!custom_hotkey_active(i))anyInactive=true;}
    if(matches==0u)return;bool targetActive=anyInactive;bool changed=false;
    for(u32 i=0;i<kHUDObjectCount;++i){u32 packed=(u32)_InterlockedCompareExchange(&g_hotkeyPacked[i],0,0);if((packed&kHotkeyPackedBound)==0u)continue;if((packed&0xFFu)!=key||((packed>>8)&0xFFu)!=mods)continue;if(set_hotkey_state(i,targetActive,key,mods))changed=true;}
    for(u32 i=0;i<kCustomElementCap;++i){u32 packed=(u32)_InterlockedCompareExchange(&g_customHotkeyPacked[i],0,0);if((packed&kHotkeyPackedBound)==0u)continue;if((packed&0xFFu)!=key||((packed>>8)&0xFFu)!=mods)continue;if(set_custom_hotkey_state(i,targetActive,key,mods))changed=true;}
    if(changed)_InterlockedExchange(&g_hotkeyPolicyDirty,1);
}
static void* g_hotkeyInputVtable[9]={
    (void*)&hotkey_input_dtor,(void*)&hotkey_should_handle,(void*)&hotkey_input_noop,(void*)&hotkey_input_noop,(void*)&hotkey_input_noop,
    (void*)&hotkey_input_noop,(void*)&hotkey_input_noop,(void*)&hotkey_input_noop,(void*)&hotkey_on_button
};
static RawInputUser g_hotkeyInputUser={g_hotkeyInputVtable,1u,{0,0,0,0,0,0,0}};

static bool register_hotkey_input_handler(){
    if(_InterlockedCompareExchange(&g_hotkeyInputRegistered,0,0)!=0)return true;
    if(!g_addresses.menuControlsSingleton||!g_addresses.memoryManagerSingleton||
       !g_addresses.memoryManagerAllocate||!g_addresses.memoryManagerFree){
        error_log_line("[Hotkeys] native input DISABLED: required Address Library capabilities missing.");
        return false;
    }

    void* menu=0;
    if(!safe_read((void*)g_addresses.menuControlsSingleton,&menu)||!menu||
       !is_readable_process_range((u8*)menu+kMenuControlsHandlersOffset,sizeof(RawBSTArray))){
        error_log_line("[Hotkeys] native input DISABLED: MenuControls singleton unavailable/invalid.");
        return false;
    }

    RawBSTArray* handlers=(RawBSTArray*)((u8*)menu+kMenuControlsHandlersOffset);
    RawBSTArray array={};
    memcpy(&array,handlers,sizeof(array));
    if(array.size>array.capacity||array.capacity>1024u||
       (array.size&&(!array.data||!is_readable_process_range(array.data,(SIZE_T)array.size*sizeof(void*))))){
        error_log_line("[Hotkeys] native input DISABLED: MenuControls handler array contract failed.");
        return false;
    }
    for(u32 i=0;i<array.size;++i){
        if(array.data[i]!=&g_hotkeyInputUser)continue;
        g_menuControls=menu;
        _InterlockedExchange(&g_hotkeyInputRegistered,1);
        verbose_log_line("[Hotkeys] native input handler already registered.");
        return true;
    }

    if(array.size==array.capacity){
        u32 nextCap=array.capacity?array.capacity*2u:1u;
        if(nextCap<array.size+1u)nextCap=array.size+1u;
        if(nextCap>1024u){
            error_log_line("[Hotkeys] native input DISABLED: handler array growth rejected.");
            return false;
        }
        auto getManager=(MemoryManagerGetFn)g_addresses.memoryManagerSingleton;
        auto alloc=(MemoryManagerAllocFn)g_addresses.memoryManagerAllocate;
        auto freeMem=(MemoryManagerFreeFn)g_addresses.memoryManagerFree;
        void* manager=getManager?getManager():0;
        if(!manager){
            error_log_line("[Hotkeys] native input DISABLED: game MemoryManager unavailable.");
            return false;
        }
        void** newData=(void**)alloc(manager,nextCap*(u32)sizeof(void*),0u,0u);
        if(!newData){
            error_log_line("[Hotkeys] native input DISABLED: handler array allocation failed.");
            return false;
        }
        if(array.size)memcpy(newData,array.data,(SIZE_T)array.size*sizeof(void*));
        if(array.data)freeMem(manager,array.data,0u);
        handlers->data=newData;
        handlers->capacity=nextCap;
        array.data=newData;
        array.capacity=nextCap;
    }

    handlers->data[array.size]=&g_hotkeyInputUser;
    handlers->size=array.size+1u;
    g_menuControls=menu;
    _InterlockedExchange(&g_hotkeyInputRegistered,1);
    verbose_log_line("[Hotkeys] native BSInputEventUser handler=READY (MenuControls handler array).");
    return true;
}

static u64 now_us();
static bool finite_float(float v);
static s64 float_milli(float v);

// ---------------- Runtime trigger telemetry ----------------
// Cold validation helpers are shared by production trigger samplers. Expensive RTTI/safe-read
// work is kept off steady hot paths once a live object identity has been proven.
static bool rtti_is(void*object,const char*expected){char type[160];return object&&expected&&copy_rtti_name(object,type,sizeof(type))&&str_eq(type,expected);}


struct RuntimeStateSnapshot {
    void*player;u32 actorStateWord;u32 weaponState;u32 gunState;u32 sourceMask;u32 actorValueMask;
    u32 healthPct;u32 actionPointsPct;u32 crosshairMode;
    float healthCurrent;float healthMax;float actionPointsCurrent;float actionPointsMax;
    u8 weaponDrawn;
        u8 weaponHolstered;
        u8 aiming;
        u8 sprinting;
        u8 sneaking;
        u8 inCombat;
        u8 inVATS;
        u8 vatsValid;
        u8 thirdPerson;
        u8 perspectiveValid;
        u8 radiationActive;
        u8 radiationValid;
        u8 powerArmorActive;
        u8 powerArmorValid;
        u8 crosshairModeValid;
};
static RuntimeStateSnapshot g_lastRuntimeState={};

// Production VATS telemetry: use Bethesda/F4SE UI::IsMenuOpen("VATSMenu") on the HUD AdvanceMovie thread.
// VATSMenu remains open across targeting/selection and playback, so its native menu state is the VATS trigger authority.
struct FixedStringRef { void* data; };
using FixedStringCtorFn=FixedStringRef* (*)(FixedStringRef*,const char*);
using UIIsMenuOpenFn=bool (*)(void*,const FixedStringRef*);
static FixedStringRef g_vatsMenuName={};
static bool g_vatsMenuNameReady=false;
static void* g_validatedMenuStateUI=0;

struct NativeMenuNameRef { const char* literal; FixedStringRef ref; bool ready; };
static NativeMenuNameRef g_buttonHintMenuNames[]={
    {"PipboyMenu",{},false},
    {"ExamineMenu",{},false},
    {"ExamineConfirmMenu",{},false},
    {"CookingMenu",{},false},
    {"PowerArmorModMenu",{},false},
    {"RobotModMenu",{},false},
    {"WorkshopMenu",{},false},
    {"Workshop_CaravanMenu",{},false}
};
static bool g_buttonHintMenuNamesReady=false;
static __declspec(noinline) bool prepare_vats_menu_name_cold();


// -----------------------------------------------------------------------------
// Custom HUD support
// -----------------------------------------------------------------------------
// Discovery is dormant during normal gameplay. A standard MCM hotkey raises an atomic request; the
// next HUD AdvanceMovie performs one bounded UI-thread scan and writes disabled INIs. Runtime control
// consumes only enabled INIs. Standalone custom menus use the native IMenu display gate; targets
// hosted by HUDMenu/native menus retain the conservative child-level GFx visibility fallback.
static constexpr s32 kGFxTypeBool=2;
static constexpr s32 kGFxTypeInt=3;
static constexpr s32 kGFxTypeUInt=4;
static constexpr s32 kGFxTypeNumber=5;
static constexpr s32 kGFxTypeString=6;
static constexpr s32 kGFxTypeObject=8;
static constexpr s32 kGFxTypeArray=9;
static constexpr s32 kGFxTypeDisplayObject=10;
static constexpr s32 kGFxManaged=1<<6;
static constexpr s32 kGFxTypeMask=0x8F;
using GFxReleaseFn=void (*)(void*,GFxValueLite*,void*);
using GFxGetMemberFn=bool (*)(void*,void*,const char*,GFxValueLite*,bool);
using GFxSetMemberFn=bool (*)(void*,void*,const char*,const GFxValueLite*,bool);
using GFxInvokeFn=bool (*)(void*,void*,GFxValueLite*,const char*,GFxValueLite*,u64,bool);
static bool g_customDiscoveryCapable=false;

// Discovery hotkey assignment is standard original-MCM state. The settings watcher parses
// Data\MCM\Settings\Keybinds.json and publishes one cached binding to the existing native input handler.
// No custom Scaleform callback is required; discovery execution still happens later on HUD AdvanceMovie.

struct CustomHUDCandidate {char menu[96];char path[512];char source[260];char name[128];bool stablePath;};
static constexpr u32 kCustomCandidateCap=64u;
static CustomHUDCandidate g_customDiscoveryScratch[kCustomCandidateCap]={};
static char g_customDiscoveryWriteBuffer[8192]={};
static char g_customDiscoveryMessageBuffer[32768]={};

static __forceinline s32 custom_gfx_base_type(const GFxValueLite&v){return v.type&kGFxTypeMask;}
static __forceinline bool custom_gfx_is_managed(const GFxValueLite&v){return (v.type&kGFxManaged)!=0;}
static __forceinline bool custom_gfx_is_display(const GFxValueLite&v){return custom_gfx_base_type(v)==kGFxTypeDisplayObject;}
static __forceinline bool custom_gfx_is_objectish(const GFxValueLite&v){s32 t=custom_gfx_base_type(v);return t==kGFxTypeObject||t==kGFxTypeArray||t==kGFxTypeDisplayObject;}
static void custom_gfx_clear(GFxValueLite*v){if(!v)return;if(custom_gfx_is_managed(*v)&&v->objectInterface&&v->data.object&&g_addresses.gfxObjectRelease)((GFxReleaseFn)g_addresses.gfxObjectRelease)(v->objectInterface,v,v->data.object);memset(v,0,sizeof(*v));}
static bool custom_runtime_cached_value(const CustomRuntimeState&rt,GFxValueLite*out){if(!out||!rt.cachedTargetValid)return false;*out=rt.cachedTarget;return out->objectInterface&&out->data.object&&custom_gfx_is_objectish(*out);}
static void custom_runtime_drop_cached_value(CustomRuntimeState&rt){if(rt.cachedTargetValid)custom_gfx_clear(&rt.cachedTarget);rt.cachedTargetValid=false;}
static bool custom_gfx_get(const GFxValueLite&owner,const char*name,GFxValueLite*out){if(!out)return false;memset(out,0,sizeof(*out));if(!name||!owner.objectInterface||!owner.data.object||!g_addresses.gfxGetMember||!custom_gfx_is_objectish(owner))return false;return ((GFxGetMemberFn)g_addresses.gfxGetMember)(owner.objectInterface,owner.data.object,name,out,custom_gfx_is_display(owner));}
static bool custom_gfx_invoke(const GFxValueLite&owner,const char*name,GFxValueLite*out,GFxValueLite*args,u32 count){if(out)memset(out,0,sizeof(*out));if(!name||!owner.objectInterface||!owner.data.object||!g_addresses.gfxInvoke||!custom_gfx_is_objectish(owner))return false;return ((GFxInvokeFn)g_addresses.gfxInvoke)(owner.objectInterface,owner.data.object,out,name,args,(u64)count,custom_gfx_is_display(owner));}
static bool custom_gfx_number(const GFxValueLite&v,double*out){if(!out)return false;switch(custom_gfx_base_type(v)){case kGFxTypeInt:*out=(double)v.data.s32v;return true;case kGFxTypeUInt:*out=(double)v.data.u32v;return true;case kGFxTypeNumber:*out=v.data.number;return true;default:return false;}}
static bool custom_copy_readable_cstr(const char*src,char*out,SIZE_T cap){if(!src||!out||cap<2u)return false;out[0]=0;MemoryBasicInformation mbi={};if(VirtualQuery(src,&mbi,sizeof(mbi))!=sizeof(mbi)||mbi.state!=MEM_COMMIT||(mbi.protect&(PAGE_NOACCESS|PAGE_GUARD))!=0)return false;u64 start=(u64)src,base=(u64)mbi.baseAddress,end=base+mbi.regionSize;if(start<base||start>=end)return false;SIZE_T max=(SIZE_T)(end-start);if(max>cap-1u)max=cap-1u;for(SIZE_T i=0;i<max;++i){char c=src[i];out[i]=c;if(!c)return i>0u;}out[max]=0;return out[0]!=0;}
static bool custom_gfx_string(const GFxValueLite&v,char*out,SIZE_T cap){if(!out||cap<2u||custom_gfx_base_type(v)!=kGFxTypeString)return false;const char*src=0;if(custom_gfx_is_managed(v)){if(!v.data.managedString||!safe_read(v.data.managedString,&src))return false;}else src=v.data.string;return custom_copy_readable_cstr(src,out,cap);}
static bool custom_fixed_string_copy(const FixedStringRef&ref,char*out,SIZE_T cap){if(!out||cap<2u)return false;out[0]=0;void*entry=ref.data;if(!entry)return false;for(u32 depth=0;depth<4u;++depth){u32 state=0;if(!safe_read((u8*)entry+8u,&state))return false;if(!(state&0x4000u))return custom_copy_readable_cstr((const char*)((u8*)entry+0x18u),out,cap);void*next=0;if(!safe_read((u8*)entry+0x10u,&next)||!next||next==entry)return false;entry=next;}return false;}
static bool custom_generated_name(const char*s){if(!s)return true;const char*p="instance";u32 i=0;while(p[i]&&s[i]==p[i])++i;if(p[i])return false;if(!s[i])return false;for(;s[i];++i)if(s[i]<'0'||s[i]>'9')return false;return true;}
static bool custom_path_join(char*out,SIZE_T cap,const char*parent,const char*name){if(!out||cap<4u||!parent||!name||!*name)return false;SIZE_T p=0;append_text_cap(out,cap,&p,parent);append_char_cap(out,cap,&p,'.');append_text_cap(out,cap,&p,name);return p+1u<cap;}
static bool custom_gfx_name(const GFxValueLite&node,char*out,SIZE_T cap){GFxValueLite v={};bool ok=custom_gfx_get(node,"name",&v)&&custom_gfx_string(v,out,cap);custom_gfx_clear(&v);return ok;}
static u32 custom_gfx_child_count(const GFxValueLite&node){GFxValueLite v={};double n=0.0;u32 out=0u;if(custom_gfx_get(node,"numChildren",&v)&&custom_gfx_number(v,&n)&&n>0.0){if(n>128.0)n=128.0;out=(u32)n;}custom_gfx_clear(&v);return out;}
static bool custom_gfx_child(const GFxValueLite&node,u32 index,GFxValueLite*out){GFxValueLite arg={};arg.type=kGFxTypeInt;arg.data.s32v=(s32)index;return custom_gfx_invoke(node,"getChildAt",out,&arg,1u)&&custom_gfx_is_objectish(*out);}
static bool custom_gfx_source(const GFxValueLite&node,char*out,SIZE_T cap){if(!out||cap<2u)return false;out[0]=0;GFxValueLite info={};GFxValueLite url={};bool ok=false;if(custom_gfx_get(node,"loaderInfo",&info)&&custom_gfx_is_objectish(info)&&custom_gfx_get(info,"url",&url))ok=custom_gfx_string(url,out,cap);custom_gfx_clear(&url);custom_gfx_clear(&info);if(ok)return true;if(custom_gfx_get(node,"contentLoaderInfo",&info)&&custom_gfx_is_objectish(info)&&custom_gfx_get(info,"url",&url))ok=custom_gfx_string(url,out,cap);custom_gfx_clear(&url);custom_gfx_clear(&info);return ok;}
static bool custom_gfx_visible(const GFxValueLite&node,bool*outVisible){if(!outVisible)return false;GFxValueLite v={};bool ok=custom_gfx_get(node,"visible",&v)&&custom_gfx_base_type(v)==kGFxTypeBool;if(ok)*outVisible=v.data.u32v!=0u;custom_gfx_clear(&v);return ok;}
static bool custom_gfx_set_visible(const GFxValueLite&node,bool visible){if(!g_addresses.gfxSetMember||!node.objectInterface||!node.data.object||!custom_gfx_is_objectish(node))return false;GFxValueLite arg={};arg.type=kGFxTypeBool;arg.data.u32v=visible?1u:0u;return ((GFxSetMemberFn)g_addresses.gfxSetMember)(node.objectInterface,node.data.object,"visible",&arg,custom_gfx_is_display(node));}

// ButtonBarMenu owns both the global button-hint bar and a separate prompt panel.
// The feature therefore suppresses only ButtonBarHolder_mc; the native menu remains open and
// continues handling keyboard/controller actions. Context reuses the same native UI::IsMenuOpen
// capability as the production VATS trigger. ButtonBarMenu::AdvanceMovie samples only while the
// master feature is enabled, then performs a holder visibility write only when the desired state
// changes. No menu-stack hook or Scaleform traversal is used for context classification.
static const char* button_hints_context_name(ButtonHintsContext context){switch(context){case ButtonHintsContext::PipBoy:return "PipBoy";case ButtonHintsContext::Workbench:return "Workbench";case ButtonHintsContext::VATS:return "VATS";case ButtonHintsContext::Menus:return "Menus";default:return "Unknown";}}
static bool button_hints_should_hide(ButtonHintsContext context){
    if(!g_settings.enabled||!g_settings.hideButtonHints)return false;
    switch(context){case ButtonHintsContext::PipBoy:return g_settings.hideButtonHintsPipBoy;case ButtonHintsContext::Workbench:return g_settings.hideButtonHintsWorkbench;case ButtonHintsContext::VATS:return g_settings.hideButtonHintsVATS;case ButtonHintsContext::Menus:return g_settings.hideButtonHintsMenus;default:return false;}
}
static bool menu_state_get_ui_fast(void**outUI){
    if(outUI)*outUI=0;if(!outUI||!g_addresses.uiSingleton||!g_addresses.uiIsMenuOpen)return false;
    void*ui=*(void* volatile*)(u64)g_addresses.uiSingleton;if(!ui)return false;
    if(ui!=g_validatedMenuStateUI){if(!rtti_is(ui,".?AVUI@@"))return false;g_validatedMenuStateUI=ui;}
    *outUI=ui;return true;
}
static bool native_menu_name_prepare(NativeMenuNameRef*menu){
    if(!menu||!menu->literal||!g_addresses.fixedStringCtor)return false;if(menu->ready&&menu->ref.data)return true;
    menu->ref.data=0;FixedStringRef*result=((FixedStringCtorFn)g_addresses.fixedStringCtor)(&menu->ref,menu->literal);
    menu->ready=result==&menu->ref&&menu->ref.data!=0;return menu->ready;
}
static bool prepare_button_hint_menu_names_cold(){
    if(g_buttonHintMenuNamesReady)return true;
    for(u32 i=0;i<(u32)(sizeof(g_buttonHintMenuNames)/sizeof(g_buttonHintMenuNames[0]));++i){
        if(!native_menu_name_prepare(&g_buttonHintMenuNames[i])){if(debug_errors_enabled()){char b[520];SIZE_T p=0;append_text(b,&p,"[ButtonHints] BSFixedString construction failed menu=");append_text(b,&p,g_buttonHintMenuNames[i].literal);append_text(b,&p,"; feature left fail-open.");line_end(b,&p);log_raw(b);}return false;}
    }
    g_buttonHintMenuNamesReady=true;return true;
}
static __forceinline bool native_menu_open(void*ui,const FixedStringRef&name){return ui&&name.data&&((UIIsMenuOpenFn)g_addresses.uiIsMenuOpen)(ui,&name);}
static ButtonHintsContext button_hints_sample_context(char*outName,SIZE_T cap){
    if(outName&&cap)outName[0]=0;if(!g_settings.enabled||!g_settings.hideButtonHints)return ButtonHintsContext::Menus;
    void*ui=0;if(!menu_state_get_ui_fast(&ui)||(!g_vatsMenuNameReady&&!prepare_vats_menu_name_cold())||!prepare_button_hint_menu_names_cold())return ButtonHintsContext::Unknown;
    if(native_menu_open(ui,g_vatsMenuName)){if(outName)copy_text(outName,cap,"VATSMenu");return ButtonHintsContext::VATS;}
    if(native_menu_open(ui,g_buttonHintMenuNames[0].ref)){if(outName)copy_text(outName,cap,g_buttonHintMenuNames[0].literal);return ButtonHintsContext::PipBoy;}
    for(u32 i=1u;i<(u32)(sizeof(g_buttonHintMenuNames)/sizeof(g_buttonHintMenuNames[0]));++i){if(native_menu_open(ui,g_buttonHintMenuNames[i].ref)){if(outName)copy_text(outName,cap,g_buttonHintMenuNames[i].literal);return ButtonHintsContext::Workbench;}}
    if(outName)copy_text(outName,cap,"<other>");return ButtonHintsContext::Menus;
}
static void button_hints_bind_menu(void*menu){
    if(g_buttonHintsRuntime.menuObject==menu)return;
    g_buttonHintsRuntime.menuObject=menu;g_buttonHintsRuntime.appliedHidden=false;g_buttonHintsRuntime.failureLogged=false;
    g_buttonHintsRuntime.context=ButtonHintsContext::Unknown;g_buttonHintsRuntime.contextMenu[0]=0;
}
static void button_hints_update_context(bool perfSample){
    if(g_perfTelemetryEnabled)++g_perfButtonHintsClassifyCalls;
    s64 perfStart=perfSample?perf_qpc():0;
    char name[96]={};ButtonHintsContext next=button_hints_sample_context(name,sizeof(name));
    if(perfSample){s64 perfEnd=perf_qpc();perf_add(g_perfButtonHintsClassify,perfStart,perfEnd);}
    bool changed=next!=g_buttonHintsRuntime.context||!str_eq(g_buttonHintsRuntime.contextMenu,name);
    g_buttonHintsRuntime.context=next;copy_text(g_buttonHintsRuntime.contextMenu,sizeof(g_buttonHintsRuntime.contextMenu),name);
    if(changed&&debug_verbose_enabled()){char b[620];SIZE_T p=0;append_text(b,&p,"[ButtonHints] context=");append_text(b,&p,button_hints_context_name(next));append_text(b,&p," menuState=");append_text(b,&p,name[0]?name:"<unknown>");append_text(b,&p," hide=");append_u64(b,&p,button_hints_should_hide(next)?1u:0u);line_end(b,&p);log_raw(b);}
}
static bool button_hints_get_holder(void*menu,GFxValueLite*out){
    if(!menu||!out||!g_buttonHintsCapable)return false;memset(out,0,sizeof(*out));GFxValueLite stage={};
    if(!safe_read((u8*)menu+0x20u,&stage)||!custom_gfx_is_objectish(stage))return false;
    if(!custom_gfx_get(stage,"ButtonBarHolder_mc",out)||!custom_gfx_is_objectish(*out)){custom_gfx_clear(out);return false;}
    return true;
}
static void button_hints_log_transition(const char*event,void*menu){
    if(!debug_verbose_enabled())return;char b[700];SIZE_T p=0;append_text(b,&p,"[ButtonHints] event=");append_text(b,&p,event);
    append_text(b,&p," context=");append_text(b,&p,button_hints_context_name(g_buttonHintsRuntime.context));append_text(b,&p," menuState=");append_text(b,&p,g_buttonHintsRuntime.contextMenu[0]?g_buttonHintsRuntime.contextMenu:"<unknown>");
    append_text(b,&p," menuObject=");append_hex64(b,&p,(u64)menu);line_end(b,&p);log_raw(b);
}
static void button_hints_note_failure(const char*reason,void*menu){
    if(g_buttonHintsRuntime.failureLogged||!debug_errors_enabled())return;g_buttonHintsRuntime.failureLogged=true;
    char b[760];SIZE_T p=0;append_text(b,&p,"[ButtonHints] operation failed; feature left fail-open reason=");append_text(b,&p,reason);
    append_text(b,&p," context=");append_text(b,&p,button_hints_context_name(g_buttonHintsRuntime.context));append_text(b,&p," menuObject=");append_hex64(b,&p,(u64)menu);line_end(b,&p);log_raw(b);
}
static void button_hints_after_advance(void*menu,bool perfSample){
    if(!g_buttonHintsCapable||!menu)return;button_hints_bind_menu(menu);
    if(g_settings.enabled&&g_settings.hideButtonHints)button_hints_update_context(perfSample);
    else {g_buttonHintsRuntime.context=ButtonHintsContext::Menus;copy_text(g_buttonHintsRuntime.contextMenu,sizeof(g_buttonHintsRuntime.contextMenu),"<disabled>");}
    bool wantHidden=button_hints_should_hide(g_buttonHintsRuntime.context);
    if(g_buttonHintsRuntime.appliedHidden==wantHidden)return;
    bool measureTransition=g_perfTelemetryEnabled;if(measureTransition)++g_perfButtonHintsTransitions;s64 gfxStart=measureTransition?perf_qpc():0;
    GFxValueLite holder={};if(!button_hints_get_holder(menu,&holder)){if(measureTransition){s64 gfxEnd=perf_qpc();perf_add(g_perfButtonHintsGFx,gfxStart,gfxEnd);}button_hints_note_failure("ButtonBarHolder_mc-unavailable",menu);return;}
    bool ok=custom_gfx_set_visible(holder,!wantHidden);custom_gfx_clear(&holder);
    if(measureTransition){s64 gfxEnd=perf_qpc();perf_add(g_perfButtonHintsGFx,gfxStart,gfxEnd);}
    if(!ok){button_hints_note_failure("visible-write",menu);return;}
    g_buttonHintsRuntime.appliedHidden=wantHidden;g_buttonHintsRuntime.failureLogged=false;
    button_hints_log_transition(wantHidden?"HIDE":"RELEASE",menu);
}
struct CustomMenuStackLite{void**entries;u32 capacity;u32 pad0C;u32 count;u32 pad14;};
static bool custom_find_active_menu(const char*menuName,void**outMenu){if(outMenu)*outMenu=0;if(!menuName||!outMenu||!g_addresses.uiSingleton)return false;void*ui=0;if(!safe_read((void*)g_addresses.uiSingleton,&ui)||!ui)return false;CustomMenuStackLite stack={};if(!safe_read((u8*)ui+0x190u,&stack)||!stack.entries||stack.count>64u||stack.capacity<stack.count||stack.capacity>256u||!is_readable_process_range(stack.entries,(SIZE_T)stack.count*sizeof(void*)))return false;for(u32 i=0;i<stack.count;++i){void*menu=0;if(!safe_read(stack.entries+i,&menu)||!menu)continue;FixedStringRef nameRef={};if(!safe_read((u8*)menu+0x50u,&nameRef))continue;char name[96]={};if(!custom_fixed_string_copy(nameRef,name,sizeof(name))||!str_ieq(name,menuName))continue;*outMenu=menu;return true;}return false;}
static bool custom_get_menu_stage(const char*menuName,GFxValueLite*out){if(!menuName||!out)return false;memset(out,0,sizeof(*out));void*menu=0;if(!custom_find_active_menu(menuName,&menu))return false;GFxValueLite stage={};if(!safe_read((u8*)menu+0x20u,&stage)||!custom_gfx_is_objectish(stage))return false;*out=stage;return true;}
static bool custom_target_prefix(const char*target,const char*candidate){if(!target||!candidate)return false;SIZE_T i=0;while(candidate[i]){if(target[i]!=candidate[i])return false;++i;}return target[i]==0||target[i]=='.';}
static bool custom_source_eq(const char*a,const char*b){if(!a||!b)return false;for(;;){char ca=ascii_lower(*a++),cb=ascii_lower(*b++);if(ca=='/')ca='\\';if(cb=='/')cb='\\';if(ca!=cb)return false;if(!ca)return true;}}
static constexpr u32 kCustomMenuCanBeVisibleOffset=0x61u;
static constexpr u32 kCustomMenuDisplayStateVSlot=0x0Au;
using CustomMenuDisplayStateFn=void(__fastcall*)(void*);
enum CustomMenuGateFailureReason:u32 { kCustomMenuFailNone=0u,kCustomMenuFailLayoutRead=1u,kCustomMenuFailVtableRead=2u,kCustomMenuFailSlotRead=3u,kCustomMenuFailCallbackNotExecutable=4u,kCustomMenuFailWrite=5u,kCustomMenuFailRejected=6u };
static const char* custom_menu_failure_name(u32 reason){switch(reason){case kCustomMenuFailLayoutRead:return "layout-read";case kCustomMenuFailVtableRead:return "vtable-read";case kCustomMenuFailSlotRead:return "slot-read";case kCustomMenuFailCallbackNotExecutable:return "callback-not-executable";case kCustomMenuFailWrite:return "gate-write";case kCustomMenuFailRejected:return "gate-rejected";default:return "none";}}
static u64 custom_menu_retry_delay(u32 streak){u32 shift=streak>5u?5u:streak;u64 delay=1ULL<<shift;return delay>30ULL?30ULL:delay;}
static bool custom_known_vanilla_menu(const char*name);
static bool custom_menu_gate_candidate(const CustomElementSettings&cfg){if(!cfg.menu[0])return false;if(str_ieq(cfg.menu,"HUDMenu")||str_ieq(cfg.menu,"PowerArmorHUDMenu"))return false;return !custom_known_vanilla_menu(cfg.menu);}
static bool custom_find_path_recursive(const GFxValueLite&parent,const char*parentPath,const char*target,u32 depth,u32*visited,GFxValueLite*out){if(depth>8u||!visited||*visited>512u)return false;u32 n=custom_gfx_child_count(parent);for(u32 i=0;i<n&&*visited<512u;++i){++*visited;GFxValueLite child={};if(!custom_gfx_child(parent,i,&child)){custom_gfx_clear(&child);continue;}char name[128]={};if(!custom_gfx_name(child,name,sizeof(name))||!name[0]){custom_gfx_clear(&child);continue;}char path[512]={};if(!custom_path_join(path,sizeof(path),parentPath,name)){custom_gfx_clear(&child);continue;}if(str_eq(path,target)){*out=child;return true;}bool descend=custom_target_prefix(target,path);if(descend&&custom_find_path_recursive(child,path,target,depth+1u,visited,out)){custom_gfx_clear(&child);return true;}custom_gfx_clear(&child);}return false;}
static bool custom_resolve_target(const CustomElementSettings&cfg,GFxValueLite*out,bool*outOwned){if(!out||!outOwned)return false;memset(out,0,sizeof(*out));*outOwned=false;GFxValueLite stage={};if(!custom_get_menu_stage(cfg.menu,&stage))return false;if(str_eq(cfg.path,"root1")){*out=stage;*outOwned=false;}else{u32 visited=0u;if(!custom_find_path_recursive(stage,"root1",cfg.path,0u,&visited,out))return false;*outOwned=true;}if(cfg.source[0]&&!str_eq(cfg.source,"<unknown>")){char source[260]={};if(custom_gfx_source(*out,source,sizeof(source))&&source[0]&&!custom_source_eq(source,cfg.source)){if(*outOwned)custom_gfx_clear(out);memset(out,0,sizeof(*out));*outOwned=false;return false;}}return true;}
static void custom_menu_note_failure(u32 index,const CustomElementSettings&cfg,CustomRuntimeState&rt,u32 reason,void*menu,void**vt,void*fn){++g_customMenuGateFailures;if(rt.menuFailureStreak<0xFFFFFFFFu)++rt.menuFailureStreak;u64 delay=custom_menu_retry_delay(rt.menuFailureStreak);rt.menuNextAttemptFrame=g_advanceCount+delay;if(rt.menuLastFailureReason==reason)return;rt.menuLastFailureReason=reason;if(!debug_errors_enabled())return;char b[1100];SIZE_T p=0;append_text(b,&p,"[CustomHUD] menu index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,cfg.name);append_text(b,&p," event=ACQUIRE_FAILED reason=");append_text(b,&p,custom_menu_failure_name(reason));append_text(b,&p," menu=");append_text(b,&p,cfg.menu);append_text(b,&p," menuObject=");append_hex64(b,&p,(u64)menu);append_text(b,&p," vtable=");append_hex64(b,&p,(u64)vt);append_text(b,&p," callback=");append_hex64(b,&p,(u64)fn);append_text(b,&p," retryInFrames=");append_u64(b,&p,delay);line_end(b,&p);log_raw(b);}
static bool custom_menu_get_display_state_fn(void*menu,void***outVtable,void**outFn,u32*outReason){if(outVtable)*outVtable=0;if(outFn)*outFn=0;if(outReason)*outReason=kCustomMenuFailNone;if(!menu){if(outReason)*outReason=kCustomMenuFailVtableRead;return false;}void**vt=0;if(!safe_read(menu,&vt)||!vt||!is_readable_process_range(vt,(SIZE_T)(kCustomMenuDisplayStateVSlot+1u)*sizeof(void*))){if(outReason)*outReason=kCustomMenuFailVtableRead;return false;}void*fn=0;if(!safe_read(vt+kCustomMenuDisplayStateVSlot,&fn)||!fn){if(outVtable)*outVtable=vt;if(outReason)*outReason=kCustomMenuFailSlotRead;return false;}if(!is_executable_process_address(fn)){if(outVtable)*outVtable=vt;if(outFn)*outFn=fn;if(outReason)*outReason=kCustomMenuFailCallbackNotExecutable;return false;}if(outVtable)*outVtable=vt;if(outFn)*outFn=fn;return true;}
static bool custom_menu_call_display_state(void*menu){void**vt=0;void*fn=0;u32 reason=kCustomMenuFailNone;if(!custom_menu_get_display_state_fn(menu,&vt,&fn,&reason))return false;((CustomMenuDisplayStateFn)fn)(menu);return true;}
static void custom_menu_log_event(u32 index,const char*event,const CustomElementSettings&cfg,void*menu,u8 before,u8 after){if(!debug_verbose_enabled())return;char b[1000];SIZE_T p=0;append_text(b,&p,"[CustomHUD] menu index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,cfg.name);append_text(b,&p," event=");append_text(b,&p,event);append_text(b,&p," menu=");append_text(b,&p,cfg.menu);append_text(b,&p," menuObject=");append_hex64(b,&p,(u64)menu);append_text(b,&p," menuCanBeVisible=");append_u64(b,&p,before?1u:0u);append_text(b,&p,"->");append_u64(b,&p,after?1u:0u);line_end(b,&p);log_raw(b);}
static bool custom_menu_apply_gate(u32 index,const CustomElementSettings&cfg){if(index>=kCustomElementCap)return false;CustomRuntimeState&rt=g_customRuntimeStates[index];if(rt.menuGateApplied)return true;if(rt.menuNextAttemptFrame&&g_advanceCount<rt.menuNextAttemptFrame)return false;void*menu=0;if(!custom_find_active_menu(cfg.menu,&menu)){++g_customMenuAbsentRetries;if(rt.menuFailureStreak<0xFFFFFFFFu)++rt.menuFailureStreak;rt.menuNextAttemptFrame=g_advanceCount+custom_menu_retry_delay(rt.menuFailureStreak);return false;}u8 natural=0;if(!safe_read((u8*)menu+kCustomMenuCanBeVisibleOffset,&natural)){custom_menu_note_failure(index,cfg,rt,kCustomMenuFailLayoutRead,menu,0,0);return false;}void**vt=0;void*fn=0;u32 reason=kCustomMenuFailNone;if(!custom_menu_get_display_state_fn(menu,&vt,&fn,&reason)){custom_menu_note_failure(index,cfg,rt,reason,menu,vt,fn);return false;}if(!safe_write_u8((u8*)menu+kCustomMenuCanBeVisibleOffset,0u)){custom_menu_note_failure(index,cfg,rt,kCustomMenuFailWrite,menu,vt,fn);return false;}((CustomMenuDisplayStateFn)fn)(menu);u8 after=1u;if(!safe_read((u8*)menu+kCustomMenuCanBeVisibleOffset,&after)||after!=0u){safe_write_u8((u8*)menu+kCustomMenuCanBeVisibleOffset,natural);((CustomMenuDisplayStateFn)fn)(menu);custom_menu_note_failure(index,cfg,rt,kCustomMenuFailRejected,menu,vt,fn);custom_menu_log_event(index,"GATE_REJECTED",cfg,menu,natural,after);return false;}custom_runtime_drop_cached_value(rt);rt.lastObject=0;rt.haveNatural=false;rt.appliedSuppression=false;rt.restoreFailed=false;rt.menuObject=menu;rt.menuGateApplied=true;rt.menuHaveNatural=true;rt.menuNaturalCanBeVisible=natural!=0u;rt.menuFailureStreak=0u;rt.menuLastFailureReason=kCustomMenuFailNone;rt.menuNextAttemptFrame=0u;++g_customMenuGateHides;custom_menu_log_event(index,"HIDE",cfg,menu,natural,after);return true;}
static bool custom_menu_release_gate(u32 index,const CustomElementSettings&cfg,const char*event){if(index>=kCustomElementCap)return false;CustomRuntimeState&rt=g_customRuntimeStates[index];if(!rt.menuGateApplied)return true;void*active=0;bool same=custom_find_active_menu(cfg.menu,&active)&&active==rt.menuObject;bool restored=false;u8 before=0u,after=0u;if(same&&rt.menuHaveNatural&&safe_read((u8*)active+kCustomMenuCanBeVisibleOffset,&before)){u8 desired=rt.menuNaturalCanBeVisible?1u:0u;if(safe_write_u8((u8*)active+kCustomMenuCanBeVisibleOffset,desired)&&custom_menu_call_display_state(active)&&safe_read((u8*)active+kCustomMenuCanBeVisibleOffset,&after))restored=after==desired;}if(restored){++g_customMenuGateReleases;custom_menu_log_event(index,event?event:"RELEASE",cfg,active,before,after);}else{if(same)++g_customMenuGateFailures;else ++g_customMenuStaleDrops;if(debug_errors_enabled()){char b[1000];SIZE_T p=0;append_text(b,&p,"[CustomHUD] menu index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,cfg.name);append_text(b,&p," event=");append_text(b,&p,same?"RELEASE_FAILED":"STALE_RELEASE");append_text(b,&p," reason=");append_text(b,&p,event?event:"release");append_text(b,&p," menu=");append_text(b,&p,cfg.menu);append_text(b,&p," ownedMenu=");append_hex64(b,&p,(u64)rt.menuObject);append_text(b,&p," activeMenu=");append_hex64(b,&p,(u64)active);line_end(b,&p);log_raw(b);}}rt.menuObject=0;rt.menuGateApplied=false;rt.menuHaveNatural=false;rt.menuNaturalCanBeVisible=false;rt.menuFailureStreak=0u;rt.menuLastFailureReason=kCustomMenuFailNone;rt.menuNextAttemptFrame=0u;return restored;}
static void custom_log_runtime(u32 index,const char*event,const CustomElementSettings&cfg,void*object){if(!debug_verbose_enabled())return;char b[1200];SIZE_T p=0;append_text(b,&p,"[CustomHUD] runtime index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,cfg.name);append_text(b,&p," event=");append_text(b,&p,event);append_text(b,&p," menu=");append_text(b,&p,cfg.menu);append_text(b,&p," path=");append_text(b,&p,cfg.path);append_text(b,&p," object=");append_hex64(b,&p,(u64)object);line_end(b,&p);log_raw(b);}
static u64 custom_resolve_retry_delay(u32 streak){u32 shift=streak>5u?5u:streak;u64 delay=1ULL<<shift;return delay>30ULL?30ULL:delay;}
static constexpr u64 kCustomValidationIntervalFrames=60ULL;
static bool custom_resolve_and_bind_runtime_target(u32 index,const CustomElementSettings&cfg,GFxValueLite*out,bool*outOwned,bool validation){
    if(index>=kCustomElementCap||!out||!outOwned)return false;
    CustomRuntimeState&rt=g_customRuntimeStates[index];u64 frame=g_advanceCount;memset(out,0,sizeof(*out));*outOwned=false;
    if(rt.nextResolveFrame&&frame<rt.nextResolveFrame)return false;
    GFxValueLite resolved={};bool resolvedOwned=false;
    if(validation){++g_customRuntimeValidations;}
    if(!custom_resolve_target(cfg,&resolved,&resolvedOwned)){
        ++g_customRuntimeResolveFailures;if(validation)++g_customRuntimeValidationFailures;
        if(rt.resolveFailureStreak<0xFFFFFFFFu)++rt.resolveFailureStreak;rt.nextResolveFrame=frame+custom_resolve_retry_delay(rt.resolveFailureStreak);
        if(rt.lastObject&&!rt.resolveLogged){custom_log_runtime(index,"UNRESOLVED",cfg,rt.lastObject);rt.resolveLogged=true;}
        custom_runtime_drop_cached_value(rt);rt.lastObject=0;rt.haveNatural=false;rt.appliedSuppression=false;rt.nextValidationFrame=0u;
        return false;
    }
    rt.resolveFailureStreak=0u;rt.nextResolveFrame=0u;rt.nextValidationFrame=frame+kCustomValidationIntervalFrames;
    void*object=resolved.data.object;bool changed=object!=rt.lastObject;
    if(changed){custom_runtime_drop_cached_value(rt);rt.lastObject=object;rt.haveNatural=false;rt.appliedSuppression=false;rt.restoreFailed=false;rt.resolveLogged=false;++g_customRuntimeBinds;custom_log_runtime(index,"BOUND",cfg,object);}else if(rt.resolveLogged){rt.resolveLogged=false;custom_log_runtime(index,"RECOVERED",cfg,object);}
    if(!rt.cachedTargetValid&&resolvedOwned&&custom_gfx_is_managed(resolved)){rt.cachedTarget=resolved;rt.cachedTargetValid=true;memset(&resolved,0,sizeof(resolved));resolvedOwned=false;}
    if(rt.cachedTargetValid){if(resolvedOwned)custom_gfx_clear(&resolved);if(custom_runtime_cached_value(rt,out)){return true;}custom_runtime_drop_cached_value(rt);}
    *out=resolved;*outOwned=resolvedOwned;return custom_gfx_is_objectish(*out)&&out->objectInterface&&out->data.object;
}
static bool custom_prepare_runtime_target(u32 index,const CustomElementSettings&cfg,GFxValueLite*out,bool*outOwned,bool validateIfDue){
    if(index>=kCustomElementCap||!out||!outOwned)return false;CustomRuntimeState&rt=g_customRuntimeStates[index];u64 frame=g_advanceCount;
    if(rt.cachedTargetValid&&!(validateIfDue&&rt.nextValidationFrame&&frame>=rt.nextValidationFrame)){if(custom_runtime_cached_value(rt,out)){*outOwned=false;return true;}custom_runtime_drop_cached_value(rt);}
    return custom_resolve_and_bind_runtime_target(index,cfg,out,outOwned,validateIfDue&&rt.lastObject!=0);
}
static bool custom_policy_suppressed(u32 index){if(index>=g_customSettings.count)return false;const CustomElementSettings&cfg=g_customSettings.entries[index];const ObjectControlState&st=g_customControlStates[index];return g_settings.enabled&&cfg.policy.controlMode!=kControlOff&&st.policyInitialized&&st.gateClosed&&(((u32)st.reasonMask&kReasonTelemetryFailOpen)==0u);}
static void custom_runtime_before_advance(){if(!g_customControlCapable||!g_settings.enabled)return;for(u32 i=0;i<g_customSettings.count;++i){const CustomElementSettings&cfg=g_customSettings.entries[i];if(cfg.policy.controlMode==kControlOff)continue;CustomRuntimeState&rt=g_customRuntimeStates[i];if(custom_menu_gate_candidate(cfg)){if(rt.menuGateApplied&&!custom_policy_suppressed(i))custom_menu_release_gate(i,cfg,"RELEASE");continue;}rt.restoreFailed=false;if(!rt.appliedSuppression)continue;GFxValueLite target={};bool owned=false;if(!custom_prepare_runtime_target(i,cfg,&target,&owned,false))continue;if(rt.haveNatural){if(custom_gfx_set_visible(target,rt.naturalVisible))rt.appliedSuppression=false;else{rt.restoreFailed=true;rt.appliedSuppression=false;rt.haveNatural=false;custom_runtime_drop_cached_value(rt);rt.lastObject=0;rt.nextValidationFrame=0u;}}else rt.appliedSuppression=false;if(owned)custom_gfx_clear(&target);}}
static void custom_runtime_after_advance(){if(!g_customControlCapable||!g_settings.enabled)return;for(u32 i=0;i<g_customSettings.count;++i){const CustomElementSettings&cfg=g_customSettings.entries[i];if(cfg.policy.controlMode==kControlOff)continue;CustomRuntimeState&rt=g_customRuntimeStates[i];if(custom_menu_gate_candidate(cfg)){if(custom_policy_suppressed(i)&&!rt.menuGateApplied)custom_menu_apply_gate(i,cfg);continue;}if(!custom_policy_suppressed(i))continue;GFxValueLite target={};bool owned=false;if(!custom_prepare_runtime_target(i,cfg,&target,&owned,true))continue;if(rt.restoreFailed){if(owned)custom_gfx_clear(&target);continue;}bool natural=false;if(custom_gfx_visible(target,&natural)){rt.naturalVisible=natural;rt.haveNatural=true;}else{rt.haveNatural=false;rt.appliedSuppression=false;custom_runtime_drop_cached_value(rt);rt.lastObject=0;rt.nextValidationFrame=0u;if(owned)custom_gfx_clear(&target);continue;}if(natural){if(custom_gfx_set_visible(target,false)){rt.appliedSuppression=true;++g_customRuntimeVisibleWrites;}else{rt.appliedSuppression=false;custom_runtime_drop_cached_value(rt);rt.lastObject=0;rt.nextValidationFrame=0u;}}else rt.appliedSuppression=false;if(owned)custom_gfx_clear(&target);}}
static void custom_release_all_owned(const CustomSettingsSnapshot&settings,const char*reason){if(!g_customControlCapable)return;for(u32 i=0;i<settings.count&&i<kCustomElementCap;++i){CustomRuntimeState&rt=g_customRuntimeStates[i];if(custom_menu_gate_candidate(settings.entries[i])&&rt.menuGateApplied)custom_menu_release_gate(i,settings.entries[i],reason?reason:"RELEASE");bool restored=false;if(rt.appliedSuppression&&rt.haveNatural&&rt.lastObject){GFxValueLite target={};if(custom_runtime_cached_value(rt,&target)){restored=custom_gfx_set_visible(target,rt.naturalVisible);}else{bool owned=false;if(custom_resolve_target(settings.entries[i],&target,&owned)&&target.data.object==rt.lastObject)restored=custom_gfx_set_visible(target,rt.naturalVisible);if(owned)custom_gfx_clear(&target);}if(restored&&debug_verbose_enabled())custom_log_runtime(i,reason?reason:"RELEASE",settings.entries[i],rt.lastObject);}custom_runtime_drop_cached_value(rt);rt.appliedSuppression=false;rt.haveNatural=false;rt.lastObject=0;rt.nextValidationFrame=0u;rt.nextResolveFrame=0u;rt.menuObject=0;rt.menuGateApplied=false;rt.menuHaveNatural=false;rt.menuNaturalCanBeVisible=false;rt.menuFailureStreak=0u;rt.menuLastFailureReason=kCustomMenuFailNone;rt.menuNextAttemptFrame=0u;}}

static constexpr const char* kHUDDiscoveryContainers[]={
    "root1.BottomCenterGroup_mc","root1.CenterGroup_mc","root1.HUDNotificationsGroup_mc","root1.LeftMeters_mc","root1.RightMeters_mc","root1.TopCenterGroup_mc","root1.TopRightGroup_mc",
    // FallUI HUD Framework infrastructure: do not export the holder itself, but keep traversing it so
    // actual addon content loaded beneath the holder remains discoverable.
    "root1.M8r.Framework.HUDMenu::HUDMenuFwCore"
};
static constexpr const char* kHUDDiscoveryIgnoredElements[]={
    "root1.FloatingQuestMarkerBase",
    "root1.BottomCenterGroup_mc.CompassWidget_mc","root1.BottomCenterGroup_mc.CritMeter_mc","root1.BottomCenterGroup_mc.PerkVaultBoy_mc","root1.BottomCenterGroup_mc.SubtitleText_mc",
    "root1.CenterGroup_mc.DirectionalHitIndicatorBase_mc","root1.CenterGroup_mc.ExplosiveIndicatorBase_mc","root1.CenterGroup_mc.HUDCrosshair_mc","root1.CenterGroup_mc.HitIndicator_mc","root1.CenterGroup_mc.QuickContainerWidget_mc","root1.CenterGroup_mc.RolloverWidget_mc",
    "root1.HUDNotificationsGroup_mc.Messages_mc","root1.HUDNotificationsGroup_mc.ObjectiveUpdates_mc","root1.HUDNotificationsGroup_mc.QuestUpdates_mc","root1.HUDNotificationsGroup_mc.QuestVaultBoy_mc","root1.HUDNotificationsGroup_mc.TutorialText_mc","root1.HUDNotificationsGroup_mc.XPMeter_mc","root1.HUDNotificationsGroup_mc.PromptMessageHolder_mc",
    "root1.LeftMeters_mc.HPMeter_mc","root1.LeftMeters_mc.LocationText_mc","root1.LeftMeters_mc.RadsMeter_mc",
    "root1.RightMeters_mc.ActionPointMeter_mc","root1.RightMeters_mc.AmmoCount_mc","root1.RightMeters_mc.ExplosiveAmmoCount_mc","root1.RightMeters_mc.FatigueWarning_mc","root1.RightMeters_mc.FlashLightWidget_mc","root1.RightMeters_mc.HUDActiveEffectsWidget_mc","root1.RightMeters_mc.PowerArmorLowBatteryWarning_mc",
    "root1.TopCenterGroup_mc.EnemyHealthMeter_mc","root1.TopCenterGroup_mc.StealthMeter_mc","root1.TopRightGroup_mc.VaultBoyCondition_mc"
};
static bool custom_is_hud_discovery_container(const char*menuName,const char*path){
    if(!str_ieq(menuName,"HUDMenu"))return false;
    if(str_ieq(path,"root1.SafeRect_mc"))return true;
    for(u32 i=0;i<(u32)(sizeof(kHUDDiscoveryContainers)/sizeof(kHUDDiscoveryContainers[0]));++i)if(str_ieq(path,kHUDDiscoveryContainers[i]))return true;
    return false;
}
static bool custom_is_hud_discovery_ignored_element(const char*menuName,const char*path){
    if(str_ieq(menuName,"PowerArmorHUDMenu"))return str_ieq(path,"root1.CompassWidget_mc");
    if(!str_ieq(menuName,"HUDMenu"))return false;
    for(u32 i=0;i<(u32)(sizeof(kHUDDiscoveryIgnoredElements)/sizeof(kHUDDiscoveryIgnoredElements[0]));++i)if(str_ieq(path,kHUDDiscoveryIgnoredElements[i]))return true;
    return false;
}
static bool custom_known_vanilla_menu(const char*name){static constexpr const char* names[]={"FaderMenu","LoadingMenu","ButtonBarMenu","PauseMenu","PipboyMenu","FavoritesMenu","ContainerMenu","BarterMenu","InventoryMenu","DialogueMenu","VATSMenu","WorkshopMenu","LockpickingMenu","TerminalMenu","MainMenu","MapMenu","MessageBoxMenu","SleepWaitMenu","LevelUpMenu","LooksMenu","ExamineMenu","BookMenu","Console","ScopeMenu","PowerArmorModMenu"};for(u32 i=0;i<(u32)(sizeof(names)/sizeof(names[0]));++i)if(str_ieq(name,names[i]))return true;return false;}
static bool custom_candidate_exists(const CustomHUDCandidate*c,u32 count,const char*menu,const char*path){for(u32 i=0;i<count;++i)if(str_ieq(c[i].menu,menu)&&str_ieq(c[i].path,path))return true;return false;}
static bool custom_add_candidate(CustomHUDCandidate*c,u32*count,const char*menu,const char*path,const char*source,const char*name,bool stable){if(!c||!count||*count>=kCustomCandidateCap||!menu||!path||!name||!*name||custom_candidate_exists(c,*count,menu,path))return false;CustomHUDCandidate&x=c[(*count)++];memset(&x,0,sizeof(x));copy_text(x.menu,sizeof(x.menu),menu);copy_text(x.path,sizeof(x.path),path);copy_text(x.source,sizeof(x.source),source&&*source?source:"<unknown>");copy_text(x.name,sizeof(x.name),name);x.stablePath=stable;return true;}

static void custom_scan_hud_container(const GFxValueLite&parent,const char*parentPath,const char*menuName,const char*rootSource,CustomHUDCandidate*c,u32*count,u32 depth){if(depth>2u||!*parentPath)return;u32 n=custom_gfx_child_count(parent);for(u32 i=0;i<n&&*count<kCustomCandidateCap;++i){GFxValueLite child={};if(!custom_gfx_child(parent,i,&child)){custom_gfx_clear(&child);continue;}char name[128]={};bool named=custom_gfx_name(child,name,sizeof(name));bool stable=named&&!custom_generated_name(name);char path[512]={};if(stable)custom_path_join(path,sizeof(path),parentPath,name);if(stable&&custom_is_hud_discovery_ignored_element(menuName,path)){custom_gfx_clear(&child);continue;}if(stable&&custom_is_hud_discovery_container(menuName,path)){if(!str_ieq(path,"root1.SafeRect_mc"))custom_scan_hud_container(child,path,menuName,rootSource,c,count,depth+1u);custom_gfx_clear(&child);continue;}char source[260]={};bool haveSource=custom_gfx_source(child,source,sizeof(source));bool foreignSource=haveSource&&rootSource&&*rootSource&&!str_ieq(source,rootSource);if(stable){custom_add_candidate(c,count,menuName,path,haveSource?source:rootSource,name,true);}else if(foreignSource){char unstable[512]={};SIZE_T p=0;append_text_cap(unstable,sizeof(unstable),&p,parentPath);append_text_cap(unstable,sizeof(unstable),&p,".<generated>");custom_add_candidate(c,count,menuName,unstable,source,"GeneratedLoadedWidget",false);}custom_gfx_clear(&child);}}

static __declspec(noinline) void custom_scan_menu(void*menu,CustomHUDCandidate*c,u32*count,u32*menusScanned){if(!menu||!c||!count)return;FixedStringRef nameRef={};GFxValueLite stage={};if(!safe_read((u8*)menu+0x50u,&nameRef)||!safe_read((u8*)menu+0x20u,&stage)||!custom_gfx_is_objectish(stage))return;char menuName[96]={};if(!custom_fixed_string_copy(nameRef,menuName,sizeof(menuName)))return;++*menusScanned;char rootSource[260]={};custom_gfx_source(stage,rootSource,sizeof(rootSource));if(str_ieq(menuName,"HUDMenu")||str_ieq(menuName,"PowerArmorHUDMenu")){custom_scan_hud_container(stage,"root1",menuName,rootSource,c,count,0u);return;}if(custom_known_vanilla_menu(menuName))return;u32 n=custom_gfx_child_count(stage);u32 before=*count;for(u32 i=0;i<n&&*count<kCustomCandidateCap;++i){GFxValueLite child={};if(!custom_gfx_child(stage,i,&child)){custom_gfx_clear(&child);continue;}char name[128]={};bool named=custom_gfx_name(child,name,sizeof(name));if(named&&!custom_generated_name(name)){char path[512]={};custom_path_join(path,sizeof(path),"root1",name);char source[260]={};if(!custom_gfx_source(child,source,sizeof(source)))copy_text(source,sizeof(source),rootSource);custom_add_candidate(c,count,menuName,path,source,name,true);}custom_gfx_clear(&child);}if(*count==before&&rootSource[0])custom_add_candidate(c,count,menuName,"root1",rootSource,menuName,true);}

static bool custom_suffix_at(const char*s,SIZE_T pos,const char*needle){if(!s||!needle)return false;SIZE_T i=0;while(needle[i]){if(!s[pos+i]||ascii_lower(s[pos+i])!=ascii_lower(needle[i]))return false;++i;}return true;}
static bool build_dynamic_hud_data_path(const char*relativeUnderData,char*out,SIZE_T cap){
    if(!relativeUnderData||!out||cap<32u||!g_module)return false;
    DWORD n=GetModuleFileNameA(g_module,out,(DWORD)cap);if(!n||n>=cap)return false;
    SIZE_T cut=0;const char*suffix="\\F4SE\\Plugins\\";
    for(SIZE_T i=0;i<n;++i)if((out[i]=='\\'||out[i]=='/')&&custom_suffix_at(out,i,suffix)){cut=i+1u;break;}
    if(cut==0u){char rel[1024]={};SIZE_T rp=0;append_text_cap(rel,sizeof(rel),&rp,"Data\\");append_text_cap(rel,sizeof(rel),&rp,relativeUnderData);return build_game_file_path(rel,out,cap);}
    SIZE_T p=cut;SIZE_T i=0;while(relativeUnderData[i]&&p+1u<cap)out[p++]=relativeUnderData[i++];if(relativeUnderData[i])return false;out[p]=0;return true;
}
static bool custom_ensure_directory(const char*path,const char*label){
    if(!path||!*path)return false;
    DWORD attrs=GetFileAttributesA(path);
    if(attrs!=INVALID_FILE_ATTRIBUTES){
        if((attrs&FILE_ATTRIBUTE_DIRECTORY)!=0u)return true;
        char b[1400];SIZE_T p=0;append_text(b,&p,"[CustomHUD] output path exists but is not a directory label=");append_text(b,&p,label?label:"?");append_text(b,&p," path=");append_text(b,&p,path);line_end(b,&p);log_raw(b);return false;
    }
    if(CreateDirectoryA(path,0))return true;
    DWORD err=GetLastError();
    attrs=GetFileAttributesA(path);
    if(err==ERROR_ALREADY_EXISTS&&attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)!=0u)return true;
    char b[1500];SIZE_T p=0;append_text(b,&p,"[CustomHUD] could not prepare output directory label=");append_text(b,&p,label?label:"?");append_text(b,&p," winError=");append_u64(b,&p,(u64)err);append_text(b,&p," path=");append_text(b,&p,path);line_end(b,&p);log_raw(b);return false;
}
static bool custom_make_directories(){
    static constexpr const char* dirs[]={"MCM","MCM\\Config","MCM\\Config\\DynamicHUD","MCM\\Config\\DynamicHUD\\CustomElements"};
    char path[1024];
    for(u32 i=0;i<(u32)(sizeof(dirs)/sizeof(dirs[0]));++i)if(!build_dynamic_hud_data_path(dirs[i],path,sizeof(path))||!custom_ensure_directory(path,dirs[i]))return false;
    return true;
}
static void custom_sanitize_filename(const char*src,char*out,SIZE_T cap){if(!out||cap<2u)return;SIZE_T p=0;for(SIZE_T i=0;src&&src[i]&&p+1u<cap;++i){char c=src[i];if(c=='/'||c=='\\')p=0;else if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')out[p++]=c;else if(c=='.')break;else out[p++]='_';}out[p]=0;if(!out[0])copy_text(out,cap,"CustomHUD");}
static bool custom_write_all(HANDLE f,const char*data,SIZE_T bytes){while(bytes){DWORD take=bytes>0x7FFFFFFFULL?0x7FFFFFFFu:(DWORD)bytes,w=0;if(!WriteFile(f,data,take,&w,0)||w!=take)return false;data+=take;bytes-=take;}return true;}
static __declspec(noinline) bool custom_write_candidate_ini(const CustomHUDCandidate&c,char*createdName,SIZE_T createdNameCap){
    if(createdName&&createdNameCap)createdName[0]=0;
    char sourceBase[160]={};custom_sanitize_filename(c.source,sourceBase,sizeof(sourceBase));
    char nameBase[128]={};custom_sanitize_filename(c.name,nameBase,sizeof(nameBase));
    char relative[1024]={};SIZE_T rp=0;append_text_cap(relative,sizeof(relative),&rp,"MCM\\Config\\DynamicHUD\\CustomElements\\");SIZE_T nameOffset=rp;append_text_cap(relative,sizeof(relative),&rp,sourceBase);if(!str_ieq(sourceBase,nameBase)){append_char_cap(relative,sizeof(relative),&rp,'_');append_text_cap(relative,sizeof(relative),&rp,nameBase);}append_text_cap(relative,sizeof(relative),&rp,".ini");
    char full[1024]={};if(!build_dynamic_hud_data_path(relative,full,sizeof(full)))return false;
    HANDLE f=CreateFileA(full,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);if(f==INVALID_HANDLE_VALUE){DWORD err=GetLastError();char b[1500];SIZE_T p=0;if(err==ERROR_FILE_EXISTS||err==ERROR_ALREADY_EXISTS)append_text(b,&p,"[CustomHUD] INI already exists; preserved path=Data\\");else{append_text(b,&p,"[CustomHUD] INI create failed winError=");append_u64(b,&p,(u64)err);append_text(b,&p," path=Data\\");}append_text(b,&p,relative);append_text(b,&p," fullPath=");append_text(b,&p,full);line_end(b,&p);log_raw(b);return false;}
    char (&b)[8192]=g_customDiscoveryWriteBuffer;SIZE_T p=0;memset(b,0,sizeof(b));
    append_text(b,&p,"; Dynamic HUD - discovered modded HUD element\r\n; Generated by the one-shot Custom HUD discovery MCM hotkey.\r\n; This file starts disabled. Set bControl=1 to enable runtime control; changes are live-reloaded.\r\n; Discovery itself runs only when the MCM discovery hotkey is pressed.\r\n;\r\n; Source SWF: ");append_text(b,&p,c.source);append_text(b,&p,"\r\n; Menu:       ");append_text(b,&p,c.menu);append_text(b,&p,"\r\n; Path:       ");append_text(b,&p,c.path);append_text(b,&p,"\r\n");if(!c.stablePath)append_text(b,&p,"; WARNING: the discovered path contains a generated instance and is not yet considered stable across game sessions.\r\n");append_text(b,&p,";\r\n[CustomHUD]\r\n\r\n; Friendly label used by Dynamic HUD logs.\r\nsName=");append_text(b,&p,c.name);append_text(b,&p,"\r\n\r\n; Technical target. Normally do not edit these values.\r\n; IMPORTANT: Discovery reports active custom UI, not only gameplay HUD elements.\r\n; Enable only actual gameplay HUD displays. Interactive/configuration/selection menus may own cursor/input/camera state and should normally remain disabled.\r\n; For a standalone custom menu, sMenu is the suppression boundary; enable one representative INI per actual HUD-bearing menu.\r\n; sPath/sSourceSWF identify the discovered object and are used by the HUDMenu/native-host child fallback.\r\nsMenu=");append_text(b,&p,c.menu);append_text(b,&p,"\r\nsPath=");append_text(b,&p,c.path);append_text(b,&p,"\r\nsSourceSWF=");append_text(b,&p,c.source);append_text(b,&p,"\r\n\r\n; Master control for this custom entry. 0 = disabled, 1 = Dynamic HUD controls it.\r\n; Do not enable multiple INIs for the same standalone custom menu.\r\n; If a discovered menu behaves like a configuration/editor/cursor UI rather than a gameplay HUD, leave it disabled.\r\nbControl=0\r\n\r\n; 1 = use Show While triggers below. 0 = use the permanent Mode setting.\r\nbAutomatic=1\r\n\r\n; Automatic mode: 0 = HIDE, 1 = HIDE / SHOW.\r\n; SHOW means release Dynamic HUD suppression and let the source mod decide natural visibility.\r\niMode=0\r\n\r\n; Automatic OFF: 0 = ALWAYS HIDE, 1 = ALWAYS SHOW.\r\n; ALWAYS SHOW releases Dynamic HUD suppression; it does not force the source mod to display invalid content.\r\niAlways=0\r\n\r\n; Optional native toggle hotkey. Leave empty for none.\r\n; Format: MCM keycode, or keycode,modifierMask where 1=Shift, 2=Ctrl, 4=Alt.\r\nsHotkey=\r\n\r\n; Seconds to keep the element visible after the last Show While condition ends. Range 0-60.\r\niHideDelay=0\r\n\r\n; Show While conditions. 0 = off, 1 = on.\r\nbWhenWeaponDrawn=0\r\nbWhenWeaponHolstered=0\r\nbWhenAiming=0\r\nbWhenSprinting=0\r\nbWhenSneaking=0\r\nbWhenCombat=0\r\nbWhenVATS=0\r\nbWhenFirstPerson=0\r\nbWhenThirdPerson=0\r\nbWhenPowerArmor=0\r\nbWhenRadiation=0\r\nbWhenLowHealth=0\r\nbWhenLowAP=0\r\nbWhenActivate=0\r\nbWhenCommand=0\r\n");
    bool ok=custom_write_all(f,b,p);if(ok)FlushFileBuffers(f);CloseHandle(f);if(ok){if(createdName&&createdNameCap)copy_text(createdName,createdNameCap,relative+nameOffset);char lb[1400];SIZE_T lp=0;append_text(lb,&lp,"[CustomHUD] generated Data\\");append_text(lb,&lp,relative);append_text(lb,&lp," menu=");append_text(lb,&lp,c.menu);append_text(lb,&lp," path=");append_text(lb,&lp,c.path);append_text(lb,&lp," source=");append_text(lb,&lp,c.source);append_text(lb,&lp," fullPath=");append_text(lb,&lp,full);line_end(lb,&lp);log_raw(lb);}return ok;
}

using MessageMenuCreateFn=void(__fastcall*)(void*,const char*,const char*,void*,u32,const char*,const char*,const char*,const char*,bool);
static __declspec(noinline) bool custom_discovery_show_results(const char*body){
    if(!g_addresses.messageMenuManagerSingleton||!g_addresses.messageMenuManagerCreate){error_log_line("[CustomHUD] discovery result message capability unavailable; INI creation completed normally.");return false;}
    void*manager=0;if(!safe_read((void*)g_addresses.messageMenuManagerSingleton,&manager)||!manager||!is_readable_process_range(manager,0x50u)){error_log_line("[CustomHUD] discovery result message unavailable: MessageMenuManager singleton invalid; INI creation completed normally.");return false;}
    MessageMenuCreateFn create=(MessageMenuCreateFn)g_addresses.messageMenuManagerCreate;
    create(manager,"Dynamic HUD",body&&*body?body:"Created INI files:\n\nNone",0,0x1Au,"OK",0,0,0,false);
    return true;
}

static __declspec(noinline) void custom_discovery_run_once(){
    if(_InterlockedExchange(&g_customDiscoveryRequested,0)==0)return;
    if(!g_customDiscoveryCapable||!g_addresses.uiSingleton){log_line("[CustomHUD] discovery unavailable: required UI/Scaleform capability is missing.");return;}
    s64 start=0,end=0;QueryPerformanceCounter(&start);
    void*ui=0;if(!safe_read((void*)g_addresses.uiSingleton,&ui)||!ui){log_line("[CustomHUD] discovery failed: UI singleton unavailable.");return;}
    CustomMenuStackLite stack={};if(!safe_read((u8*)ui+0x190u,&stack)||!stack.entries||stack.count>64u||stack.capacity<stack.count||stack.capacity>256u||!is_readable_process_range(stack.entries,(SIZE_T)stack.count*sizeof(void*))){log_line("[CustomHUD] discovery failed: active menu stack contract unavailable.");return;}
    memset(g_customDiscoveryScratch,0,sizeof(g_customDiscoveryScratch));u32 count=0,menus=0;for(u32 i=0;i<stack.count&&count<kCustomCandidateCap;++i){void*menu=0;if(!safe_read(stack.entries+i,&menu)||!menu)continue;custom_scan_menu(menu,g_customDiscoveryScratch,&count,&menus);}
    char (&message)[32768]=g_customDiscoveryMessageBuffer;memset(message,0,sizeof(message));SIZE_T mp=0;append_text(message,&mp,"Created INI files:\n\n");
    u32 written=0;if(count&&custom_make_directories()){for(u32 i=0;i<count;++i){char created[320]={};if(custom_write_candidate_ini(g_customDiscoveryScratch[i],created,sizeof(created))){++written;append_text(message,&mp,created);append_char_cap(message,sizeof(message),&mp,'\n');}}}else if(count){log_line("[CustomHUD] discovery found candidates but could not prepare Data\\MCM\\Config\\DynamicHUD\\CustomElements.");}
    if(!written)append_text(message,&mp,"None");
    QueryPerformanceCounter(&end);u64 us=(g_qpcFreq>0&&end>=start)?(u64)(((end-start)*1000000LL)/g_qpcFreq):0u;char b[900];SIZE_T p=0;append_text(b,&p,"[CustomHUD] one-shot discovery complete menus=");append_u64(b,&p,menus);append_text(b,&p," candidates=");append_u64(b,&p,count);append_text(b,&p," filesWritten=");append_u64(b,&p,written);append_text(b,&p," elapsedUs=");append_u64(b,&p,us);append_text(b,&p,". Discovery is now idle.");line_end(b,&p);log_raw(b);
    if(custom_discovery_show_results(message)&&debug_verbose_enabled()){char mb[240];SIZE_T m=0;append_text(mb,&m,"[CustomHUD] discovery result message queued filesWritten=");append_u64(mb,&m,written);line_end(mb,&m);log_raw(mb);}
}

// Power Armor lifecycle authority is the native gameplay predicate PowerArmor::ActorInPowerArmor.
// Gameplay lifecycle is never inferred from UI/menu presentation state because presentation state can
// remain stale across save loads and is not a deterministic Power Armor state source.
static void* g_paValidatedPlayer=0;
static void* g_paRejectedPlayer=0;
static bool g_paStateCapabilityPass=false;
static u64 g_paStateSamples=0u;
static u64 g_paStateFailures=0u;
// Shared native PA sample for both lifecycle ownership and the user-facing Power Armor trigger.
// HookAdvance samples the native predicate once before policy evaluation; trigger sampling consumes this cache.
static volatile long g_paTriggerStateKnown=0;
static volatile long g_paTriggerStateActive=0;

static __declspec(noinline) bool prepare_vats_menu_name_cold(){
    if(g_vatsMenuNameReady&&g_vatsMenuName.data)return true;
    if(!g_addresses.fixedStringCtor)return false;
    g_vatsMenuName.data=0;
    FixedStringRef*result=((FixedStringCtorFn)g_addresses.fixedStringCtor)(&g_vatsMenuName,"VATSMenu");
    g_vatsMenuNameReady=result==&g_vatsMenuName&&g_vatsMenuName.data!=0;
    if(!g_vatsMenuNameReady)error_log_line("[VATS] BSFixedString(VATSMenu) construction failed; VATS trigger fail-open.");
    return g_vatsMenuNameReady;
}
static __forceinline bool sample_vats_runtime_fast(RuntimeStateSnapshot*st){
    if(!st||!g_addresses.fixedStringCtor)return false;
    void*ui=0;if(!menu_state_get_ui_fast(&ui))return false;
    if(!g_vatsMenuNameReady&&!prepare_vats_menu_name_cold())return false;
    st->inVATS=native_menu_open(ui,g_vatsMenuName)?1u:0u;
    st->vatsValid=1u;st->sourceMask|=kSourceVATS;return true;
}

// Production Radiation Exposure telemetry. HUDRadiationMeter embeds a
// BSTValueEventSink<CurrentRadsDisplayMagnitude> at +0x328. Before vanilla consumes each pending
// update, +0x338 is dirty, +0x334 is the event-value validity bit, and +0x330 holds the float value.
// Static RE of HUDRadiationMeter::Update proves vanilla clears +0x334/+0x338 after consumption.
// Therefore the stable policy state is maintained here as a latch:
//   dirty && valid && magnitude > 0  -> exposure ON/update
//   dirty && !valid                 -> exposure OFF/clear
// The latch is reset to unknown on HUD/lifecycle rebinding and selected policies fail-open until a
// native update establishes state. No timer and no accumulated-Rads polling are involved.
static volatile long g_radiationStateKnown=0;
static volatile long g_radiationActive=0;
static u64 g_radiationTransitions=0;
static bool g_radiationCapabilityChecked=false;
static bool g_radiationCapability=false;
static void** g_radiationSinkVtable=0;

static bool code_contains(const u8*code,u32 count,const u8*pattern,u32 patternCount){
    if(!code||!pattern||!patternCount||patternCount>count)return false;
    for(u32 i=0;i+patternCount<=count;++i){bool same=true;for(u32 j=0;j<patternCount;++j)if(code[i+j]!=pattern[j]){same=false;break;}if(same)return true;}return false;
}
static __declspec(noinline) bool validate_radiation_cache_contract_cold(){
    g_radiationCapabilityChecked=true;g_radiationCapability=false;
    if(!g_componentInventoryReady||!g_arrayResolved)return false;
    void*object=g_hudComponents[kRadiationComponentSlot].object;if(!object)return false;
    if(!g_radiationSinkVtable)g_radiationSinkVtable=find_primary_vtable_by_rtti_slots(".?AV?$BSTValueEventSink@UCurrentRadsDisplayMagnitude@@@@",2u);
    if(!g_radiationSinkVtable){error_log_line("[Radiation] capability UNAVAILABLE: CurrentRadsDisplayMagnitude sink RTTI/vtable not found.");return false;}
    void**embedded=0;
    if(!safe_read((u8*)object+kRadiationSinkOffset,&embedded)||embedded!=g_radiationSinkVtable){
        error_log_line("[Radiation] capability UNAVAILABLE: HUDRadiationMeter +0x328 is not the expected CurrentRadsDisplayMagnitude sink.");
        return false;
    }
    void*process=0;
    if(!safe_read(g_radiationSinkVtable+1,&process)||!process||!is_executable_image((u64)process)){
        error_log_line("[Radiation] capability UNAVAILABLE: sink ProcessEvent slot invalid.");
        return false;
    }
    u8 code[256]={};if(!safe_read_bytes(process,code,sizeof(code))){error_log_line("[Radiation] capability UNAVAILABLE: sink ProcessEvent code unreadable.");return false;}
    const u8 valueCopy[]={0x8B,0x06,0x89,0x01};
    const u8 validSet[]={0xC6,0x41,0x04,0x01};
    const u8 validClear[]={0x44,0x88,0x79,0x04};
    const u8 dirtySet[]={0xC6,0x47,0x10,0x01};
    if(!code_contains(code,sizeof(code),valueCopy,sizeof(valueCopy))||
       !code_contains(code,sizeof(code),validSet,sizeof(validSet))||
       !code_contains(code,sizeof(code),validClear,sizeof(validClear))||
       !code_contains(code,sizeof(code),dirtySet,sizeof(dirtySet))){
        error_log_line("[Radiation] capability UNAVAILABLE: sink ProcessEvent field contract not proven.");
        return false;
    }
    g_radiationCapability=true;
    if(debug_verbose_enabled()){char b[640];
        SIZE_T p=0;
        append_text(b,&p,"[Radiation] capability=PASS object=");
        append_hex64(b,&p,(u64)object);
        append_text(b,&p," sinkVtable=");
        append_hex64(b,&p,(u64)g_radiationSinkVtable);
        append_text(b,&p," processEvent=");
        append_hex64(b,&p,(u64)process);
        append_text(b,&p," cacheOffsets={value:0x330,valid:0x334,dirty:0x338}");
        line_end(b,&p);
        log_raw(b);
        }
    return true;
}

static __forceinline void observe_radiation_event_fast(){
    if(!g_componentInventoryReady||!g_arrayResolved)return;
    // Do not pay the cold contract-discovery cost until some policy first requests Radiation. Once
    // proven for this HUD generation, keep watching the cheap native dirty byte even if the final
    // Radiation policy is temporarily disabled. That preserves an accurate ACTIVE/CLEAR latch
    // across MCM changes instead of allowing a missed clear event to leave stale state behind.
    if(!g_radiationCapabilityChecked){
        if(!(g_requiredTriggerMask&kReasonRadiation))return;
        if(!validate_radiation_cache_contract_cold())return;
    }
    if(!g_radiationCapability)return;
    void*object=g_hudComponents[kRadiationComponentSlot].object; // id 19: HUDRadiationMeter
    if(!object)return;
    u8 dirty=*(volatile u8*)((u8*)object+kRadiationDirtyOffset);
    if(dirty!=1u)return;
    u8 valid=*(volatile u8*)((u8*)object+kRadiationValidOffset);
    bool next=false;float magnitude=0.0f;
    if(valid==1u){magnitude=*(volatile float*)((u8*)object+kRadiationValueOffset);next=finite_float(magnitude)&&magnitude>0.0f;}
    long oldKnown=_InterlockedExchange(&g_radiationStateKnown,1);
    long oldActive=_InterlockedExchange(&g_radiationActive,next?1:0);
    if(!oldKnown||oldActive!=(next?1:0)){
        ++g_radiationTransitions;
        if(debug_verbose_enabled()){char b[640];
            SIZE_T p=0;
            append_text(b,&p,"[Radiation] t_us=");
            append_u64(b,&p,now_us());
            append_text(b,&p," event=");
            append_text(b,&p,next?"ACTIVE":"CLEAR");
            append_text(b,&p," valid=");
            append_u64(b,&p,valid);
            append_text(b,&p," magnitudeMilli=");
            append_s64(b,&p,float_milli(valid?magnitude:0.0f));
            line_end(b,&p);
            log_raw(b);
            }
    }
}
static __forceinline void sample_radiation_runtime_fast(RuntimeStateSnapshot*st){
    if(!st||_InterlockedCompareExchange(&g_radiationStateKnown,0,0)==0)return;
    st->radiationActive=_InterlockedCompareExchange(&g_radiationActive,0,0)!=0?1u:0u;
    st->radiationValid=1u;st->sourceMask|=kSourceRadiation;
}

static u64 now_us(){s64 q=0;if(!g_qpcFreq||!QueryPerformanceCounter(&q))return 0;return (u64)(((q-g_qpcStart)*1000000LL)/g_qpcFreq);}
static s64 perf_qpc(){s64 q=0;if(!g_qpcFreq||!QueryPerformanceCounter(&q))return 0;return q;}
static void perf_add(PerfBucket&b,s64 start,s64 end){if(!start||end<start)return;u64 dt=(u64)(end-start);++b.calls;b.totalTicks+=dt;if(dt>b.maxTicks)b.maxTicks=dt;}
static void perf_add_ticks(PerfBucket&b,u64 dt){if(!dt)return;++b.calls;b.totalTicks+=dt;if(dt>b.maxTicks)b.maxTicks=dt;}
static u64 perf_avg_ns(const PerfBucket&b){if(!b.calls||!g_qpcFreq)return 0;return (b.totalTicks*1000000000ULL)/(u64)g_qpcFreq/b.calls;}
static u64 perf_max_us(const PerfBucket&b){if(!b.maxTicks||!g_qpcFreq)return 0;return (b.maxTicks*1000000ULL)/(u64)g_qpcFreq;}
static void perf_zero_bucket(PerfBucket&b){b.calls=0;b.totalTicks=0;b.maxTicks=0;}
static void reset_perf_window(u64 frame){
    g_perfWindowStartFrame=frame;g_perfSampledFrames=0;g_perfDetailedFrames=0;
    perf_zero_bucket(g_perfPluginPre);perf_zero_bucket(g_perfPluginSteady);perf_zero_bucket(g_perfPluginMaintenance);
    perf_zero_bucket(g_perfPluginDetailed);perf_zero_bucket(g_perfSettingsCommit);perf_zero_bucket(g_perfBindingValidation);
    perf_zero_bucket(g_perfDiscovery);perf_zero_bucket(g_perfStatePolicy);perf_zero_bucket(g_perfStateSample);
    perf_zero_bucket(g_perfStateCore);perf_zero_bucket(g_perfActorValues);perf_zero_bucket(g_perfCrosshair);
    perf_zero_bucket(g_perfPolicyEval);perf_zero_bucket(g_perfDelayedGates);perf_zero_bucket(g_perfOriginalAdvance);
    perf_zero_bucket(g_perfPAFull);perf_zero_bucket(g_perfPAPre);perf_zero_bucket(g_perfPAPost);perf_zero_bucket(g_perfPAAwait);perf_zero_bucket(g_perfPABind);
    perf_zero_bucket(g_perfCustomBefore);perf_zero_bucket(g_perfCustomAfter);
    perf_zero_bucket(g_perfButtonHintsTotal);perf_zero_bucket(g_perfButtonHintsClassify);perf_zero_bucket(g_perfButtonHintsGFx);
    g_perfButtonHintsHookCalls=0;g_perfButtonHintsClassifyCalls=0;g_perfButtonHintsTransitions=0;
    g_perfSafeReadCalls=0;g_perfSafeReadBytes=0;g_perfSafeReadFailures=0;g_perfVirtualQueryCalls=0;g_perfReadProcessCalls=0;
    g_perfGateCalls=0;g_perfGateManaged=0;g_perfGateVeto=0;
    g_perfAPRequestCalls=0;g_perfAPRequestAllows=0;g_perfAPRequestDenies=0;
    g_perfHealthRequestCalls=0;g_perfHealthRequestAllows=0;g_perfHealthRequestDenies=0;g_perfSettingsCommits=0;
}
template<SIZE_T N> static void append_perf_bucket(char (&b)[N],SIZE_T* p,const char* name,const PerfBucket&v){
    append_text(b,p," ");append_text(b,p,name);append_text(b,p,"AvgNs=");append_u64(b,p,perf_avg_ns(v));
    append_text(b,p," ");append_text(b,p,name);append_text(b,p,"MaxUs=");append_u64(b,p,perf_max_us(v));
    append_text(b,p," ");append_text(b,p,name);append_text(b,p,"N=");append_u64(b,p,v.calls);
}
static void log_perf_summary(u64 frame){
    if(!g_perfTelemetryEnabled||!g_qpcFreq)return;
    char b[2600];SIZE_T p=0;
    append_text(b,&p,"[Perf] window advances=");append_u64(b,&p,g_perfWindowStartFrame?frame-g_perfWindowStartFrame:0);
    append_text(b,&p," outerSamples=");append_u64(b,&p,g_perfSampledFrames);
    append_text(b,&p," outerStride=");append_u64(b,&p,kPerfSampleStride);
    append_text(b,&p," detailedSamples=");append_u64(b,&p,g_perfDetailedFrames);
    append_text(b,&p," detailedStride=");append_u64(b,&p,kPerfDetailedStride);
    append_text(b,&p," safeReads=");append_u64(b,&p,g_perfSafeReadCalls);
    append_text(b,&p," safeReadBytes=");append_u64(b,&p,g_perfSafeReadBytes);
    append_text(b,&p," safeReadFailures=");append_u64(b,&p,g_perfSafeReadFailures);
    append_text(b,&p," virtualQueryCalls=");append_u64(b,&p,g_perfVirtualQueryCalls);
    append_text(b,&p," readProcessCalls=");append_u64(b,&p,g_perfReadProcessCalls);
    append_text(b,&p," gateCalls=");append_u64(b,&p,g_perfGateCalls);
    append_text(b,&p," managedGateCalls=");append_u64(b,&p,g_perfGateManaged);
    append_text(b,&p," vetoes=");append_u64(b,&p,g_perfGateVeto);
    append_text(b,&p," apRequestCalls=");append_u64(b,&p,g_perfAPRequestCalls);
    append_text(b,&p," apRequestTrue=");append_u64(b,&p,g_perfAPRequestAllows);
    append_text(b,&p," apRequestFalse=");append_u64(b,&p,g_perfAPRequestDenies);
    append_text(b,&p," healthRequestCalls=");append_u64(b,&p,g_perfHealthRequestCalls);
    append_text(b,&p," healthRequestTrue=");append_u64(b,&p,g_perfHealthRequestAllows);
    append_text(b,&p," healthRequestFalse=");append_u64(b,&p,g_perfHealthRequestDenies);
    append_text(b,&p," settingsCommits=");append_u64(b,&p,g_perfSettingsCommits);
    append_text(b,&p," logQueued=");append_u64(b,&p,(u64)atomic_load_long_long(&g_logQueued));
    append_text(b,&p," logDropped=");append_u64(b,&p,(u64)atomic_load_long_long(&g_logDropped));
    append_text(b,&p," logWritten=");append_u64(b,&p,(u64)atomic_load_long_long(&g_logWritten));
    line_end(b,&p);log_raw(b);

    p=0;append_text(b,&p,"[Perf] outer");
    append_perf_bucket(b,&p,"pluginPre",g_perfPluginPre);append_perf_bucket(b,&p,"pluginSteady",g_perfPluginSteady);
    append_perf_bucket(b,&p,"pluginMaintenance",g_perfPluginMaintenance);append_perf_bucket(b,&p,"originalAdvance",g_perfOriginalAdvance);
    append_perf_bucket(b,&p,"paFull",g_perfPAFull);append_perf_bucket(b,&p,"paPre",g_perfPAPre);append_perf_bucket(b,&p,"paPost",g_perfPAPost);
    append_perf_bucket(b,&p,"customPre",g_perfCustomBefore);append_perf_bucket(b,&p,"customPost",g_perfCustomAfter);
    line_end(b,&p);log_raw(b);

    p=0;append_text(b,&p,"[Perf] detailed");
    append_perf_bucket(b,&p,"pluginDetailed",g_perfPluginDetailed);append_perf_bucket(b,&p,"statePolicy",g_perfStatePolicy);
    append_perf_bucket(b,&p,"stateSample",g_perfStateSample);append_perf_bucket(b,&p,"stateCore",g_perfStateCore);
    append_perf_bucket(b,&p,"actorValues",g_perfActorValues);append_perf_bucket(b,&p,"crosshair",g_perfCrosshair);
    append_perf_bucket(b,&p,"policy",g_perfPolicyEval);append_perf_bucket(b,&p,"delayed",g_perfDelayedGates);
    append_perf_bucket(b,&p,"binding",g_perfBindingValidation);append_perf_bucket(b,&p,"discovery",g_perfDiscovery);
    append_perf_bucket(b,&p,"settings",g_perfSettingsCommit);append_perf_bucket(b,&p,"paAwait",g_perfPAAwait);append_perf_bucket(b,&p,"paBind",g_perfPABind);
    line_end(b,&p);log_raw(b);

    p=0;append_text(b,&p,"[Perf] buttonHints hookCalls=");append_u64(b,&p,g_perfButtonHintsHookCalls);
    append_text(b,&p," sampleStride=");append_u64(b,&p,kPerfSampleStride);
    append_text(b,&p," classifyCalls=");append_u64(b,&p,g_perfButtonHintsClassifyCalls);
    append_text(b,&p," transitions=");append_u64(b,&p,g_perfButtonHintsTransitions);
    append_perf_bucket(b,&p,"pluginTotal",g_perfButtonHintsTotal);
    append_perf_bucket(b,&p,"classify",g_perfButtonHintsClassify);
    append_perf_bucket(b,&p,"gfxTransition",g_perfButtonHintsGFx);
    line_end(b,&p);log_raw(b);
    reset_perf_window(frame);
}
static int managed_index_for_rtti(const char*name){for(int i=0;i<(int)kManagedCount;++i)if(str_eq(name,g_elements[i].rtti))return i;return -1;}
static bool object_should_contract(void*object,u32 managedIndex){if(!object||managedIndex>=kManagedCount)return false;
    u64 expected=g_elements[managedIndex].expectedShould;
    if(!expected)return false;
    void**vt=0;
    if(!safe_read(object,&vt)||!vt)return false;
    if(vt!=g_componentVtables[managedIndex])return false;
    void*fn=0;
    if(!safe_read(vt+7,&fn))return false;
    if(fn==(void*)expected)return true;
    return g_gateHooks[managedIndex]&&fn==g_gateHooks[managedIndex]&&g_origShould[managedIndex]==(ShouldBeVisibleFn)expected;
    }

struct ArrayCandidate {u32 offset;u32 count;u32 capacity;bool local;u32 managedMask;u32 rttiObjects;void**data;void*objects[kManagedCount];u32 indices[kManagedCount];};
static void pa_runtime_reset();
static bool inspect_array_candidate(void*hud,u32 offset,ArrayCandidate*out){if(!hud||!out)return false;
    SmallHUDArrayView a={};
    if(!safe_read((u8*)hud+offset,&a))return false;
    u32 cap=a.capFlags&0x7FFFFFFFu;
    bool local=(a.capFlags&0x80000000u)!=0;
    u32 count=a.size;
    if(count<4u||count>128u||cap<count||cap>256u)return false;
        if(local&&cap>32u)return false;
        void**data=local?(void**)((u8*)hud+offset+8u):(void**)a.storage.heap;
        if(!data)return false;
    ArrayCandidate c={};c.offset=offset;c.count=count;c.capacity=cap;c.local=local;c.data=data;u32 hits[kManagedCount]={0,0,0,0};
    for(u32 i=0;i<count;++i){void*obj=0;
        if(!safe_read(data+i,&obj)||!obj)continue;
        char type[192];
        if(!copy_rtti_name(obj,type,sizeof(type)))continue;
        ++c.rttiObjects;
        int m=managed_index_for_rtti(type);
        if(m<0)continue;
        if(!object_should_contract(obj,(u32)m))continue;
        ++hits[m];
        if(hits[m]==1u){c.managedMask|=(1u<<(u32)m);
        c.objects[m]=obj;
        c.indices[m]=i;
        }}
    if(c.managedMask!=0x0Fu)return false;for(u32 i=0;i<kManagedCount;++i)if(hits[i]!=1u)return false;*out=c;return true;}

static void reset_bound_state(){custom_release_all_owned(g_customSettings,"lifecycle reset");g_arrayResolved=false;
    g_validatedMenuStateUI=0;
    g_radiationStateKnown=0;
    g_radiationActive=0;
    g_radiationCapabilityChecked=false;
    g_radiationCapability=false;
    g_arrayOffset=0;
    g_arrayCount=0;
    g_arrayCapacity=0;
    g_arrayLocal=false;
    g_arrayData=0;
    memset(g_componentArrayIndex,0,sizeof(g_componentArrayIndex));
    g_crosshairValueSink=0;
    g_crosshairValueSinkOffset=0;
    g_crosshairSinkDiscoveryAttempted=false;
    g_healthAVInfo=0;
    g_actionPointsAVInfo=0;
    g_avDiscoveryAttemptedMask=0u;
    g_validatedPlayer=0;
    g_rejectedPlayer=0;
    g_perspectiveValidatedPlayer=0;
    g_perspectiveRejectedPlayer=0;
    g_isInThirdPersonFn=0;
    g_playerContractFailureStage="none";
    g_combatFn=0;
    g_avCurrentFn=0;
    g_avPermanentFn=0;
    g_avModifierFn=0;
    g_haveRuntimeState=0;
    g_policyFailOpen=0;
    g_playerLogged=0;
    g_componentInventoryReady=false;
    memset(&g_lastRuntimeState,0,sizeof(g_lastRuntimeState));
    pa_runtime_reset();
    for(u32 i=0;i<kNormalHUDObjectCount;++i){g_hudComponents[i].object=0;g_hudComponents[i].arrayIndex=0;}
    for(u32 i=0;i<kHUDObjectCount;++i){ObjectControlState&c=g_controlStates[i];c.suppress=0;c.reasonMask=0;c.policyInitialized=0;c.gateClosed=0;c.hidePending=0;c.hideDeadlineUs=0;}
    for(u32 i=0;i<kCustomElementCap;++i){ObjectControlState&c=g_customControlStates[i];c.suppress=0;c.reasonMask=0;c.policyInitialized=0;c.gateClosed=0;c.hidePending=0;c.hideDeadlineUs=0;}memset(g_customRuntimeStates,0,sizeof(g_customRuntimeStates));
    for(u32 i=0;i<kManagedCount;++i)g_elements[i].object=0;}

static void discover_crosshair_value_sink(){if(g_crosshairSinkDiscoveryAttempted)return;
    g_crosshairSinkDiscoveryAttempted=true;
    void*cross=g_elements[kCrosshair].object;
    if(!cross)return;
    if(!g_crosshairValueSinkVtable)g_crosshairValueSinkVtable=find_primary_vtable_by_rtti_slots(".?AV?$BSTValueEventSink@VPlayerCrosshairModeEvent@@@@",2u);
    if(!g_crosshairValueSinkVtable){error_log_line("[Crosshair] value-sink RTTI vtable not found; mode telemetry will fail open.");return;}
    u32 hits=0,hitOff=0;for(u32 off=0;off<=0x900u;off+=8u){void*p=0;if(!safe_read((u8*)cross+off,&p))continue;if(p==g_crosshairValueSinkVtable){++hits;hitOff=off;}}
    if(hits!=1u){if(!debug_errors_enabled())return;
        char b[480];
        SIZE_T p=0;
        append_text(b,&p,"[Crosshair] value-sink structural discovery ");
        append_text(b,&p,hits?"AMBIGUOUS":"NOT_FOUND");
        append_text(b,&p," hits=");
        append_u64(b,&p,hits);
        line_end(b,&p);
        log_raw(b);
        return;
        }
    g_crosshairValueSink=(u8*)cross+hitOff;
        g_crosshairValueSinkOffset=hitOff;
        if(!debug_verbose_enabled())return;
        char b[500];
        SIZE_T p=0;
        append_text(b,&p,"[Crosshair] value-sink PASS object=");
        append_hex64(b,&p,(u64)g_crosshairValueSink);
        append_text(b,&p," offset=");
        append_hex64(b,&p,hitOff);
        line_end(b,&p);
        log_raw(b);
        }

static void discover_component_inventory(const ArrayCandidate&winner){
    for(u32 i=0;i<kNormalHUDObjectCount;++i){g_hudComponents[i].object=0;g_hudComponents[i].arrayIndex=0;}
    u32 found=0u,duplicates=0u;
    for(u32 i=0;i<winner.count;++i){void*obj=0;
        if(!safe_read(winner.data+i,&obj)||!obj)continue;
        char type[192];
        if(!copy_rtti_name(obj,type,sizeof(type)))continue;
        HUDComponent*d=component_by_rtti(type);
        if(!d)continue;
        if(d->object){++duplicates;
        continue;
        }void**vt=0;
        if(!safe_read(obj,&vt)||vt!=d->vtable)continue;
        d->object=obj;
        d->arrayIndex=i;
        ++found;
        }
    g_componentInventoryReady=(found==kNormalHUDObjectCount&&duplicates==0u);
    if(debug_verbose_enabled()){char b[760];
        SIZE_T p=0;
        append_text(b,&p,"[Components] inventory found=");
        append_u64(b,&p,found);
        append_text(b,&p," duplicates=");
        append_u64(b,&p,duplicates);
        append_text(b,&p," expected=");
        append_u64(b,&p,kNormalHUDObjectCount);
        append_text(b,&p," status=");
        append_text(b,&p,g_componentInventoryReady?"PASS":"INCOMPLETE");
        line_end(b,&p);
        log_raw(b);
        if(g_componentInventoryReady){for(u32 i=0;i<kNormalHUDObjectCount;++i){HUDComponent&d=g_hudComponents[i];
        p=0;
        append_text(b,&p,"[Components] component id=");
        append_u64(b,&p,d.id);
        append_text(b,&p," index=");
        append_u64(b,&p,d.arrayIndex);
        append_text(b,&p," name=");
        append_text(b,&p,d.name);
        append_text(b,&p," object=");
        append_hex64(b,&p,(u64)d.object);
        append_text(b,&p," vtable=");
        append_hex64(b,&p,(u64)d.vtable);
        append_text(b,&p," predicate=");
        append_text(b,&p,d.derived?"derived":"base");
        append_text(b,&p," hook=");
        append_text(b,&p,d.managedIndex>=0?"special-controller":(d.hookInstalled?"generic-controller":"none"));
        line_end(b,&p);
        log_raw(b);
        }}}
}

static __declspec(noinline) bool bind_array_candidate(const ArrayCandidate&winner){g_arrayResolved=true;
    g_arrayOffset=winner.offset;
    g_arrayCount=winner.count;
    g_arrayCapacity=winner.capacity;
    g_arrayLocal=winner.local;
    g_arrayData=winner.data;
    for(u32 i=0;i<kManagedCount;++i){g_elements[i].object=winner.objects[i];g_componentArrayIndex[i]=winner.indices[i];}
    discover_component_inventory(winner);
    if(debug_verbose_enabled()){char b[1100];
        SIZE_T p=0;
        append_text(b,&p,"[HUDArray] PASS unique offset=");
        append_hex64(b,&p,winner.offset);
        append_text(b,&p," count=");
        append_u64(b,&p,winner.count);
        append_text(b,&p," capacity=");
        append_u64(b,&p,winner.capacity);
        append_text(b,&p," storage=");
        append_text(b,&p,winner.local?"local":"heap");
        for(u32 i=0;i<kManagedCount;++i){append_text(b,&p," ");
        append_text(b,&p,g_elements[i].name);
        append_text(b,&p,"=");
        append_hex64(b,&p,(u64)winner.objects[i]);
        }line_end(b,&p);
        log_raw(b);
        }discover_crosshair_value_sink();
        if(lifecycle_suspend_exchange(0)==1)verbose_log_line("[Lifecycle] validated HUD binding recovered; lifecycle fail-open released.");
        return true;
        }

static __declspec(noinline) bool discover_hud_array(void*hud){if(!hud||!g_componentContractsPass)return false;ArrayCandidate winner={};u32 winners=0;
    for(u32 off=0x20u;off<=0x500u;off+=8u){ArrayCandidate c={};
        if(!inspect_array_candidate(hud,off,&c))continue;
        ++winners;
        winner=c;
        if(!debug_verbose_enabled())continue;
        char b[700];
        SIZE_T p=0;
        append_text(b,&p,"[HUDArray] validated candidate offset=");
        append_hex64(b,&p,c.offset);
        append_text(b,&p," count=");
        append_u64(b,&p,c.count);
        append_text(b,&p," capacity=");
        append_u64(b,&p,c.capacity);
        append_text(b,&p," storage=");
        append_text(b,&p,c.local?"local":"heap");
        append_text(b,&p," rttiObjects=");
        append_u64(b,&p,c.rttiObjects);
        line_end(b,&p);
        log_raw(b);
        }
    if(winners!=1u){if(!debug_errors_enabled())return false;
        char b[384];
        SIZE_T p=0;
        append_text(b,&p,"[HUDArray] discovery ");
        append_text(b,&p,winners?"AMBIGUOUS":"PENDING/NOT_FOUND");
        append_text(b,&p," validatedCandidates=");
        append_u64(b,&p,winners);
        line_end(b,&p);
        log_raw(b);
        return false;
        }return bind_array_candidate(winner);
        }

static bool validate_binding_sentinel(void*hud){
    if(!g_arrayResolved||!hud||hud!=g_lastHud||!g_arrayData)return false;
    // hud is the live HUDMenu currently executing AdvanceMovie, so this header read is a trusted in-object read.
    // Discovery-grade safe_read/RTTI validation is retained for initial bind and any sentinel failure.
    SmallHUDArrayView a={};memcpy(&a,(u8*)hud+g_arrayOffset,sizeof(a));u32 cap=a.capFlags&0x7FFFFFFFu;bool local=(a.capFlags&0x80000000u)!=0;
    if(a.size!=g_arrayCount||cap!=g_arrayCapacity||local!=g_arrayLocal)return false;
        void**data=local?(void**)((u8*)hud+g_arrayOffset+8u):(void**)a.storage.heap;
        if(!data||data!=g_arrayData)return false;
    for(u32 i=0;i<kManagedCount;++i){u32 idx=g_componentArrayIndex[i];
        if(idx>=a.size)return false;
        void*obj=0;
        if(local)memcpy(&obj,data+idx,sizeof(obj));
        else if(!safe_read(data+idx,&obj))return false;
        if(obj!=g_elements[i].object)return false;
        }
    if(g_componentInventoryReady){for(u32 i=0;i<kNormalHUDObjectCount;++i){HUDComponent&d=g_hudComponents[i];
        if(d.arrayIndex>=a.size||!d.object)return false;
        void*obj=0;
        if(local)memcpy(&obj,data+d.arrayIndex,sizeof(obj));
        else if(!safe_read(data+d.arrayIndex,&obj))return false;
        if(obj!=d.object)return false;
        }}
    if(g_crosshairValueSink&&g_crosshairValueSink!=(u8*)g_elements[kCrosshair].object+g_crosshairValueSinkOffset)return false;
    return true;
}

static HUDComponent* generic_component_from_self(void*self){if(!self)return 0;
    void**vt=*(void***)self;
    if(!vt)return 0;
    for(u32 i=0;i<kNormalHUDObjectCount;++i){HUDComponent&d=g_hudComponents[i];
    if(d.hookInstalled&&d.vtable==vt)return &d;
    }return 0;
    }
static bool call_with_native_request(u32 slot,void*self,ShouldBeVisibleFn original){if(!original)return true;
    if(!g_genericNativeRequestCapability||!g_genericPersistentRequestOffset)return original(self);
    volatile u8*request=(volatile u8*)((u8*)self+g_genericPersistentRequestOffset);
    u8 saved=*request;
    *request=1u;
    bool visible=original(self);
    *request=saved;
    if(g_perfTelemetryEnabled){if(slot==kSlotHealth){++g_perfHealthRequestCalls;
    if(visible)++g_perfHealthRequestAllows;
    else ++g_perfHealthRequestDenies;
    }else if(slot==kSlotActionPoints){++g_perfAPRequestCalls;
    if(visible)++g_perfAPRequestAllows;
    else ++g_perfAPRequestDenies;
    }}return visible;
    }
static bool controlled_should_be_visible(u32 slot,void*expectedObject,ShouldBeVisibleFn original,void*self){
    bool perf=g_perfTelemetryEnabled;
    if(perf)++g_perfGateCalls;
    if(!original)return true;
    if(lifecycle_suspend_load_acquire()!=0)return original(self);
    if(slot>=kNormalHUDObjectCount||self!=expectedObject)return original(self);

    const ElementSettings& cfg=g_settings.elements[slot];
    ObjectControlState& state=g_controlStates[slot];
    if(!g_settings.enabled||cfg.controlMode==kControlOff||!state.policyInitialized||(((u32)state.reasonMask&kReasonTelemetryFailOpen)!=0u))return original(self);
    if(perf)++g_perfGateManaged;
    if(state.gateClosed){if(perf)++g_perfGateVeto;return false;}

    // In ALWAYS HIDE, an open Dynamic HUD gate always means the hotkey inversion is still being
    // presented: either the hotkey is active or its return-to-hidden Hide Delay is pending.
    // Keep Bethesda's positive request alive for that entire open-gate interval. Otherwise the
    // native predicate can hide the component before Dynamic HUD's delayed gate actually closes.
    const bool alwaysHideRequestHeld=!cfg.automatic&&cfg.permanentVisibility==kAlwaysHide;
    if(cfg.controlMode==kControlHideShow||alwaysHideRequestHeld)return call_with_native_request(slot,self,original);
    return original(self);
}
static bool HookGenericShouldBeVisible(void*self){HUDComponent*d=generic_component_from_self(self);
    if(!d||!d->original)return true;
    return controlled_should_be_visible(d->id-1u,d->object,d->original,self);
    }
static bool HookCompassShouldBeVisible(void*self){return controlled_should_be_visible(kSlotCompass,g_elements[kCompass].object,g_origShould[kCompass],self);}
static bool HookHealthShouldBeVisible(void*self){return controlled_should_be_visible(kSlotHealth,g_elements[kHealth].object,g_origShould[kHealth],self);}
static bool HookAPShouldBeVisible(void*self){return controlled_should_be_visible(kSlotActionPoints,g_elements[kActionPoints].object,g_origShould[kActionPoints],self);}
static bool HookCrosshairShouldBeVisible(void*self){return controlled_should_be_visible(kSlotCrosshair,g_elements[kCrosshair].object,g_origShould[kCrosshair],self);}
static bool HookPACompassShouldBeVisible(void*self){
    bool perf=g_perfTelemetryEnabled;if(perf)++g_perfGateCalls;ShouldBeVisibleFn original=g_origPACompassShould;if(!original)return true;
    if(lifecycle_suspend_load_acquire()!=0||!g_paLifecycleActive)return original(self);
    const ElementSettings&cfg=g_settings.elements[kSlotPACompass];ObjectControlState&state=g_controlStates[kSlotPACompass];
    if(!g_settings.enabled||cfg.controlMode==kControlOff||!state.policyInitialized||(((u32)state.reasonMask&kReasonTelemetryFailOpen)!=0u))return original(self);
    if(perf)++g_perfGateManaged;if(state.gateClosed){if(perf)++g_perfGateVeto;return false;}
    const bool alwaysHideRequestHeld=!cfg.automatic&&cfg.permanentVisibility==kAlwaysHide;
    if(cfg.controlMode==kControlHideShow||alwaysHideRequestHeld)return call_with_native_request(kSlotPACompass,self,original);
    return original(self);
}

static __declspec(noinline) void log_gate_event(u32 slot,const char*what){if(!debug_verbose_enabled()||slot>=kHUDObjectCount)return;
    ObjectControlState&c=g_controlStates[slot];
    char b[560];
    SIZE_T p=0;
    append_text(b,&p,"[Gate] id=");
    append_u64(b,&p,slot+1u);
    append_text(b,&p," name=");
    append_text(b,&p,hud_object_name(slot));
    append_text(b,&p," event=");
    append_text(b,&p,what);
    append_text(b,&p," t_us=");
    append_u64(b,&p,now_us());
    append_text(b,&p," suppress=");
    append_u64(b,&p,c.suppress?1u:0u);
    append_text(b,&p," closed=");
    append_u64(b,&p,c.gateClosed?1u:0u);
    append_text(b,&p," pending=");
    append_u64(b,&p,c.hidePending?1u:0u);
    line_end(b,&p);
    log_raw(b);
    }
static const char*weapon_state_name(u32 v){switch(v){case 0:return"Sheathed";
    case 1:return"WantToDraw";
    case 2:return"Drawing";
    case 3:return"Drawn";
    case 4:return"WantToSheathe";
    case 5:return"Sheathing";
    default:return"Unknown";
    }}
static const char*gun_state_name(u32 v){switch(v){case 0:return"Drawn";
    case 1:return"Relaxed";
    case 2:return"Blocked";
    case 3:return"Alert";
    case 4:return"Reloading";
    case 5:return"Throwing";
    case 6:return"Sighted";
    case 7:return"Fire";
    case 8:return"FireSighted";
    default:return"Unknown";
    }}
static const char*crosshair_mode_name(u32 v){switch(v){case 7:return"Command";case 8:return"Activate";default:return v<=8u?"Other":"Unknown";}}
static bool finite_float(float v){u32 bits=0;memcpy(&bits,&v,4);return(bits&0x7F800000u)!=0x7F800000u;}
static s64 float_milli(float v){return(s64)(v*1000.0f);}
static u32 ratio_percent_validated(float current,float maximum){float r=current/maximum;if(r<0.0f)r=0.0f;if(r>1.0f)r=1.0f;return(u32)(r*100.0f+0.5f);}
static bool validate_actor_value_info(void*info,u32 expectedFormID){if(!info)return false;
    u32 formID=0;
    if(!safe_read((u8*)info+0x14,&formID)||formID!=expectedFormID)return false;
    char type[96];
    return copy_rtti_name(info,type,sizeof(type))&&str_eq(type,".?AVActorValueInfo@@");
    }
static bool av_scan_root_seen(void**roots,u32 count,void*p){for(u32 i=0;i<count;++i)if(roots[i]==p)return true;return false;}
static bool av_add_scan_root(void**roots,u8*depths,u32*count,void*p,u8 depth){if(!roots||!depths||!count||!p||*count>=96u||is_in_image((u64)p)||av_scan_root_seen(roots,*count,p))return false;
    u64 probe=0;
    if(!safe_read(p,&probe))return false;
    roots[*count]=p;
    depths[*count]=depth;
    ++(*count);
    return true;
    }
static __declspec(noinline) void log_av_discovery_hit(const char*name,void*info,void*root,u64 offset){if(!debug_verbose_enabled())return;
    char b[640];
    SIZE_T p=0;
    append_text(b,&p,"[AV] discovered ");
    append_text(b,&p,name);
    append_text(b,&p," info=");
    append_hex64(b,&p,(u64)info);
    append_text(b,&p," root=");
    append_hex64(b,&p,(u64)root);
    append_text(b,&p," slotOffset=");
    append_hex64(b,&p,offset);
    line_end(b,&p);
    log_raw(b);
    }
static __declspec(noinline) void*discover_avif_from_meter(const char*name,void*meter,u32 expectedFormID){if(!meter)return 0;
    void*roots[96]={};
    u8 depths[96]={};
    u32 rootCount=0;
    roots[rootCount]=meter;
    depths[rootCount]=0;
    ++rootCount;
    for(u32 idx=0;idx<rootCount;++idx){void*root=roots[idx];
        u8 depth=depths[idx];
        u64 span=(depth==0u)?0x1000ULL:((depth==1u)?0x600ULL:0x200ULL);
        for(u64 off=0;off<span;off+=8ULL){void*candidate=0;
        if(!safe_read((u8*)root+off,&candidate)||!candidate)continue;
        if(validate_actor_value_info(candidate,expectedFormID)){log_av_discovery_hit(name,candidate,root,off);
        return candidate;
        }if(depth<2u)av_add_scan_root(roots,depths,&rootCount,candidate,(u8)(depth+1u));
        }}
    if(!debug_errors_enabled())return 0;
        char b[480];
        SIZE_T p=0;
        append_text(b,&p,"[AV] ");
        append_text(b,&p,name);
        append_text(b,&p," not found rootsScanned=");
        append_u64(b,&p,rootCount);
        line_end(b,&p);
        log_raw(b);
        return 0;
        }
static __declspec(noinline) void discover_actor_values(u32 wantedMask){if(!wantedMask)return;
    ++g_avDiscoveryAttempts;
    if(wantedMask&kActorValueHealth){if(g_healthAVInfo&&!validate_actor_value_info(g_healthAVInfo,0x2D4u))g_healthAVInfo=0;
    if(!g_healthAVInfo)g_healthAVInfo=discover_avif_from_meter("Health",g_elements[kHealth].object,0x2D4u);
    }if(wantedMask&kActorValueActionPoints){if(g_actionPointsAVInfo&&!validate_actor_value_info(g_actionPointsAVInfo,0x2D5u))g_actionPointsAVInfo=0;
    if(!g_actionPointsAVInfo)g_actionPointsAVInfo=discover_avif_from_meter("ActionPoints",g_elements[kActionPoints].object,0x2D5u);
    }}
static bool validate_player_identity(void*player){if(!player)return false;
    u32 formID=0;
    if(!safe_read((u8*)player+0x14,&formID)||formID!=0x14u)return false;
    char type[192];
    return copy_rtti_name(player,type,sizeof(type))&&str_eq(type,".?AVPlayerCharacter@@");
    }
static __declspec(noinline) bool establish_perspective_hotpath_cold(void*player){
    if(!player||player==g_perspectiveRejectedPlayer)return false;
    if(!validate_player_identity(player)){g_perspectiveValidatedPlayer=0;g_perspectiveRejectedPlayer=player;g_isInThirdPersonFn=0;
        error_log_line("[Perspective] PlayerCharacter identity validation FAILED; perspective-selected policies will fail-open.");return false;}
    void*owner=(u8*)player+kPlayerPerspectiveSubobjectOffset;void**vt=0;
    if(!safe_read(owner,&vt)||!vt||!validate_vtable_slots(vt,kIsInThirdPersonVslot+1u)){g_perspectiveValidatedPlayer=0;g_perspectiveRejectedPlayer=player;g_isInThirdPersonFn=0;
        error_log_line("[Perspective] IMovementPlayerControlsFilter vtable validation FAILED; perspective-selected policies will fail-open.");return false;}
    u64 colVA=0;CompleteObjectLocator64 col={};char type[192];
    if(!safe_read(((u64*)vt)-1,&colVA)||!is_in_image(colVA)||!safe_read((void*)colVA,&col)||col.signature!=1u||
       col.offset!=kPlayerPerspectiveSubobjectOffset||col.selfRVA!=(u32)((u8*)colVA-g_image.base)||
       !copy_rtti_name(owner,type,sizeof(type))||!str_eq(type,".?AVPlayerCharacter@@")){
        g_perspectiveValidatedPlayer=0;g_perspectiveRejectedPlayer=player;g_isInThirdPersonFn=0;
        error_log_line("[Perspective] PlayerCharacter +0x4C0 secondary-RTTI contract FAILED; perspective-selected policies will fail-open.");return false;}
    void*fn=0;if(!safe_read(vt+kIsInThirdPersonVslot,&fn)||!fn||!is_executable_image((u64)fn)){
        g_perspectiveValidatedPlayer=0;g_perspectiveRejectedPlayer=player;g_isInThirdPersonFn=0;
        error_log_line("[Perspective] IsInThirdPerson vslot 06 validation FAILED; perspective-selected policies will fail-open.");return false;}
    g_isInThirdPersonFn=(ActorBoolFn)fn;g_perspectiveValidatedPlayer=player;g_perspectiveRejectedPlayer=0;
    if(debug_verbose_enabled()){char b[520];SIZE_T p=0;append_text(b,&p,"[Perspective] native contract=PASS subobjectOffset=");append_hex64(b,&p,kPlayerPerspectiveSubobjectOffset);
        append_text(b,&p," vslot=");append_u64(b,&p,kIsInThirdPersonVslot);append_text(b,&p," function=");append_hex64(b,&p,(u64)fn);line_end(b,&p);log_raw(b);}
    return true;
}
static __forceinline bool sample_perspective_runtime_fast(RuntimeStateSnapshot*st){
    if(!st||!st->player)return false;void*player=st->player;
    if(player!=g_perspectiveValidatedPlayer&&!establish_perspective_hotpath_cold(player))return false;
    if(!g_isInThirdPersonFn)return false;
    st->thirdPerson=g_isInThirdPersonFn((u8*)player+kPlayerPerspectiveSubobjectOffset)?1u:0u;
    st->perspectiveValid=1u;st->sourceMask|=kSourcePerspective;return true;
}
static __declspec(noinline) bool validate_pa_player_identity_cold(void*player){
    if(!player||player==g_paRejectedPlayer)return false;
    if(!validate_player_identity(player)){g_paValidatedPlayer=0;
        g_paRejectedPlayer=player;
        error_log_line("[PAGroups] native PA-state player identity validation FAILED; PA adapter fail-open/dormant.");
        return false;
        }
    g_paValidatedPlayer=player;g_paRejectedPlayer=0;verbose_log_line("[PAGroups] native PA-state PlayerCharacter identity PASS.");return true;
}
static __forceinline bool sample_power_armor_native_fast(bool*outActive){
    if(outActive)*outActive=false;++g_paStateSamples;
    if(!outActive||!g_paStateCapabilityPass||!g_addresses.playerSingleton||!g_addresses.actorInPowerArmor){++g_paStateFailures;return false;}
    void*player=*(void* volatile*)(u64)g_addresses.playerSingleton;if(!player){++g_paStateFailures;return false;}
    if(player!=g_paValidatedPlayer&&!validate_pa_player_identity_cold(player)){++g_paStateFailures;return false;}
    *outActive=((ActorBoolFn)g_addresses.actorInPowerArmor)(player);return true;
}
static __declspec(noinline) bool cache_player_hotpath_contracts(void*player){
    g_playerContractFailureStage="playerIdentity";if(!validate_player_identity(player))return false;
    g_playerContractFailureStage="sprintStaticContract";if(!g_sprintContractPass||!g_actorStateVtable||!g_addresses.actorSprintState||!g_addresses.actorIsSneaking)return false;
    // The live movement-state subobject may use a derived vtable. Validate the semantic identity
    // of vslot 0x1F instead of requiring the subobject vtable to equal the base RTTI vtable.
    void**stateVt=0;g_playerContractFailureStage="movementStateVtable";if(!safe_read((u8*)player+kPlayerMovementStateOffset,&stateVt)||!stateVt)return false;
    void*sprint=0;g_playerContractFailureStage="movementStateSprintSlot";if(!safe_read(stateVt+0x1Fu,&sprint)||sprint!=(void*)g_addresses.actorSprintState)return false;
    void**actorVt=0;
        g_playerContractFailureStage="actorVtable";
        if(!safe_read(player,&actorVt)||!actorVt)return false;
        void*combat=0;
        g_playerContractFailureStage="combatSlot";
        if(!safe_read(actorVt+kActorCombatVslot,&combat)||!combat||!is_executable_image((u64)combat))return false;
    void*owner=(u8*)player+kActorValueOwnerOffset;void**avVt=0;g_playerContractFailureStage="actorValueOwnerVtable";if(!safe_read(owner,&avVt)||!avVt)return false;void*cur=0;void*perm=0;void*mod=0;
    g_playerContractFailureStage="actorValueCurrent";if(!safe_read(avVt+1u,&cur)||!cur||!is_executable_image((u64)cur))return false;
    g_playerContractFailureStage="actorValuePermanent";if(!safe_read(avVt+2u,&perm)||!perm||!is_executable_image((u64)perm))return false;
    g_playerContractFailureStage="actorValueModifier";if(!safe_read(avVt+7u,&mod)||!mod||!is_executable_image((u64)mod))return false;
    g_combatFn=(ActorBoolFn)combat;
        g_avCurrentFn=(ActorValueGetFn)cur;
        g_avPermanentFn=(ActorValueGetFn)perm;
        g_avModifierFn=(ActorValueModifierFn)mod;
        g_playerContractFailureStage="none";
        return true;
}
static __forceinline bool sample_one_actor_value_fast(void*owner,void*info,float*current,float*maximum,u32*pct){
    float c=g_avCurrentFn(owner,info);
        float p=g_avPermanentFn(owner,info);
        float t=g_avModifierFn(owner,1u,info);
        if(!finite_float(c)||!finite_float(p)||!finite_float(t))return false;
        float m=p+t;
        if(!finite_float(m)||m<=0.0f)return false;
    *current=c;*maximum=m;*pct=ratio_percent_validated(c,m);return true;
}
static __forceinline void sample_actor_values_fast(RuntimeStateSnapshot*st,u32 wantedMask){void*owner=(u8*)st->player+kActorValueOwnerOffset;
    if((wantedMask&kActorValueHealth)&&g_healthAVInfo){if(sample_one_actor_value_fast(owner,g_healthAVInfo,&st->healthCurrent,&st->healthMax,&st->healthPct))st->actorValueMask|=kActorValueHealth;
        else ++g_avReadFailures;
        }
    if((wantedMask&kActorValueActionPoints)&&g_actionPointsAVInfo){if(sample_one_actor_value_fast(owner,g_actionPointsAVInfo,&st->actionPointsCurrent,&st->actionPointsMax,&st->actionPointsPct))st->actorValueMask|=kActorValueActionPoints;
        else ++g_avReadFailures;
        }}
static __forceinline void sample_crosshair_mode_fast(RuntimeStateSnapshot*st){st->crosshairMode=kInvalidCrosshairMode;
    st->crosshairModeValid=0;
    if(!g_crosshairValueSink)return;
    u8 valid=*(volatile u8*)((u8*)g_crosshairValueSink+kCrosshairSinkValidOffset);
    if(valid!=1u)return;
    u32 mode=*(volatile u32*)((u8*)g_crosshairValueSink+kCrosshairSinkModeOffset);
    if(mode>kCrosshairModeActivate)return;
    st->crosshairMode=mode;
    st->crosshairModeValid=1;
    st->sourceMask|=kSourceCrosshair;
    }
static __declspec(noinline) bool establish_player_hotpath_cold(void*player){
    if(!player||player==g_rejectedPlayer)return false;
        if(!cache_player_hotpath_contracts(player)){g_validatedPlayer=0;
        g_rejectedPlayer=player;
        g_combatFn=0;
        g_avCurrentFn=0;
        g_avPermanentFn=0;
        g_avModifierFn=0;
        if(debug_errors_enabled()){char b[320];
        SIZE_T p=0;
        append_text(b,&p,"[Player] runtime identity/hot-path contract validation FAILED stage=");
        append_text(b,&p,g_playerContractFailureStage);
        append_text(b,&p,"; state sampling fail-open until lifecycle/player change.");
        line_end(b,&p);
        log_raw(b);
        }return false;
        }g_validatedPlayer=player;
        g_rejectedPlayer=0;
        verbose_log_line("[Player] runtime identity + hot-path ABI validation PASS.");
        return true;
}
static __declspec(noinline) void discover_actor_values_cold(u32 wantedMask){u32 pending=wantedMask&~g_avDiscoveryAttemptedMask;
    if(!pending)return;
    g_avDiscoveryAttemptedMask|=pending;
    discover_actor_values(pending);
    }
static __forceinline bool sample_runtime_state_all_fast(RuntimeStateSnapshot*out,s64 totalStart){
    void*player=*(void* volatile*)(u64)g_addresses.playerSingleton;
        if(!player){++g_stateFailures;
        return false;
        }if(player!=g_validatedPlayer&&!establish_player_hotpath_cold(player)){++g_stateFailures;
        return false;
        }
    u32 avMissing=(!g_healthAVInfo?1u:0u)|(!g_actionPointsAVInfo?2u:0u);if(avMissing&&(avMissing&~g_avDiscoveryAttemptedMask))discover_actor_values_cold(avMissing);
    out->player=player;
        s64 coreStart=g_perfSampleThisFrame?perf_qpc():0;
        out->actorStateWord=*(volatile u32*)((u8*)player+kPlayerActorStateOffset);
        out->weaponState=(out->actorStateWord>>kActorStateWeaponShift)&kActorStateWeaponMask;
        out->gunState=(out->actorStateWord>>kActorStateGunShift)&kActorStateGunMask;
        out->weaponDrawn=(out->weaponState>=kWeaponStateDrawn)?1u:0u;
        out->weaponHolstered=(out->weaponState==0u)?1u:0u;
        out->aiming=(out->gunState==kGunStateSighted||out->gunState==kGunStateFireSighted)?1u:0u;
    out->sprinting=((ActorBoolFn)g_addresses.actorSprintState)((u8*)player+kPlayerMovementStateOffset)?1u:0u;
        out->sourceMask|=kSourceSprint;
        out->sneaking=((ActorBoolFn)g_addresses.actorIsSneaking)(player)?1u:0u;
        out->sourceMask|=kSourceSneak;
        out->inCombat=g_combatFn(player)?1u:0u;
        out->sourceMask|=kSourceCombat;
        sample_vats_runtime_fast(out);
        sample_perspective_runtime_fast(out);
        sample_radiation_runtime_fast(out);
        if(_InterlockedCompareExchange(&g_paTriggerStateKnown,0,0)!=0){out->powerArmorValid=1u;
        out->powerArmorActive=_InterlockedCompareExchange(&g_paTriggerStateActive,0,0)!=0?1u:0u;
        out->sourceMask|=kSourcePowerArmor;
        }
    if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfStateCore,coreStart,e);
        }s64 avStart=g_perfSampleThisFrame?perf_qpc():0;
        sample_actor_values_fast(out,3u);
        if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfActorValues,avStart,e);
        }s64 chStart=g_perfSampleThisFrame?perf_qpc():0;
        sample_crosshair_mode_fast(out);
        if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfCrosshair,chStart,e);
        }
    ++g_stateSamples;if(g_perfSampleThisFrame){s64 e=perf_qpc();perf_add(g_perfStateSample,totalStart,e);}return true;
}
static __declspec(noinline) bool sample_runtime_state_pruned(RuntimeStateSnapshot*out,u32 required,s64 totalStart){
    void*player=*(void* volatile*)(u64)g_addresses.playerSingleton;
        if(!player){++g_stateFailures;
        return false;
        }if(player!=g_validatedPlayer&&!establish_player_hotpath_cold(player)){++g_stateFailures;
        return false;
        }
    u32 avWanted=((required&kReasonLowHealth)?kActorValueHealth:0u)|((required&kReasonLowAP)?kActorValueActionPoints:0u);
        u32 avMissing=0u;
        if((avWanted&1u)&&!g_healthAVInfo)avMissing|=1u;
        if((avWanted&2u)&&!g_actionPointsAVInfo)avMissing|=2u;
        if(avMissing&&(avMissing&~g_avDiscoveryAttemptedMask))discover_actor_values_cold(avMissing);
    out->player=player;
        s64 coreStart=g_perfSampleThisFrame?perf_qpc():0;
        if(required&(kReasonWeapon|kReasonWeaponHolstered|kReasonAim)){out->actorStateWord=*(volatile u32*)((u8*)player+kPlayerActorStateOffset);
        out->weaponState=(out->actorStateWord>>kActorStateWeaponShift)&kActorStateWeaponMask;
        out->gunState=(out->actorStateWord>>kActorStateGunShift)&kActorStateGunMask;
        if(required&kReasonWeapon)out->weaponDrawn=(out->weaponState>=kWeaponStateDrawn)?1u:0u;
        if(required&kReasonWeaponHolstered)out->weaponHolstered=(out->weaponState==0u)?1u:0u;
        if(required&kReasonAim)out->aiming=(out->gunState==kGunStateSighted||out->gunState==kGunStateFireSighted)?1u:0u;
        }
    if(required&kReasonSprint){out->sprinting=((ActorBoolFn)g_addresses.actorSprintState)((u8*)player+kPlayerMovementStateOffset)?1u:0u;
        out->sourceMask|=kSourceSprint;
        }if(required&kReasonSneak){out->sneaking=((ActorBoolFn)g_addresses.actorIsSneaking)(player)?1u:0u;
        out->sourceMask|=kSourceSneak;
        }if(required&kReasonCombat){out->inCombat=g_combatFn(player)?1u:0u;
        out->sourceMask|=kSourceCombat;
        }if(required&kReasonVATS)sample_vats_runtime_fast(out);
        if(required&(kReasonFirstPerson|kReasonThirdPerson))sample_perspective_runtime_fast(out);
        if(required&kReasonRadiation)sample_radiation_runtime_fast(out);
        if((required&kReasonPowerArmor)&&_InterlockedCompareExchange(&g_paTriggerStateKnown,0,0)!=0){out->powerArmorValid=1u;
        out->powerArmorActive=_InterlockedCompareExchange(&g_paTriggerStateActive,0,0)!=0?1u:0u;
        out->sourceMask|=kSourcePowerArmor;
        }
    if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfStateCore,coreStart,e);
        }s64 avStart=g_perfSampleThisFrame?perf_qpc():0;
        if(avWanted)sample_actor_values_fast(out,avWanted);
        if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfActorValues,avStart,e);
        }s64 chStart=g_perfSampleThisFrame?perf_qpc():0;
        if(required&(kReasonActivate|kReasonCommand))sample_crosshair_mode_fast(out);
        if(g_perfSampleThisFrame){s64 e=perf_qpc();
        perf_add(g_perfCrosshair,chStart,e);
        }
    ++g_stateSamples;if(g_perfSampleThisFrame){s64 e=perf_qpc();perf_add(g_perfStateSample,totalStart,e);}return true;
}
static __forceinline bool sample_runtime_state(RuntimeStateSnapshot*out){if(!out||!g_addresses.playerSingleton)return false;
    s64 totalStart=g_perfSampleThisFrame?perf_qpc():0;
    u32 required=g_requiredTriggerMask;
    if(required==kAllConfigurableTriggers)return sample_runtime_state_all_fast(out,totalStart);
    if(!required){++g_stateSamples;
    if(g_perfSampleThisFrame){s64 e=perf_qpc();
    perf_add(g_perfStateSample,totalStart,e);
    }return true;
    }return sample_runtime_state_pruned(out,required,totalStart);
}
static bool runtime_state_equal(const RuntimeStateSnapshot&a,const RuntimeStateSnapshot&b){
    bool aLowHP=(a.actorValueMask&kActorValueHealth)&&a.healthPct<g_settings.lowHealthThresholdPct;
    bool bLowHP=(b.actorValueMask&kActorValueHealth)&&b.healthPct<g_settings.lowHealthThresholdPct;
    bool aLowAP=(a.actorValueMask&kActorValueActionPoints)&&a.actionPointsPct<g_settings.lowAPThresholdPct;
    bool bLowAP=(b.actorValueMask&kActorValueActionPoints)&&b.actionPointsPct<g_settings.lowAPThresholdPct;
    return a.player==b.player&&
           a.weaponDrawn==b.weaponDrawn&&
           a.weaponHolstered==b.weaponHolstered&&
           a.aiming==b.aiming&&
           a.sprinting==b.sprinting&&
           a.sneaking==b.sneaking&&
           a.inCombat==b.inCombat&&
           a.vatsValid==b.vatsValid&&
           a.inVATS==b.inVATS&&
           a.perspectiveValid==b.perspectiveValid&&
           a.thirdPerson==b.thirdPerson&&
           a.radiationValid==b.radiationValid&&
           a.radiationActive==b.radiationActive&&
           a.powerArmorValid==b.powerArmorValid&&
           a.powerArmorActive==b.powerArmorActive&&
           a.actorValueMask==b.actorValueMask&&
           aLowHP==bLowHP&&
           aLowAP==bLowAP&&
           a.crosshairModeValid==b.crosshairModeValid&&
           a.crosshairMode==b.crosshairMode;
}
static __declspec(noinline) void log_runtime_state(const RuntimeStateSnapshot& st,const char* reason){
    if(!debug_verbose_enabled())return;
    char b[1700];SIZE_T p=0;
    append_text(b,&p,"[State] t_us=");append_u64(b,&p,now_us());
    append_text(b,&p," reason=");append_text(b,&p,reason);
    append_text(b,&p," player=");append_hex64(b,&p,(u64)st.player);
    append_text(b,&p," raw=");append_hex64(b,&p,st.actorStateWord);
    append_text(b,&p," weaponState=");append_u64(b,&p,st.weaponState);append_text(b,&p,"(");append_text(b,&p,weapon_state_name(st.weaponState));append_text(b,&p,")");
    append_text(b,&p," gunState=");append_u64(b,&p,st.gunState);append_text(b,&p,"(");append_text(b,&p,gun_state_name(st.gunState));append_text(b,&p,")");
    append_text(b,&p," weaponDrawn=");append_u64(b,&p,st.weaponDrawn);
    append_text(b,&p," weaponHolstered=");append_u64(b,&p,st.weaponHolstered);
    append_text(b,&p," aiming=");append_u64(b,&p,st.aiming);
    append_text(b,&p," sprint=");append_u64(b,&p,st.sprinting);
    append_text(b,&p," sneak=");append_u64(b,&p,st.sneaking);
    append_text(b,&p," combat=");append_u64(b,&p,st.inCombat);
    append_text(b,&p," vatsValid=");
        append_u64(b,&p,st.vatsValid);
        append_text(b,&p," vats=");
        append_u64(b,&p,st.inVATS);
        append_text(b,&p," perspectiveValid=");
        append_u64(b,&p,st.perspectiveValid);
        append_text(b,&p," thirdPerson=");
        append_u64(b,&p,st.thirdPerson);
        append_text(b,&p," radiationValid=");
        append_u64(b,&p,st.radiationValid);
        append_text(b,&p," radiation=");
        append_u64(b,&p,st.radiationActive);
        append_text(b,&p," paValid=");
        append_u64(b,&p,st.powerArmorValid);
        append_text(b,&p," pa=");
        append_u64(b,&p,st.powerArmorActive);
    append_text(b,&p," crosshairValid=");append_u64(b,&p,st.crosshairModeValid);
    append_text(b,&p," crosshairMode=");append_u64(b,&p,st.crosshairMode);append_text(b,&p,"(");append_text(b,&p,crosshair_mode_name(st.crosshairMode));append_text(b,&p,")");
    append_text(b,&p," avMask=");append_hex64(b,&p,st.actorValueMask);
    if(st.actorValueMask&kActorValueHealth){
        append_text(b,&p," hp=");append_s64(b,&p,float_milli(st.healthCurrent));append_text(b,&p,"/");append_s64(b,&p,float_milli(st.healthMax));
        append_text(b,&p," hpPct=");append_u64(b,&p,st.healthPct);
    }
    if(st.actorValueMask&kActorValueActionPoints){
        append_text(b,&p," ap=");append_s64(b,&p,float_milli(st.actionPointsCurrent));append_text(b,&p,"/");append_s64(b,&p,float_milli(st.actionPointsMax));
        append_text(b,&p," apPct=");append_u64(b,&p,st.actionPointsPct);
    }
    append_text(b,&p," sourceMask=");append_hex64(b,&p,st.sourceMask);
    line_end(b,&p);log_raw(b);
}
template<SIZE_T N> static void append_reason_names(char (&b)[N],SIZE_T* p,u32 m){
    append_text(b,p,"[");bool any=false;
    for(u32 i=0;i<kConfigurableTriggerCount;++i){
        const ConfigurableTriggerMetadata& trigger=kConfigurableTriggers[i];
        if(!(m&trigger.bit))continue;if(any)append_text(b,p,"|");append_text(b,p,trigger.diagnosticName);any=true;
    }
    struct InternalReasonName{u32 bit;const char*name;};
    const InternalReasonName internal[]={{kReasonHotkey,"HOTKEY"},{kReasonAlwaysHide,"ALWAYS_HIDE"},{kReasonAlwaysShow,"ALWAYS_SHOW"},{kReasonTelemetryFailOpen,"FAIL_OPEN"}};
    for(u32 i=0;i<(u32)(sizeof(internal)/sizeof(internal[0]));++i){if(!(m&internal[i].bit))continue;if(any)append_text(b,p,"|");append_text(b,p,internal[i].name);any=true;}
    if(!any)append_text(b,p,"NONE");append_text(b,p,"]");
}
static bool permanent_show_requested(const ElementSettings&cfg){return !cfg.automatic&&cfg.controlMode==kControlHideShow&&cfg.permanentVisibility==kAlwaysShow;}
// One state machine owns all 37 logical objects. Hide Delay is only armed on a transition into suppression;
// show transitions and initial policy establishment are immediate. allowHideDelay is false only for
// passthrough/fail-open paths where delaying a veto would be semantically wrong.
static void set_object_policy(u32 slot,long suppress,u32 reasonMask,bool allowHideDelay){
    if(slot>=kHUDObjectCount)return;
    ObjectControlState& c=g_controlStates[slot];
    const ElementSettings& cfg=g_settings.elements[slot];
    // Normal HUDMenu objects require the validated 30-object inventory. Power Armor logical groups are
    // scene-graph overlays and may legitimately be unbound while the player is outside Power Armor.
    if(slot<kNormalHUDObjectCount&&cfg.controlMode!=kControlOff&&(!g_hudComponents[slot].object||!g_componentInventoryReady)){
        suppress=0;
        reasonMask|=kReasonTelemetryFailOpen;
    }
    if(!c.policyInitialized){
        c.policyInitialized=1;c.suppress=suppress;c.reasonMask=(long)reasonMask;
        c.hidePending=0;c.hideDeadlineUs=0;c.gateClosed=suppress?1:0;++g_policyChanges;
        if(debug_verbose_enabled()&&cfg.controlMode!=kControlOff){
            char b[720];SIZE_T p=0;
            append_text(b,&p,"[Policy] INIT id=");append_u64(b,&p,slot+1u);
            append_text(b,&p," name=");append_text(b,&p,hud_object_name(slot));
            append_text(b,&p," mode=");append_u64(b,&p,cfg.controlMode);
            append_text(b,&p," automatic=");append_u64(b,&p,cfg.automatic?1u:0u);
            append_text(b,&p," reasons=");append_hex64(b,&p,reasonMask);append_reason_names(b,&p,reasonMask);
            append_text(b,&p,suppress?" SUPPRESS_IMMEDIATE":" SHOW/PASSTHROUGH");line_end(b,&p);log_raw(b);
        }
        return;
    }
    if((u32)c.reasonMask!=reasonMask){
        c.reasonMask=(long)reasonMask;
        if(debug_verbose_enabled()&&cfg.controlMode!=kControlOff){
            char b[720];SIZE_T p=0;
            append_text(b,&p,"[Policy] REASONS id=");append_u64(b,&p,slot+1u);
            append_text(b,&p," name=");append_text(b,&p,hud_object_name(slot));
            append_text(b,&p," reasons=");append_hex64(b,&p,reasonMask);append_reason_names(b,&p,reasonMask);
            line_end(b,&p);log_raw(b);
        }
    }
    if(c.suppress==suppress)return;
    c.suppress=suppress;++g_policyChanges;
    if(suppress){
        if(allowHideDelay&&cfg.hideDelaySeconds>0u){
            c.hidePending=1;
            c.hideDeadlineUs=(s64)(now_us()+(u64)cfg.hideDelaySeconds*1000000ULL);
        }else{
            c.hidePending=0;c.hideDeadlineUs=0;
            if(!c.gateClosed){c.gateClosed=1;log_gate_event(slot,"CLOSE_IMMEDIATE");}
        }
    }else{
        c.hidePending=0;c.hideDeadlineUs=0;
        if(c.gateClosed){c.gateClosed=0;log_gate_event(slot,"OPEN_SHOW");}
    }
    if(debug_verbose_enabled()&&cfg.controlMode!=kControlOff){
        char b[720];SIZE_T p=0;
        append_text(b,&p,"[Policy] id=");append_u64(b,&p,slot+1u);
        append_text(b,&p," name=");append_text(b,&p,hud_object_name(slot));
        append_text(b,&p," reasons=");append_hex64(b,&p,reasonMask);append_reason_names(b,&p,reasonMask);
        append_text(b,&p,suppress?(allowHideDelay&&cfg.hideDelaySeconds>0u?" HIDE_DELAY_ARMED":" HIDE_IMMEDIATE"):" SHOW_IMMEDIATE");
        line_end(b,&p);log_raw(b);
    }
}
static void update_delayed_gates(){bool anyPending=false;
    for(u32 i=0;i<kHUDObjectCount;++i){const ObjectControlState&c=g_controlStates[i];
    if(c.policyInitialized&&c.suppress&&c.hidePending){anyPending=true;
    break;
    }}if(!anyPending)return;
    u64 now=now_us();
    for(u32 i=0;i<kHUDObjectCount;++i){ObjectControlState&c=g_controlStates[i];
    if(c.policyInitialized&&c.suppress&&c.hidePending&&now>=(u64)c.hideDeadlineUs){c.hidePending=0;
    c.hideDeadlineUs=0;
    if(!c.gateClosed){c.gateClosed=1;
    log_gate_event(i,"CLOSE_AFTER_DELAY");
    }}}}
static void apply_permanent_policy(u32 i,const ElementSettings&cfg,bool hot){
    // In permanent mode the hotkey inverts the selected baseline; it is the only dynamic trigger.
    if(permanent_show_requested(cfg)){
        if(hot)set_object_policy(i,1,kReasonHotkey,true);
        else set_object_policy(i,0,kReasonAlwaysShow,false);
    }else{
        if(hot)set_object_policy(i,0,kReasonHotkey,false);
        else set_object_policy(i,1,kReasonAlwaysHide,true);
    }
}
static void apply_permanent_or_fail_open(u32 i){const ElementSettings&cfg=g_settings.elements[i];
    if(!g_settings.enabled||cfg.controlMode==kControlOff){set_object_policy(i,0,0u,false);return;}
    bool hot=hotkey_active(i);
    if(!cfg.automatic){apply_permanent_policy(i,cfg,hot);return;}
    if(hot)set_object_policy(i,0,kReasonHotkey,false);
    else set_object_policy(i,0,kReasonTelemetryFailOpen,false);
}
static void custom_log_gate_event(u32 index,const char*what){if(!debug_verbose_enabled()||index>=g_customSettings.count)return;ObjectControlState&c=g_customControlStates[index];const CustomElementSettings&cfg=g_customSettings.entries[index];char b[900];SIZE_T p=0;append_text(b,&p,"[CustomHUD] gate index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,cfg.name);append_text(b,&p," event=");append_text(b,&p,what);append_text(b,&p," suppress=");append_u64(b,&p,c.suppress?1u:0u);append_text(b,&p," closed=");append_u64(b,&p,c.gateClosed?1u:0u);append_text(b,&p," pending=");append_u64(b,&p,c.hidePending?1u:0u);line_end(b,&p);log_raw(b);}
static void custom_set_policy(u32 index,long suppress,u32 reasonMask,bool allowHideDelay){if(index>=g_customSettings.count||index>=kCustomElementCap)return;ObjectControlState&c=g_customControlStates[index];const ElementSettings&cfg=g_customSettings.entries[index].policy;if(!c.policyInitialized){c.policyInitialized=1;c.suppress=suppress;c.reasonMask=(long)reasonMask;c.hidePending=0;c.hideDeadlineUs=0;c.gateClosed=suppress?1:0;++g_policyChanges;if(debug_verbose_enabled()&&cfg.controlMode!=kControlOff){char b[900];SIZE_T p=0;append_text(b,&p,"[CustomHUD] policy INIT index=");append_u64(b,&p,index);append_text(b,&p," name=");append_text(b,&p,g_customSettings.entries[index].name);append_text(b,&p," reasons=");append_hex64(b,&p,reasonMask);append_reason_names(b,&p,reasonMask);append_text(b,&p,suppress?" SUPPRESS_IMMEDIATE":" SHOW/PASSTHROUGH");line_end(b,&p);log_raw(b);}return;}if((u32)c.reasonMask!=reasonMask)c.reasonMask=(long)reasonMask;if(c.suppress==suppress)return;c.suppress=suppress;++g_policyChanges;if(suppress){if(allowHideDelay&&cfg.hideDelaySeconds>0u){c.hidePending=1;c.hideDeadlineUs=(s64)(now_us()+(u64)cfg.hideDelaySeconds*1000000ULL);}else{c.hidePending=0;c.hideDeadlineUs=0;if(!c.gateClosed){c.gateClosed=1;custom_log_gate_event(index,"CLOSE_IMMEDIATE");}}}else{c.hidePending=0;c.hideDeadlineUs=0;if(c.gateClosed){c.gateClosed=0;custom_log_gate_event(index,"OPEN_SHOW");}}}
static void custom_apply_permanent_policy(u32 index,const ElementSettings&cfg,bool hot){if(permanent_show_requested(cfg)){if(hot)custom_set_policy(index,1,kReasonHotkey,true);else custom_set_policy(index,0,kReasonAlwaysShow,false);}else{if(hot)custom_set_policy(index,0,kReasonHotkey,false);else custom_set_policy(index,1,kReasonAlwaysHide,true);}}
static void custom_apply_permanent_or_fail_open(u32 index){if(index>=g_customSettings.count)return;const ElementSettings&cfg=g_customSettings.entries[index].policy;if(!g_settings.enabled||cfg.controlMode==kControlOff){custom_set_policy(index,0,0u,false);return;}bool hot=custom_hotkey_active(index);if(!cfg.automatic){custom_apply_permanent_policy(index,cfg,hot);return;}if(hot)custom_set_policy(index,0,kReasonHotkey,false);else custom_set_policy(index,0,kReasonTelemetryFailOpen,false);}
static void custom_update_delayed_gates(){u64 now=0u;for(u32 i=0;i<g_customSettings.count;++i){ObjectControlState&c=g_customControlStates[i];if(!(c.policyInitialized&&c.suppress&&c.hidePending))continue;if(!now)now=now_us();if(now>=(u64)c.hideDeadlineUs){c.hidePending=0;c.hideDeadlineUs=0;if(!c.gateClosed){c.gateClosed=1;custom_log_gate_event(i,"CLOSE_AFTER_DELAY");}}}}

static void update_policy_from_runtime_state(const RuntimeStateSnapshot& st){
    u32 required=g_requiredTriggerMask;
    u32 known=0u,active=0u;
    if(required&kReasonWeapon){known|=kReasonWeapon;if(st.weaponDrawn)active|=kReasonWeapon;}
    if(required&kReasonWeaponHolstered){known|=kReasonWeaponHolstered;if(st.weaponHolstered)active|=kReasonWeaponHolstered;}
    if(required&kReasonAim){known|=kReasonAim;if(st.aiming)active|=kReasonAim;}
    if(required&kReasonSprint){known|=kReasonSprint;if(st.sprinting)active|=kReasonSprint;}
    if(required&kReasonSneak){known|=kReasonSneak;if(st.sneaking)active|=kReasonSneak;}
    if(required&kReasonCombat){known|=kReasonCombat;if(st.inCombat)active|=kReasonCombat;}
    if((required&kReasonVATS)&&st.vatsValid){known|=kReasonVATS;if(st.inVATS)active|=kReasonVATS;}
    if((required&(kReasonFirstPerson|kReasonThirdPerson))&&st.perspectiveValid){
        known|=required&(kReasonFirstPerson|kReasonThirdPerson);
        if(st.thirdPerson){if(required&kReasonThirdPerson)active|=kReasonThirdPerson;}
        else if(required&kReasonFirstPerson)active|=kReasonFirstPerson;
    }
    if((required&kReasonLowHealth)&&(st.actorValueMask&kActorValueHealth)){
        known|=kReasonLowHealth;if(st.healthPct<g_settings.lowHealthThresholdPct)active|=kReasonLowHealth;
    }
    if((required&kReasonLowAP)&&(st.actorValueMask&kActorValueActionPoints)){
        known|=kReasonLowAP;if(st.actionPointsPct<g_settings.lowAPThresholdPct)active|=kReasonLowAP;
    }
    if((required&(kReasonActivate|kReasonCommand))&&st.crosshairModeValid){
        known|=required&(kReasonActivate|kReasonCommand);
        if(st.crosshairMode==kCrosshairModeActivate)active|=kReasonActivate;
        else if(st.crosshairMode==kCrosshairModeCommand)active|=kReasonCommand;
    }
    if((required&kReasonRadiation)&&st.radiationValid){known|=kReasonRadiation;if(st.radiationActive)active|=kReasonRadiation;}
    if((required&kReasonPowerArmor)&&st.powerArmorValid){known|=kReasonPowerArmor;if(st.powerArmorActive)active|=kReasonPowerArmor;}

    for(u32 i=0;i<kHUDObjectCount;++i){
        const ElementSettings& cfg=g_settings.elements[i];
        if(!g_settings.enabled||cfg.controlMode==kControlOff){set_object_policy(i,0,0u,false);continue;}
        bool hot=hotkey_active(i);
        if(!cfg.automatic){apply_permanent_policy(i,cfg,hot);continue;}
        u32 selected=cfg.triggerMask&kAllConfigurableTriggers;
        if((selected&~known)&&!hot){set_object_policy(i,0,kReasonTelemetryFailOpen,false);continue;}
        u32 reasons=active&selected;
        if(hot)reasons|=kReasonHotkey;
        set_object_policy(i,reasons?0:1,reasons,true);
    }
    for(u32 i=0;i<g_customSettings.count;++i){const ElementSettings&cfg=g_customSettings.entries[i].policy;if(!g_settings.enabled||cfg.controlMode==kControlOff){custom_set_policy(i,0,0u,false);continue;}bool hot=custom_hotkey_active(i);if(!cfg.automatic){custom_apply_permanent_policy(i,cfg,hot);continue;}u32 selected=cfg.triggerMask&kAllConfigurableTriggers;if((selected&~known)&&!hot){custom_set_policy(i,0,kReasonTelemetryFailOpen,false);continue;}u32 reasons=active&selected;if(hot)reasons|=kReasonHotkey;custom_set_policy(i,reasons?0:1,reasons,true);}
}
static __declspec(noinline) void enter_policy_fail_open_cold(){bool first=!g_policyFailOpen;
    for(u32 i=0;i<kHUDObjectCount;++i)apply_permanent_or_fail_open(i);for(u32 i=0;i<g_customSettings.count;++i)custom_apply_permanent_or_fail_open(i);
    g_policyFailOpen=true;
    if(first)error_log_line("[Policy] FAIL_OPEN telemetry unavailable; hotkey triggers/permanent policies retained, other automatic triggers passthrough.");
    }
static __declspec(noinline) void leave_policy_fail_open_cold(){g_policyFailOpen=false;error_log_line("[Policy] RECOVERED native state sampling restored.");}
static __declspec(noinline) void sample_and_update_runtime_policy(){bool hotkeyChanged=_InterlockedExchange(&g_hotkeyPolicyDirty,0)!=0;
    RuntimeStateSnapshot st={};
    if(!g_arrayResolved||!sample_runtime_state(&st)){if(!g_policyFailOpen||hotkeyChanged)enter_policy_fail_open_cold();
    return;
    }bool recovered=g_policyFailOpen;
    if(recovered)leave_policy_fail_open_cold();
    bool changed=!g_haveRuntimeState||!runtime_state_equal(st,g_lastRuntimeState);
    if(changed){g_lastRuntimeState=st;
    g_haveRuntimeState=1;
    ++g_stateChanges;
    log_runtime_state(st,"CHANGE");
    }if(!changed&&!recovered&&!hotkeyChanged)return;
    s64 ps=g_perfSampleThisFrame?perf_qpc():0;
    update_policy_from_runtime_state(st);
    if(g_perfSampleThisFrame){s64 e=perf_qpc();
    perf_add(g_perfPolicyEval,ps,e);
    }}

// ---------------- Power Armor logical-group adapter ----------------
// Direct NiAVObject::fadeAmount writes are ignored by the Power Armor interface
// renderer, while Bethesda's own SetAppCulled virtual works on individual live BSTriShape leaves. The adapter
// therefore implements binary visibility only. It is an overlay: while a PA group is allowed, Bethesda's
// native cull state is left untouched; while suppressed, the selected leaves are forced app-culled.
// A complete 36-target binding is validated per PA geometry generation. Suppression transitions are applied
// after Bethesda AdvanceMovie; release transitions restore the captured baseline before the next AdvanceMovie.
// While a group remains suppressed, cached leaf flags are checked after every Bethesda AdvanceMovie and any
// leaf Bethesda exposed is immediately re-culled. The 120-frame maintenance pass remains responsible only
// for guarded geometry/root validation. No per-leaf guarded-memory validation runs in the steady path.
static constexpr u32 kPAOffset_DashDials=0xE8u;
static constexpr u32 kPAOffset_Dash=0x110u;
static constexpr u32 kPAOffset_QueueInit=0x173u;
static constexpr u32 kPAOffset_Initialized=0x176u;
static constexpr u32 kNiAVObjectOffset_Flags=0x108u;
static constexpr u32 kNiAVObjectVSlot_SetAppCulled=0x2Du;
static constexpr u32 kNiAVObjectVSlot_GetObjectByName=0x2Eu;

using PAGetObjectByNameFn=void* (*)(void*,const FixedStringRef*);
using PASetAppCulledFn=void (*)(void*,bool);

enum PAGroupId : u32 {
    kPAAmmoDisplay=0u,
    kPAArmorCondition=1u,
    kPACompassGroup=2u,
    kPADashboard=3u,
    kPADashboardLights=4u,
    kPAGauges=5u,
    kPAVisorShadow=6u
};
struct PAGroupTarget {
    const char* name;
    u32 group;
    FixedStringRef fixedName;
    void* object;
    PASetAppCulledFn setCull;
    bool baselineHidden;
    bool overrideApplied;
};
static PAGroupTarget g_paGroupTargets[]={
    // Power Armor - Ammo Display
    {"AmmoCountHunds:0",kPAAmmoDisplay,{},0,0,false,false},{"AmmoCountTens:0",kPAAmmoDisplay,{},0,0,false,false},{"AmmoCountOnes:0",kPAAmmoDisplay,{},0,0,false,false},
    {"ClipCountHunds:0",kPAAmmoDisplay,{},0,0,false,false},{"ClipCountTens:0",kPAAmmoDisplay,{},0,0,false,false},{"ClipCountOnes:0",kPAAmmoDisplay,{},0,0,false,false},
    // Power Armor - Armor Condition
    {"Plane008:0",kPAArmorCondition,{},0,0,false,false},{"Plane008:1",kPAArmorCondition,{},0,0,false,false},{"Plane008:2",kPAArmorCondition,{},0,0,false,false},
    {"Plane008:3",kPAArmorCondition,{},0,0,false,false},{"Plane008:4",kPAArmorCondition,{},0,0,false,false},{"Plane008:5",kPAArmorCondition,{},0,0,false,false},
    {"DamageLights:0",kPAArmorCondition,{},0,0,false,false},{"DamageLightsFatigue:0",kPAArmorCondition,{},0,0,false,false},
    // Power Armor - Compass
    {"Compass:0",kPACompassGroup,{},0,0,false,false},{"QuestMarker:0",kPACompassGroup,{},0,0,false,false},{"LocMarker:0",kPACompassGroup,{},0,0,false,false},
    {"EnemyMarker:0",kPACompassGroup,{},0,0,false,false},{"LocMarkerDiscovered:0",kPACompassGroup,{},0,0,false,false},{"CompassGlass:0",kPACompassGroup,{},0,0,false,false},
    // Power Armor - Dashboard
    {"PADash01:0",kPADashboard,{},0,0,false,false},
    // Power Armor - Dashboard Lights
    {"DashLightSpot:0",kPADashboardLights,{},0,0,false,false},{"DashLightAux:0",kPADashboardLights,{},0,0,false,false},{"DashLightScope:0",kPADashboardLights,{},0,0,false,false},
    {"WarningLight:0",kPADashboardLights,{},0,0,false,false},{"Glow:0",kPADashboardLights,{},0,0,false,false},
    // Power Armor - Gauges (shared face + all gauge needles and radiation numeric readout)
    {"PAGauges01:0",kPAGauges,{},0,0,false,false},{"HPNeedle:0",kPAGauges,{},0,0,false,false},{"RadsNeedle:0",kPAGauges,{},0,0,false,false},
    {"PowerNeedle:0",kPAGauges,{},0,0,false,false},{"MaxPowerNeedle:0",kPAGauges,{},0,0,false,false},{"RadioNeedle:0",kPAGauges,{},0,0,false,false},
    {"RadsCountTens:0",kPAGauges,{},0,0,false,false},{"RadsCountOnes:0",kPAGauges,{},0,0,false,false},
    // Power Armor - Visor / Shadow Effects
    {"DarknessShadowPlane001:0",kPAVisorShadow,{},0,0,false,false},{"DarknessShadowPlane:0",kPAVisorShadow,{},0,0,false,false}
};
static constexpr u32 kPAGroupTargetCount=(u32)(sizeof(g_paGroupTargets)/sizeof(g_paGroupTargets[0]));
static bool g_paGroupNamesReady=false;
static bool g_paGroupNamesAttempted=false;
static void* g_paGroupLastGeometry=0;
static void* g_paGroupSearchBase=0;
static u32 g_paGroupBindingGeneration=0u;
static u32 g_paGroupBoundCount[kPAGroupCount]={};
static constexpr u32 g_paGroupExpectedCount[kPAGroupCount]={6u,8u,6u,1u,5u,8u,2u};
static_assert(kPAGroupTargetCount==36u,"Power Armor target inventory changed; review group mapping and structural contract");
static_assert(6u+8u+6u+1u+5u+8u+2u==kPAGroupTargetCount,"Power Armor expected group counts must cover every target");

// Lifecycle-driven PA adapter state. The authoritative state is Bethesda's native
// PowerArmor::ActorInPowerArmor(PlayerCharacter*) predicate. Scene-graph discovery/validation remains
// cold-path only: a real PA entry, explicit PA policy transitions, or the existing 120-frame maintenance
// cadence. UI/menu presentation state is not used for lifecycle detection. The steady PA path only reads the
// cached NiAVObject flags needed to keep active suppressions sticky; it performs no per-frame guarded polling.
enum class PAAdapterState:u32{Dormant=0u,AwaitGeometry=1u,Bound=2u};
static const char*pa_adapter_state_name(PAAdapterState s){return s==PAAdapterState::Dormant?"DORMANT":(s==PAAdapterState::AwaitGeometry?"AWAIT_GEOMETRY":"BOUND");}
static bool g_paLifecycleKnown=false;
static PAAdapterState g_paAdapterState=PAAdapterState::Dormant;
static bool g_paAppliedSuppressed[kPAGroupCount]={};
static u32 g_paApplyAfterAdvanceMask=0u;
static u64 g_paNextBindRetryFrame=0u;
static u64 g_paNextFullBindRetryFrame=0u;
static void* g_paReadyCandidateGeometry=0;
static void* g_paReadyCandidateBase=0;
static u32 g_paAwaitProbeStreak=0u;
static u32 g_paIncompleteBindStreak=0u;

static u64 g_paReadinessProbes=0u;
static u64 g_paReadinessNotReady=0u;
static u64 g_paReadinessCandidates=0u;
static u64 g_paLifecycleTransitions=0u;
static u64 g_paBindAttempts=0u;
static u64 g_paIncompleteBindAttempts=0u;
static u64 g_paGroupApplyCount=0u;
static u64 g_paMaintenanceChecks=0u;
static u64 g_paSuppressionReassertFrames=0u;
static u64 g_paSuppressionReassertions=0u;

static bool pa_read_ptr(void* base,u32 offset,void** out){if(!out){return false;}*out=0;if(!base)return false;return safe_read((u8*)base+offset,out);}
static bool pa_niavobject_compatible(void* object){
    if(!object)return false;void**vt=0;if(!safe_read(object,&vt)||!vt||!is_readable_image((u64)vt))return false;void*setCull=0;void*getByName=0;
    if(!safe_read(vt+kNiAVObjectVSlot_SetAppCulled,&setCull)||!setCull||!is_executable_image((u64)setCull))return false;
    if(!safe_read(vt+kNiAVObjectVSlot_GetObjectByName,&getByName)||!getByName||!is_executable_image((u64)getByName))return false;
    return is_readable_process_range((u8*)object+kNiAVObjectOffset_Flags,sizeof(u64));
}
static void* pa_get_object_by_name(void*base,const FixedStringRef*name){if(!pa_niavobject_compatible(base)||!name||!name->data)return 0;
    void**vt=0;
    void*fn=0;
    if(!safe_read(base,&vt)||!safe_read(vt+kNiAVObjectVSlot_GetObjectByName,&fn)||!fn||!is_executable_image((u64)fn))return 0;
    void*result=((PAGetObjectByNameFn)fn)(base,name);
    return pa_niavobject_compatible(result)?result:0;
    }
static bool pa_prepare_group_names(){
    if(g_paGroupNamesReady)return true;if(g_paGroupNamesAttempted)return false;g_paGroupNamesAttempted=true;
    if(!g_addresses.fixedStringCtor){error_log_line("[PAGroups] BSFixedString constructor unavailable; PA groups disabled.");return false;}
    for(u32 i=0;i<kPAGroupTargetCount;++i){FixedStringRef*result=((FixedStringCtorFn)g_addresses.fixedStringCtor)(&g_paGroupTargets[i].fixedName,g_paGroupTargets[i].name);
        if(result!=&g_paGroupTargets[i].fixedName||!g_paGroupTargets[i].fixedName.data){error_log_line("[PAGroups] target-name construction failed; PA groups disabled.");
        return false;
        }}
    g_paGroupNamesReady=true;verbose_log_line("[PAGroups] target BSFixedStrings READY.");return true;
}
static void pa_clear_binding(){for(u32 i=0;i<kPAGroupTargetCount;++i){g_paGroupTargets[i].object=0;
    g_paGroupTargets[i].setCull=0;
    g_paGroupTargets[i].overrideApplied=false;
    g_paGroupTargets[i].baselineHidden=false;
    }for(u32 i=0;i<kPAGroupCount;++i)g_paGroupBoundCount[i]=0u;
    g_paGroupSearchBase=0;
    }
static bool pa_cache_target(PAGroupTarget& t){
    if(!t.object||!pa_niavobject_compatible(t.object))return false;void**vt=0;void*fn=0;
    if(!safe_read(t.object,&vt)||!vt||!safe_read(vt+kNiAVObjectVSlot_SetAppCulled,&fn)||!fn||!is_executable_image((u64)fn))return false;
    t.setCull=(PASetAppCulledFn)fn;return true;
}
static inline bool pa_read_hidden_fast(const PAGroupTarget& t){u64 flags=0;memcpy(&flags,(const u8*)t.object+kNiAVObjectOffset_Flags,sizeof(flags));return (flags&1ULL)!=0;}
static inline void pa_set_culled_fast(PAGroupTarget& t,bool culled){if(t.object&&t.setCull)t.setCull(t.object,culled);}
static bool pa_bind_group_targets(void*base);

static bool pa_read_geometry_base_cold(void**outGeometry,void**outBase){
    if(outGeometry)*outGeometry=0;if(outBase)*outBase=0;if(!outGeometry||!outBase||!g_addresses.powerArmorGeometrySingleton)return false;
    void*geometry=0;if(!safe_read((void*)g_addresses.powerArmorGeometrySingleton,&geometry)||!geometry)return false;
    void*root=0;void*dash=0;if(!pa_read_ptr(geometry,kPAOffset_DashDials,&root))root=0;if(!root&&!pa_read_ptr(geometry,kPAOffset_Dash,&dash))dash=0;void*base=root?root:dash;
    if(!base)return false;*outGeometry=geometry;*outBase=base;return true;
}
struct PAGeometryReadySnapshot{void*geometry;void*base;bool initialized;bool queueInit;};
static bool pa_read_geometry_readiness_cold(PAGeometryReadySnapshot*out){
    if(!out)return false;memset(out,0,sizeof(*out));if(!g_addresses.powerArmorGeometrySingleton)return false;
    void*geometry=0;if(!safe_read((void*)g_addresses.powerArmorGeometrySingleton,&geometry)||!geometry)return false;
    u8 slice[0x90]={};if(!safe_read_bytes((u8*)geometry+kPAOffset_DashDials,slice,sizeof(slice)))return false;
    void*root=0;void*dash=0;memcpy(&root,slice+0x00,sizeof(root));memcpy(&dash,slice+(kPAOffset_Dash-kPAOffset_DashDials),sizeof(dash));
    bool queueInit=slice[kPAOffset_QueueInit-kPAOffset_DashDials]!=0;bool initialized=slice[kPAOffset_Initialized-kPAOffset_DashDials]!=0;
    out->geometry=geometry;out->base=root?root:dash;out->initialized=initialized;out->queueInit=queueInit;return true;
}
static u64 pa_await_backoff_frames(){
    // PA entry is presentation-critical: once ActorInPowerArmor becomes true, retry readiness every HUD frame
    // through the normal initialization window. Only fall back to wider spacing if geometry itself remains
    // unavailable abnormally long.
    if(g_paAwaitProbeStreak<16u)return 1ULL;
    if(g_paAwaitProbeStreak<32u)return 4ULL;
    if(g_paAwaitProbeStreak<64u)return 15ULL;
    return 60ULL;
}
static u64 pa_incomplete_bind_backoff_frames(){
    // A usable PA geometry root can be published before its named children are populated (notably during
    // post-load reconstruction). Keep the cheap readiness probe hot, but do not repeat the expensive 36-name
    // inventory every frame against the same incomplete root. A changed root bypasses this backoff immediately.
    if(g_paIncompleteBindStreak<=1u)return 2ULL;
    if(g_paIncompleteBindStreak==2u)return 4ULL;
    if(g_paIncompleteBindStreak==3u)return 8ULL;
    if(g_paIncompleteBindStreak==4u)return 15ULL;
    if(g_paIncompleteBindStreak==5u)return 30ULL;
    return 60ULL;
}
static bool pa_validate_cached_binding_cold(){
    if(!g_paGroupSearchBase||!g_paGroupLastGeometry)return false;
        void*geometry=0;
        void*base=0;
        if(!pa_read_geometry_base_cold(&geometry,&base))return false;
        return geometry==g_paGroupLastGeometry&&base==g_paGroupSearchBase;
}
static void pa_log_binding_summary(){
    if(!debug_errors_enabled())return;char b[1000];SIZE_T p=0;append_text(b,&p,"[PAGroups] binding generation=");append_u64(b,&p,g_paGroupBindingGeneration);
    for(u32 i=0;i<kPAGroupCount;++i){append_text(b,&p," #");
        append_u64(b,&p,kFirstPAGroupSlot+i+1u);
        append_text(b,&p,"=");
        append_text(b,&p,kPAGroupDisplayNames[i]);
        append_text(b,&p," ");
        append_u64(b,&p,g_paGroupBoundCount[i]);
        append_text(b,&p,"/");
        append_u64(b,&p,g_paGroupExpectedCount[i]);
        }
    line_end(b,&p);log_raw(b);
}
static bool pa_binding_complete(){
    if(!g_paGroupSearchBase)return false;for(u32 g=0;g<kPAGroupCount;++g)if(g_paGroupBoundCount[g]!=g_paGroupExpectedCount[g])return false;
    for(u32 i=0;i<kPAGroupTargetCount;++i)if(!g_paGroupTargets[i].object||!g_paGroupTargets[i].setCull)return false;return true;
}
static bool pa_bind_group_targets(void*base){
    pa_clear_binding();if(!base||!pa_prepare_group_names()||!pa_niavobject_compatible(base))return false;g_paGroupSearchBase=base;
    for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        t.object=pa_get_object_by_name(base,&t.fixedName);
        if(t.object&&!pa_cache_target(t))t.object=0;
        if(t.object&&t.group<kPAGroupCount)++g_paGroupBoundCount[t.group];
        }
    bool complete=pa_binding_complete();if(complete)++g_paGroupBindingGeneration;
    if(debug_verbose_enabled()&&complete){for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        char b[600];
        SIZE_T p=0;
        append_text(b,&p,"[PAGroups] target group=");
        append_text(b,&p,t.group<kPAGroupCount?kPAGroupDisplayNames[t.group]:"?");
        append_text(b,&p," name=");
        append_text(b,&p,t.name);
        append_text(b,&p," object=");
        append_hex64(b,&p,(u64)t.object);
        line_end(b,&p);
        log_raw(b);
        }}
    pa_log_binding_summary();if(!complete)pa_clear_binding();return complete;
}
static bool pa_group_suppressed(u32 group){if(group>=kPAGroupCount)return false;
    u32 slot=kFirstPAGroupSlot+group;
    const ElementSettings&cfg=g_settings.elements[slot];
    const ObjectControlState&st=g_controlStates[slot];
    return g_settings.enabled&&cfg.controlMode!=kControlOff&&st.policyInitialized&&st.gateClosed&&(((u32)st.reasonMask&kReasonTelemetryFailOpen)==0u);
    }
static void pa_restore_group_before_advance(u32 group){
    if(group>=kPAGroupCount)return;
        for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        if(t.group!=group||!t.overrideApplied||!t.object||!t.setCull)continue;
        pa_set_culled_fast(t,t.baselineHidden);
        t.overrideApplied=false;
        }g_paAppliedSuppressed[group]=false;
}
static void pa_release_overrides_before_discard_cold(){
    if(g_paAdapterState!=PAAdapterState::Bound||!g_paGroupSearchBase||!pa_validate_cached_binding_cold())return;
    for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        if(t.overrideApplied&&t.object&&t.setCull){pa_set_culled_fast(t,t.baselineHidden);
        t.overrideApplied=false;
        }}
    memset(g_paAppliedSuppressed,0,sizeof(g_paAppliedSuppressed));
}
static void pa_apply_group_after_advance(u32 group){
    if(group>=kPAGroupCount)return;
        bool suppress=pa_group_suppressed(group);
        for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        if(t.group!=group||!t.object||!t.setCull)continue;
        bool nativeHidden=pa_read_hidden_fast(t);
        t.baselineHidden=nativeHidden;
        if(suppress&&!nativeHidden){pa_set_culled_fast(t,true);
        t.overrideApplied=true;
        }else t.overrideApplied=false;
        }g_paAppliedSuppressed[group]=suppress;
        ++g_paGroupApplyCount;
}
static void pa_apply_all_after_bind(){for(u32 g=0;g<kPAGroupCount;++g){g_paAppliedSuppressed[g]=false;
    if(pa_group_suppressed(g))pa_apply_group_after_advance(g);
    }g_paApplyAfterAdvanceMask=0u;
    }
static bool pa_try_bind_after_advance(u64 frame,bool perfSample){
    if(!g_paLifecycleActive||g_paAdapterState!=PAAdapterState::AwaitGeometry)return g_paAdapterState==PAAdapterState::Bound;
    if(frame<g_paNextBindRetryFrame)return false;
    s64 awaitStart=perfSample?perf_qpc():0;++g_paReadinessProbes;++g_paAwaitProbeStreak;PAGeometryReadySnapshot snap={};
    if(!pa_read_geometry_readiness_cold(&snap)||!snap.geometry||!snap.base||!snap.initialized||snap.queueInit){
        ++g_paReadinessNotReady;g_paReadyCandidateGeometry=0;g_paReadyCandidateBase=0;g_paIncompleteBindStreak=0u;g_paNextFullBindRetryFrame=0u;
        g_paNextBindRetryFrame=frame+pa_await_backoff_frames();
        if(perfSample){s64 e=perf_qpc();perf_add(g_perfPAAwait,awaitStart,e);}return false;
    }
    g_paNextBindRetryFrame=0u;
    bool rootChanged=snap.geometry!=g_paReadyCandidateGeometry||snap.base!=g_paReadyCandidateBase;
    if(rootChanged){
        g_paReadyCandidateGeometry=snap.geometry;g_paReadyCandidateBase=snap.base;++g_paReadinessCandidates;
        g_paIncompleteBindStreak=0u;g_paNextFullBindRetryFrame=0u;
    }
    // The initialized/non-queueing snapshot plus pa_bind_group_targets()'s complete 36/36 check are the
    // safety boundary. Keep the first usable root immediate to minimize Power Armor entry latency; if that
    // same root proves incomplete, let cheap readiness polling continue while throttling full inventories.
    if(frame<g_paNextFullBindRetryFrame){
        if(perfSample){s64 e=perf_qpc();perf_add(g_perfPAAwait,awaitStart,e);}return false;
    }
    ++g_paBindAttempts;s64 bindStart=perfSample?perf_qpc():0;bool complete=pa_bind_group_targets(snap.base);if(perfSample){s64 e=perf_qpc();perf_add(g_perfPABind,bindStart,e);}
    if(!complete){++g_paIncompleteBindAttempts;++g_paIncompleteBindStreak;
        g_paGroupLastGeometry=0;
        g_paNextFullBindRetryFrame=frame+pa_incomplete_bind_backoff_frames();
        if(debug_verbose_enabled()){char b[680];
        SIZE_T p=0;
        append_text(b,&p,"[PAGroups] AWAIT_GEOMETRY full-bind incomplete attempt=");
        append_u64(b,&p,g_paBindAttempts);
        append_text(b,&p," streak=");
        append_u64(b,&p,g_paIncompleteBindStreak);
        append_text(b,&p," retryFrame=");
        append_u64(b,&p,g_paNextFullBindRetryFrame);
        line_end(b,&p);
        log_raw(b);
        }if(perfSample){s64 e=perf_qpc();
        perf_add(g_perfPAAwait,awaitStart,e);
        }return false;
        }
    g_paGroupLastGeometry=snap.geometry;
        g_paAdapterState=PAAdapterState::Bound;
        g_paNextBindRetryFrame=0u;
        g_paNextFullBindRetryFrame=0u;
        g_paReadyCandidateGeometry=0;
        g_paReadyCandidateBase=0;
        g_paAwaitProbeStreak=0u;
        g_paIncompleteBindStreak=0u;
        pa_apply_all_after_bind();
    if(debug_verbose_enabled()){char b[420];
        SIZE_T p=0;
        append_text(b,&p,"[PAGroups] state=BOUND complete ");
        append_u64(b,&p,kPAGroupTargetCount);
        append_text(b,&p,"/");
        append_u64(b,&p,kPAGroupTargetCount);
        append_text(b,&p," geometry targets after initialized geometry readiness.");
        line_end(b,&p);
        log_raw(b);
        }
    if(perfSample){s64 e=perf_qpc();perf_add(g_perfPAAwait,awaitStart,e);}return true;
}
static void pa_invalidate_binding_for_rebind(const char*reason){
    pa_clear_binding();
        g_paGroupLastGeometry=0;
        g_paReadyCandidateGeometry=0;
        g_paReadyCandidateBase=0;
        g_paAwaitProbeStreak=0u;
        g_paIncompleteBindStreak=0u;
        g_paAdapterState=g_paLifecycleActive?PAAdapterState::AwaitGeometry:PAAdapterState::Dormant;
        g_paNextBindRetryFrame=0u;
        g_paNextFullBindRetryFrame=0u;
        memset(g_paAppliedSuppressed,0,sizeof(g_paAppliedSuppressed));
        g_paApplyAfterAdvanceMask=0u;
        if(reason&&debug_verbose_enabled()){char b[420];
        SIZE_T p=0;
        append_text(b,&p,"[PAGroups] binding invalidated: ");
        append_text(b,&p,reason);
        append_text(b,&p," -> AWAIT_GEOMETRY");
        line_end(b,&p);
        log_raw(b);
        }
}
static void pa_runtime_reset(){
    g_paLifecycleKnown=false;
        g_paLifecycleActive=false;
        _InterlockedExchange(&g_paTriggerStateKnown,0);
        _InterlockedExchange(&g_paTriggerStateActive,0);
        g_paAdapterState=PAAdapterState::Dormant;
        g_paGroupLastGeometry=0;
        g_paReadyCandidateGeometry=0;
        g_paReadyCandidateBase=0;
        g_paAwaitProbeStreak=0u;
        g_paIncompleteBindStreak=0u;
        g_paNextBindRetryFrame=0u;
        g_paNextFullBindRetryFrame=0u;
        g_paApplyAfterAdvanceMask=0u;
        g_paValidatedPlayer=0;
        g_paRejectedPlayer=0;
        memset(g_paAppliedSuppressed,0,sizeof(g_paAppliedSuppressed));
        pa_clear_binding();
}
static void pa_poll_lifecycle_fast(){
    bool active=false;
        if(!sample_power_armor_native_fast(&active)){_InterlockedExchange(&g_paTriggerStateKnown,0);
        _InterlockedExchange(&g_paTriggerStateActive,0);
        if(g_paLifecycleKnown||g_paLifecycleActive||g_paAdapterState!=PAAdapterState::Dormant){pa_release_overrides_before_discard_cold();
        g_paLifecycleKnown=false;
        g_paLifecycleActive=false;
        pa_invalidate_binding_for_rebind(0);
        verbose_log_line("[PAGroups] native PA-state unavailable; fail-open state=DORMANT.");
        }return;
        }
    _InterlockedExchange(&g_paTriggerStateActive,active?1:0);_InterlockedExchange(&g_paTriggerStateKnown,1);
    if(!g_paLifecycleKnown){g_paLifecycleKnown=true;
        g_paLifecycleActive=active;
        if(active){g_paAdapterState=PAAdapterState::AwaitGeometry;
        g_paNextBindRetryFrame=0u;
        g_paNextFullBindRetryFrame=0u;
        g_paIncompleteBindStreak=0u;
        verbose_log_line("[PAGroups] native lifecycle initial=IN_POWER_ARMOR; state=AWAIT_GEOMETRY.");
        }else{g_paAdapterState=PAAdapterState::Dormant;
        verbose_log_line("[PAGroups] native lifecycle initial=OUTSIDE_POWER_ARMOR; state=DORMANT.");
        }return;
        }
    if(active==g_paLifecycleActive)return;
        ++g_paLifecycleTransitions;
        if(active){g_paLifecycleActive=true;
        pa_invalidate_binding_for_rebind(0);
        verbose_log_line("[PAGroups] native lifecycle ENTER; state=AWAIT_GEOMETRY.");
        }else{pa_release_overrides_before_discard_cold();
        g_paLifecycleActive=false;
        pa_invalidate_binding_for_rebind(0);
        verbose_log_line("[PAGroups] native lifecycle EXIT; state=DORMANT.");
        }
}
static void pa_prepare_policy_transitions_before_advance(){
    if(!g_paLifecycleActive||g_paAdapterState!=PAAdapterState::Bound||!g_paGroupSearchBase)return;
        u32 changed=0u;
        for(u32 g=0;g<kPAGroupCount;++g)if(pa_group_suppressed(g)!=g_paAppliedSuppressed[g])changed|=(1u<<g);
        if(!changed)return;
    if(!pa_validate_cached_binding_cold()){pa_invalidate_binding_for_rebind("policy-transition sentinel changed");return;}
    for(u32 g=0;g<kPAGroupCount;++g){u32 bit=1u<<g;
        if(!(changed&bit))continue;
        bool desired=pa_group_suppressed(g);
        if(g_paAppliedSuppressed[g]&&!desired)pa_restore_group_before_advance(g);
        else if(!g_paAppliedSuppressed[g]&&desired)g_paApplyAfterAdvanceMask|=bit;
        }
}
static void pa_maintenance_after_advance(u64 frame){
    if(!g_paLifecycleActive||g_paAdapterState!=PAAdapterState::Bound||!g_paGroupSearchBase||(frame%120ULL)!=0ULL)return;
        ++g_paMaintenanceChecks;
        if(!pa_validate_cached_binding_cold())pa_invalidate_binding_for_rebind("maintenance sentinel changed");
}
static void pa_reassert_suppressed_after_advance_fast(){
    if(!g_paLifecycleActive||g_paAdapterState!=PAAdapterState::Bound||!g_paGroupSearchBase)return;
    ++g_paSuppressionReassertFrames;
    for(u32 i=0;i<kPAGroupTargetCount;++i){PAGroupTarget&t=g_paGroupTargets[i];
        if(!t.object||!t.setCull||t.group>=kPAGroupCount||!g_paAppliedSuppressed[t.group])continue;
        if(pa_read_hidden_fast(t))continue;
        // Bethesda has exposed a leaf while Dynamic HUD still owns suppression. Capture the now-visible
        // native baseline and immediately reapply our overlay so camera/presentation transitions cannot flash it.
        t.baselineHidden=false;
        pa_set_culled_fast(t,true);
        t.overrideApplied=true;
        ++g_paSuppressionReassertions;
        }
}
static void pa_groups_after_advance(u64 frame,bool perfSample){
    if(!g_paLifecycleActive)return;
        if(g_paAdapterState==PAAdapterState::AwaitGeometry){pa_try_bind_after_advance(frame,perfSample);
        if(g_paAdapterState!=PAAdapterState::Bound)return;
        }
    if(g_paApplyAfterAdvanceMask){u32 mask=g_paApplyAfterAdvanceMask;
        g_paApplyAfterAdvanceMask=0u;
        for(u32 g=0;g<kPAGroupCount;++g)if(mask&(1u<<g))pa_apply_group_after_advance(g);
        }
    // Validate cached geometry on the existing cold cadence, then make suppression sticky against any
    // Bethesda app-cull writes performed by AdvanceMovie this frame.
    pa_maintenance_after_advance(frame);
    pa_reassert_suppressed_after_advance_fast();
}

static __declspec(noinline) void log_summary(void* hud,u64 frame){
    (void)hud;if(!debug_verbose_enabled())return;
    u64 controlled=0,suppressed=0,closed=0,pending=0;
    for(u32 i=0;i<kHUDObjectCount;++i){
        const ElementSettings& cfg=g_settings.elements[i];const ObjectControlState& c=g_controlStates[i];
        if(cfg.controlMode!=kControlOff){if(i<kNormalHUDObjectCount){if(g_hudComponents[i].object)++controlled;}else ++controlled;}
        if(c.suppress)++suppressed;if(c.gateClosed)++closed;if(c.hidePending)++pending;
    }
    char b[1400];SIZE_T p=0;
    append_text(b,&p,"[Summary] advance=");append_u64(b,&p,frame);
    append_text(b,&p," controlled=");append_u64(b,&p,controlled);
    append_text(b,&p," suppressPolicies=");append_u64(b,&p,suppressed);
    append_text(b,&p," gatesClosed=");append_u64(b,&p,closed);
    append_text(b,&p," hidePending=");append_u64(b,&p,pending);
    append_text(b,&p," stateSamples=");append_u64(b,&p,(u64)g_stateSamples);
    append_text(b,&p," stateChanges=");append_u64(b,&p,(u64)g_stateChanges);
    append_text(b,&p," stateFailures=");append_u64(b,&p,(u64)g_stateFailures);
    append_text(b,&p," avDiscoveries=");append_u64(b,&p,(u64)g_avDiscoveryAttempts);
    append_text(b,&p," avReadFailures=");append_u64(b,&p,(u64)g_avReadFailures);
    append_text(b,&p," arrayOffset=");append_hex64(b,&p,g_arrayOffset);
    append_text(b,&p," crosshairSinkOffset=");append_hex64(b,&p,g_crosshairValueSinkOffset);
    append_text(b,&p," policyChanges=");append_u64(b,&p,(u64)g_policyChanges);
    append_text(b,&p," radiationKnown=");append_u64(b,&p,(u64)(_InterlockedCompareExchange(&g_radiationStateKnown,0,0)!=0));
    append_text(b,&p," radiationActive=");append_u64(b,&p,(u64)(_InterlockedCompareExchange(&g_radiationActive,0,0)!=0));
    append_text(b,&p," radiationTransitions=");append_u64(b,&p,g_radiationTransitions);
    append_text(b,&p," paActive=");append_u64(b,&p,g_paLifecycleActive?1u:0u);
    append_text(b,&p," paState=");append_text(b,&p,pa_adapter_state_name(g_paAdapterState));
    append_text(b,&p," paBinds=");append_u64(b,&p,g_paGroupBindingGeneration);
    append_text(b,&p," paBindAttempts=");append_u64(b,&p,g_paBindAttempts);
    append_text(b,&p," paIncomplete=");
        append_u64(b,&p,g_paIncompleteBindAttempts);
        append_text(b,&p," paReadyProbes=");
        append_u64(b,&p,g_paReadinessProbes);
        append_text(b,&p," paNotReady=");
        append_u64(b,&p,g_paReadinessNotReady);
        append_text(b,&p," paCandidates=");
        append_u64(b,&p,g_paReadinessCandidates);
    append_text(b,&p," paTransitions=");append_u64(b,&p,g_paLifecycleTransitions);
    append_text(b,&p," paStateSamples=");append_u64(b,&p,g_paStateSamples);
    append_text(b,&p," paStateFailures=");append_u64(b,&p,g_paStateFailures);
    append_text(b,&p," paGroupApplies=");append_u64(b,&p,g_paGroupApplyCount);
    append_text(b,&p," paMaint=");append_u64(b,&p,g_paMaintenanceChecks);
    append_text(b,&p," paReassertFrames=");append_u64(b,&p,g_paSuppressionReassertFrames);
    append_text(b,&p," paReasserts=");append_u64(b,&p,g_paSuppressionReassertions);
    append_text(b,&p," paCompassGate=");append_u64(b,&p,g_paCompassHookInstalled?1u:0u);
    u32 customControlled=0u,customBound=0u,customSuppressed=0u;for(u32 i=0;i<g_customSettings.count;++i){if(g_customSettings.entries[i].policy.controlMode!=kControlOff)++customControlled;if(g_customRuntimeStates[i].lastObject||g_customRuntimeStates[i].menuGateApplied)++customBound;if(g_customRuntimeStates[i].appliedSuppression||g_customRuntimeStates[i].menuGateApplied)++customSuppressed;}append_text(b,&p," customLoaded=");append_u64(b,&p,g_customSettings.count);append_text(b,&p," customControlled=");append_u64(b,&p,customControlled);append_text(b,&p," customBound=");append_u64(b,&p,customBound);append_text(b,&p," customSuppressed=");append_u64(b,&p,customSuppressed);append_text(b,&p," customBinds=");append_u64(b,&p,g_customRuntimeBinds);append_text(b,&p," customResolveFail=");append_u64(b,&p,g_customRuntimeResolveFailures);append_text(b,&p," customWrites=");append_u64(b,&p,g_customRuntimeVisibleWrites);append_text(b,&p," customValidations=");append_u64(b,&p,g_customRuntimeValidations);append_text(b,&p," customValidationFail=");append_u64(b,&p,g_customRuntimeValidationFailures);append_text(b,&p," customMenuHides=");append_u64(b,&p,g_customMenuGateHides);append_text(b,&p," customMenuReleases=");append_u64(b,&p,g_customMenuGateReleases);append_text(b,&p," customMenuFail=");append_u64(b,&p,g_customMenuGateFailures);append_text(b,&p," customMenuStale=");append_u64(b,&p,g_customMenuStaleDrops);append_text(b,&p," customMenuAbsent=");append_u64(b,&p,g_customMenuAbsentRetries);
    line_end(b,&p);log_raw(b);
}
static __declspec(noinline) void log_player_identity_once(){if(g_playerLogged)return;
    g_playerLogged=true;
    if(!g_addresses.playerSingleton){error_log_line("[Player] singleton address unavailable.");
    return;
    }void*player=0;
    if(!safe_read((void*)g_addresses.playerSingleton,&player)||!player){error_log_line("[Player] singleton resolved but current PlayerCharacter pointer is null/unreadable.");
    g_playerLogged=false;
    return;
    }if(!debug_verbose_enabled())return;
    char type[192];
    bool hasType=copy_rtti_name(player,type,sizeof(type));
    char b[640];
    SIZE_T p=0;
    append_text(b,&p,"[Player] singleton address=");
    append_hex64(b,&p,g_addresses.playerSingleton);
    append_text(b,&p," object=");
    append_hex64(b,&p,(u64)player);
    append_text(b,&p," rtti=");
    append_text(b,&p,hasType?type:"<unavailable>");
    line_end(b,&p);
    log_raw(b);
    }
static __declspec(noinline) bool process_advance_maintenance(void*hud,u64 frame,bool perfSample){
    bool maintenanceFrame=false;bool forceDiscovery=false;
    if(atomic_exchange_long(&g_lifecycleResetRequested,0)==1){maintenanceFrame=true;
        verbose_log_line("[Lifecycle] deferred HUD/runtime reset consumed on AdvanceMovie thread.");
        g_lastHud=0;
        reset_bound_state();
        forceDiscovery=true;
        }
    if(hud!=g_lastHud){maintenanceFrame=true;
        if(debug_verbose_enabled()){char b[480];
        SIZE_T p=0;
        append_text(b,&p,"[HUD] generation old=");
        append_hex64(b,&p,(u64)g_lastHud);
        append_text(b,&p," new=");
        append_hex64(b,&p,(u64)hud);
        line_end(b,&p);
        log_raw(b);
        }g_lastHud=hud;
        reset_bound_state();
        forceDiscovery=true;
        }
    if(!g_playerLogged)log_player_identity_once();
    if(g_arrayResolved&&(frame%120ULL)==0ULL){maintenanceFrame=true;
        s64 bs=perfSample?perf_qpc():0;
        bool valid=validate_binding_sentinel(hud);
        if(perfSample){s64 e=perf_qpc();
        perf_add(g_perfBindingValidation,bs,e);
        }if(!valid){error_log_line("[HUDArray] binding sentinel failed; fail-open and perform hardened rediscovery.");
        lifecycle_suspend_store_release(1);
        reset_bound_state();
        forceDiscovery=true;
        }}
    if(!g_arrayResolved&&(forceDiscovery||frame<=5ULL||(frame%120ULL)==0ULL)){maintenanceFrame=true;
        s64 ds=perfSample?perf_qpc():0;
        discover_hud_array(hud);
        if(perfSample){s64 e=perf_qpc();
        perf_add(g_perfDiscovery,ds,e);
        }}
    return maintenanceFrame;
}

static __declspec(noinline) void emit_periodic_diagnostics(void*hud,u64 frame){log_summary(hud,frame);log_perf_summary(frame);}

static __declspec(noinline) void HookButtonBarAdvance(void*menu,float delta,u64 time){
    ((AdvanceFn)g_origButtonBarAdvance)(menu,delta,time);
    bool perfEnabled=g_perfTelemetryEnabled;u64 perfCall=0u;bool perfSample=false;
    if(perfEnabled){perfCall=++g_perfButtonHintsHookCalls;perfSample=(perfCall%kPerfSampleStride)==0ULL;}
    s64 perfStart=perfSample?perf_qpc():0;
    button_hints_after_advance(menu,perfSample);
    if(perfSample){s64 perfEnd=perf_qpc();perf_add(g_perfButtonHintsTotal,perfStart,perfEnd);}
}

static __declspec(noinline) void HookAdvance(void*hud,float delta,u64 time){
    u64 frame=++g_advanceCount;if(!g_perfWindowStartFrame)reset_perf_window(frame-1ULL);
    bool perfEnabled=g_perfTelemetryEnabled;
        bool detailedSample=perfEnabled&&((frame%kPerfDetailedStride)==0ULL);
        bool outerSample=perfEnabled&&!detailedSample&&((frame%kPerfSampleStride)==0ULL);
        g_perfSampleThisFrame=detailedSample;
        if(outerSample)++g_perfSampledFrames;
        if(detailedSample)++g_perfDetailedFrames;
    bool anySample=outerSample||detailedSample;s64 pluginStart=anySample?perf_qpc():0;bool maintenanceFrame=false;
    s64 ts=detailedSample?perf_qpc():0;
        if(atomic_load_long(&g_settingsPending)!=0)consume_pending_settings();
        if(detailedSample){s64 e=perf_qpc();
        perf_add(g_perfSettingsCommit,ts,e);
        }
    if(atomic_load_long(&g_lifecycleResetRequested)!=0||
       hud!=g_lastHud||!g_arrayResolved||!g_playerLogged||
       (frame%120ULL)==0ULL){
        maintenanceFrame=process_advance_maintenance(hud,frame,detailedSample);
    }
    observe_radiation_event_fast();
    // Sample native Power Armor state once per HUD advance. Both lifecycle ownership and the optional
    // user-facing POWER_ARMOR trigger consume this same cached result.
    u64 paTicks=0u;s64 paPreStart=anySample?perf_qpc():0;pa_poll_lifecycle_fast();if(anySample){s64 e=perf_qpc();perf_add(g_perfPAPre,paPreStart,e);paTicks+=(u64)(e-paPreStart);}
    s64 sp=detailedSample?perf_qpc():0;sample_and_update_runtime_policy();if(detailedSample){s64 e=perf_qpc();perf_add(g_perfStatePolicy,sp,e);}
    s64 dg=detailedSample?perf_qpc():0;update_delayed_gates();custom_update_delayed_gates();if(detailedSample){s64 e=perf_qpc();perf_add(g_perfDelayedGates,dg,e);}
    paPreStart=anySample?perf_qpc():0;
        pa_prepare_policy_transitions_before_advance();
        if(anySample){s64 e=perf_qpc();
        perf_add(g_perfPAPre,paPreStart,e);
        paTicks+=(u64)(e-paPreStart);
        }
    s64 customPreStart=anySample?perf_qpc():0;
    custom_runtime_before_advance();
    if(anySample){s64 e=perf_qpc();perf_add(g_perfCustomBefore,customPreStart,e);}
    s64 beforeOrig=anySample?perf_qpc():0;
        if(outerSample){perf_add(g_perfPluginPre,pluginStart,beforeOrig);
        perf_add(maintenanceFrame?g_perfPluginMaintenance:g_perfPluginSteady,pluginStart,beforeOrig);
        }else if(detailedSample)perf_add(g_perfPluginDetailed,pluginStart,beforeOrig);
    ((AdvanceFn)g_origAdvance)(hud,delta,time);if(outerSample){s64 e=perf_qpc();perf_add(g_perfOriginalAdvance,beforeOrig,e);}g_perfSampleThisFrame=false;
    s64 customPostStart=anySample?perf_qpc():0;
    custom_runtime_after_advance();
    if(anySample){s64 e=perf_qpc();perf_add(g_perfCustomAfter,customPostStart,e);}
    s64 paPostStart=anySample?perf_qpc():0;
        pa_groups_after_advance(frame,anySample);
        if(anySample){s64 e=perf_qpc();
        perf_add(g_perfPAPost,paPostStart,e);
        paTicks+=(u64)(e-paPostStart);
        perf_add_ticks(g_perfPAFull,paTicks);
        }
    if(g_customDiscoveryRequested!=0)custom_discovery_run_once();
    if((frame%kSummaryEveryAdvances)==0ULL)emit_periodic_diagnostics(hud,frame);
}

struct NativeRequestProof { bool pass;u32 persistentOffset;u32 timedOffset; };
static void log_native_request_failure(const char*label,const char*reason){
    if(!debug_errors_enabled())return;
        char b[640];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] ");
        append_text(b,&p,label);
        append_text(b,&p," native request override=UNAVAILABLE; ");
        append_text(b,&p,reason);
        line_end(b,&p);
        log_raw(b);
}
static __declspec(noinline) NativeRequestProof prove_native_request_contract(const char*label,u64 derivedShouldBeVisible,u32 derivedScanBytes){
    NativeRequestProof proof={};
    if(!derivedShouldBeVisible||!g_addresses.crosshairShouldBeVisible)return proof;
    if(!is_executable_image(derivedShouldBeVisible)||!is_executable_image(g_addresses.crosshairShouldBeVisible))return proof;
    if(derivedScanBytes>64u)derivedScanBytes=64u;u8 derivedCode[64]={};if(!safe_read_bytes((void*)derivedShouldBeVisible,derivedCode,derivedScanBytes))return proof;
    bool callsBase=false;u32 callOffset=0;
    for(u32 i=0;i+5u<=derivedScanBytes;++i){if(derivedCode[i]!=0xE8u)continue;
        s32 rel=0;
        memcpy(&rel,derivedCode+i+1u,sizeof(rel));
        u64 target=derivedShouldBeVisible+(u64)i+5ULL+(s64)rel;
        if(target==g_addresses.crosshairShouldBeVisible){callsBase=true;
        callOffset=i;
        break;
        }}
    if(!callsBase){log_native_request_failure(label,"derived predicate does not directly call resolved HUDComponentBase::ShouldBeVisible.");return proof;}
    u8 baseCode[48]={};
        if(!safe_read_bytes((void*)g_addresses.crosshairShouldBeVisible,baseCode,sizeof(baseCode))){log_native_request_failure(label,"base predicate bytes unreadable.");
        return proof;
        }
    u32 cmpOffsets[4]={};
        u32 cmpCount=0;
        for(u32 i=0;i+7u<=sizeof(baseCode)&&cmpCount<4u;++i){if(baseCode[i]==0x80u&&baseCode[i+1u]==0xB9u&&baseCode[i+6u]==0x00u){u32 disp=0;
        memcpy(&disp,baseCode+i+2u,4u);
        if(disp&&disp<0x1000u)cmpOffsets[cmpCount++]=disp;
        }}
    if(cmpCount<2u||cmpOffsets[0]==cmpOffsets[1]){log_native_request_failure(label,"base visibility request fields not structurally identified.");return proof;}
    u32 persistentOffset=cmpOffsets[0],timedOffset=cmpOffsets[1];
        u64 base=g_addresses.crosshairShouldBeVisible;
        u64 scanStart=base>0x180ULL?base-0x180ULL:base;
        u8 setterRegion[0x180]={};
    if(scanStart==base||!safe_read_bytes((void*)scanStart,setterRegion,sizeof(setterRegion))){log_native_request_failure(label,"persistent setter region unreadable.");
        return proof;
        }
    void*setter=0;
    for(u32 i=0;i+15u<=sizeof(setterRegion);++i){
        if(setterRegion[i]!=0xC6u||setterRegion[i+1u]!=0x81u||setterRegion[i+6u]!=0x01u||
           setterRegion[i+7u]!=0xC6u||setterRegion[i+8u]!=0x81u||
           setterRegion[i+13u]!=0x00u||setterRegion[i+14u]!=0xC3u)continue;
        u32 d1=0,d2=0;
        memcpy(&d1,setterRegion+i+2u,4u);
        memcpy(&d2,setterRegion+i+9u,4u);
        if(d1==persistentOffset&&d2==timedOffset){setter=(void*)(scanStart+i);
        break;
        }}
    if(!setter){log_native_request_failure(label,"persistent-on setter pattern not proven.");return proof;}
    proof.pass=true;proof.persistentOffset=persistentOffset;proof.timedOffset=timedOffset;
    if(debug_verbose_enabled()){char b[900];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] ");
        append_text(b,&p,label);
        append_text(b,&p," native request override=PASS derived=");
        append_hex64(b,&p,derivedShouldBeVisible);
        append_text(b,&p," base=");
        append_hex64(b,&p,g_addresses.crosshairShouldBeVisible);
        append_text(b,&p," callOffset=");
        append_hex64(b,&p,callOffset);
        append_text(b,&p," persistentOffset=");
        append_hex64(b,&p,persistentOffset);
        append_text(b,&p," timedOffset=");
        append_hex64(b,&p,timedOffset);
        append_text(b,&p," persistentOn=");
        append_hex64(b,&p,(u64)setter);
        line_end(b,&p);
        log_raw(b);
        }
    return proof;
}

static bool function_directly_calls(u64 fn,u64 target,u32 scanBytes){if(!fn||!target||!is_executable_image(fn))return false;
    if(scanBytes>96u)scanBytes=96u;
    u8 code[96]={};
    if(!safe_read_bytes((void*)fn,code,scanBytes))return false;
    for(u32 i=0;i+5u<=scanBytes;++i){if(code[i]!=0xE8u)continue;
    s32 rel=0;
    memcpy(&rel,code+i+1u,4u);
    u64 dst=fn+(u64)i+5ULL+(s64)rel;
    if(dst==target)return true;
    }return false;
    }
static void validate_generic_native_request_contract(const NativeRequestProof&ap,const NativeRequestProof&health){
    g_genericNativeRequestCapability=false;g_genericPersistentRequestOffset=0;
    if(!ap.pass||!health.pass)return;
    if(ap.persistentOffset!=health.persistentOffset||ap.timedOffset!=health.timedOffset){error_log_line("[Compatibility] generic native request=UNAVAILABLE; AP/Health field proofs disagree.");
        return;
        }
    g_genericPersistentRequestOffset=ap.persistentOffset;g_genericNativeRequestCapability=true;
    if(debug_verbose_enabled()){char b[560];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] generic native request=PASS persistentOffset=");
        append_hex64(b,&p,g_genericPersistentRequestOffset);
        append_text(b,&p," timedOffset=");
        append_hex64(b,&p,ap.timedOffset);
        line_end(b,&p);
        log_raw(b);
        }
}
static void validate_button_hints_contract(){
    g_buttonBarContractPass=false;g_buttonBarVtable=find_primary_vtable_by_rtti_slots(".?AVButtonBarMenu@@",5u);g_buttonBarExpectedAdvance=0;
    if(g_buttonBarVtable)safe_read(g_buttonBarVtable+4,&g_buttonBarExpectedAdvance);
    g_buttonBarContractPass=g_buttonBarVtable&&g_buttonBarExpectedAdvance&&is_executable_image((u64)g_buttonBarExpectedAdvance);
    if(debug_verbose_enabled()){char b[760];SIZE_T p=0;append_text(b,&p,"[Compatibility] optional contract=ButtonBarMenu AdvanceMovie slot=0x4 rtti=.?AVButtonBarMenu@@ vtable=");
        append_hex64(b,&p,(u64)g_buttonBarVtable);append_text(b,&p," advance=");append_hex64(b,&p,(u64)g_buttonBarExpectedAdvance);
        append_text(b,&p,g_buttonBarContractPass?" PASS":" UNAVAILABLE");line_end(b,&p);log_raw(b);}
    if(!g_buttonBarContractPass&&g_settings.hideButtonHints&&debug_errors_enabled())error_log_line("[ButtonHints] optional ButtonBarMenu AdvanceMovie contract unavailable; Hide Button Hints will fail open.");
}

static void validate_component_contracts(){
    u32 pass=0u;
    for(u32 i=0;i<kNormalHUDObjectCount;++i){
        HUDComponent& d=g_hudComponents[i];
        d.contractPass=false;d.original=0;d.vtable=find_primary_vtable_by_rtti(d.rtti);
        if(!d.vtable)continue;
        void* observed=0;if(!safe_read(d.vtable+7,&observed)||!observed)continue;
        if(d.managedIndex>=0){
            u64 expected=g_elements[(u32)d.managedIndex].expectedShould;
            d.contractPass=((u64)observed==expected);
        }else if(d.derived){
            d.contractPass=is_executable_image((u64)observed)&&function_directly_calls((u64)observed,g_addresses.crosshairShouldBeVisible,64u);
        }else d.contractPass=((u64)observed==g_addresses.crosshairShouldBeVisible);
        if(d.contractPass){++pass;if(d.managedIndex<0)d.original=(ShouldBeVisibleFn)observed;}
        if(debug_verbose_enabled()){
            char b[760];SIZE_T p=0;
            append_text(b,&p,"[Components] contract id=");append_u64(b,&p,d.id);
            append_text(b,&p," name=");append_text(b,&p,d.name);
            append_text(b,&p," vtable=");append_hex64(b,&p,(u64)d.vtable);
            append_text(b,&p," slot7=");append_hex64(b,&p,(u64)observed);
            append_text(b,&p," type=");append_text(b,&p,d.derived?"derived":"base");
            append_text(b,&p,d.contractPass?" PASS":" FAIL");line_end(b,&p);log_raw(b);
        }
    }
    if(debug_verbose_enabled()){
        char b[320];SIZE_T p=0;append_text(b,&p,"[Components] contracts pass=");append_u64(b,&p,pass);
        append_text(b,&p,"/");append_u64(b,&p,kNormalHUDObjectCount);line_end(b,&p);log_raw(b);
    }
}

static void validate_pa_compass_contract(){
    g_paCompassContractPass=false;g_paCompassVtable=find_primary_vtable_by_rtti_slots(".?AVPowerArmorHUDCompass@@",8u);void*observed=0;
    if(g_paCompassVtable)safe_read(g_paCompassVtable+7,&observed);
    g_paCompassContractPass=g_paCompassVtable&&observed==(void*)g_addresses.crosshairShouldBeVisible;
    char b[760];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] contract=PowerArmorHUDCompass ShouldBeVisible rtti=.?AVPowerArmorHUDCompass@@ slot=");
        append_hex64(b,&p,7u);
        append_text(b,&p," vtable=");
        append_hex64(b,&p,(u64)g_paCompassVtable);
        append_text(b,&p," observed=");
        append_hex64(b,&p,(u64)observed);
        append_text(b,&p," expectedBase=");
        append_hex64(b,&p,g_addresses.crosshairShouldBeVisible);
        append_text(b,&p,g_paCompassContractPass?" PASS":" UNAVAILABLE");
        line_end(b,&p);
    if(g_paCompassContractPass)verbose_log_raw(b);else error_log_raw(b);
}

static __declspec(noinline) bool validate_static_contracts(){
    g_elements[0].expectedShould=g_addresses.compassShouldBeVisible;
        g_elements[1].expectedShould=g_addresses.healthShouldBeVisible;
        g_elements[2].expectedShould=g_addresses.actionPointsShouldBeVisible;
        g_elements[3].expectedShould=g_addresses.crosshairShouldBeVisible;
    ContractResult contracts[]={
        {"Compass ShouldBeVisible",".?AVHUDCompass@@",7u,g_addresses.compassShouldBeVisible,0,false},
        {"Health ShouldBeVisible",".?AVHUDPlayerHealthMeter@@",7u,g_addresses.healthShouldBeVisible,0,false},
        {"AP ShouldBeVisible",".?AVHUDActionPointMeter@@",7u,g_addresses.actionPointsShouldBeVisible,0,false},
        {"Crosshair ShouldBeVisible",".?AVHUDCrosshair@@",7u,g_addresses.crosshairShouldBeVisible,0,false},
        {"ActorState sprint getter",".?AVActorState@@",0x1Fu,g_addresses.actorSprintState,0,false},
        {"HUDMenu AdvanceMovie",".?AVHUDMenu@@",4u,g_addresses.hudAdvanceMovie,0,false}
    };
    bool c0=validate_contract(&contracts[0]);
        bool c1=validate_contract(&contracts[1]);
        bool c2=validate_contract(&contracts[2]);
        bool c3=validate_contract(&contracts[3]);
        g_componentContractsPass=c0&&c1&&c2&&c3;
        for(u32 i=0;i<4u;++i)g_componentVtables[i]=contracts[i].vtable;
        g_sprintContractPass=validate_contract(&contracts[4]);
        g_actorStateVtable=contracts[4].vtable;
        g_hudAdvanceContractPass=validate_contract(&contracts[5]);
        g_hudVtable=contracts[5].vtable;
        validate_button_hints_contract();
    if(g_componentContractsPass)verbose_log_line("[Compatibility] four HUD ShouldBeVisible contracts=PASS");
        else error_log_line("[Compatibility] four HUD ShouldBeVisible contracts=FAIL");
    if(g_sprintContractPass)verbose_log_line("[Compatibility] ActorState sprint contract=PASS");else error_log_line("[Compatibility] ActorState sprint contract=FAIL");
    if(g_hudAdvanceContractPass)verbose_log_line("[Compatibility] HUDMenu AdvanceMovie contract=PASS");
        else error_log_line("[Compatibility] HUDMenu AdvanceMovie contract=FAIL");
        NativeRequestProof apProof=prove_native_request_contract("AP",g_addresses.actionPointsShouldBeVisible,32u);
        NativeRequestProof healthProof=prove_native_request_contract("Health",g_addresses.healthShouldBeVisible,48u);
        validate_generic_native_request_contract(apProof,healthProof);
        validate_component_contracts();
        validate_pa_compass_contract();
    return g_hudAdvanceContractPass;
}

static bool rollback_component_gates(){bool ok=true;
    for(s32 i=(s32)kManagedCount-1;i>=0;--i){if(!g_origShould[i]||!g_gateHooks[i]||!g_componentVtables[i])continue;
    bool restored=restore_ptr_if_owned(g_componentVtables[i]+7,g_gateHooks[i],(void*)g_origShould[i]);
    if(!restored){error_log_line("[Hooks] WARNING: component rollback could not prove/restore owned slot; original pointer retained for safe forwarding.");
    ok=false;
    }else{g_origShould[i]=0;
    g_gateHooks[i]=0;
    }}return ok;
    }
static bool install_component_gates(){if(!g_componentContractsPass){error_log_line("[Hooks] component gates DISABLED: identity contracts failed.");
    return false;
    }void*hooks[kManagedCount]={(void*)&HookCompassShouldBeVisible,(void*)&HookHealthShouldBeVisible,(void*)&HookAPShouldBeVisible,(void*)&HookCrosshairShouldBeVisible};
    for(u32 i=0;i<kManagedCount;++i){void*observed=0;
        if(!g_componentVtables[i]||
           !safe_read(g_componentVtables[i]+7,&observed)||
           observed!=(void*)g_elements[i].expectedShould){
            error_log_line("[Hooks] component gate PRECHECK failed: slot identity changed before patching; no component slots modified.");
            return false;
        }
    }
    for(u32 i=0;i<kManagedCount;++i){void*orig=0;
        if(!patch_ptr(g_componentVtables[i]+7,hooks[i],&orig)){error_log_line("[Hooks] component gate patch failed; transactional rollback.");
        rollback_component_gates();
        return false;
        }g_origShould[i]=(ShouldBeVisibleFn)orig;
        g_gateHooks[i]=hooks[i];
        if(orig!=(void*)g_elements[i].expectedShould){error_log_line("[Hooks] component gate identity changed during patch; restoring observed predecessor and rolling back.");
        rollback_component_gates();
        return false;
        }if(!debug_verbose_enabled())continue;
        char b[600];
        SIZE_T p=0;
        append_text(b,&p,"[Hooks] gate installed name=");
        append_text(b,&p,g_elements[i].name);
        append_text(b,&p," vtable=");
        append_hex64(b,&p,(u64)g_componentVtables[i]);
        append_text(b,&p," original=");
        append_hex64(b,&p,(u64)orig);
        line_end(b,&p);
        log_raw(b);
        }return true;
        }
static bool rollback_generic_hooks(){bool ok=true;
    for(s32 i=(s32)kNormalHUDObjectCount-1;i>=0;--i){HUDComponent&d=g_hudComponents[i];
    if(!d.hookInstalled||!d.vtable||!d.original)continue;
    bool restored=restore_ptr_if_owned(d.vtable+7,(void*)&HookGenericShouldBeVisible,(void*)d.original);
    if(!restored){error_log_line("[Hooks] WARNING: generic component rollback could not prove/restore owned slot; predecessor retained for safe forwarding.");
    ok=false;
    }else d.hookInstalled=false;
    }return ok;
    }
static bool install_generic_hooks(){
    u32 targetCount=0u;
    for(u32 i=0;i<kNormalHUDObjectCount;++i){
        HUDComponent&d=g_hudComponents[i];
        if(d.managedIndex>=0)continue;
        ++targetCount;
        if(!d.contractPass||!d.vtable||!d.original){
            error_log_line("[Hooks] generic component precheck failed: target contract/original unavailable; no generic slots modified.");
            return false;
        }
        void*observed=0;
        if(!safe_read(d.vtable+7,&observed)||observed!=(void*)d.original){
            error_log_line("[Hooks] generic component precheck failed: target vslot identity changed; no generic slots modified.");
            return false;
        }
    }
    if(targetCount!=kGenericHookTargetCount){
        error_log_line("[Hooks] generic component precheck failed: descriptor-table target count mismatch; no generic slots modified.");
        return false;
    }
    for(u32 i=0;i<kNormalHUDObjectCount;++i){
        HUDComponent&d=g_hudComponents[i];
        if(d.managedIndex>=0)continue;
        ShouldBeVisibleFn expected=d.original;
        void*orig=0;
        if(!patch_ptr(d.vtable+7,(void*)&HookGenericShouldBeVisible,&orig)){
            error_log_line("[Hooks] generic component patch failed; transactional generic rollback.");
            rollback_generic_hooks();
            return false;
        }
        if(orig!=(void*)expected){
            error_log_line("[Hooks] generic component identity changed during patch; transactional generic rollback.");
            restore_ptr_if_owned(d.vtable+7,(void*)&HookGenericShouldBeVisible,orig);
            rollback_generic_hooks();
            return false;
        }
        d.original=(ShouldBeVisibleFn)orig;
        d.hookInstalled=true;
    }
    if(!g_genericNativeRequestCapability)
        error_log_line("[Compatibility] native positive request proof unavailable: HIDE remains functional; HIDE/SHOW fails open to Bethesda SHOW behavior.");
    if(debug_verbose_enabled()){
        char b[520];SIZE_T p=0;
        append_text(b,&p,"[Hooks] generic component backend=READY count=");
        append_u64(b,&p,targetCount);
        line_end(b,&p);log_raw(b);
    }
    return true;
}

static bool rollback_pa_compass_gate(){
    if(!g_paCompassHookInstalled||!g_paCompassVtable||!g_origPACompassShould)return true;
    bool restored=restore_ptr_if_owned(g_paCompassVtable+7,(void*)&HookPACompassShouldBeVisible,(void*)g_origPACompassShould);
    if(restored){g_paCompassHookInstalled=false;g_origPACompassShould=0;}
    else error_log_line("[Hooks] WARNING: PowerArmorHUDCompass rollback could not prove/restore owned slot; predecessor retained for safe forwarding.");
    return restored;
}
static bool install_pa_compass_gate(){
    if(!g_paCompassContractPass||!g_paCompassVtable){error_log_line("[Hooks] PowerArmorHUDCompass gate UNAVAILABLE: class contract failed; 3D compass control remains active.");
        return false;
        }
    void*observed=0;
    if(!safe_read(g_paCompassVtable+7,&observed)||
       observed!=(void*)g_addresses.crosshairShouldBeVisible){
        error_log_line("[Hooks] PowerArmorHUDCompass gate PRECHECK failed; 3D compass control remains active.");
        return false;
    }
    void*orig=0;
        if(!patch_ptr(g_paCompassVtable+7,(void*)&HookPACompassShouldBeVisible,&orig)){error_log_line("[Hooks] PowerArmorHUDCompass gate patch failed; 3D compass control remains active.");
        return false;
        }
    if(orig!=(void*)g_addresses.crosshairShouldBeVisible){error_log_line("[Hooks] PowerArmorHUDCompass gate identity changed during patch; restoring predecessor.");
        restore_ptr_if_owned(g_paCompassVtable+7,(void*)&HookPACompassShouldBeVisible,orig);
        return false;
        }
    g_origPACompassShould=(ShouldBeVisibleFn)orig;g_paCompassHookInstalled=true;
    if(debug_verbose_enabled()){char b[520];
        SIZE_T p=0;
        append_text(b,&p,"[Hooks] PowerArmorHUDCompass gate installed vtable=");
        append_hex64(b,&p,(u64)g_paCompassVtable);
        append_text(b,&p," original=");
        append_hex64(b,&p,(u64)orig);
        line_end(b,&p);
        log_raw(b);
        }
    return true;
}

static bool install_button_hints_hook(){
    if(!g_buttonHintsCapable){if(g_settings.hideButtonHints&&debug_errors_enabled())error_log_line("[ButtonHints] Scaleform capability unavailable; feature will fail open.");return false;}
    if(!g_buttonBarContractPass||!g_buttonBarVtable||!g_buttonBarExpectedAdvance)return false;
    void*observedAdvance=0;
    if(!safe_read(g_buttonBarVtable+4,&observedAdvance)||observedAdvance!=g_buttonBarExpectedAdvance){
        if(g_settings.hideButtonHints&&debug_errors_enabled())error_log_line("[ButtonHints] ButtonBarMenu AdvanceMovie hook precheck failed; feature will fail open.");return false;}
    void*origAdvance=0;if(!patch_ptr(g_buttonBarVtable+4,(void*)&HookButtonBarAdvance,&origAdvance)){
        if(g_settings.hideButtonHints&&debug_errors_enabled())error_log_line("[ButtonHints] ButtonBarMenu AdvanceMovie hook patch failed; feature will fail open.");return false;}
    g_origButtonBarAdvance=origAdvance;
    if(origAdvance!=g_buttonBarExpectedAdvance){
        bool restoredAdvance=restore_ptr_if_owned(g_buttonBarVtable+4,(void*)&HookButtonBarAdvance,origAdvance);if(restoredAdvance)g_origButtonBarAdvance=0;
        if(g_settings.hideButtonHints&&debug_errors_enabled())error_log_line("[ButtonHints] ButtonBarMenu AdvanceMovie identity changed during patch; optional hook rolled back/fail open.");return false;}
    if(debug_verbose_enabled()){char b[620];SIZE_T p=0;append_text(b,&p,"[ButtonHints] backend=READY vtable=");append_hex64(b,&p,(u64)g_buttonBarVtable);append_text(b,&p," advanceOriginal=");append_hex64(b,&p,(u64)origAdvance);append_text(b,&p," context=UI::IsMenuOpen");line_end(b,&p);log_raw(b);}
    return true;
}

static bool install_advance_hook(){
    if(!g_hudAdvanceContractPass||!g_hudVtable){error_log_line("[Hooks] AdvanceMovie DISABLED: identity contract failed.");return false;}
    void* observed=0;
    if(!safe_read(g_hudVtable+4,&observed)||observed!=(void*)g_addresses.hudAdvanceMovie){
        error_log_line("[Hooks] AdvanceMovie PRECHECK failed: slot identity changed before patching.");return false;
    }
    void* orig=0;
    if(!patch_ptr(g_hudVtable+4,(void*)&HookAdvance,&orig)){error_log_line("[Hooks] AdvanceMovie patch failed.");return false;}
    g_origAdvance=orig;
    if(orig!=(void*)g_addresses.hudAdvanceMovie){
        error_log_line("[Hooks] AdvanceMovie identity changed during patch; restoring observed predecessor.");
        if(restore_ptr_if_owned(g_hudVtable+4,(void*)&HookAdvance,orig))g_origAdvance=0;
        else error_log_line("[Hooks] WARNING: AdvanceMovie restore failed; residual hook retains predecessor for safe forwarding.");
        return false;
    }
    verbose_log_hex("[Hooks] AdvanceMovie installed original=",(u64)orig);
    return true;
}
// Install as one transaction: no partially-controlled HUD is considered READY. Any failed phase rolls
// back every hook already owned by Dynamic HUD; residual hooks only remain when restoration itself fails,
// and those residual hooks retain their predecessor so they continue forwarding/fail-open.
static bool install_hook_backend(){
    if(g_hookBackendState==HookBackendState::Ready)return true;
    if(g_hookBackendState==HookBackendState::Failed)return false;
    if(!install_component_gates()){
        lifecycle_suspend_store_release(1);g_hookBackendState=HookBackendState::Failed;
        error_log_line("[Hooks] backend=FAILED before AdvanceMovie install; no partial HUD backend retained.");
        return false;
    }
    if(!install_generic_hooks()){
        bool rollbackCore=rollback_component_gates();
        lifecycle_suspend_store_release(1);g_hookBackendState=HookBackendState::Failed;
        error_log_line(rollbackCore?"[Hooks] backend=FAILED; generic precheck/patch failed and core gates rolled back.":
                                  "[Hooks] backend=FAILED; rollback incomplete, residual hooks remain fail-open/forwarding.");
        return false;
    }
    bool paCompassGate=install_pa_compass_gate();
    if(!install_advance_hook()){
        bool rollbackPA=rollback_pa_compass_gate();
        bool rollbackGeneric=rollback_generic_hooks();
        bool rollbackCore=rollback_component_gates();
        lifecycle_suspend_store_release(1);g_hookBackendState=HookBackendState::Failed;
        error_log_line((rollbackPA&&rollbackGeneric&&rollbackCore)?"[Hooks] backend=FAILED; component gates rolled back.":
                                                                "[Hooks] backend=FAILED; rollback incomplete, residual hooks remain fail-open/forwarding.");
        return false;
    }
    g_hookBackendState=HookBackendState::Ready;
    install_button_hints_hook();
    if(debug_verbose_enabled()){char b[420];
        SIZE_T p=0;
        append_text(b,&p,"[Hooks] backend=READY: ");
        append_u64(b,&p,kNormalHUDObjectCount);
        append_text(b,&p," normal HUD gates + ");
        if(paCompassGate)append_text(b,&p,"PowerArmorHUDCompass gate + ");
        append_text(b,&p,"AdvanceMovie; ");
        append_u64(b,&p,kPAGroupCount);
        append_text(b,&p," PA groups use scene culling");
        if(!paCompassGate)append_text(b,&p,", PowerArmorHUDCompass SWF gate unavailable");
        append_text(b,&p,".");
        line_end(b,&p);
        log_raw(b);
        }
    return true;
}
static void request_lifecycle_reset(const char* reason,bool suspend){
    if(suspend)lifecycle_suspend_store_release(1);
    reset_hotkey_trigger_states();
    atomic_store_long(&g_lifecycleResetRequested,1);
    if(debug_verbose_enabled()){
        char b[320];SIZE_T p=0;
        append_text(b,&p,"[Lifecycle] reset requested: ");
        append_text(b,&p,reason?reason:"unknown");
        line_end(b,&p);
        log_raw(b);
    }
}
static void OnF4SEMessage(F4SEMessagingInterface::Message* message){
    if(!message)return;
    switch(message->type){
        case kMessage_PreLoadGame:
            drain_log_queue_sync();flush_log_file_sync();request_lifecycle_reset("PreLoadGame",true);break;
        case kMessage_PostLoadGame:
            request_lifecycle_reset(message->data?"PostLoadGame success":"PostLoadGame failed/resume",true);break;
        case kMessage_PreSaveGame:
            drain_log_queue_sync();flush_log_file_sync();verbose_log_line("[Lifecycle] PreSaveGame");break;
        case kMessage_PostSaveGame:
            verbose_log_line("[Lifecycle] PostSaveGame");break;
        case kMessage_NewGame:
            request_lifecycle_reset("NewGame",true);break;
        case kMessage_GameLoaded:
            request_lifecycle_reset("GameLoaded",true);break;
        case kMessage_GameDataReady:
            verbose_log_line(message->data?"[Lifecycle] GameDataReady=true":"[Lifecycle] GameDataReady=false");
            if(message->data)register_hotkey_input_handler();
            break;
        default:break;
    }
}
static bool register_lifecycle_messaging(const F4SEInterface* f4se){
    if(!f4se||!f4se->QueryInterface||!f4se->GetPluginHandle){
        error_log_line("[Lifecycle] F4SE messaging hardening unavailable: interface entrypoints missing.");return false;
    }
    auto* messaging=(F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
    if(!messaging||messaging->interfaceVersion<1u||!messaging->RegisterListener){
        error_log_line("[Lifecycle] F4SE messaging hardening unavailable: messaging interface missing/old.");return false;
    }
    bool ok=messaging->RegisterListener(f4se->GetPluginHandle(),"F4SE",&OnF4SEMessage);
    if(ok)verbose_log_line("[Lifecycle] F4SE messaging hardening=READY.");
    else error_log_line("[Lifecycle] F4SE messaging hardening registration rejected; HUD generation validation remains active.");
    return ok;
}

static __declspec(noinline) bool initialize_runtime(){
    HMODULE game=GetModuleHandleA("Fallout4.exe");if(!game)game=GetModuleHandleA(0);
    if(!game){error_log_line("[Startup] FAILED: Fallout4.exe module not found.");return false;}
    if(!parse_image(game)){error_log_line("[Startup] FAILED: Fallout4.exe PE parsing failed.");return false;}
    if(debug_verbose_enabled()){
        char b[500];SIZE_T p=0;
        append_text(b,&p,"[Startup] image base=");append_hex64(b,&p,(u64)g_image.base);
        append_text(b,&p," size=");append_hex64(b,&p,g_image.size);
        append_text(b,&p," readableSections=");append_u64(b,&p,g_image.readableCount);
        append_text(b,&p," executableSections=");append_u64(b,&p,g_image.executableCount);
        line_end(b,&p);log_raw(b);
    }
    load_address_table();
    if(!g_addresses.scanCompleted){error_log_line("[Startup] Address Library scan incomplete; no hooks installed.");return true;}
    g_customDiscoveryCapable=g_addresses.uiSingleton&&g_addresses.gfxObjectRelease&&g_addresses.gfxGetMember&&g_addresses.gfxInvoke;
    g_customControlCapable=g_customDiscoveryCapable&&g_addresses.gfxSetMember;
    g_buttonHintsCapable=g_addresses.gfxObjectRelease&&g_addresses.gfxGetMember&&g_addresses.gfxSetMember;
    bool discoveryMessageCapable=g_addresses.messageMenuManagerSingleton&&g_addresses.messageMenuManagerCreate;
    if(g_customDiscoveryCapable)verbose_log_line("[CustomHUD] one-shot discovery capability=READY; dormant until the cached standard MCM hotkey is pressed.");
    else error_log_line("[CustomHUD] one-shot discovery capability=UNAVAILABLE; core HUD control remains fail-open.");
    if(discoveryMessageCapable)verbose_log_line("[CustomHUD] discovery result message capability=READY (native MessageMenuManager::Create).");
    else error_log_line("[CustomHUD] discovery result message capability=UNAVAILABLE; INI creation remains functional without the popup.");
    if(g_customControlCapable)verbose_log_line("[CustomHUD] runtime custom-element control capability=READY (native IMenu gate for standalone custom menus + child-level GFx fallback for HUDMenu/native hosts).");
    else error_log_line("[CustomHUD] runtime custom-element control capability=UNAVAILABLE; enabled CustomElements fail open.");
    if(g_buttonHintsCapable)verbose_log_line("[ButtonHints] Scaleform child-visibility capability=READY; optional ButtonBarMenu hook will be validated independently.");
    else if(g_settings.hideButtonHints)error_log_line("[ButtonHints] Scaleform child-visibility capability=UNAVAILABLE; Hide Button Hints will fail open.");
    g_paStateCapabilityPass=g_addresses.playerSingleton&&g_addresses.actorInPowerArmor;
    if(g_paStateCapabilityPass){char b[520];
        SIZE_T p=0;
        append_text(b,&p,"[Compatibility] PowerArmor::ActorInPowerArmor capability=PASS id=");
        append_u64(b,&p,g_profile->ids.actorInPowerArmor);
        append_text(b,&p," address=");
        append_hex64(b,&p,g_addresses.actorInPowerArmor);
        line_end(b,&p);
        verbose_log_raw(b);
        }
    else error_log_line("[Compatibility] PowerArmor::ActorInPowerArmor capability=FAIL; PA groups fail-open/dormant.");
    if(g_addresses.uiSingleton&&g_addresses.uiIsMenuOpen&&g_addresses.fixedStringCtor){
        verbose_log_line("[VATS] production menu-state capability=PASS (UI singleton + IsMenuOpen + BSFixedString ctor).");
    }
    else error_log_line("[VATS] production menu-state capability=UNAVAILABLE; VATS-selected policies will fail-open.");
    validate_static_contracts();
    if(g_addresses.playerSingleton)verbose_log_line("[Compatibility] Player singleton capability=PASS");
    else error_log_line("[Compatibility] Player singleton capability=FAIL");
    if(g_addresses.actorIsSneaking)verbose_log_line("[Compatibility] Actor::IsSneaking capability=PASS");
    else error_log_line("[Compatibility] Actor::IsSneaking capability=FAIL");
    if(g_addresses.crosshairEventSource)verbose_log_line("[Compatibility] Crosshair native event-source identity=PASS");
    else error_log_line("[Compatibility] Crosshair native event-source identity=FAIL");
    QueryPerformanceFrequency(&g_qpcFreq);QueryPerformanceCounter(&g_qpcStart);
    bool backend=install_hook_backend();
    if(!backend)error_log_line("[Startup] visibility policy inactive/fail-open because transactional hook backend is unavailable.");
    verbose_log_line("[Startup] Dynamic HUD 1.0 active.");
    return true;
}

extern "C" __declspec(dllexport) bool F4SEPlugin_Query(const F4SEInterface* f4se,PluginInfo* info){
    if(info){info->infoVersion=1;info->name="DynamicHUD";info->version=kPluginVersion;}
    if(!f4se||!info||f4se->isEditor)return false;
    return select_runtime_profile(f4se->runtimeVersion)!=0;
}
extern "C" BOOL WINAPI DllMain(HMODULE module,DWORD,void*){g_module=module;return TRUE;}
extern "C" __declspec(dllexport) bool F4SEPlugin_Preload(const F4SEInterface*){return true;}
extern "C" __declspec(dllexport) __declspec(noinline) bool F4SEPlugin_Load(const F4SEInterface* f4se){
    if(_InterlockedCompareExchange(&g_started,1,0)!=0)return true;
    g_process=GetCurrentProcess();

    // Open logging before parsing settings so malformed startup keyinput values can be diagnosed.
    // Follow the standard F4SE convention: Documents\My Games\Fallout4\F4SE\DynamicHUD.log.
    wchar_t logPath[512];
    if(build_f4se_log_path(L"DynamicHUD.log",logPath,sizeof(logPath)/sizeof(logPath[0])))
        g_log=CreateFileW(logPath,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    bool asyncLog=start_log_writer();
    error_log_line("Dynamic HUD 1.0");
    if(asyncLog)verbose_log_line("[Logging] async writer=READY; gameplay log calls enqueue only.");
    else error_log_line("[Logging] async writer=FAILED; synchronous fallback active.");

    g_settings=read_settings_from_disk();g_pendingSettings=g_settings;read_custom_elements_from_disk(&g_customSettings);memcpy(&g_pendingCustomSettings,&g_customSettings,sizeof(g_customSettings));
    g_requiredTriggerMask=required_trigger_mask(g_settings,g_customSettings);
    atomic_store_long(&g_debugLogLevel,(long)g_settings.debugLog);
    g_perfTelemetryEnabled=g_settings.performanceTelemetry;
    publish_hotkey_bindings(g_settings,g_customSettings,true);

    if(!f4se){error_log_line("[Startup] FAILED: null F4SE interface.");return false;}
    if(f4se->isEditor){error_log_line("[Startup] FAILED: editor runtime unsupported.");return false;}
    g_runtimeVersion=f4se->runtimeVersion;
    g_profile=select_runtime_profile(g_runtimeVersion);
    if(debug_verbose_enabled()){
        char b[700];SIZE_T p=0;
        append_text(b,&p,"[Compatibility] runtimeVersion=");append_hex64(b,&p,g_runtimeVersion);
        append_text(b,&p," parsed=");append_runtime(b,&p,g_runtimeVersion);
        append_text(b,&p," f4seVersion=");append_hex64(b,&p,f4se->f4seVersion);
        line_end(b,&p);log_raw(b);
    }
    if(!g_profile){error_log_line("[Compatibility] unsupported runtime family; no hooks installed.");return false;}
    if(debug_verbose_enabled()){
        log_raw("[Compatibility] ABI family=");log_line(g_profile->familyName);
        log_settings_snapshot("[Settings] startup",g_settings,g_customSettings);
    }
    register_lifecycle_messaging(f4se);
    bool ok=initialize_runtime();
    if(ok){
        start_settings_watcher();
    }
    drain_log_queue_sync();flush_log_file_sync();
    return ok;
}
