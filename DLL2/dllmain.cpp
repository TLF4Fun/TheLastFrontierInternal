#include <windows.h>
#include <stdio.h>
#include <polyhook2/Virtuals/VFuncSwapHook.hpp>
#include <polyhook2/Virtuals/VTableSwapHook.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include <iostream>

// ===================== STRUCTS =====================

struct FString {
    TCHAR* Data;
    int32_t Count;
    int32_t Max;
};
struct TArray {
    void* Data;
    int num;
    int max;
};
struct FLinearColor { float R, G, B, A; };
struct DrawLineParams {
    float X1, Y1, X2, Y2;
    FLinearColor Color;
    float thickness;
};
struct DrawTextParams {
    FString text;
    FLinearColor color;
    float X, Y;
    void* font;
    float scale;
    bool scalePosition;
};
struct fakename { uint32_t comp, num; };
struct FVector { float x, y, z; };
struct FRotator { float x, y, z; };
struct FVector2D { float x, y; };
struct FQuat { float X, Y, Z, W; };
struct FTransform {
    FQuat   Rotation;
    FVector Translation;
    float   Padding1;
    FVector Scale3D;
    float   Padding2;
};
struct PlayerBounds { float minX, minY, maxX, maxY; bool valid; };
struct LootScreenPoolItem { FVector2D Pos; FString Name; FLinearColor Color; };
struct BoneConnection { int from, to; };
struct FYPlayerInventory {
    int32_t m_permutationIndex;
    int32_t m_inventoryComponentId;
    TArray  m_replicatedItems;
};
struct FDataTableRowHandle { void* DataTable; fakename RowName; };
struct FYInventoryItem {
    FString           m_customItemID;
    FDataTableRowHandle m_item;
    int32_t           m_amount;
    int32_t           m_durability;
    float             m_weight;
    char pad_2C[0x04];
    char pad_30[0x20];
    TArray            m_vanityEntries;
    char pad_60[0x38];
    uint8_t           m_insurance;
    char pad_99[0x07];
    FString           m_insuranceOwnerPlayfabId;
    FString           m_insuredAttachmentId;
    char pad_C0[0x28];
};
struct PlayerBones { std::map<int, FVector2D> boneScreenPositions; };

// ===================== BONE CONNECTIONS =====================

std::vector<BoneConnection> boneConnections = {
    {1,3},{3,4},{4,5},{5,6},{6,7},{7,8},{8,9},{9,10},
    {8,56},{8,86},{56,57},{57,58},{86,87},{87,88},
    {1,115},{115,116},{116,117},{117,118},
    {1,119},{119,120},{120,121},{121,122},
};

// ===================== CONFIG =====================

struct Config {
    bool  showSkeleton = false;
    bool  showBoxes = false;
    bool  showHealthBars = false;
    bool  showLoot = false;
    bool  showContainers = false;
    bool  noRecoil = false;
    bool  noSpread = false;
    bool  rapidFire = false;
    bool  instaAds = false;
    bool  aimbot = false;
    bool  showFOVCircle = false;
    bool  infiniteStamina = false;
    float fovRadius = 150.0f;
    float lineThickness = 1.0f;
    int   minRarityLoot = 1;
    int   minRarityContainer = 1;

} cfg;
struct WeaponTuning {
    float DefaultSpread = 0.0f;
    float SpreadIncreaseSpeed = 0.0f;
    float SpreadDecreaseSpeed = 0.0f;
    float SpreadMax = 0.0f;
    float TimeToAds = 0.0f;
    float TimeBetweenShots = 0.0f;
    uintptr_t SpreadCurve = 0;
    uintptr_t RecoilCurve = 0;
};
// ===================== ANSI COLORS =====================

#define COL_RESET   "\033[0m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_CYAN    "\033[36m"
#define COL_WHITE   "\033[97m"
#define COL_GRAY    "\033[90m"

// ===================== GLOBALS =====================

uintptr_t BaseAddress = 0x0;
HMODULE    g_hModule = nullptr;
bool       g_running = true;

std::vector<std::pair<FVector2D, FString>> PlayerScreenPositions;
std::vector<LootScreenPoolItem>            LootScreenPositions;
std::vector<FVector2D>                     BonePositions;
static std::vector<PlayerBones>            cachedPlayerBones;
static std::vector<std::pair<PlayerBounds, FLinearColor>> cachedPlayerBounds;
std::vector<float>                         cachedPlayerHealth;
std::map<void*, WeaponTuning>              g_weaponTunings;
bool  ConsoleFlag = true;
float screenCenterX = (float)GetSystemMetrics(SM_CXSCREEN) / 2.0;
float screenCenterY = (float)GetSystemMetrics(SM_CYSCREEN) / 2.0;

// ===================== FUNCTION POINTERS =====================

void* DrawLineFn = nullptr;
void* DrawTextFn = nullptr;
void* K2_GetActorLocationFn = nullptr;
void* GetPlayerNameFn = nullptr;
void* GetItemRarity = nullptr;
void* GetBaseItemRowHandle = nullptr;
void* GetCameraLocation = nullptr;
void* FindLookAtRotation = nullptr;
void* SetControlRotation = nullptr;
void* LineOfSightTo = nullptr;
void* ConsoleClass = nullptr;
void* SpawnObjectFn = nullptr;
void* GameplayStaticsClass = nullptr;
void* CheatManager = nullptr;
void* ToggleDebugCameraFn = nullptr;
void* GetInventory = nullptr;
void* GetNameByRowHandle = nullptr;
void* GetItemRarityFromRowHandle = nullptr;

int ViewportTickOffset = 0x389C790;
int PostRenderOffset = 0x38FBFC0;
int ProcessEventOffset = 0x213D030;
int ProjectWorldToScreenOffset = 0x3892AE0;
int StaticFindObjectOffset = 0x2145E00;
int GetFullNameOffset = 0x38FBFC0;
int GetObjectsOfClassOffset = 0x2154AD0;
int FNameGetEntryOffset = 0x1F2F670;
int FNameToStringOffset = 0x1F36710;
int GetBoneNameOffset = 0x37ADC00;
int GetBoneTransformOffset = 0x37ADD30;

using ProcessEventFn = void(__fastcall*)(void*, void*, void*);
using ProjectWorldToScreenFn = bool(__fastcall*)(void*, const FVector*, FVector2D*, bool);
using StaticFindObjectFn = void* (__fastcall*)(void*, void*, const wchar_t*, uint8_t);
using GetFullNameFn = FString * (__fastcall*)(void*, FString*, const void*, bool);
using GetObjectsOfClassFn = void(__fastcall*)(void*, TArray*, char, int, int);
using FNameGetEntryFn = char* (__fastcall*)(unsigned int);
using FNameToStringFn = __int64(__fastcall*)(fakename*, wchar_t*);
using GetBoneNameFn = void* (__fastcall*)(void*, fakename*, int);
using GetBoneTransformFn = FTransform * (__fastcall*)(void*, FTransform*, __int64);
using GameViewportClientTick = void(__fastcall*)(void*, float);
using PostRenderFn = void(__fastcall*)(void*);

ProcessEventFn         ProcessEvent = nullptr;
ProjectWorldToScreenFn ProjectWorldToScreen = nullptr;
StaticFindObjectFn     StaticFindObject = nullptr;
GetFullNameFn          GetFullName = nullptr;
GetObjectsOfClassFn    GetObjectsOfClass = nullptr;
FNameGetEntryFn        FNameGetEntry = nullptr;
FNameToStringFn        FNameToString = nullptr;
GetBoneNameFn          GetBoneName = nullptr;
GetBoneTransformFn     GetBoneTransform = nullptr;
GameViewportClientTick viewportOriginalFn = nullptr;
PostRenderFn           PostRenderOriginal = nullptr;

std::unique_ptr<PLH::x64Detour> viewportDetour;
std::unique_ptr<PLH::x64Detour> PostRenderDetour;

// ===================== HELPERS =====================

inline std::wstring FStringToWString(const FString& str) {
    if (!str.Data || str.Count <= 0) return L"";
    return std::wstring(str.Data, str.Count);
}

FString WStringToFString(const std::wstring& ws) {
    FString result{};
    int len = (int)ws.size() + 1;
    result.Data = (wchar_t*)malloc(len * sizeof(wchar_t));
    memcpy(result.Data, ws.c_str(), len * sizeof(wchar_t));
    result.Count = len;
    result.Max = len;
    return result;
}

bool SafeFNameToString(fakename* name, wchar_t* outBuf, int bufSize) {
    __try {
        if (!name || name->comp == 0) return false;
        FNameToString(name, outBuf);
        outBuf[bufSize - 1] = L'\0';
        return outBuf[0] != L'\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

float Distance(FVector a, FVector b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

bool inFOV(FVector2D screenPos) {
    float dx = screenPos.x - screenCenterX;
    float dy = screenPos.y - screenCenterY;
    return (dx * dx + dy * dy) <= (cfg.fovRadius * cfg.fovRadius);
}

FTransform SafeGetBoneTransform(void* skelemesh, int boneIndex) {
    FTransform outTrans{};
    __try {
        FTransform* result = GetBoneTransform(skelemesh, &outTrans, (__int64)boneIndex);
        if (result && result != (FTransform*)-1) return *result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return outTrans;
}
void SafeExtend(void* o) {
    __try {
        *(float*)((uint8_t*)o + 0x148) = 999999999.0f; // NetCullDistanceSquared

    }
    __except (EXCEPTION_EXECUTE_HANDLER) {

    }

}
void Unload() {
    g_running = false;

    // Restore any modified weapon tunings before unhooking
    for (auto& [tuningInfo, orig] : g_weaponTunings) {
        __try {
            *(float*)((uint8_t*)tuningInfo + 0x84) = orig.DefaultSpread;
            *(float*)((uint8_t*)tuningInfo + 0x88) = orig.SpreadIncreaseSpeed;
            *(float*)((uint8_t*)tuningInfo + 0x8C) = orig.SpreadDecreaseSpeed;
            *(float*)((uint8_t*)tuningInfo + 0x90) = orig.SpreadMax;
            *(float*)((uint8_t*)tuningInfo + 0xa8) = orig.TimeToAds;
            *(float*)((uint8_t*)tuningInfo + 0x0124) = orig.TimeBetweenShots;
            *(uintptr_t*)((uint8_t*)tuningInfo + 0x00E0) = orig.SpreadCurve;
            *(uintptr_t*)((uint8_t*)tuningInfo + 0x1a8) = orig.RecoilCurve;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    g_weaponTunings.clear();

    // Clear render data so the hooked PostRender draws nothing during teardown
    PlayerScreenPositions.clear();
    LootScreenPositions.clear();
    BonePositions.clear();
    cachedPlayerBones.clear();
    cachedPlayerBounds.clear();
    cachedPlayerHealth.clear();

    // Give the game one frame to finish any in-progress hook calls before removing
    Sleep(100);

    if (viewportDetour) { viewportDetour->unHook();  viewportDetour.reset(); }
    if (PostRenderDetour) { PostRenderDetour->unHook(); PostRenderDetour.reset(); }

    printf("[Cheat] nloaded. Goodbye!\n");
    Sleep(300);

    FreeConsole();
    FreeLibraryAndExitThread(g_hModule, 0);
}
void SafeApplyGunChanges(void* weaponCompController) {
    __try {
        void* TuningInfo = *(void**)((uint8_t*)weaponCompController + 0x5e0);

        // No tuning info (knife/melee/special) - just do nothing, preserve cache
        if (!TuningInfo) return;

        if (g_weaponTunings.find(TuningInfo) == g_weaponTunings.end()) {
            WeaponTuning wt{};
            wt.DefaultSpread = *(float*)((uint8_t*)TuningInfo + 0x84);
            wt.SpreadIncreaseSpeed = *(float*)((uint8_t*)TuningInfo + 0x88);
            wt.SpreadDecreaseSpeed = *(float*)((uint8_t*)TuningInfo + 0x8C);
            wt.SpreadMax = *(float*)((uint8_t*)TuningInfo + 0x90);
            wt.TimeToAds = *(float*)((uint8_t*)TuningInfo + 0xa8);
            wt.TimeBetweenShots = *(float*)((uint8_t*)TuningInfo + 0x0124);
            wt.SpreadCurve = *(uintptr_t*)((uint8_t*)TuningInfo + 0x00E0);
            wt.RecoilCurve = *(uintptr_t*)((uint8_t*)TuningInfo + 0x1a8);

            if (wt.RecoilCurve == 0 || wt.SpreadCurve == 0) return;

            g_weaponTunings[TuningInfo] = wt;
            printf("Cached weapon %p  RecoilCurve: %p\n", TuningInfo, (void*)wt.RecoilCurve);
        }

        const WeaponTuning& orig = g_weaponTunings[TuningInfo];

        // No Spread
        if (cfg.noSpread) {
            *(float*)((uint8_t*)TuningInfo + 0x84) = 0.0f;
            *(float*)((uint8_t*)TuningInfo + 0x88) = 0.0f;
            *(float*)((uint8_t*)TuningInfo + 0x8C) = 100.0f;
            *(float*)((uint8_t*)TuningInfo + 0x90) = 0.0f;
            *(uintptr_t*)((uint8_t*)TuningInfo + 0x00E0) = 0;
        }
        else {
            *(float*)((uint8_t*)TuningInfo + 0x84) = orig.DefaultSpread;
            *(float*)((uint8_t*)TuningInfo + 0x88) = orig.SpreadIncreaseSpeed;
            *(float*)((uint8_t*)TuningInfo + 0x8C) = orig.SpreadDecreaseSpeed;
            *(float*)((uint8_t*)TuningInfo + 0x90) = orig.SpreadMax;
            *(uintptr_t*)((uint8_t*)TuningInfo + 0x00E0) = orig.SpreadCurve;
        }

        // No Recoil
        if (cfg.noRecoil) {
            *(uintptr_t*)((uint8_t*)TuningInfo + 0x1a8) = 0;
        }
        else {
            *(uintptr_t*)((uint8_t*)TuningInfo + 0x1a8) = orig.RecoilCurve;
        }

        // Insta ADS
        *(float*)((uint8_t*)TuningInfo + 0xa8) = cfg.instaAds
            ? 0.0f : orig.TimeToAds;

        // Rapid Fire
        *(float*)((uint8_t*)TuningInfo + 0x0124) = cfg.rapidFire
            ? 0.0f : orig.TimeBetweenShots;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

FLinearColor RarityToColor(uint8_t rarity) {
    switch (rarity) {
    case 2:  return { 0.0f, 0.5f, 0.0f, 1.0f };
    case 3:  return { 0.0f, 0.0f, 1.0f, 1.0f };
    case 4:  return { 0.5f, 0.0f, 0.5f, 1.0f };
    case 5:  return { 1.0f, 0.0f, 0.0f, 1.0f };
    case 6:  return { 1.0f, 1.0f, 0.0f, 1.0f };
    default: return { 0.5f, 0.5f, 0.5f, 1.0f };
    }
}

// ===================== CONSOLE MENU =====================

void EnableConsoleColors() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

const char* Toggle(bool val) {
    return val ? COL_GREEN "[ON] " COL_RESET : COL_RED "[OFF]" COL_RESET;
}

void PrintMenu() {
    system("cls");
    printf(COL_CYAN    "=====================================\n" COL_RESET);
    printf(COL_YELLOW  "         PROSPECT CHEAT MENU\n"         COL_RESET);
    printf(COL_CYAN    "=====================================\n\n" COL_RESET);

    printf(COL_WHITE   " VISUALS\n" COL_RESET);
    printf(COL_GRAY "  [f1]" COL_RESET " Skeleton       %s\n", Toggle(cfg.showSkeleton));
    printf(COL_GRAY "  [f2]" COL_RESET " Boxes          %s\n", Toggle(cfg.showBoxes));
    printf(COL_GRAY "  [f3]" COL_RESET " Health Bars    %s\n", Toggle(cfg.showHealthBars));
    printf(COL_GRAY "  [f4]" COL_RESET " FOV Circle     %s\n", Toggle(cfg.showFOVCircle));
    printf(COL_GRAY "  [-]" COL_RESET " FOV Radius     " COL_YELLOW "%.0f" COL_RESET " ([ / ] to adjust)\n", cfg.fovRadius);

    printf("\n" COL_WHITE " LOOT\n" COL_RESET);
    printf(COL_GRAY "  [f5]" COL_RESET " Ground Loot    %s\n", Toggle(cfg.showLoot));
    printf(COL_GRAY "  [f6]" COL_RESET " Containers     %s\n", Toggle(cfg.showContainers));
    printf(COL_GRAY "  [-]" COL_RESET " Min Loot Rar   " COL_YELLOW "%d" COL_RESET " (; / ' to adjust)\n", cfg.minRarityLoot);
    printf(COL_GRAY "  [-]" COL_RESET " Min Cont Rar   " COL_YELLOW "%d" COL_RESET " (, / . to adjust)\n", cfg.minRarityContainer);

    printf("\n" COL_WHITE " COMBAT\n" COL_RESET);
    printf(COL_GRAY "  [F7]" COL_RESET " Aimbot (F)     %s\n", Toggle(cfg.aimbot));
    printf(COL_GRAY "  [F8]" COL_RESET " No Recoil      %s\n", Toggle(cfg.noRecoil));
    printf(COL_GRAY "  [F9]" COL_RESET " No Spread      %s\n", Toggle(cfg.noSpread));
    printf(COL_GRAY " [F10]" COL_RESET " Rapid Fire     %s\n", Toggle(cfg.rapidFire));
    printf(COL_GRAY " [F11]" COL_RESET " Insta ADS      %s\n", Toggle(cfg.instaAds));

    printf("\n" COL_WHITE " PLAYER\n" COL_RESET);
    printf(COL_GRAY "  [f12]" COL_RESET " Inf. Stamina   %s\n", Toggle(cfg.infiniteStamina));

    printf("\n" COL_CYAN "=====================================\n" COL_RESET);
    printf(COL_GRAY     " Press keybind to toggle\n"             COL_RESET);
    printf(COL_CYAN     "=====================================\n" COL_RESET);
}

DWORD WINAPI ConsoleMenuThread(LPVOID) {
    EnableConsoleColors();
    PrintMenu();

    while (g_running) {
        if (GetAsyncKeyState(VK_END) & 1) { Unload(); return 0; }

        if (GetAsyncKeyState(VK_INSERT) & 1) { cfg.showSkeleton = !cfg.showSkeleton;       PrintMenu(); }
        if (GetAsyncKeyState(VK_F2) & 1) { cfg.showBoxes = !cfg.showBoxes;           PrintMenu(); }
        if (GetAsyncKeyState(VK_F3) & 1) { cfg.showHealthBars = !cfg.showHealthBars;      PrintMenu(); }
        if (GetAsyncKeyState(VK_F4) & 1) { cfg.showFOVCircle = !cfg.showFOVCircle;       PrintMenu(); }
        if (GetAsyncKeyState(VK_F5) & 1) { cfg.showLoot = !cfg.showLoot;            PrintMenu(); }
        if (GetAsyncKeyState(VK_F6) & 1) { cfg.showContainers = !cfg.showContainers;      PrintMenu(); }
        if (GetAsyncKeyState(VK_F7) & 1) { cfg.aimbot = !cfg.aimbot;              PrintMenu(); }
        if (GetAsyncKeyState(VK_F8) & 1) { cfg.noRecoil = !cfg.noRecoil;            PrintMenu(); }
        if (GetAsyncKeyState(VK_F9) & 1) { cfg.noSpread = !cfg.noSpread;   PrintMenu(); }
        if (GetAsyncKeyState(VK_F10) & 1) { cfg.rapidFire = !cfg.rapidFire;  PrintMenu(); }
        if (GetAsyncKeyState(VK_F11) & 1) { cfg.instaAds = !cfg.instaAds;   PrintMenu(); }
        if (GetAsyncKeyState(VK_F12) & 1) { cfg.infiniteStamina = !cfg.infiniteStamina;     PrintMenu(); }

        // FOV radius: [ and ]
        if (GetAsyncKeyState(VK_OEM_4) & 1) { cfg.fovRadius = max(10.0f, cfg.fovRadius - 10.0f); PrintMenu(); }
        if (GetAsyncKeyState(VK_OEM_6) & 1) { cfg.fovRadius = min(500.0f, cfg.fovRadius + 10.0f); PrintMenu(); }

        // Min loot rarity: ; and '
        if (GetAsyncKeyState(VK_OEM_1) & 1) { cfg.minRarityLoot = max(1, cfg.minRarityLoot - 1); PrintMenu(); }
        if (GetAsyncKeyState(VK_OEM_7) & 1) { cfg.minRarityLoot = min(6, cfg.minRarityLoot + 1); PrintMenu(); }

        // Min container rarity: , and .
        if (GetAsyncKeyState(VK_OEM_COMMA) & 1) { cfg.minRarityContainer = max(1, cfg.minRarityContainer - 1); PrintMenu(); }
        if (GetAsyncKeyState(VK_OEM_PERIOD) & 1) { cfg.minRarityContainer = min(6, cfg.minRarityContainer + 1); PrintMenu(); }

        Sleep(50);
    }
    return 0;
}

// ===================== POST RENDER =====================

void __fastcall hksPostRender(void* hud) {

    // Player name lines + text
    if (cfg.showSkeleton) {
        for (auto& current : PlayerScreenPositions) {
            DrawLineParams line{};
            line.X1 = screenCenterX; line.Y1 = 0.0f;
            line.X2 = current.first.x; line.Y2 = current.first.y;
            line.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            line.thickness = cfg.lineThickness;
            ProcessEvent(hud, DrawLineFn, &line);

            DrawTextParams text{};
            text.text = current.second;
            text.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            text.X = current.first.x; text.Y = current.first.y;
            text.font = nullptr; text.scale = 1.0f; text.scalePosition = true;
            ProcessEvent(hud, DrawTextFn, &text);
        }
    }


    // Loot lines + text
    if (cfg.showLoot || cfg.showContainers) {
        for (auto& item : LootScreenPositions) {
            DrawLineParams line{};
            line.X1 = screenCenterX; line.Y1 = 0.0f;
            line.X2 = item.Pos.x; line.Y2 = item.Pos.y;
            line.Color = item.Color; line.thickness = cfg.lineThickness;
            ProcessEvent(hud, DrawLineFn, &line);

            DrawTextParams text{};
            text.text = item.Name; text.color = item.Color;
            text.X = item.Pos.x; text.Y = item.Pos.y;
            text.font = nullptr; text.scale = 1.0f; text.scalePosition = true;
            ProcessEvent(hud, DrawTextFn, &text);
        }
    }

    // Skeletons
    if (cfg.showSkeleton) {
        for (const auto& playerBones : cachedPlayerBones) {
            for (const auto& conn : boneConnections) {
                auto b1 = playerBones.boneScreenPositions.find(conn.from);
                auto b2 = playerBones.boneScreenPositions.find(conn.to);
                if (b1 == playerBones.boneScreenPositions.end() ||
                    b2 == playerBones.boneScreenPositions.end()) continue;

                DrawLineParams line{};
                line.X1 = b1->second.x; line.Y1 = b1->second.y;
                line.X2 = b2->second.x; line.Y2 = b2->second.y;
                line.Color = { 1.0f, 1.0f, 1.0f, 1.0f }; line.thickness = 2.0f;
                ProcessEvent(hud, DrawLineFn, &line);
            }
        }
    }

    // Boxes + health bars
    for (int i = 0; i < (int)cachedPlayerBounds.size(); i++) {
        const auto& bounds = cachedPlayerBounds[i];
        if (!bounds.first.valid) continue;
        if (i >= (int)cachedPlayerHealth.size()) continue;

        float pad = 5.0f;
        float x1 = bounds.first.minX - pad, y1 = bounds.first.minY - pad;
        float x2 = bounds.first.maxX + pad, y2 = bounds.first.maxY + pad;
        float boxHeight = y2 - y1;
        FLinearColor boxColor = bounds.second;

        if (cfg.showBoxes) {
            DrawLineParams top{ x1,y1,x2,y1, boxColor, 1.0f };
            DrawLineParams bottom{ x1,y2,x2,y2, boxColor, 1.0f };
            DrawLineParams left{ x1,y1,x1,y2, boxColor, 1.0f };
            DrawLineParams right{ x2,y1,x2,y2, boxColor, 1.0f };
            ProcessEvent(hud, DrawLineFn, &top);
            ProcessEvent(hud, DrawLineFn, &bottom);
            ProcessEvent(hud, DrawLineFn, &left);
            ProcessEvent(hud, DrawLineFn, &right);
        }

        if (cfg.showHealthBars) {
            float barWidth = 4.0f;
            float barX = x1 - barWidth - 2.0f;
            float healthPct = max(0.0f, min(1.0f, cachedPlayerHealth[i] / 100.0f));

            DrawLineParams bg{ barX,y1, barX,y2, {0.2f,0.0f,0.0f,1.0f}, barWidth };
            ProcessEvent(hud, DrawLineFn, &bg);

            float fillY2 = y1 + (boxHeight * (1.0f - healthPct));
            DrawLineParams fill{ barX,fillY2, barX,y2, {1.0f - healthPct, healthPct, 0.0f, 1.0f}, barWidth };
            ProcessEvent(hud, DrawLineFn, &fill);
        }
    }

    // FOV circle
    if (cfg.showFOVCircle) {
        constexpr int   segments = 64;
        constexpr float angleStep = (2.0f * 3.14159f) / segments;
        for (int i = 0; i < segments; i++) {
            float a1 = i * angleStep, a2 = (i + 1) * angleStep;
            DrawLineParams line{
                screenCenterX + cosf(a1) * cfg.fovRadius,
                screenCenterY + sinf(a1) * cfg.fovRadius,
                screenCenterX + cosf(a2) * cfg.fovRadius,
                screenCenterY + sinf(a2) * cfg.fovRadius,
                {1.0f,1.0f,1.0f,1.0f}, 1.0f
            };
            ProcessEvent(hud, DrawLineFn, &line);
        }
    }

    // Logo
    DrawTextParams logo{};
    logo.X = 0.0f; logo.Y = 0.0f;
    logo.color = { 1.0f,1.0f,1.0f,1.0f };
    logo.font = nullptr; logo.scale = 1.3f; logo.scalePosition = true;
    logo.text = WStringToFString(L"https://discord.gg/dAsCS5rH5z");
    ProcessEvent(hud, DrawTextFn, &logo);

    return PostRenderOriginal(hud);
}

// ===================== VIEWPORT TICK =====================

void __fastcall hksGameViewportClientTick(void* obj, float dt) {
    void* gameinstance = *(void**)((uint8_t*)obj + 0xb8);
    if (!gameinstance) return viewportOriginalFn(obj, dt);
    TArray* lps = (TArray*)((uint8_t*)gameinstance + 0x70);
    if (!lps || !lps->Data) return viewportOriginalFn(obj, dt);
    void* lp = ((void**)lps->Data)[0];
    if (!lp) return viewportOriginalFn(obj, dt);

    void* playerController = *(void**)((uint8_t*)lp + 0x68);
    void* playerCamera = *(void**)((uint8_t*)playerController + 0x340);

    // No recoil
    void* weaponCompController = *(void**)((uint8_t*)playerController + 0x9a8);
    if (weaponCompController && (cfg.noRecoil || cfg.noSpread || cfg.rapidFire || cfg.instaAds)) {
        SafeApplyGunChanges(weaponCompController);
    }

    // Cache UFunction pointers once
    if (!K2_GetActorLocationFn)    K2_GetActorLocationFn = StaticFindObject(nullptr, nullptr, L"/Script/Engine.Actor:K2_GetActorLocation", false);
    if (!GetPlayerNameFn)          GetPlayerNameFn = StaticFindObject(nullptr, nullptr, L"/Script/Engine.PlayerState:GetPlayerName", false);
    if (!GetItemRarity)            GetItemRarity = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YPickupActor:GetItemRarity", false);
    if (!GetBaseItemRowHandle)     GetBaseItemRowHandle = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YPickupActor:GetBaseItemRowHandle", false);
    if (!GetCameraLocation)        GetCameraLocation = StaticFindObject(nullptr, nullptr, L"/Script/Engine.PlayerCameraManager:GetCameraLocation", false);
    if (!FindLookAtRotation)       FindLookAtRotation = StaticFindObject(nullptr, nullptr, L"/Script/Engine.KismetMathLibrary:FindLookAtRotation", false);
    if (!SetControlRotation)       SetControlRotation = StaticFindObject(nullptr, nullptr, L"/Script/Engine.Controller:SetControlRotation", false);
    if (!LineOfSightTo)            LineOfSightTo = StaticFindObject(nullptr, nullptr, L"/Script/Engine.Controller:LineOfSightTo", false);
    if (!ConsoleClass)             ConsoleClass = StaticFindObject(nullptr, nullptr, L"/Script/Engine.Console", false);
    if (!GameplayStaticsClass)     GameplayStaticsClass = StaticFindObject(nullptr, nullptr, L"/Script/Engine.GameplayStatics", false);
    if (!SpawnObjectFn)            SpawnObjectFn = StaticFindObject(nullptr, nullptr, L"/Script/Engine.GameplayStatics:SpawnObject", false);
    if (!CheatManager)             CheatManager = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YCheatManager", false);
    if (!DrawLineFn)               DrawLineFn = StaticFindObject(nullptr, nullptr, L"/Script/Engine.HUD:DrawLine", false);
    if (!DrawTextFn)               DrawTextFn = StaticFindObject(nullptr, nullptr, L"/Script/Engine.HUD:DrawText", false);
    if (!GetInventory)             GetInventory = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YLootContainer:GetInventory", false);
    if (!GetNameByRowHandle)       GetNameByRowHandle = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YItemFunctionsLibrary:GetNameByRowHandle", false);
    if (!GetItemRarityFromRowHandle) GetItemRarityFromRowHandle = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YItemFunctionsLibrary:GetItemRarityFromItemRowHandle", false);

    void* world = *(void**)((uint8_t*)obj + 0xB0);
    if (!world) return viewportOriginalFn(obj, dt);
    void* gamestate = *(void**)((uint8_t*)world + 0x1f80);
    if (!gamestate) return viewportOriginalFn(obj, dt);
    TArray* players = (TArray*)((uint8_t*)gamestate + 0x298);
    if (!players || !players->Data) return viewportOriginalFn(obj, dt);

    // Spawn console + cheat manager once
    if (ConsoleFlag) {
        ConsoleFlag = false;
        TArray StaticsOut{};
        GetObjectsOfClass(GameplayStaticsClass, &StaticsOut, true, 0, 0);
        void* StaticObject = ((void**)StaticsOut.Data)[0];

        struct { void* ObjectClass, * Outer, * ReturnValue; } SpawnParams{};
        SpawnParams.ObjectClass = ConsoleClass; SpawnParams.Outer = obj;
        ProcessEvent(StaticObject, SpawnObjectFn, &SpawnParams);

        struct { void* ObjectClass, * Outer, * ReturnValue; } SpawnCheatParams{};
        SpawnCheatParams.ObjectClass = CheatManager; SpawnCheatParams.Outer = playerController;
        ProcessEvent(StaticObject, SpawnObjectFn, &SpawnCheatParams);

        if (SpawnParams.ReturnValue)      *(void**)((uint8_t*)obj + 0x78) = SpawnParams.ReturnValue;
        if (SpawnCheatParams.ReturnValue) *(void**)((uint8_t*)playerController + 0x3c0) = SpawnCheatParams.ReturnValue;

        printf("Console: %p  CheatManager: %p\n", SpawnParams.ReturnValue, SpawnCheatParams.ReturnValue);
    }

    PlayerScreenPositions.clear();
    BonePositions.clear();

    void* localPawn = *(void**)((uint8_t*)playerController + 0x2B0);

    std::vector<PlayerBones>                      allPlayerBones;
    std::vector<std::pair<PlayerBounds, FLinearColor>> allPlayerBounds;
    static std::vector<float>                     allPlayerHealth;
    std::vector<FVector>                          heads;

    // Static class pointer — only looked up once
    static void* YCharacterObj = nullptr;
    if (!YCharacterObj) YCharacterObj = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YCharacter", false);

    TArray CharactersOut{};
    GetObjectsOfClass(YCharacterObj, &CharactersOut, true, 0, 0);
    void** Characters = (void**)CharactersOut.Data;

    for (int i = 0; i < CharactersOut.num; i++) {
        void* character = Characters[i];

        if (character == localPawn) {
            if (cfg.infiniteStamina) {
                void* staminaComponent = *(void**)((uint8_t*)character + 0x738);
                if (staminaComponent) {
                    *(bool*)((uint8_t*)staminaComponent + 0x138) = false;
                    *(float*)((uint8_t*)staminaComponent + 0x150) = 100.0f;
                }
            }
            continue;
        }

        void* playerstate = *(void**)((uint8_t*)character + 0x2A0);
        if (!playerstate) continue;
        void* skelemesh = *(void**)((uint8_t*)character + 0x2e0);
        if (!skelemesh) continue;

        void* healthComponent = *(void**)((uint8_t*)character + 0x538);
        float health = *(float*)((uint8_t*)healthComponent + 0x218);
        allPlayerHealth.push_back(health);

        struct { void* other; FVector view_point{}; bool alternate_checks, out; } losParams;
        losParams.other = character;
        losParams.view_point = { 0,0,0 };
        losParams.alternate_checks = false;
        ProcessEvent(playerController, LineOfSightTo, &losParams);
        FLinearColor visible = losParams.out
            ? FLinearColor{ 0.0f, 0.0f, 1.0f, 1.0f }
        : FLinearColor{ 1.0f, 0.0f, 0.0f, 1.0f };

        static const std::vector<int> bonesNeeded = {
            1,2,3,4,5,6,7,8,9,10,54,56,57,58,86,87,88,115,116,117,118,119,120,121,122
        };

        PlayerBones  playerBones;
        PlayerBounds bounds{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, false };

        for (int boneIndex : bonesNeeded) {
            FTransform trans = SafeGetBoneTransform(skelemesh, boneIndex);
            FVector loc = trans.Translation;
            if (loc.x == 0.0f && loc.y == 0.0f && loc.z == 0.0f) continue;

            FVector2D screen{};
            if (!ProjectWorldToScreen(playerController, &loc, &screen, false)) continue;

            playerBones.boneScreenPositions[boneIndex] = screen;
            if (screen.x < bounds.minX) bounds.minX = screen.x;
            if (screen.y < bounds.minY) bounds.minY = screen.y;
            if (screen.x > bounds.maxX) bounds.maxX = screen.x;
            if (screen.y > bounds.maxY) bounds.maxY = screen.y;
            bounds.valid = true;

            if (boneIndex == 10 && inFOV(screen))
                heads.push_back(loc);
        }

        allPlayerBones.push_back(playerBones);
        allPlayerBounds.emplace_back(bounds, visible);

        FVector location{};
        ProcessEvent(character, K2_GetActorLocationFn, &location);
        FString res{};
        ProcessEvent(playerstate, GetPlayerNameFn, &res);
        //FString res = WStringToFString(L"PlayerName");        // <-- hardcoded placeholder
        location.z += 100.0f;

        FVector2D out{};
        if (ProjectWorldToScreen(playerController, &location, &out, false))
            if (out.x != 0.0f || out.y != 0.0f)
                PlayerScreenPositions.emplace_back(out, res);
    }

    cachedPlayerBones = allPlayerBones;
    cachedPlayerBounds = allPlayerBounds;
    cachedPlayerHealth = allPlayerHealth;
    allPlayerHealth.clear();

    // Aimbot
    if (cfg.aimbot) {
        FVector CameraPos{};
        ProcessEvent(playerCamera, GetCameraLocation, &CameraPos);

        float   lowest = FLT_MAX;
        FVector selected{};
        for (const FVector& head : heads) {
            float dist = Distance(head, CameraPos);
            if (dist < lowest) { lowest = dist; selected = head; }
        }
        if (GetAsyncKeyState(0x47) & 0x8000) {
            struct { FVector ReturnValue; } camParams{};
            ProcessEvent(playerCamera, GetCameraLocation, &camParams);

            if (selected.x != 0.0f || selected.y != 0.0f || selected.z != 0.0f) {
                struct { FVector start, target; FRotator ReturnValue; } lookAt{};
                lookAt.start = camParams.ReturnValue; lookAt.target = selected;
                ProcessEvent(playerController, FindLookAtRotation, &lookAt);

                struct { FRotator NewRotation; } rotParams{};
                rotParams.NewRotation = lookAt.ReturnValue;
                ProcessEvent(playerController, SetControlRotation, &rotParams);
            }
        }
    }

    LootScreenPositions.clear();

    // Ground loot
    if (cfg.showLoot) {
        static void* YPickupActorClass = nullptr;
        if (!YPickupActorClass) YPickupActorClass = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YPickupActor", false);

        TArray outArray{};
        GetObjectsOfClass(YPickupActorClass, &outArray, true, 0, 0);
        void** pickups = (void**)outArray.Data;

        for (int i = 0; i < outArray.num; i++) {
            void* o = pickups[i];
            if (!o) continue;
			printf("Object pointer: %p\n", o);
            FVector pickupLoc{};
            ProcessEvent(o, K2_GetActorLocationFn, &pickupLoc);
            if (pickupLoc.x == 0.0f) continue;
            FVector2D screenpos{};
            if (!ProjectWorldToScreen(playerController, &pickupLoc, &screenpos, false)) continue;


            int32_t amount = *(int32_t*)((uint8_t*)o + 0x02E0);
            FYInventoryItem* invItem = (FYInventoryItem*)((uint8_t*)o + 0x02E8);
            if (!invItem->m_item.DataTable) continue;

            wchar_t nameBuf[256] = {};
            if (!SafeFNameToString(&invItem->m_item.RowName, nameBuf, 256)) continue;
            std::wstring itemName(nameBuf);
            if (itemName.find(L"None") != std::wstring::npos) continue;

            struct { uint8_t ReturnValue; } RarityParams{};
            ProcessEvent(o, GetItemRarity, &RarityParams);
            if (RarityParams.ReturnValue < cfg.minRarityLoot) continue;

            LootScreenPoolItem tempitem{};
            tempitem.Pos = screenpos;
            tempitem.Name = (amount > 1)
                ? WStringToFString(itemName + L" [" + std::to_wstring(amount) + L"]")
                : WStringToFString(itemName);
            tempitem.Color = RarityToColor(RarityParams.ReturnValue);
            LootScreenPositions.emplace_back(tempitem);
        }
    }

    // Containers
    if (cfg.showContainers) {
        static void* LootContainerClass = nullptr;
        static void* ItemFuncLibClass = nullptr;
        if (!LootContainerClass) LootContainerClass = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YLootContainer", false);
        if (!ItemFuncLibClass)   ItemFuncLibClass = StaticFindObject(nullptr, nullptr, L"/Script/Prospect.YItemFunctionsLibrary", false);

        TArray LootOut{};
        GetObjectsOfClass(LootContainerClass, &LootOut, true, 0, 0);

        TArray LibOut{};
        GetObjectsOfClass(ItemFuncLibClass, &LibOut, true, 0, 0);
        if (!LibOut.Data || LibOut.num == 0) return viewportOriginalFn(obj, dt);
        void* LibInstance = ((void**)LibOut.Data)[0];

        void** Containers = (void**)LootOut.Data;
        for (int i = 0; i < LootOut.num; i++) {
            void* container = Containers[i];
            if (!container) continue;

            FVector containerLoc{};
            ProcessEvent(container, K2_GetActorLocationFn, &containerLoc);
            if (containerLoc.x == 0.0f && containerLoc.y == 0.0f && containerLoc.z == 0.0f) continue;

            FVector2D containerScreen{};
            if (!ProjectWorldToScreen(playerController, &containerLoc, &containerScreen, false)) continue;

            void* invComp = *(void**)((uint8_t*)container + 0x530);
            if (!invComp) continue;

            FYPlayerInventory* player_inv = (FYPlayerInventory*)((uint8_t*)invComp + 0x228);
            if (!player_inv->m_replicatedItems.Data) continue;
            if (player_inv->m_replicatedItems.num <= 0 || player_inv->m_replicatedItems.num > 500) continue;

            FYInventoryItem* items = (FYInventoryItem*)player_inv->m_replicatedItems.Data;

            for (int j = 0; j < player_inv->m_replicatedItems.num; j++) {
                FYInventoryItem& rep_item = items[j];
                if (!rep_item.m_item.DataTable) continue;

                wchar_t nameBuf[256] = {};
                if (!SafeFNameToString(&rep_item.m_item.RowName, nameBuf, 256)) continue;
                std::wstring itemName(nameBuf);
                if (itemName.empty() || itemName.find(L"None") != std::wstring::npos) continue;

                struct { FDataTableRowHandle itemDataTableRowHandle; uint8_t ReturnValue; } RarityParams{};
                RarityParams.itemDataTableRowHandle = rep_item.m_item;
                ProcessEvent(LibInstance, GetItemRarityFromRowHandle, &RarityParams);
                if (RarityParams.ReturnValue < cfg.minRarityContainer) continue;

                FVector2D itemScreen = containerScreen;
                itemScreen.y += j * 15.0f;

                LootScreenPoolItem tempitem{};
                tempitem.Pos = itemScreen;
                tempitem.Name = (rep_item.m_amount > 1)
                    ? WStringToFString(itemName + L" [" + std::to_wstring(rep_item.m_amount) + L"]")
                    : WStringToFString(itemName);
                tempitem.Color = RarityToColor(RarityParams.ReturnValue);
                LootScreenPositions.emplace_back(tempitem);
            }
        }
    }

    return viewportOriginalFn(obj, dt);
}

// ===================== UNLOAD =====================



// ===================== HOOK SETUP =====================

void hkSetupGameViewportClientTick() {
    uintptr_t target = BaseAddress + ViewportTickOffset;
    viewportDetour = std::make_unique<PLH::x64Detour>(target, (uint64_t)&hksGameViewportClientTick, (uint64_t*)&viewportOriginalFn);
    if (viewportDetour->hook()) printf("Hooked ViewportTick\n");
}

void hkSetupPostRender() {
    uintptr_t target = BaseAddress + PostRenderOffset;
    PostRenderDetour = std::make_unique<PLH::x64Detour>(target, (uint64_t)&hksPostRender, (uint64_t*)&PostRenderOriginal);
    if (PostRenderDetour->hook()) printf("Hooked PostRender\n");
}

void hooks() {
    hkSetupGameViewportClientTick();
    Sleep(500);
    hkSetupPostRender();
}

// ===================== CONSOLE + ENTRY =====================

void CreateConsole() {
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONIN$", "r", stdin);
    freopen_s(&f, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio();
    printf("[Cheat] Console initialized!\n");
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    BaseAddress = (uintptr_t)GetModuleHandle(NULL);
    ProcessEvent = (ProcessEventFn)(BaseAddress + ProcessEventOffset);
    ProjectWorldToScreen = (ProjectWorldToScreenFn)(BaseAddress + ProjectWorldToScreenOffset);
    StaticFindObject = (StaticFindObjectFn)(BaseAddress + StaticFindObjectOffset);
    GetFullName = (GetFullNameFn)(BaseAddress + GetFullNameOffset);
    GetObjectsOfClass = (GetObjectsOfClassFn)(BaseAddress + GetObjectsOfClassOffset);
    FNameGetEntry = (FNameGetEntryFn)(BaseAddress + FNameGetEntryOffset);
    FNameToString = (FNameToStringFn)(BaseAddress + FNameToStringOffset);
    GetBoneName = (GetBoneNameFn)(BaseAddress + GetBoneNameOffset);
    GetBoneTransform = (GetBoneTransformFn)(BaseAddress + GetBoneTransformOffset);

    hooks();
    CreateThread(nullptr, 0, ConsoleMenuThread, nullptr, 0, nullptr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateConsole();
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}
