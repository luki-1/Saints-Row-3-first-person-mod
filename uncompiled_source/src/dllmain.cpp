// SRTT First Person - a toggleable first-person camera for Saints Row: The Third (2011 PC release),
// plus an optional level-of-detail distance multiplier.
//
// Built as a dinput8.dll proxy: both SaintsRowTheThird.exe (DX9) and SaintsRowTheThird_DX11.exe
// import DirectInput8Create, so Windows loads this DLL from the game folder before the game starts.
// Every dinput8 export is forwarded to the real system DLL.
//
// How the first-person camera works:
//  * The free camera (on foot and in vehicles) is driven by the "submodes" of camera_free.xtbl. The
//    game parses them into a static table with, per submode, a look-at offset, pitch limits, FOV, orbit
//    distance and shoulder shift; vehicles write their own values from vehicle_cameras.xtbl into their
//    submodes when you get in. The game's scoped-weapon "zoom" submode is already a first-person camera:
//    eye-height look-at, ~0 distance, no shift.
//  * While first person is on, the on-foot and vehicle submodes get zoom-style values (vehicles keep
//    their own pivot). Values the game writes into those submodes meanwhile are picked up so switching
//    back restores them. The camera's submode controller caches the active submode's values, so the
//    cached copies are updated on toggle to make the switch instant.
//  * That camera pivots on a fixed point, which the head moves away from while walking, sitting in a
//    car, etc. So after the game computes the camera each frame it is moved to the player's animated
//    "head" tag, slightly up and forward to the eyes, easing in and out with the game's own blends.
//  * The camera's heading is kept in world space. While driving, the game swings it back behind the
//    vehicle (and locks it behind some aircraft); in first person that is switched off and the heading
//    is turned by however much the vehicle turned, so the view stays wherever it was pointed.
//
// Level of detail (LODScale): the game has several independent LOD systems, each scaled separately:
// character LOD distances, the level-object fade categories (built-in and fade_categories.xtbl),
// instanced meshes (LOD range selection and shader fade parameters), vehicle LOD ratios and item LODs.
//
// Game code and data are located by byte signatures (validated against both executables), so nothing
// is hard-coded per build. If anything does not match, the mod logs why and leaves that part alone.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD_VERSION "1.3"

static const float kPi = 3.14159265f;
static const float kTwoPi = 6.28318531f;
static const float kDegToRad = 0.017453292f;
static const float kRadToDeg = 57.29578f;
static const float kMinNearClip = 0.15f;  // the game's own lower limit for the near clip distance

// --------------------------------------------------------------------------------------------------
// Log and settings
// --------------------------------------------------------------------------------------------------

static char g_modDir[MAX_PATH];  // folder this DLL was loaded from (the game folder)
static FILE* g_log;

static void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

struct Config
{
    int toggleKey;          // virtual-key code
    bool startEnabled;
    bool vehicles;          // first person in vehicles too
    bool vehicleAutoCenter; // keep the game's swing back behind the vehicle in first person
    float eyeHeight;       // look-at height above the feet while standing (m); fallback for the head
    float crouchEyeHeight;  // ... while crouching
    bool attachToHead;      // follow the animated head instead of a fixed point
    float headUp;           // eye offset from the head tag (m)
    float headForward;      // ... along the horizontal view direction
    float headSmoothing;    // seconds; damps head bob, 0 = off
    float fov;              // degrees; 0 keeps the game's value
    float sprintFov;
    float aimFov;
    float vehicleFov;
    float lookUpLimit;      // degrees; 0 keeps the game's limit
    float lookDownLimit;
    float nearClip;         // metres; 0 leaves the near clip plane alone
    float lodScale;         // level-of-detail distance multiplier; 1 = unchanged
    bool log;
    bool debug;
};

static Config g_cfg;

static void IniString(const char* ini, const char* key, const char* def, char* out, DWORD size)
{
    GetPrivateProfileStringA("FirstPerson", key, def, out, size, ini);
    // allow trailing comments: "0.2 ; metres"
    char* semi = strchr(out, ';');
    if (semi)
        *semi = 0;
    size_t n = strlen(out);
    while (n && isspace((unsigned char)out[n - 1]))
        out[--n] = 0;
}

static float IniFloat(const char* ini, const char* key, float def)
{
    char defStr[32], buf[64];
    sprintf_s(defStr, "%g", def);
    IniString(ini, key, defStr, buf, sizeof buf);
    return buf[0] ? (float)atof(buf) : def;
}

static int IniInt(const char* ini, const char* key, int def)
{
    char defStr[32], buf[64];
    sprintf_s(defStr, "%d", def);
    IniString(ini, key, defStr, buf, sizeof buf);
    return buf[0] ? (int)strtol(buf, NULL, 0) : def;
}

// "L", "F5" style names are too ambiguous across layouts; accept a single letter/digit or a VK code.
static int IniKey(const char* ini, const char* key, int def)
{
    char buf[64];
    IniString(ini, key, "", buf, sizeof buf);
    if (!buf[0])
        return def;
    if (buf[1] == 0 && isalnum((unsigned char)buf[0]))
        return toupper((unsigned char)buf[0]);
    int vk = (int)strtol(buf, NULL, 0);
    return (vk > 0 && vk < 256) ? vk : def;
}

static void LoadConfig()
{
    char ini[MAX_PATH];
    sprintf_s(ini, "%s\\SRTT_FirstPerson.ini", g_modDir);
    g_cfg.toggleKey = IniKey(ini, "ToggleKey", 'L');
    g_cfg.startEnabled = IniInt(ini, "StartEnabled", 0) != 0;
    g_cfg.vehicles = IniInt(ini, "VehicleFirstPerson", 1) != 0;
    g_cfg.vehicleAutoCenter = IniInt(ini, "VehicleAutoCenter", 0) != 0;
    g_cfg.eyeHeight = IniFloat(ini, "EyeHeight", 1.60f);
    g_cfg.crouchEyeHeight = IniFloat(ini, "CrouchEyeHeight", 1.30f);
    g_cfg.attachToHead = IniInt(ini, "AttachToHead", 1) != 0;
    g_cfg.headUp = IniFloat(ini, "HeadUp", 0.08f);
    g_cfg.headForward = IniFloat(ini, "HeadForward", 0.10f);
    g_cfg.headSmoothing = IniFloat(ini, "HeadSmoothing", 0.08f);
    g_cfg.fov = IniFloat(ini, "FOV", 82.8f);
    g_cfg.sprintFov = IniFloat(ini, "SprintFOV", 91.08f);
    g_cfg.aimFov = IniFloat(ini, "AimFOV", 55.2f);
    g_cfg.vehicleFov = IniFloat(ini, "VehicleFOV", 82.8f);
    g_cfg.lookUpLimit = IniFloat(ini, "LookUpLimit", 80.0f);
    g_cfg.lookDownLimit = IniFloat(ini, "LookDownLimit", 80.0f);
    g_cfg.nearClip = IniFloat(ini, "NearClip", 0.15f);
    if (g_cfg.nearClip > 0.0f && g_cfg.nearClip < kMinNearClip)
        g_cfg.nearClip = kMinNearClip;
    g_cfg.lodScale = IniFloat(ini, "LODScale", 4.0f);
    if (!(g_cfg.lodScale >= 0.25f))
        g_cfg.lodScale = 1.0f;
    if (g_cfg.lodScale > 16.0f)
        g_cfg.lodScale = 16.0f;
    g_cfg.log = IniInt(ini, "Log", 1) != 0;
    g_cfg.debug = IniInt(ini, "Debug", 0) != 0;
}

// --------------------------------------------------------------------------------------------------
// dinput8.dll forwarding
// --------------------------------------------------------------------------------------------------

static HMODULE g_realDinput;

static FARPROC RealDinput(const char* name)
{
    if (!g_realDinput)
    {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for this 32-bit process
        if (n == 0 || n > MAX_PATH - 16)
            return NULL;
        strcat_s(path, "\\dinput8.dll");
        g_realDinput = LoadLibraryA(path);
        if (!g_realDinput)
        {
            Log("ERROR: could not load %s (error %lu)", path, GetLastError());
            return NULL;
        }
    }
    return GetProcAddress(g_realDinput, name);
}

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, void* outer)
{
    typedef HRESULT(WINAPI * Fn)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
    Fn fn = (Fn)RealDinput("DirectInput8Create");
    return fn ? fn(inst, version, iid, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)RealDinput("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

extern "C" HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out)
{
    typedef HRESULT(WINAPI * Fn)(REFCLSID, REFIID, LPVOID*);
    Fn fn = (Fn)RealDinput("DllGetClassObject");
    return fn ? fn(clsid, iid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI Proxy_DllRegisterServer()
{
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)RealDinput("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" HRESULT WINAPI Proxy_DllUnregisterServer()
{
    typedef HRESULT(WINAPI * Fn)();
    Fn fn = (Fn)RealDinput("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

extern "C" const void* WINAPI Proxy_GetdfDIJoystick()
{
    typedef const void*(WINAPI * Fn)();
    Fn fn = (Fn)RealDinput("GetdfDIJoystick");
    return fn ? fn() : NULL;
}

// --------------------------------------------------------------------------------------------------
// Signatures
// --------------------------------------------------------------------------------------------------

// Free-camera submode controller update: void __thiscall(controller, float dt). The first 10 bytes
// (sub esp,14h / push ebx / mov ebx,[abs]) are position independent and get relocated to a trampoline.
static const char* kSigCtrlUpdate = "83 EC 14 53 8B 1D ?? ?? ?? ?? 55 56 8B 35 ?? ?? ?? ?? 57 8B F9 8D AF 40 01 00 00 8B CD";
static const size_t kCtrlUpdateStolen = 10;

// shl eax,6 / add eax,<submode table> / movaps xmm0,[eax] / movaps [esi+50h],xmm0 / ...
static const char* kSigSubmodeTable = "C1 E0 06 05 ?? ?? ?? ?? 0F 28 00 0F 29 46 50 D9 40 10 D9 5E 60";

// camera_free.xtbl parser: lookup of a submode name in the 61-entry name table
static const char* kSigSubmodeNames = "6A 01 53 6A 3D 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 10 83 F8 FF";

// void __cdecl set_near_clip(float value, bool flag): clamps to >= 0.15 and stores the near clip
// distance (read by the renderer every frame) at the address encoded at +30.
static const char* kSigSetNearClip =
    "F3 0F 10 44 24 04 F3 0F 10 0D ?? ?? ?? ?? 0F 2F C1 77 03 0F 28 C1 8A 44 24 08 F3 0F 11 05 ?? ?? ?? ?? A2 ?? ?? ?? ?? C3";
static const size_t kNearClipVarOffset = 30;

// Free camera update: calls the main camera computation, after which the camera position and
// orientation for this frame are final. The movaps after the call reads the camera position (vec4);
// the orientation rows follow it: right +0x40, up +0x50, forward +0x60.
static const char* kSigCameraUpdateCall =
    "80 3D ?? ?? ?? ?? 00 75 11 D9 45 08 51 D9 1C 24 E8 ?? ?? ?? ?? 83 C4 04 EB 07 C6 05 ?? ?? ?? ?? 00 0F 28 05 ?? ?? ?? ??";
static const size_t kCameraUpdateCallOffset = 16;
static const size_t kCameraPosOffset = 36;
static const int kCamForwardRow = 0x60 / 4;  // in floats from the camera position

// vec4* get_tag_world_pos(const char* tag [ecx], vec4* out, u32 handle_lo, u32 handle_hi), caller pops.
// Resolves a rig tag such as "head" on an object to its current animated world position. An empty
// name, or a tag the rig does not have, gives the object's own position. `out` must be 16-byte aligned.
static const char* kSigGetTagPos =
    "55 8B EC 83 E4 F0 83 EC 74 A1 ?? ?? ?? ?? 33 C4 89 44 24 70 8B 45 0C 53 8B 5D 08 56 57 8B F9 8B 4D 10 "
    "89 44 24 14 0B C1 89 4C 24 18 74 3E 68 ?? ?? ?? ?? 8D 44 24 18 50 B9 ?? ?? ?? ?? E8";

// Main camera update: call get_local_player (mov eax,[player]; ret) / mov esi,eax / ...
static const char* kSigPlayerCall =
    "E8 ?? ?? ?? ?? 8B F0 89 74 24 24 85 FF 0F 84 ?? ?? ?? ?? 0F 28 05 ?? ?? ?? ?? 0F 28 0D ?? ?? ?? ??";
static const int kObjectHandle = 0x08;  // 64-bit object handle (lo, hi)

// Free-camera orbit swing while following a vehicle: void(float dt, context*) with the orbit state in
// edi, caller pops; it turns the heading back towards the direction of travel and eases the pitch back
// to level. It starts by resolving the camera target handle to an object: handle lo/hi (+0x36/+0x3C),
// object type (+0x4D), object manager (+0x57), lookup call (+0x5B, thiscall(manager, &handle, type))
// and the object type-info table (+0x73). The first 9 bytes (frame setup) move to a trampoline.
static const char* kSigVehicleSwing =
    "55 8B EC 83 E4 F0 83 EC 68 A1 ?? ?? ?? ?? 33 C4 89 44 24 64 53 8B 5D 0C 56 E8 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? "
    "83 F8 02 74 0A F3 0F 11 47 60 F3 0F 11 47 64 A1 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 89 44 24 10 0B C1 89 4C 24 14 74 ?? "
    "68 ?? ?? ?? ?? 8D 44 24 14 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B F0 85 F6 74 ?? F6 46 33 10 75 ?? 0F B6 4E 34 "
    "8B 14 8D ?? ?? ?? ??";
static const size_t kVehicleSwingStolen = 9;
static const int kObjFlags = 0x33;          // byte; 0x10 = being deleted
static const int kObjType = 0x34;           // byte; index into the type-info table
static const int kTypeInfoFlags6 = 0x06;    // byte in the type info; 0x80 = vehicle

// Orbit update while the camera follows a vehicle: cmp byte [esp+13h],0 (the local player drives) /
// je <not driving> / ... For some vehicles (aircraft) a driving player gets the heading locked behind
// the vehicle. The Euler-to-matrix call at +0x34 is only used by the offline test.
static const char* kSigVehicleLock = "80 7C 24 13 00 0F 84 ?? ?? ?? ?? F3 0F 10 47 04 F3 0F 10 57 10 F3 0F 10 5F 24";
static const size_t kVehicleLockPatch = 11;

// End of the orbit update: [offset] = heading - yaw of the camera target, computed with
// get_target_matrix (esi = 16-byte aligned out matrix, handle lo, hi; caller pops) at +24 and
// matrix_angles (thiscall(matrix, vec4* out); yaw in out[0]) at +34. The heading address is at +46.
static const char* kSigTargetHeading =
    "8B 0D ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 56 8D 44 24 10 50 51 52 8D 74 24 2C E8 ?? ?? ?? ?? 83 C4 08 8B C8 "
    "E8 ?? ?? ?? ?? 0F 28 00 F3 0F 10 0D ?? ?? ?? ??";

// Character definition parser: copies the default character LOD distances (4 floats, +16 and +32)
// and adds each character's LOD_Distance offset.
static const char* kSigCharLodDefaults =
    "F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 03 F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 86 84 00 00 00 "
    "F3 0F 7E 05 ?? ?? ?? ?? 68 ?? ?? ?? ?? 66 0F D6 86 8C 00 00 00";

// Level objects point at a fade category {medium_lod^2, low_lod^2, fade^2}: ids 0-4 use a built-in
// read-only table (address at +17), other ids records of fade_categories.xtbl.
static const char* kSigFadeCategoryGet = "8B 41 48 83 C1 48 83 F8 05 73 0D 8D 04 40 8D 14 85 ?? ?? ?? ?? 89 11 C3 50 51 E8";
static const int kFadeBuiltinCount = 5;

// fade_categories.xtbl loader, void __cdecl(void); it refills {hash, medium^2, low^2, fade^2} records.
static const char* kSigFadeLoader =
    "55 8B EC 83 E4 F8 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 83 EC 20 53 56 57 A1 ?? ?? ?? ?? "
    "33 C4 50 8D 44 24 30 64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 7C 0A C7 05 ?? ?? ?? ?? 00 00 00 00 "
    "6A 01 6A 00 68 ?? ?? ?? ?? C7 05 ?? ?? ?? ?? FF FF FF FF E8";
static const size_t kFadeLoaderStolen = 6;
// In the loader: shl eax,4 / add eax,[records] / mov [count],ecx / movq [eax],xmm0
static const char* kSigFadeTableStore = "C1 E0 04 03 05 ?? ?? ?? ?? 89 0D ?? ?? ?? ?? 66 0F D6 00";

// Instanced meshes (rl_d3d_clone_mesh_instance): the LOD range pick gets squared near/far camera
// distances from a helper (inputs in edi/esi, two float* outputs, caller pops) ...
static const char* kSigCloneDistCall =
    "8D 4C 24 10 51 8D 54 24 1C 8D 7E 28 8D 75 40 52 E8 ?? ?? ?? ?? 8B 85 08 01 00 00 8B 8D 0C 01 00 00";
static const size_t kCloneDistCallOffset = 16;
static const char* kSigCloneDistFn = "83 EC 50 F3 0F 10 07 F3 0F 10 0E";
// ... and void __thiscall(instance, int lod) turns a LOD's distance range into shader fade parameters
// {1/(end-start), -start/(end-start)} x2 at instance+0x90.
static const char* kSigCloneFadeFn = "83 EC 18 80 3D ?? ?? ?? ?? 00 8B 44 24 1C 8B 91 0C 01 00 00";
static const size_t kCloneFadeStolen = 10;
static const int kCloneFadeParams = 0x90;

// Vehicle info LOD_DistanceRatio list (distance / vehicle radius per LOD): bool parse(info, bool
// use_defaults) with the XML node in eax and a template info in ecx. Without a list of its own a
// vehicle copies the global defaults or its template's list.
static const char* kSigVehicleLods =
    "53 56 57 8B 7C 24 10 68 ?? ?? ?? ?? 50 8B F1 E8 ?? ?? ?? ?? 8B D8 83 C4 08 85 DB 74 5A "
    "68 ?? ?? ?? ?? 53 E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 53 89 87 B0 04 00 00";
static const size_t kVehicleLodsStolen = 7;
static const char* kSigVehicleLodDefaults =
    "8B 0D ?? ?? ?? ?? 33 C0 89 8F B0 04 00 00 39 05 ?? ?? ?? ?? 76 54 8D 8F B4 04 00 00 "
    "8D 9B 00 00 00 00 D9 04 85 ?? ?? ?? ??";
static const int kVehicleLodCount = 0x4B0;
static const int kVehicleLodRatios = 0x4B4;
static const int kMaxLods = 16;

// items_3d.xtbl LODs: parse_float(&item->lod_distance[i], node, 0) inside the item parser.
static const char* kSigItemLodParse = "8B 86 94 00 00 00 6A 00 8D 8C 86 98 00 00 00 40 57 51 89 86 94 00 00 00 E8";
static const size_t kItemLodParseCallOffset = 24;

// --------------------------------------------------------------------------------------------------
// Scanning and patching
// --------------------------------------------------------------------------------------------------

static uint8_t* g_imageBase;
static size_t g_imageSize;
static uint8_t* g_text;
static size_t g_textSize;

static bool InImage(const void* p, size_t len)
{
    const uint8_t* b = (const uint8_t*)p;
    return b >= g_imageBase && b + len <= g_imageBase + g_imageSize;
}

static bool ParsePattern(const char* pat, uint8_t* bytes, bool* mask, size_t* len, size_t cap)
{
    size_t n = 0;
    for (const char* p = pat; *p;)
    {
        if (*p == ' ')
        {
            p++;
            continue;
        }
        if (n >= cap)
            return false;
        if (p[0] == '?' && p[1] == '?')
        {
            mask[n] = false;
            bytes[n++] = 0;
            p += 2;
            continue;
        }
        char hex[3] = {p[0], p[1], 0};
        char* end;
        long v = strtol(hex, &end, 16);
        if (end != hex + 2)
            return false;
        mask[n] = true;
        bytes[n++] = (uint8_t)v;
        p += 2;
    }
    *len = n;
    return n > 0;
}

// Returns the single match of `pat` in [start, start+size), or NULL if it occurs zero or several times.
static uint8_t* FindUnique(uint8_t* start, size_t size, const char* pat, int* hitsOut)
{
    uint8_t bytes[128];
    bool mask[128];
    size_t len;
    *hitsOut = 0;
    if (!ParsePattern(pat, bytes, mask, &len, sizeof bytes) || len > size || !mask[0])
        return NULL;
    uint8_t* found = NULL;
    uint8_t* end = start + size - len;
    for (uint8_t* p = start; p <= end; p++)
    {
        p = (uint8_t*)memchr(p, bytes[0], end - p + 1);
        if (!p)
            break;
        size_t i = 1;
        while (i < len && (!mask[i] || p[i] == bytes[i]))
            i++;
        if (i == len)
        {
            if (++*hitsOut == 1)
                found = p;
        }
    }
    return *hitsOut == 1 ? found : NULL;
}

static uint8_t* Find(const char* what, const char* sig)
{
    int hits;
    uint8_t* p = FindUnique(g_text, g_textSize, sig, &hits);
    if (!p)
        Log("ERROR: signature '%s' matched %d times (expected 1)", what, hits);
    return p;
}

template <typename T>
static T ReadPtr(const uint8_t* at)
{
    T p;
    memcpy(&p, at, sizeof p);
    return p;
}

static uint8_t* CallTarget(uint8_t* call)
{
    return call[0] == 0xE8 ? call + 5 + ReadPtr<int32_t>(call + 1) : NULL;
}

static bool WriteCode(uint8_t* at, const void* src, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(at, src, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

static bool RedirectCall(uint8_t* call, const void* dest)
{
    int32_t rel = (int32_t)((const uint8_t*)dest - (call + 5));
    return WriteCode(call + 1, &rel, 4);
}

// Copies the first `stolen` bytes of `target` into an executable stub followed by a jump back.
static void* MakeTrampoline(uint8_t* target, size_t stolen)
{
    uint8_t* t = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t)
        return NULL;
    memcpy(t, target, stolen);
    t[stolen] = 0xE9;
    int32_t rel = (int32_t)((target + stolen) - (t + stolen + 5));
    memcpy(t + stolen + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    return t;
}

static bool InstallJump(uint8_t* at, const void* dest, size_t stolen)
{
    uint8_t buf[16];
    buf[0] = 0xE9;
    int32_t rel = (int32_t)((const uint8_t*)dest - (at + 5));
    memcpy(buf + 1, &rel, 4);
    for (size_t i = 5; i < stolen; i++)
        buf[i] = 0x90;
    return WriteCode(at, buf, stolen);
}

// Detours the function at `at` to `hook`; returns the trampoline that runs the original, or NULL.
static void* Detour(uint8_t* at, const void* hook, size_t stolen)
{
    void* tramp = MakeTrampoline(at, stolen);
    if (tramp && !InstallJump(at, hook, stolen))
    {
        VirtualFree(tramp, 0, MEM_RELEASE);
        tramp = NULL;
    }
    return tramp;
}

// --------------------------------------------------------------------------------------------------
// Camera submodes
// --------------------------------------------------------------------------------------------------

// Submode table entry (0x40 bytes). The controller keeps three copies of the same first 0x38 bytes:
// the blend source (+0x50), the previous frame (+0x90) and the current values (+0xD0).
static const int kNumSubmodes = 61;
static const int kEntrySize = 0x40;
static const int kCtrlSubmode = 0x10;  // int: submode the controller is in / blending to
static const int kCtrlBlend = 0x1C;    // float: blend progress towards that submode, 0..1
static const int kCtrlBlocks[] = {0x50, 0x90, 0xD0};

enum Field
{
    F_LOOK_X,     // look-at offset from the target, in its frame (Y is scaled by body size)
    F_LOOK_Y,
    F_LOOK_Z,
    F_MIN_PITCH,  // radians; the camera's elevation, positive = looking down (game: -75 to 55 on foot)
    F_MAX_PITCH,
    F_FOV,        // degrees
    F_ANGLE,      // elevation offset of the orbit, atan(y_dist / z_dist)
    F_DIST,       // orbit distance, sqrt(z_dist^2 + y_dist^2)
    F_XSHIFT,     // over-the-shoulder shift
    F_COUNT
};
static const int kFieldOffset[F_COUNT] = {0x00, 0x04, 0x08, 0x10, 0x14, 0x20, 0x24, 0x28, 0x34};

enum Category
{
    CAT_STAND,
    CAT_CROUCH,
    CAT_SPRINT,
    CAT_AIM,
    CAT_AIM_CROUCH,
    CAT_VEHICLE,      // driving: keeps the vehicle's own pivot
    CAT_VEHICLE_AIM,  // aiming from a vehicle: also keeps the weapon camera's pitch limits and FOV
};

struct FirstPersonSubmode
{
    int index;
    const char* name;  // checked against the game's own name table at startup
    Category cat;
};

// Submodes that switch to first person. Everything else (the zoom modes that are already first
// person, swimming, ragdoll, falling through the sky, human shields, takedowns, remote-controlled
// vehicles, co-op downed/spectator, ...) keeps the game's camera.
static const FirstPersonSubmode kModes[] = {
    {0, "exterior close", CAT_STAND},
    {1, "interior close", CAT_STAND},
    {2, "interior sprint", CAT_SPRINT},
    {16, "fence", CAT_STAND},
    {18, "falling", CAT_STAND},
    {20, "fine aim", CAT_AIM},
    {21, "fine aim underslung", CAT_AIM},
    {22, "fine aim crouch", CAT_AIM_CROUCH},
    {23, "melee lock", CAT_STAND},
    {27, "riot shield", CAT_CROUCH},
    {28, "riot shield fine aim", CAT_AIM_CROUCH},
    {31, "sprint", CAT_SPRINT},
    {32, "crouch", CAT_CROUCH},
    {33, "crouch interior", CAT_CROUCH},
    {56, "Script", CAT_STAND},
    {57, "Script Fine Aim", CAT_AIM},
    {58, "Script Crouch", CAT_CROUCH},
    {59, "Script Fine Aim Crouch", CAT_AIM_CROUCH},
    {60, "MAS Fine Aim", CAT_AIM},
    {3, "vehicle driver", CAT_VEHICLE},
    {4, "vehicle driver alt", CAT_VEHICLE},
    {5, "watercraft driver", CAT_VEHICLE},
    {6, "helicopter driver", CAT_VEHICLE},
    {10, "airplane driver", CAT_VEHICLE},
    {11, "tank driver", CAT_VEHICLE},
    {7, "vehicle aim", CAT_VEHICLE_AIM},
    {8, "vehicle fine aim", CAT_VEHICLE_AIM},
    {24, "fine aim vehicle", CAT_VEHICLE_AIM},
};
static const int kNumModes = sizeof kModes / sizeof kModes[0];

static bool IsVehicleCategory(Category c)
{
    return c == CAT_VEHICLE || c == CAT_VEHICLE_AIM;
}

// Fields first person overrides for a category.
static unsigned FieldMask(Category c)
{
    const unsigned all = (1u << F_COUNT) - 1;
    const unsigned lookAt = (1u << F_LOOK_X) | (1u << F_LOOK_Y) | (1u << F_LOOK_Z);
    if (c == CAT_VEHICLE)
        return all & ~lookAt;
    if (c == CAT_VEHICLE_AIM)
        return (1u << F_ANGLE) | (1u << F_DIST) | (1u << F_XSHIFT);
    return all;
}

static uint8_t* g_table;              // camera_free.xtbl submode table
static const char* const* g_names;    // the game's submode name table
static int g_modeOf[kNumSubmodes];    // submode index -> index into kModes, or -1
static bool g_modeOn[kNumModes];      // vehicle modes can be switched off in the settings
static unsigned g_mask[kNumModes];
static float g_saved[kNumModes][F_COUNT];  // the game's values for the fields we override
static float g_fp[kNumModes][F_COUNT];

static bool g_want;      // requested by the player
static bool g_active;    // first-person values currently applied
static bool g_keyDown;
static bool g_failed;    // disabled after an unexpected fault

static float* g_nearClip;     // the game's near clip distance
static float g_gameNearClip;  // the game's value while ours is applied
static bool g_nearApplied;

static uint8_t* g_ctrl;           // submode controller, remembered from its update
static float* g_camPos;           // camera position; orientation rows follow
static uint8_t* const* g_player;  // the local player object
static void* g_getTagPos;
static float g_headOffset[3];     // smoothed head position relative to the feet
static bool g_headTracked;
static bool g_headMissingLogged;
static float g_lastHeadHeight;    // for the debug log
static float g_lastHeadWeight;
static int g_lastSubmode = -1;    // to know which submode a blend started from
static int g_blendFrom = -1;

static float* FieldPtr(uint8_t* block, int f)
{
    return (float*)(block + kFieldOffset[f]);
}

static uint8_t* Entry(int submode)
{
    return g_table + submode * kEntrySize;
}

static const char* SubmodeName(int submode)
{
    return (submode >= 0 && submode < kNumSubmodes) ? g_names[submode] : "?";
}

// camera_free.xtbl is parsed well after the DLL loads; until then the table is zero.
static bool TableLoaded()
{
    float y = *FieldPtr(Entry(0), F_LOOK_Y);
    float fov = *FieldPtr(Entry(0), F_FOV);
    return y > 0.3f && y < 5.0f && fov > 5.0f && fov < 170.0f;
}

static void BuildFirstPersonValues(int m)
{
    const float* o = g_saved[m];
    float* v = g_fp[m];
    Category c = kModes[m].cat;
    bool crouch = c == CAT_CROUCH || c == CAT_AIM_CROUCH;
    bool aim = c == CAT_AIM || c == CAT_AIM_CROUCH;
    float fov = c == CAT_VEHICLE ? g_cfg.vehicleFov : aim ? g_cfg.aimFov : c == CAT_SPRINT ? g_cfg.sprintFov : g_cfg.fov;

    memcpy(v, o, sizeof g_fp[m]);
    v[F_LOOK_X] = 0.0f;
    v[F_LOOK_Y] = crouch ? g_cfg.crouchEyeHeight : g_cfg.eyeHeight;
    v[F_LOOK_Z] = 0.0f;
    v[F_MIN_PITCH] = g_cfg.lookUpLimit > 0.0f ? -g_cfg.lookUpLimit * kDegToRad : o[F_MIN_PITCH];
    v[F_MAX_PITCH] = g_cfg.lookDownLimit > 0.0f ? g_cfg.lookDownLimit * kDegToRad : o[F_MAX_PITCH];
    v[F_FOV] = fov > 0.0f ? fov : o[F_FOV];
    v[F_ANGLE] = 0.0f;
    v[F_DIST] = 0.01f;  // same as the game's own first-person zoom submode
    v[F_XSHIFT] = 0.0f;
}

static void WriteBlock(uint8_t* block, const float* values, unsigned mask)
{
    for (int f = 0; f < F_COUNT; f++)
        if (mask & (1u << f))
            *FieldPtr(block, f) = values[f];
}

static void WriteTable(const float (*values)[F_COUNT])
{
    for (int m = 0; m < kNumModes; m++)
        if (g_modeOn[m])
            WriteBlock(Entry(kModes[m].index), values[m], g_mask[m]);
}

// Keeps our values in the table. The game writes some submodes at runtime (vehicles put their camera
// settings in when you get in, transitions copy values between entries); anything that differs from
// what we wrote came from the game, so remember it as the value to restore.
static void EnforceTable()
{
    for (int m = 0; m < kNumModes; m++)
    {
        if (!g_modeOn[m])
            continue;
        uint8_t* e = Entry(kModes[m].index);
        bool changed = false;
        for (int f = 0; f < F_COUNT; f++)
        {
            float cur = *FieldPtr(e, f);
            if ((g_mask[m] & (1u << f)) && cur != g_fp[m][f])
            {
                g_saved[m][f] = cur;
                changed = true;
            }
        }
        if (changed)
        {
            // keep unmasked fields current too, they feed "keep the game's value" settings
            for (int f = 0; f < F_COUNT; f++)
                if (!(g_mask[m] & (1u << f)))
                    g_saved[m][f] = *FieldPtr(e, f);
            BuildFirstPersonValues(m);
        }
        WriteBlock(e, g_fp[m], g_mask[m]);
    }
}

// The controller only reads the table when a submode transition starts, so update its cached copies
// too; otherwise the camera would keep its old position until the player next changes stance.
static void WriteController(uint8_t* ctrl, const float (*values)[F_COUNT])
{
    int cur = *(int*)(ctrl + kCtrlSubmode);
    if (cur < 0 || cur >= kNumSubmodes || g_modeOf[cur] < 0)
        return;
    int m = g_modeOf[cur];
    for (int b = 0; b < 3; b++)
        WriteBlock(ctrl + kCtrlBlocks[b], values[m], g_mask[m]);
}

static bool TogglePressed()
{
    bool down = (GetAsyncKeyState(g_cfg.toggleKey) & 0x8000) != 0;
    bool pressed = down && !g_keyDown;
    g_keyDown = down;
    if (!pressed)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static void DebugDump(uint8_t* ctrl)
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (now - last < 1000)
        return;
    last = now;
    int cur = *(int*)(ctrl + kCtrlSubmode);
    uint8_t* b = ctrl + 0xD0;
    Log("submode %d '%s' fp=%d look=(%.2f %.2f %.2f) pitch=[%.0f %.0f] fov=%.1f dist=%.3f shift=%.2f near=%.3f",
        cur, SubmodeName(cur), g_active ? 1 : 0, *FieldPtr(b, F_LOOK_X), *FieldPtr(b, F_LOOK_Y), *FieldPtr(b, F_LOOK_Z),
        *FieldPtr(b, F_MIN_PITCH) * kRadToDeg, *FieldPtr(b, F_MAX_PITCH) * kRadToDeg, *FieldPtr(b, F_FOV),
        *FieldPtr(b, F_DIST), *FieldPtr(b, F_XSHIFT), *g_nearClip);
    if (g_headTracked)
        Log("  head tag %.2f m above the feet, offset (%.2f %.2f %.2f), weight %.2f", g_lastHeadHeight,
            g_headOffset[0], g_headOffset[1], g_headOffset[2], g_lastHeadWeight);
}

// The near clip is one global that the game sets on level load and around special cameras. While the
// camera is in first person use ours (unless the game's is already further out); hand it back after.
static void UpdateNearClip(bool firstPersonView)
{
    if (g_cfg.nearClip <= 0.0f)
        return;
    float cur = *g_nearClip;
    if (firstPersonView)
    {
        if (cur < g_cfg.nearClip)
        {
            g_gameNearClip = cur;
            *g_nearClip = g_cfg.nearClip;
            g_nearApplied = true;
        }
    }
    else if (g_nearApplied)
    {
        if (cur == g_cfg.nearClip)
            *g_nearClip = g_gameNearClip;
        g_nearApplied = false;
    }
}

static bool IsFirstPersonSubmode(int submode)
{
    return submode >= 0 && submode < kNumSubmodes && g_modeOf[submode] >= 0;
}

static bool InFirstPersonView(uint8_t* ctrl)
{
    return g_active && ctrl && IsFirstPersonSubmode(*(int*)(ctrl + kCtrlSubmode));
}

static void Tick(uint8_t* ctrl)
{
    if (TogglePressed())
    {
        g_want = !g_want;
        Log("toggle key: first person %s", g_want ? "on" : "off");
    }

    if (g_want && !g_active)
    {
        if (!TableLoaded())
            return;  // try again next frame
        for (int m = 0; m < kNumModes; m++)
        {
            if (!g_modeOn[m])
                continue;
            for (int f = 0; f < F_COUNT; f++)
                g_saved[m][f] = *FieldPtr(Entry(kModes[m].index), f);
            BuildFirstPersonValues(m);
        }
        WriteTable(g_fp);
        WriteController(ctrl, g_fp);
        g_active = true;
        int cur = *(int*)(ctrl + kCtrlSubmode);
        g_lastSubmode = g_blendFrom = cur;
        g_headTracked = false;
        Log("first person enabled (camera submode %d '%s')", cur, SubmodeName(cur));
    }
    else if (!g_want && g_active)
    {
        EnforceTable();  // pick up anything the game changed since the last frame
        WriteTable(g_saved);
        WriteController(ctrl, g_saved);
        g_active = false;
        int cur = *(int*)(ctrl + kCtrlSubmode);
        Log("first person disabled (camera submode %d '%s')", cur, SubmodeName(cur));
    }
    else if (g_active)
    {
        EnforceTable();
    }

    UpdateNearClip(InFirstPersonView(ctrl));

    if (g_cfg.debug)
        DebugDump(ctrl);
}

// --------------------------------------------------------------------------------------------------
// Camera hooks
// --------------------------------------------------------------------------------------------------

typedef void(__thiscall* CtrlUpdateFn)(void* ctrl, float dt);
typedef void(__cdecl* CameraUpdateFn)(float dt);

static CtrlUpdateFn g_origCtrlUpdate;
static CameraUpdateFn g_origCameraUpdate;

static __declspec(align(16)) float g_tagPos[4];

static __declspec(noinline) const float* GetTagPos(const char* tag, uint32_t lo, uint32_t hi)
{
    void* fn = g_getTagPos;
    float* dst = g_tagPos;
    __asm {
        push hi
        push lo
        push dst
        mov ecx, tag
        call fn
        add esp, 12
    }
    return g_tagPos;
}

// How much of the head camera to use: all of it in first-person submodes, none elsewhere, eased while
// the game blends into or out of one (ragdoll, swimming, getting into a remote-controlled car, ...).
static float HeadWeight(uint8_t* ctrl)
{
    int cur = *(int*)(ctrl + kCtrlSubmode);
    if (cur != g_lastSubmode)
    {
        g_blendFrom = g_lastSubmode;
        g_lastSubmode = cur;
    }
    bool to = IsFirstPersonSubmode(cur);
    bool from = IsFirstPersonSubmode(g_blendFrom);
    float t = *(float*)(ctrl + kCtrlBlend);
    if (to == from || !(t >= 0.0f && t < 1.0f))
        return to ? 1.0f : 0.0f;
    float eased = t * t * (3.0f - 2.0f * t);
    return to ? eased : 1.0f - eased;
}

// Moves the camera computed by the game to the player's eyes.
static void AttachToHead(float dt)
{
    float weight = (g_active && g_ctrl) ? HeadWeight(g_ctrl) : 0.0f;
    g_lastHeadWeight = weight;
    if (weight <= 0.0f)
    {
        g_headTracked = false;
        return;
    }
    const uint8_t* player = *g_player;
    if (!player)
        return;
    uint32_t lo = *(const uint32_t*)(player + kObjectHandle);
    uint32_t hi = *(const uint32_t*)(player + kObjectHandle + 4);

    float feet[3], head[3];
    memcpy(feet, GetTagPos("", lo, hi), sizeof feet);
    memcpy(head, GetTagPos("head", lo, hi), sizeof head);
    float off[3] = {head[0] - feet[0], head[1] - feet[1], head[2] - feet[2]};
    float len2 = off[0] * off[0] + off[1] * off[1] + off[2] * off[2];
    if (len2 < 0.25f || len2 > 16.0f)  // no head tag (same point as the feet) or nonsense
    {
        if (!g_headMissingLogged)
        {
            Log("player head tag not found; using the fixed eye height instead");
            g_headMissingLogged = true;
        }
        g_headTracked = false;
        return;
    }
    g_lastHeadHeight = off[1];

    float alpha = 1.0f;
    if (g_headTracked && g_cfg.headSmoothing > 0.0f && dt > 0.0f)
        alpha = 1.0f - expf(-dt / g_cfg.headSmoothing);
    for (int i = 0; i < 3; i++)
    {
        float d = off[i] - g_headOffset[i];
        g_headOffset[i] = (g_headTracked && fabsf(d) < 0.5f) ? g_headOffset[i] + d * alpha : off[i];
    }
    g_headTracked = true;

    const float* fwd = g_camPos + kCamForwardRow;
    float fx = fwd[0], fz = fwd[2];
    float flen = sqrtf(fx * fx + fz * fz);
    if (flen > 1e-3f)
    {
        fx /= flen;
        fz /= flen;
    }
    else
        fx = fz = 0.0f;

    float eye[3] = {feet[0] + g_headOffset[0] + fx * g_cfg.headForward, feet[1] + g_headOffset[1] + g_cfg.headUp,
                    feet[2] + g_headOffset[2] + fz * g_cfg.headForward};
    for (int i = 0; i < 3; i++)
        g_camPos[i] += (eye[i] - g_camPos[i]) * weight;
}

static void SafeTick(uint8_t* ctrl)
{
    __try
    {
        Tick(ctrl);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_failed = true;
        Log("ERROR: exception 0x%08lX while updating the camera; first person disabled until restart",
            GetExceptionCode());
    }
}

// --------------------------------------------------------------------------------------------------
// Vehicle look: no swinging back behind the vehicle in first person
// --------------------------------------------------------------------------------------------------

typedef uint8_t*(__fastcall* LookupFn)(void* manager, void* edx, const uint32_t* handle, uint32_t type);

static bool g_vehLookReady;
static bool g_vehLook;                 // this frame: driving in first person, game re-centring off
static const uint32_t* g_camTarget;    // camera target handle (lo, hi)
static float* g_heading;               // orbit heading, radians in [0, 2pi)
static LookupFn g_lookup;
static void* g_lookupManager;
static uint32_t g_lookupType;
static const uint8_t* const* g_typeInfo;
static void* g_getTargetMatrix;
static void* g_matrixAngles;
static void* g_origSwing;
static void* g_lockDriving;            // original code after the "player drives" test
static void* g_lockNotDriving;         // ... and its branch for "not driving"
static bool g_vehYawValid;
static float g_vehYaw;                 // target yaw last frame
static uint32_t g_vehHandle[2];        // ... and which target that was
static __declspec(align(16)) float g_targetMatrix[12];
static __declspec(align(16)) float g_targetAngles[4];

static bool IsVehicle(uint32_t lo, uint32_t hi)
{
    uint32_t handle[2] = {lo, hi};
    const uint8_t* obj = g_lookup(g_lookupManager, NULL, handle, g_lookupType);
    if (!obj || (obj[kObjFlags] & 0x10))
        return false;
    const uint8_t* info = g_typeInfo[obj[kObjType]];
    return info && (info[kTypeInfoFlags6] & 0x80);
}

// Yaw of the camera target as the game measures it for its own vehicle-relative camera. Kept out of
// line (like GetTagPos): the game code clobbers XMM registers the optimiser could otherwise keep live.
static __declspec(noinline) float TargetYaw(uint32_t lo, uint32_t hi)
{
    void* getMatrix = g_getTargetMatrix;
    void* toAngles = g_matrixAngles;
    float* matrix = g_targetMatrix;
    float* angles = g_targetAngles;
    __asm {
        push esi
        mov esi, matrix
        push hi
        push lo
        call getMatrix
        add esp, 8
        push angles
        mov ecx, eax
        call toAngles
        pop esi
    }
    return g_targetAngles[0];
}

static float WrapPi(float a)  // to [-pi, pi)
{
    a = fmodf(a + kPi, kTwoPi);
    if (a < 0.0f)
        a += kTwoPi;
    return a - kPi;
}

// Runs before the orbit update: decides whether the game's re-centring is off this frame and turns the
// heading by the vehicle's change in yaw since the last frame, so the view keeps its angle to the vehicle.
static void VehicleLook()
{
    bool on = false;
    uint32_t lo = g_camTarget[0], hi = g_camTarget[1];
    if (g_active && g_ctrl && (lo | hi))
    {
        int cur = *(int*)(g_ctrl + kCtrlSubmode);
        on = IsFirstPersonSubmode(cur) && kModes[g_modeOf[cur]].cat == CAT_VEHICLE && IsVehicle(lo, hi);
    }
    g_vehLook = on;
    if (!on)
    {
        g_vehYawValid = false;
        return;
    }
    float yaw = TargetYaw(lo, hi);
    if (!(yaw >= -100.0f && yaw <= 100.0f))  // NaN or garbage
    {
        g_vehYawValid = false;
        return;
    }
    if (g_vehYawValid && lo == g_vehHandle[0] && hi == g_vehHandle[1])
    {
        float h = fmodf(*g_heading + WrapPi(yaw - g_vehYaw), kTwoPi);
        *g_heading = h < 0.0f ? h + kTwoPi : h;
    }
    g_vehYaw = yaw;
    g_vehHandle[0] = lo;
    g_vehHandle[1] = hi;
    g_vehYawValid = true;

    static DWORD lastLog;
    if (g_cfg.debug && GetTickCount() - lastLog >= 1000)
    {
        lastLog = GetTickCount();
        Log("  vehicle look: vehicle yaw %.1f, view %.1f degrees off the vehicle", yaw * kRadToDeg,
            WrapPi(*g_heading - yaw) * kRadToDeg);
    }
}

static void SafeVehicleLook()
{
    __try
    {
        VehicleLook();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_vehLook = false;
        g_vehLookReady = false;
        Log("ERROR: exception 0x%08lX in the vehicle camera; the game's re-centring is back on", GetExceptionCode());
    }
}

// Swing towards the direction of travel: skipped while driving in first person.
static __declspec(naked) void Hook_VehicleSwing()
{
    __asm {
        cmp byte ptr [g_vehLook], 0
        jne skip
        jmp dword ptr [g_origSwing]
    skip:
        ret
    }
}

// Heading lock behind aircraft: treated as "player not driving" while driving in first person. Jumped
// to in place of `cmp byte [esp+13h],0 / je`; neither branch target reads the flags.
static __declspec(naked) void Hook_VehicleLock()
{
    __asm {
        cmp byte ptr [esp+13h], 0
        je not_driving
        cmp byte ptr [g_vehLook], 0
        jne not_driving
        jmp dword ptr [g_lockDriving]
    not_driving:
        jmp dword ptr [g_lockNotDriving]
    }
}

static void __fastcall Hook_CtrlUpdate(void* ctrl, void* /*edx*/, float dt)
{
    g_ctrl = (uint8_t*)ctrl;
    if (!g_failed)
        SafeTick((uint8_t*)ctrl);
    g_origCtrlUpdate(ctrl, dt);
    if (g_vehLookReady && !g_failed)
        SafeVehicleLook();
    else
        g_vehLook = false;
}

static void SafeAttachToHead(float dt)
{
    __try
    {
        AttachToHead(dt);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_failed = true;
        Log("ERROR: exception 0x%08lX while following the head; first person disabled until restart",
            GetExceptionCode());
    }
}

static void __cdecl Hook_CameraUpdate(float dt)
{
    g_origCameraUpdate(dt);
    if (g_active && !g_failed && g_cfg.attachToHead)
        SafeAttachToHead(dt);
}

// --------------------------------------------------------------------------------------------------
// Level of detail
// --------------------------------------------------------------------------------------------------

static float g_lodScale = 1.0f;
static float g_lodInvScale = 1.0f;
static float g_lodInvScaleSq = 1.0f;

// fade_categories.xtbl records {hash, medium^2, low^2, fade^2}
typedef void(__cdecl* VoidFn)();
static VoidFn g_origFadeLoader;
static uint8_t* const* g_fadeRecords;
static const int* g_fadeCount;

static void ScaleFadeCategories()
{
    uint8_t* rec = *g_fadeRecords;
    int n = *g_fadeCount;
    if (!rec || n <= 0 || n > 4096)
        return;
    float s2 = g_lodScale * g_lodScale;
    for (int i = 0; i < n; i++)
    {
        float* d = (float*)(rec + i * 16 + 4);
        d[0] *= s2;  // medium LOD
        d[1] *= s2;  // low LOD; the fade-out distance stays
    }
    Log("LOD: scaled %d fade categories from fade_categories.xtbl", n);
}

static void __cdecl Hook_FadeLoader()
{
    g_origFadeLoader();
    __try
    {
        ScaleFadeCategories();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("ERROR: exception while scaling fade categories");
    }
}

// Instanced meshes: squared near/far camera distances into the LOD range pick.
static void* g_origCloneDist;

static __declspec(naked) void Hook_CloneDist()
{
    __asm {
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        mov eax, g_origCloneDist
        call eax  // reads its inputs from edi/esi, which are untouched
        add esp, 8
        mov eax, dword ptr [esp+4]
        movss xmm0, dword ptr [eax]
        mulss xmm0, dword ptr [g_lodInvScaleSq]
        movss dword ptr [eax], xmm0
        mov eax, dword ptr [esp+8]
        movss xmm0, dword ptr [eax]
        mulss xmm0, dword ptr [g_lodInvScaleSq]
        movss dword ptr [eax], xmm0
        ret
    }
}

// Instanced meshes: shader fade parameters. Scaling every distance by S divides the two
// 1/(end-start) terms by S and leaves the -start/(end-start) offsets as they are.
typedef void(__thiscall* CloneFadeFn)(void* instance, int lod);
static CloneFadeFn g_origCloneFade;

static void __fastcall Hook_CloneFade(uint8_t* instance, void* /*edx*/, int lod)
{
    g_origCloneFade(instance, lod);
    float* p = (float*)(instance + kCloneFadeParams);
    p[0] *= g_lodInvScale;
    p[2] *= g_lodInvScale;
}

// Vehicles: scale the global defaults once and every list that was parsed; lists copied from the
// (already scaled) defaults or a template vehicle are left alone.
static void* g_origVehicleLods;
static int* g_vehLodDefaultCount;
static float* g_vehLodDefaults;
static float g_vehDefaultsScaled[kMaxLods];
static int g_vehDefaultsScaledCount = -1;

static void __cdecl PrepareVehicleLodDefaults()
{
    int n = *g_vehLodDefaultCount;
    if (n <= 0 || n > kMaxLods)
        return;
    if (n == g_vehDefaultsScaledCount && memcmp(g_vehLodDefaults, g_vehDefaultsScaled, n * sizeof(float)) == 0)
        return;
    for (int i = 0; i < n; i++)
        g_vehLodDefaults[i] *= g_lodScale;
    memcpy(g_vehDefaultsScaled, g_vehLodDefaults, n * sizeof(float));
    g_vehDefaultsScaledCount = n;
    Log("LOD: scaled %d default vehicle LOD ratios", n);
}

static bool CopiedFrom(const float* v, int n, const float* src, int srcCount)
{
    return src && srcCount == n && memcmp(v, src, n * sizeof(float)) == 0;
}

static void AfterVehicleLods(uint8_t* info, const uint8_t* tmpl, int useDefaults)
{
    int n = *(int*)(info + kVehicleLodCount);
    if (n <= 0 || n > kMaxLods)
        return;
    float* v = (float*)(info + kVehicleLodRatios);
    if ((useDefaults & 0xFF) && CopiedFrom(v, n, g_vehLodDefaults, *g_vehLodDefaultCount))
        return;
    if (!(useDefaults & 0xFF) && tmpl)
    {
        bool copied = false;
        __try
        {
            copied = CopiedFrom(v, n, (const float*)(tmpl + kVehicleLodRatios), *(const int*)(tmpl + kVehicleLodCount));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            copied = false;  // not a real template: the list was parsed
        }
        if (copied)
            return;
    }
    for (int i = 0; i < n; i++)
        v[i] *= g_lodScale;
}

static void __cdecl SafeAfterVehicleLods(uint8_t* info, const uint8_t* tmpl, int useDefaults)
{
    __try
    {
        AfterVehicleLods(info, tmpl, useDefaults);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("ERROR: exception while scaling vehicle LODs");
    }
}

static void __cdecl SafePrepareVehicleLodDefaults()
{
    __try
    {
        PrepareVehicleLodDefaults();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("ERROR: exception while scaling default vehicle LODs");
    }
}

// Entry: [esp+4] = info, [esp+8] = use_defaults, eax = XML node, ecx = template. Returns al.
static __declspec(naked) void Hook_VehicleLods()
{
    __asm {
        push ecx
        push eax
        call SafePrepareVehicleLodDefaults
        pop eax
        mov ecx, dword ptr [esp]
        push dword ptr [esp+12]  // use_defaults
        push dword ptr [esp+12]  // info
        mov edx, g_origVehicleLods
        call edx
        add esp, 8
        push eax                 // result
        push dword ptr [esp+16]  // use_defaults
        push dword ptr [esp+8]   // template
        push dword ptr [esp+20]  // info
        call SafeAfterVehicleLods
        add esp, 12
        pop eax
        pop ecx
        ret
    }
}

// Items: each parsed LOD distance.
typedef int(__cdecl* ParseFloatFn)(float* dst, void* node, void* name);
static ParseFloatFn g_origItemLodParse;

static int __cdecl Hook_ItemLodParse(float* dst, void* node, void* name)
{
    int r = g_origItemLodParse(dst, node, name);
    *dst *= g_lodScale;
    return r;
}

static bool Sane(const float* v, int n, float maxValue)
{
    for (int i = 0; i < n; i++)
        if (!(v[i] > 0.0f && v[i] < maxValue))
            return false;
    return true;
}

static void InitLod()
{
    float s = g_cfg.lodScale;
    if (s == 1.0f)
    {
        Log("LOD: scale 1, level of detail unchanged");
        return;
    }
    g_lodScale = s;
    g_lodInvScale = 1.0f / s;
    g_lodInvScaleSq = 1.0f / (s * s);
    char done[256] = "";

    // Characters: default distances copied into every character definition when it is parsed.
    if (uint8_t* p = Find("character LOD defaults", kSigCharLodDefaults))
    {
        float* t = ReadPtr<float*>(p + 16);
        if (InImage(t, 16) && ReadPtr<float*>(p + 32) == t + 2 && Sane(t, 4, 100000.0f))
        {
            Log("LOD: character distances %.0f/%.0f/%.0f/%.0f m", t[0] * s, t[1] * s, t[2] * s, t[3] * s);
            for (int i = 0; i < 4; i++)
                t[i] *= s;
            strcat_s(done, " characters");
        }
    }

    // Level objects: built-in fade categories (read-only data) and fade_categories.xtbl.
    uint8_t* get = Find("fade category lookup", kSigFadeCategoryGet);
    uint8_t* loader = Find("fade category loader", kSigFadeLoader);
    uint8_t* store = Find("fade category records", kSigFadeTableStore);
    if (get && loader && store)
    {
        float* t = ReadPtr<float*>(get + 17);
        g_fadeRecords = ReadPtr<uint8_t* const*>(store + 5);
        g_fadeCount = ReadPtr<const int*>(store + 11);
        DWORD old;
        if (InImage(t, kFadeBuiltinCount * 12) && Sane(t, kFadeBuiltinCount * 3, 1e9f) &&
            InImage(g_fadeRecords, 4) && InImage(g_fadeCount, 4) && VirtualProtect(t, kFadeBuiltinCount * 12, PAGE_READWRITE, &old))
        {
            for (int i = 0; i < kFadeBuiltinCount; i++)
            {
                t[i * 3 + 0] *= s * s;
                t[i * 3 + 1] *= s * s;
            }
            VirtualProtect(t, kFadeBuiltinCount * 12, old, &old);
            g_origFadeLoader = (VoidFn)Detour(loader, (const void*)&Hook_FadeLoader, kFadeLoaderStolen);
            strcat_s(done, g_origFadeLoader ? " level-objects" : " level-objects(built-in only)");
        }
    }

    // Instanced meshes.
    uint8_t* distCall = Find("instanced mesh distance call", kSigCloneDistCall);
    uint8_t* distFn = Find("instanced mesh distance", kSigCloneDistFn);
    uint8_t* fadeFn = Find("instanced mesh fade", kSigCloneFadeFn);
    if (distCall && distFn && fadeFn && CallTarget(distCall + kCloneDistCallOffset) == distFn)
    {
        g_origCloneDist = distFn;
        g_origCloneFade = (CloneFadeFn)Detour(fadeFn, (const void*)&Hook_CloneFade, kCloneFadeStolen);
        if (g_origCloneFade && RedirectCall(distCall + kCloneDistCallOffset, (const void*)&Hook_CloneDist))
            strcat_s(done, " instanced-meshes");
    }

    // Vehicles.
    uint8_t* vehLods = Find("vehicle LOD parser", kSigVehicleLods);
    uint8_t* vehDefaults = Find("vehicle LOD defaults", kSigVehicleLodDefaults);
    if (vehLods && vehDefaults && vehDefaults > vehLods && vehDefaults - vehLods < 0x100)
    {
        g_vehLodDefaultCount = ReadPtr<int*>(vehDefaults + 2);
        g_vehLodDefaults = ReadPtr<float*>(vehDefaults + 37);
        if (InImage(g_vehLodDefaultCount, 4) && InImage(g_vehLodDefaults, kMaxLods * 4) &&
            ReadPtr<int*>(vehDefaults + 16) == g_vehLodDefaultCount)
        {
            g_origVehicleLods = Detour(vehLods, (const void*)&Hook_VehicleLods, kVehicleLodsStolen);
            if (g_origVehicleLods)
                strcat_s(done, " vehicles");
        }
    }

    // Items.
    if (uint8_t* p = Find("item LOD parse", kSigItemLodParse))
    {
        uint8_t* call = p + kItemLodParseCallOffset;
        uint8_t* target = CallTarget(call);
        if (target && InImage(target, 16))
        {
            g_origItemLodParse = (ParseFloatFn)target;
            if (RedirectCall(call, (const void*)&Hook_ItemLodParse))
                strcat_s(done, " items");
        }
    }

    Log("LOD: distances x%.2f for:%s", s, done[0] ? done : " nothing (signatures not found)");
}

// --------------------------------------------------------------------------------------------------
// Startup
// --------------------------------------------------------------------------------------------------

// Optional part of the camera: without it first person still works, with the game's re-centring.
static void InitVehicleLook(const uint8_t* ctrlUpdate)
{
    if (g_cfg.vehicleAutoCenter || !g_cfg.vehicles)
    {
        Log("vehicle camera: the game's re-centring is kept (%s)",
            g_cfg.vehicles ? "VehicleAutoCenter=1" : "VehicleFirstPerson=0");
        return;
    }
    uint8_t* swing = Find("vehicle camera swing", kSigVehicleSwing);
    uint8_t* lock = Find("vehicle camera lock", kSigVehicleLock);
    uint8_t* rel = Find("camera target heading", kSigTargetHeading);
    if (!swing || !lock || !rel)
    {
        Log("vehicle camera: code not found, the game's re-centring is kept");
        return;
    }
    const uint32_t* handle = ReadPtr<const uint32_t*>(swing + 0x36);
    g_lookupType = ReadPtr<uint32_t>(swing + 0x4D);
    g_lookupManager = ReadPtr<void*>(swing + 0x57);
    g_lookup = (LookupFn)CallTarget(swing + 0x5B);
    g_typeInfo = ReadPtr<const uint8_t* const*>(swing + 0x73);
    g_getTargetMatrix = CallTarget(rel + 24);
    g_matrixAngles = CallTarget(rel + 34);
    g_heading = ReadPtr<float*>(rel + 46);
    g_lockDriving = lock + kVehicleLockPatch;
    g_lockNotDriving = lock + kVehicleLockPatch + ReadPtr<int32_t>(lock + 7);
    // "not driving" branch: xorps xmm0,xmm0 / movss [edi+0Ch],xmm0 (no roll)
    static const uint8_t kNotDriving[] = {0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x11, 0x47, 0x0C};
    // every reference to the camera target handle must agree: swing (lo, hi), target heading (hi, lo)
    // and the controller update (lo at +14; its first 10 bytes are already our jump)
    bool sameHandle = ReadPtr<const uint32_t*>(swing + 0x3C) == handle + 1 && ReadPtr<const uint32_t*>(rel + 8) == handle &&
                      ReadPtr<const uint32_t*>(rel + 2) == handle + 1 && ReadPtr<const uint32_t*>(ctrlUpdate + 14) == handle;
    if (!sameHandle || !InImage(handle, 8) || !InImage(g_heading, 4) || !InImage(g_lookupManager, 4) ||
        !InImage(g_typeInfo, 256 * sizeof(void*)) || !g_lookup || !InImage((void*)g_lookup, 16) || !g_getTargetMatrix ||
        !InImage(g_getTargetMatrix, 16) || !g_matrixAngles || !InImage(g_matrixAngles, 16) ||
        !InImage(g_lockNotDriving, sizeof kNotDriving) || memcmp(g_lockNotDriving, kNotDriving, sizeof kNotDriving) != 0)
    {
        Log("ERROR: vehicle camera: unexpected game code, the game's re-centring is kept");
        return;
    }
    g_camTarget = handle;
    g_origSwing = Detour(swing, (const void*)&Hook_VehicleSwing, kVehicleSwingStolen);
    if (!g_origSwing)
    {
        Log("ERROR: vehicle camera: could not patch game code, the game's re-centring is kept");
        return;
    }
    bool locked = InstallJump(lock, (const void*)&Hook_VehicleLock, kVehicleLockPatch);
    g_vehLookReady = true;
    Log("vehicle camera: no re-centring in first person (swing %p, lock %p%s, heading %p)", swing, lock,
        locked ? "" : " NOT patched", g_heading);
}

static bool InitCamera()
{
    uint8_t* update = Find("camera controller update", kSigCtrlUpdate);
    uint8_t* tableRef = Find("submode table", kSigSubmodeTable);
    uint8_t* namesRef = Find("submode names", kSigSubmodeNames);
    uint8_t* setNear = Find("set near clip", kSigSetNearClip);
    uint8_t* camRef = Find("camera update call", kSigCameraUpdateCall);
    uint8_t* getTagPos = Find("get tag position", kSigGetTagPos);
    uint8_t* playerRef = Find("get player call", kSigPlayerCall);
    if (!update || !tableRef || !namesRef || !setNear || !camRef || !getTagPos || !playerRef)
        return false;

    g_table = ReadPtr<uint8_t*>(tableRef + 4);
    g_names = ReadPtr<const char* const*>(namesRef + 6);
    g_nearClip = ReadPtr<float*>(setNear + kNearClipVarOffset);
    g_camPos = ReadPtr<float*>(camRef + kCameraPosOffset);
    g_getTagPos = getTagPos;
    const uint8_t* getPlayer = CallTarget(playerRef);
    if (getPlayer && InImage(getPlayer, 6) && getPlayer[0] == 0xA1 && getPlayer[5] == 0xC3)  // mov eax,[player] / ret
        g_player = ReadPtr<uint8_t* const*>(getPlayer + 1);
    if (!InImage(g_table, kNumSubmodes * kEntrySize) || !InImage(g_names, kNumSubmodes * sizeof(char*)) ||
        !InImage(g_nearClip, sizeof(float)) || !InImage(g_camPos, 0x70) || ((uintptr_t)g_camPos & 15) ||
        !InImage(g_player, sizeof(void*)))
    {
        Log("ERROR: game data outside the image (table %p names %p near %p camera %p player %p)", g_table, g_names,
            g_nearClip, g_camPos, g_player);
        return false;
    }
    uint8_t* camCall = camRef + kCameraUpdateCallOffset;
    uint8_t* camUpdate = CallTarget(camCall);
    if (!camUpdate || !InImage(camUpdate, 16) || camUpdate[0] != 0x55)
    {
        Log("ERROR: unexpected camera update call at %p", camCall);
        return false;
    }

    for (int i = 0; i < kNumSubmodes; i++)
        g_modeOf[i] = -1;
    for (int m = 0; m < kNumModes; m++)
    {
        const char* name = g_names[kModes[m].index];
        if (!InImage(name, 1) || strcmp(name, kModes[m].name) != 0)
        {
            Log("ERROR: camera submode %d is not '%s'", kModes[m].index, kModes[m].name);
            return false;
        }
        g_mask[m] = FieldMask(kModes[m].cat);
        g_modeOn[m] = g_cfg.vehicles || !IsVehicleCategory(kModes[m].cat);
        if (g_modeOn[m])
            g_modeOf[kModes[m].index] = m;
    }

    // Everything checks out; patch. The game has not started running yet (we are loaded as one of
    // its imports), so no thread can be executing the code being modified.
    g_origCtrlUpdate = (CtrlUpdateFn)Detour(update, (const void*)&Hook_CtrlUpdate, kCtrlUpdateStolen);
    if (!g_origCtrlUpdate)
    {
        Log("ERROR: could not hook the camera controller");
        return false;
    }
    g_origCameraUpdate = (CameraUpdateFn)camUpdate;
    if (!RedirectCall(camCall, (const void*)&Hook_CameraUpdate))
    {
        Log("ERROR: could not patch game code");
        return false;
    }

    g_want = g_cfg.startEnabled;
    Log("hooked camera controller %p and camera update call %p; submode table %p, camera %p, player %p, near clip %p",
        update, camCall, g_table, g_camPos, g_player, g_nearClip);
    InitVehicleLook(update);
    return true;
}

static bool Init(HMODULE exe)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)exe;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((uint8_t*)exe + dos->e_lfanew);
    g_imageBase = (uint8_t*)exe;
    g_imageSize = nt->OptionalHeader.SizeOfImage;
    Log("game build timestamp %08lX, image %p (+%08lX)", nt->FileHeader.TimeDateStamp, g_imageBase, (unsigned long)g_imageSize);

    g_text = NULL;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (memcmp(sec[i].Name, ".text", 6) == 0)
        {
            g_text = g_imageBase + sec[i].VirtualAddress;
            g_textSize = sec[i].Misc.VirtualSize;
        }
    }
    if (!g_text)
    {
        Log("ERROR: no .text section");
        return false;
    }

    bool camera = InitCamera();
    if (!camera)
        Log("first person NOT active (unsupported game version?)");
    InitLod();
    return camera;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_modDir, MAX_PATH);
        char* slash = strrchr(g_modDir, '\\');
        if (slash)
            *slash = 0;
        LoadConfig();

        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        const char* exeName = strrchr(exePath, '\\') ? strrchr(exePath, '\\') + 1 : exePath;
        // Only the game itself gets hooked; anything else just gets a working dinput8.dll.
        if (_strnicmp(exeName, "SaintsRowTheThird", 17) != 0)
            return TRUE;

        if (g_cfg.log)
        {
            char logPath[MAX_PATH];
            sprintf_s(logPath, "%s\\SRTT_FirstPerson.log", g_modDir);
            g_log = _fsopen(logPath, "w", _SH_DENYWR);
        }
        Log("SRTT First Person " MOD_VERSION " in %s", exePath);
        Log("toggle key 0x%02X, vehicles %d (auto-centre %d), head %d (up %.2f fwd %.2f smooth %.2f), eye %.2f/%.2f m, "
            "fov %g/%g/%g/%g, look up/down %.0f/%.0f, near clip %.2f m, LOD x%.2f",
            g_cfg.toggleKey, g_cfg.vehicles ? 1 : 0, g_cfg.vehicleAutoCenter ? 1 : 0, g_cfg.attachToHead ? 1 : 0,
            g_cfg.headUp, g_cfg.headForward,
            g_cfg.headSmoothing, g_cfg.eyeHeight, g_cfg.crouchEyeHeight, g_cfg.fov, g_cfg.sprintFov, g_cfg.aimFov,
            g_cfg.vehicleFov, g_cfg.lookUpLimit, g_cfg.lookDownLimit, g_cfg.nearClip, g_cfg.lodScale);
        Init(GetModuleHandleA(NULL));
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_log)
        {
            fclose(g_log);
            g_log = NULL;
        }
    }
    return TRUE;
}
