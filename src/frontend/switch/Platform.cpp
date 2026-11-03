// nekoDS Switch platform layer.
// Implements melonDS::Platform with stdio + libnx primitives.
// File layout mirrors the old switch port: local files live under
// sdmc:/switch/nekoDS/. Save-writes are flushed straight through
// (write-through) — no SaveManager thread.

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <string>
#include <switch.h>
#if __has_include("dlink/dlink.h")
#include "dlink/dlink.h"
#elif __has_include("D:/l4t/libnx-dlink/include/dlink/dlink.h")
#include "D:/l4t/libnx-dlink/include/dlink/dlink.h"
#elif __has_include("D:/l4t/neko3d/extern/libnx-dlink/include/dlink/dlink.h")
#include "D:/l4t/neko3d/extern/libnx-dlink/include/dlink/dlink.h"
#else
#include <dlink/dlink.h>
#endif

#include "Platform.h"
#include "SPI_Firmware.h"
#include "SwitchLog.h"

using namespace melonDS;

namespace melonDS::Platform
{

void SignalStop(StopReason reason, void* userdata)
{
    (void)reason; (void)userdata;
}

static std::string ModeString(FileMode mode, bool exists)
{
    std::string m;
    if (mode & FileMode::Append) m += 'a';
    else if (!(mode & FileMode::Write)) m += 'r';
    else if ((mode & FileMode::NoCreate) || ((mode & FileMode::Preserve) && exists)) m += 'r';
    else m += 'w';
    if ((mode & FileMode::ReadWrite) == FileMode::ReadWrite) m += '+';
    if (!(mode & FileMode::Text)) m += 'b';
    return m;
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    if ((mode & (FileMode::ReadWrite | FileMode::Append)) == FileMode::None)
        return nullptr;
    FILE* f = nullptr;
    if (mode & FileMode::NoCreate)
    {
        f = fopen(path.c_str(), "rb");
        if (!f) return nullptr;
        std::string m = ModeString(mode, true);
        f = freopen(path.c_str(), m.c_str(), f);
        return reinterpret_cast<FileHandle*>(f);
    }
    bool exists = false;
    if (mode & FileMode::Preserve)
    {
        FILE* t = fopen(path.c_str(), "rb");
        exists = (t != nullptr);
        if (t) fclose(t);
    }
    std::string m = ModeString(mode, exists);
    f = fopen(path.c_str(), m.c_str());
    return reinterpret_cast<FileHandle*>(f);
}

std::string GetLocalFilePath(const std::string& filename)
{
    if (!filename.empty() && (filename[0] == '/' || filename.find(':') != std::string::npos))
        return filename;
    return "sdmc:/switch/nekoDS/" + filename;
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode)
{
    return OpenFile(GetLocalFilePath(path), mode);
}

bool CloseFile(FileHandle* file)
{
    if (!file) return false;
    return fclose(reinterpret_cast<FILE*>(file)) == 0;
}

bool IsEndOfFile(FileHandle* file)
{
    return feof(reinterpret_cast<FILE*>(file)) != 0;
}

bool FileReadLine(char* str, int count, FileHandle* file)
{
    return fgets(str, count, reinterpret_cast<FILE*>(file)) != nullptr;
}

bool FileExists(const std::string& name)
{
    FILE* f = fopen(name.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool LocalFileExists(const std::string& name)
{
    return FileExists(GetLocalFilePath(name));
}

bool CheckFileWritable(const std::string& filepath)
{
    FILE* f = fopen(filepath.c_str(), "ab");
    if (f) { fclose(f); return true; }
    return false;
}

bool CheckLocalFileWritable(const std::string& name)
{
    return CheckFileWritable(GetLocalFilePath(name));
}

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    int whence = SEEK_SET;
    if (origin == FileSeekOrigin::Current) whence = SEEK_CUR;
    else if (origin == FileSeekOrigin::End) whence = SEEK_END;
    return fseek(reinterpret_cast<FILE*>(file), (long)offset, whence) == 0;
}

void FileRewind(FileHandle* file)
{
    rewind(reinterpret_cast<FILE*>(file));
}

u64 FilePosition(FileHandle* file)
{
    return (u64)ftell(reinterpret_cast<FILE*>(file));
}

u64 FileRead(void* data, u64 size, u64 count, FileHandle* file)
{
    if (!size || !count) return 0;
    return fread(data, (size_t)size, (size_t)count, reinterpret_cast<FILE*>(file));
}

bool FileFlush(FileHandle* file)
{
    return fflush(reinterpret_cast<FILE*>(file)) == 0;
}

u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file)
{
    if (!size || !count) return 0;
    return fwrite(data, (size_t)size, (size_t)count, reinterpret_cast<FILE*>(file));
}

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    if (!fmt) return 0;
    va_list args;
    va_start(args, fmt);
    int n = vfprintf(reinterpret_cast<FILE*>(file), fmt, args);
    va_end(args);
    return n > 0 ? (u64)n : 0;
}

u64 FileLength(FileHandle* file)
{
    FILE* f = reinterpret_cast<FILE*>(file);
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, cur, SEEK_SET);
    return len >= 0 ? (u64)len : 0;
}

void Log(LogLevel level, const char* fmt, ...)
{
    (void)level;
    if (!fmt) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    size_t n = strlen(buf);
    while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
    SwitchLog::Log("[core] %s", buf);
}

struct ThreadWrap { ::Thread t; std::function<void()> fn; };
static void ThreadEntry(void* p)
{
    ThreadWrap* w = (ThreadWrap*)p;
    w->fn();
}
Thread* Thread_Create(std::function<void()> func)
{
    ThreadWrap* w = new ThreadWrap();
    w->fn = std::move(func);
    threadCreate(&w->t, ThreadEntry, w, nullptr, 0x10000, 0x2C, -2);
    threadStart(&w->t);
    return (Thread*)w;
}
void Thread_Free(Thread* thread) { delete (ThreadWrap*)thread; }
void Thread_Wait(Thread* thread)
{
    ThreadWrap* w = (ThreadWrap*)thread;
    threadWaitForExit(&w->t);
    threadClose(&w->t);
}

struct SemWrap { ::Semaphore s; };
Semaphore* Semaphore_Create() { SemWrap* w = new SemWrap(); semaphoreInit(&w->s, 0); return (Semaphore*)w; }
void Semaphore_Free(Semaphore* sema) { delete (SemWrap*)sema; }
void Semaphore_Reset(Semaphore* sema)
{
    SemWrap* w = (SemWrap*)sema;
    while (semaphoreTryWait(&w->s)) {}
}
void Semaphore_Wait(Semaphore* sema) { semaphoreWait(&((SemWrap*)sema)->s); }
bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    SemWrap* w = (SemWrap*)sema;
    if (timeout_ms <= 0) return semaphoreTryWait(&w->s);
    u64 deadline = armTicksToNs(armGetSystemTick()) + (u64)timeout_ms * 1000000ULL;
    while (!semaphoreTryWait(&w->s))
    {
        if (armTicksToNs(armGetSystemTick()) >= deadline) return false;
        svcSleepThread(1000000);
    }
    return true;
}
void Semaphore_Post(Semaphore* sema, int count)
{
    SemWrap* w = (SemWrap*)sema;
    for (int i = 0; i < count; i++) semaphoreSignal(&w->s);
}

struct MutexWrap { ::Mutex m; };
Mutex* Mutex_Create() { MutexWrap* w = new MutexWrap(); mutexInit(&w->m); return (Mutex*)w; }
void Mutex_Free(Mutex* mutex) { delete (MutexWrap*)mutex; }
void Mutex_Lock(Mutex* mutex) { mutexLock(&((MutexWrap*)mutex)->m); }
void Mutex_Unlock(Mutex* mutex) { mutexUnlock(&((MutexWrap*)mutex)->m); }
bool Mutex_TryLock(Mutex* mutex) { return mutexTryLock(&((MutexWrap*)mutex)->m); }

void Sleep(u64 usecs) { svcSleepThread(usecs * 1000ULL); }
u64 GetMSCount() { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }
u64 GetUSCount() { return armTicksToNs(armGetSystemTick()) / 1000ULL; }

// Write-through saves: path comes from userdata set by the frontend.
static void WriteSave(const char* path, const u8* data, u32 len)
{
    if (!path || !data || !len) return;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, len, f);
    fclose(f);
}
void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    (void)writeoffset; (void)writelen;
    WriteSave((const char*)userdata, savedata, savelen);
}
void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata)
{
    (void)writeoffset; (void)writelen;
    WriteSave((const char*)userdata, savedata, savelen);
}
void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata)
{
    (void)writeoffset; (void)writelen; (void)userdata;
    FileHandle* f = OpenLocalFile("firmware.bin", FileMode::Write);
    if (!f) return;
    const u8* data = firmware.Buffer();
    FileWrite(data, 1, firmware.Length(), f);
    CloseFile(f);
}
void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata)
{
    (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second; (void)userdata;
}

// Network / MP stubs (same as old port).
void MP_Begin(void* userdata) { (void)userdata; }
void MP_End(void* userdata) { (void)userdata; }
int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata) { (void)data; (void)len; (void)timestamp; (void)userdata; return 0; }
int MP_RecvPacket(u8* data, u64* timestamp, void* userdata) { (void)data; (void)timestamp; (void)userdata; return 0; }
int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { (void)data; (void)len; (void)timestamp; (void)userdata; return 0; }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { (void)data; (void)len; (void)timestamp; (void)aid; (void)userdata; return 0; }
int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { (void)data; (void)len; (void)timestamp; (void)userdata; return 0; }
int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { (void)data; (void)timestamp; (void)userdata; return 0; }
u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { (void)data; (void)timestamp; (void)aidmask; (void)userdata; return 0; }

int Net_SendPacket(u8* data, int len, void* userdata) { (void)data; (void)len; (void)userdata; return 0; }
int Net_RecvPacket(u8* data, void* userdata) { (void)data; (void)userdata; return 0; }

void Camera_Start(int num, void* userdata) { (void)num; (void)userdata; }
void Camera_Stop(int num, void* userdata) { (void)num; (void)userdata; }
void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata)
{
    (void)num; (void)userdata;
    // black frame
    for (int i = 0; i < width * height; i++)
        frame[i] = yuv ? 0x80008000 : 0xFF000000;
}

void Mic_Start(void* userdata) { (void)userdata; }
void Mic_Stop(void* userdata) { (void)userdata; }
int Mic_ReadInput(s16* data, int maxlength, void* userdata)
{
    (void)userdata;
    memset(data, 0, maxlength * sizeof(s16));
    return maxlength;
}

// AAC stub (DSi DSP HLE unavailable without faad).
struct AACDecoder {};
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder* dec) { (void)dec; }
bool AAC_Configure(AACDecoder* dec, int frequency, int channels) { (void)dec; (void)frequency; (void)channels; return false; }
bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen) { (void)dec; (void)input; (void)inputlen; (void)output; (void)outputlen; return false; }

bool Addon_KeyDown(KeyType type, void* userdata) { (void)type; (void)userdata; return false; }
void Addon_RumbleStart(u32 len, void* userdata) { (void)len; (void)userdata; }
void Addon_RumbleStop(void* userdata) { (void)userdata; }
float Addon_MotionQuery(MotionQueryType type, void* userdata) { (void)type; (void)userdata; return 0.0f; }

struct DynamicLibrary { void* h; };
DynamicLibrary* DynamicLibrary_Load(const char* lib)
{
    void* h = dlopen(lib, RTLD_LOCAL);
    if (!h) return nullptr;
    DynamicLibrary* d = new DynamicLibrary(); d->h = h;
    return d;
}
void DynamicLibrary_Unload(DynamicLibrary* lib)
{
    if (!lib) return;
    dlclose(lib->h);
    delete lib;
}
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name)
{
    if (!lib) return nullptr;
    return dlsym(lib->h, name);
}

} // namespace melonDS::Platform
