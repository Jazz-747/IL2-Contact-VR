// XR_APILAYER_IL2CONTACT: тонкий OpenXR API layer.
// Задача: добавить XR_EXT_hand_tracking, каждый кадр (xrEndFrame) снять позы
// головы и ключевых суставов обеих рук в STAGE-пространстве и отправить их
// UDP-пакетом на 127.0.0.1:4410. Никакой логики здесь нет, вся логика в Python.
//
// Лог: %LOCALAPPDATA%\IL2Contact\layer.log

#define XR_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <d3d11.h>
#include <unknwn.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kPort = 4410;
constexpr uint32_t kMagic = 0x48324C49;  // байты "IL2H"
constexpr uint32_t kVersion = 1;

// Суставы, которые уходят в Python (порядок важен, он зашит в receiver.py).
constexpr XrHandJointEXT kJoints[8] = {
    XR_HAND_JOINT_PALM_EXT,          // 0
    XR_HAND_JOINT_INDEX_PROXIMAL_EXT,  // 1
    XR_HAND_JOINT_INDEX_TIP_EXT,       // 2
    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, // 3
    XR_HAND_JOINT_MIDDLE_TIP_EXT,      // 4
    XR_HAND_JOINT_RING_TIP_EXT,        // 5
    XR_HAND_JOINT_LITTLE_TIP_EXT,      // 6
    XR_HAND_JOINT_THUMB_TIP_EXT,       // 7
};

#pragma pack(push, 1)
struct HandPkt {
    uint32_t active;   // 1 = рука отслеживается и все 8 суставов валидны
    float j[8][3];     // x, y, z в метрах, STAGE
};
struct Pkt {           // 256 байт
    uint32_t magic;
    uint32_t version;
    uint64_t frame;
    int64_t xrTime;    // displayTime кадра, нс
    uint32_t headValid;
    float head[7];     // x, y, z, qx, qy, qz, qw
    HandPkt hand[2];   // 0 = левая, 1 = правая
};
#pragma pack(pop)
static_assert(sizeof(Pkt) == 256, "размер пакета должен быть 256 байт");

// --- состояние -------------------------------------------------------------
PFN_xrGetInstanceProcAddr g_next = nullptr;
XrInstance g_inst = XR_NULL_HANDLE;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_stage = XR_NULL_HANDLE;
XrSpace g_view = XR_NULL_HANDLE;
XrHandTrackerEXT g_ht[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
bool g_handExt = false;

// --- маркер кончика пальца: маленький квад-слой с точкой, рисуется поверх кадра игры ---
// Включён по умолчанию; отключить: переменная окружения IL2CONTACT_MARKER=0.
// Показывается только когда рука отслеживается >30 кадров подряд и палец вытянут.
// Текстура-атлас 2*kMarkerPx x kMarkerPx: левая половина = маркер левой руки, правая = правой (у каждой своё кольцо).
// Масштаб 3200 пикс/м как раньше (64 пикс = 2 см): внутри диск маркера, снаружи кольцо заполнения при задержке.
constexpr int kMarkerPx = 96;
constexpr float kMarkerSize = 0.03f;  // сторона квада одной руки, м (при размере маркера 18 мм)
struct MarkerState {
    bool show = false;
    XrVector3f pos{0, 0, 0};
    int activeFrames = 0;
    bool pointing = false;      // жест «указывает» с гистерезисом (как в hands.py)
    ULONGLONG pointMs = 0;      // когда жест в последний раз был «сырым»
};
MarkerState g_mk[2];
XrQuaternionf g_headQ{0, 0, 0, 1};
XrVector3f g_headP{0, 0, 0};
bool g_headOk = false;
bool g_markerEnabled = true;
bool g_markerReady = false;
bool g_mbgra = false;
int64_t g_mfmt = 0;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
XrSwapchain g_msc = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> g_mtex;

PFN_xrCreateSession n_xrCreateSession = nullptr;
PFN_xrDestroySession n_xrDestroySession = nullptr;
PFN_xrDestroyInstance n_xrDestroyInstance = nullptr;
PFN_xrEndFrame n_xrEndFrame = nullptr;
PFN_xrCreateReferenceSpace n_xrCreateReferenceSpace = nullptr;
PFN_xrDestroySpace n_xrDestroySpace = nullptr;
PFN_xrLocateSpace n_xrLocateSpace = nullptr;
PFN_xrLocateViews n_xrLocateViews = nullptr;
PFN_xrCreateHandTrackerEXT n_xrCreateHandTrackerEXT = nullptr;
PFN_xrDestroyHandTrackerEXT n_xrDestroyHandTrackerEXT = nullptr;
PFN_xrLocateHandJointsEXT n_xrLocateHandJointsEXT = nullptr;
PFN_xrGetInstanceProperties n_xrGetInstanceProperties = nullptr;
PFN_xrGetSystem n_xrGetSystem = nullptr;
PFN_xrGetSystemProperties n_xrGetSystemProperties = nullptr;
PFN_xrEnumerateInstanceExtensionProperties n_xrEnumExt = nullptr;
PFN_xrCreateSwapchain n_xrCreateSwapchain = nullptr;
PFN_xrDestroySwapchain n_xrDestroySwapchain = nullptr;
PFN_xrEnumerateSwapchainFormats n_xrEnumerateSwapchainFormats = nullptr;
PFN_xrEnumerateSwapchainImages n_xrEnumerateSwapchainImages = nullptr;
PFN_xrAcquireSwapchainImage n_xrAcquireSwapchainImage = nullptr;
PFN_xrWaitSwapchainImage n_xrWaitSwapchainImage = nullptr;
PFN_xrReleaseSwapchainImage n_xrReleaseSwapchainImage = nullptr;

SOCKET g_sock = INVALID_SOCKET;
// Команды от Python (UDP 127.0.0.1:4411, 8 байт): цвет и заполнение маркера.
// magic 0x4B4D3249, base: 0 зелёный (обычный), 1 жёлтый (нужна калибровка), 2 красный (нужно обучение),
// frac 0..255: доля круга (по часовой от верха), закрашенная зелёным (прогресс калибровки/обучения).
// байты 6 и 7 (необязательные): кольцо заполнения вокруг маркера левой/правой руки, 0 = нет кольца, 1..255 = доля
// задержки перед срабатыванием зоны (по часовой от верха).
// байт 8 (необязательный): диаметр маркера в мм (5..18; 18 = обычный размер), кольцо масштабируется так же; иначе 18.
// байт 13 (необязательный): вид маркера, 0 = диск (по умолчанию), 1 = кольцо с прозрачным центром (диаметры 4:3, сектор прогресса только по кольцу).
// байты 10-12 (необязательные): основной цвет RGB (вместо зелёного по умолчанию 80,255,60); им же закрашивается доля круга; 0,0,0 = не задан.
// Если команд нет дольше 5 с (Python остановлен), маркер возвращается к обычному зелёному.
constexpr uint16_t kCmdPort = 4411;
constexpr uint32_t kCmdMagic = 0x4B4D3249;
SOCKET g_cmd = INVALID_SOCKET;
uint8_t g_mkBase = 0, g_mkFrac = 0;
uint8_t g_mkRing[2] = {0, 0};
uint8_t g_mkMain[3] = {80, 255, 60};  // основной цвет маркера (команда байты 10-12)
float g_mkScale = 1.0f;
uint8_t g_mkStyle = 0;  // вид маркера (команда байт 13): 0 диск, 1 кольцо с прозрачным центром (внешний:внутренний диаметр 4:3)

// --- отладочный показ зон (только для отладки): по кружку на зону, квад-слой в STAGE ---
// Команда Python (UDP 4411): magic 0x5A4E3249, байт N (число зон), N раз: x y z (float, STAGE, м), диаметр мм (байт), флаги (байт, 1 = жёлтое кольцо).
// Зоны показываются, пока пакеты приходят (последний не старше 1 с); пустой список скрывает их. Центр 6 мм и тонкое кольцо радиуса зоны.
// Размер зоны приходит диаметром в мм, округляется до 5 мм (5..120). Кольцо пунктирное. Атлас: колонка = диаметр, строка 0 = серое кольцо, строка 1 = жёлтое (тройка калибровки).
// Слоёв может не хватить (лимит рантайма): тогда число показанных зон уменьшается вдвое, пока кадр не примется.
constexpr uint32_t kZoneMagic = 0x5A4E3249;
constexpr int kZCell = 128;
constexpr int kZBuckets = 24;
constexpr int kZMax = 120;
struct ZoneItem {
    float x, y, z;
    uint8_t dmm, flags;  // диаметр зоны, мм
};
std::vector<ZoneItem> g_zones;
ULONGLONG g_zonesMs = 0;
size_t g_zCap = kZMax;  // сколько зон показывать (каждая зона - отдельный слой); при отказе рантайма уменьшается (см. xrEndFrame)
XrSwapchain g_zsc = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> g_ztex;
bool g_zReady = false;
ULONGLONG g_mkCmdMs = 0;
sockaddr_in g_dst{};
uint64_t g_frame = 0;
bool g_lastActive[2] = {false, false};
std::ofstream g_log;
size_t g_logBytes = 0;                    // записано в лог
constexpr size_t kLogMax = 256 * 1024;   // предел размера лога: дальше запись прекращается
std::string g_dir, g_exe;  // папка и имя exe (для диагностического CSV)
std::mutex g_mtx;

// --- лог -------------------------------------------------------------------
void openLog() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_log.is_open()) return;
    // Папка лога: рядом со слоем (%ProgramData%\\IL2Contact). Если туда писать нельзя: %LOCALAPPDATA%\\IL2Contact, затем TEMP.
    char self[MAX_PATH] = {};
    HMODULE hm = nullptr;
    std::string dir;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&openLog), &hm) &&
        GetModuleFileNameA(hm, self, MAX_PATH)) {
        std::string d = self;
        size_t k = d.find_last_of('\\');
        if (k != std::string::npos) dir = d.substr(0, k);
    }
    auto fallback = [&]() {
        char base[MAX_PATH] = {};
        if (!GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH)) {
            if (!GetTempPathA(MAX_PATH, base)) return std::string();
        }
        std::string d = std::string(base) + "\\IL2Contact";
        CreateDirectoryA(d.c_str(), nullptr);
        return d;
    };
    if (dir.empty()) dir = fallback();
    if (dir.empty()) return;
    g_dir = dir;
    // Лог на каждый процесс отдельно: слой грузится во все OpenXR-приложения,
    // и общий файл перезатирался бы чужими процессами.
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* name = strrchr(exe, '\\');
    name = name ? name + 1 : exe;
    g_exe = name;
    g_log.open(dir + "\\layer_" + name + ".log", std::ios::trunc);
    if (!g_log.is_open()) {  // нет прав на запись рядом со слоем
        dir = fallback();
        if (dir.empty()) return;
        g_dir = dir;
        g_log.open(dir + "\\layer_" + name + ".log", std::ios::trunc);
    }
}

void logf_(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_log.is_open()) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[640];
    int n = snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute,
                     st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    va_end(ap);
    if (g_logBytes >= kLogMax) return;
    g_logBytes += strlen(buf) + 2;
    if (g_logBytes >= kLogMax) {
        g_log << buf << std::endl << "... лог достиг предела " << (kLogMax / 1024) << " КБ, дальше не пишется (до перезапуска игры)" << std::endl;
        return;
    }
    g_log << buf << std::endl;
}

// --- сеть ------------------------------------------------------------------
void netInit() {
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        logf_("WSAStartup не удался");
        return;
    }
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        logf_("socket() не удался: %d", WSAGetLastError());
        return;
    }
    g_dst.sin_family = AF_INET;
    g_dst.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &g_dst.sin_addr);
    logf_("UDP -> 127.0.0.1:%u", (unsigned)kPort);

    g_cmd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_cmd != INVALID_SOCKET) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(kCmdPort);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        u_long nb = 1;
        if (bind(g_cmd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ioctlsocket(g_cmd, FIONBIO, &nb) != 0) {
            logf_("команды маркера: порт %u недоступен (%d), цвет маркера не меняется", (unsigned)kCmdPort,
                  WSAGetLastError());
            closesocket(g_cmd);
            g_cmd = INVALID_SOCKET;
        } else {
            logf_("команды маркера: UDP 127.0.0.1:%u", (unsigned)kCmdPort);
        }
    }
}

void netClose() {
    if (g_cmd != INVALID_SOCKET) {
        closesocket(g_cmd);
        g_cmd = INVALID_SOCKET;
    }
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        WSACleanup();
    }
}

// --- сессия ----------------------------------------------------------------
void teardownSession() {
    g_markerReady = false;
    g_zReady = false;
    if (g_zsc != XR_NULL_HANDLE && n_xrDestroySwapchain) n_xrDestroySwapchain(g_zsc);
    g_zsc = XR_NULL_HANDLE;
    g_ztex.clear();
    g_zones.clear();
    if (g_msc != XR_NULL_HANDLE && n_xrDestroySwapchain) n_xrDestroySwapchain(g_msc);
    g_msc = XR_NULL_HANDLE;
    g_mtex.clear();
    if (g_ctx) {
        g_ctx->Release();
        g_ctx = nullptr;
    }
    if (g_dev) {
        g_dev->Release();
        g_dev = nullptr;
    }
    for (auto& h : g_ht) {
        if (h != XR_NULL_HANDLE && n_xrDestroyHandTrackerEXT) n_xrDestroyHandTrackerEXT(h);
        h = XR_NULL_HANDLE;
    }
    if (g_stage != XR_NULL_HANDLE && n_xrDestroySpace) n_xrDestroySpace(g_stage);
    if (g_view != XR_NULL_HANDLE && n_xrDestroySpace) n_xrDestroySpace(g_view);
    g_stage = g_view = XR_NULL_HANDLE;
    g_session = XR_NULL_HANDLE;
}

// --- отладочные зоны: swapchain-атлас ---
void zonesInit(XrSession s) {
    g_zReady = false;
    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sci.format = g_mfmt;
    sci.sampleCount = 1;
    sci.width = kZBuckets * kZCell;
    sci.height = 3 * kZCell;  // строки: серое кольцо, жёлтое кольцо, точка 5 мм (зафиксированная точка при привязке)
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;
    XrResult r = n_xrCreateSwapchain(s, &sci, &g_zsc);
    if (XR_FAILED(r)) {
        logf_("зоны: xrCreateSwapchain вернул %d", (int)r);
        g_zsc = XR_NULL_HANDLE;
        return;
    }
    uint32_t cnt = 0;
    n_xrEnumerateSwapchainImages(g_zsc, 0, &cnt, nullptr);
    std::vector<XrSwapchainImageD3D11KHR> im(cnt);
    for (auto& x : im) x = XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if (cnt == 0 || XR_FAILED(n_xrEnumerateSwapchainImages(
                        g_zsc, cnt, &cnt, reinterpret_cast<XrSwapchainImageBaseHeader*>(im.data())))) {
        logf_("зоны: не получены изображения swapchain");
        n_xrDestroySwapchain(g_zsc);
        g_zsc = XR_NULL_HANDLE;
        return;
    }
    g_ztex.clear();
    for (auto& x : im) g_ztex.push_back(x.texture);
    g_zReady = true;
    logf_("зоны (отладка): готовы (изображений %u)", cnt);
}

float zoneSide(int bucket) { return (bucket + 1) * 0.005f + 0.006f; }  // диаметр зоны + поля 3 мм с каждой стороны

// Рисует атлас зон в очередное изображение swapchain (содержимое постоянное).
bool zonesDraw() {
    static std::vector<uint8_t> px;
    static bool built = false;
    const int W = kZBuckets * kZCell, H = 3 * kZCell;
    if (!built) {
        px.assign((size_t)W * H * 4, 0);
        const float c0 = (kZCell - 1) * 0.5f;
        for (int row = 0; row < 3; ++row)
            for (int b = 0; b < kZBuckets; ++b) {
                float rM = (b + 1) * 0.0025f;  // радиус кольца = половина диаметра
                float ppm = kZCell / zoneSide(b);  // пикселей на метр
                float dotR = (row == 2 ? 0.0025f : 0.003f) * ppm, ringR = rM * ppm, ringH = std::max(0.000375f * ppm, 0.6f);  // кольцо вдвое тоньше прежнего
                float circ = 6.2832f * rM;
                int nDash = std::max(8, (int)std::lround(circ / 0.005f));  // штрих + промежуток ~ 5 мм
                for (int y = 0; y < kZCell; ++y)
                    for (int x = 0; x < kZCell; ++x) {
                        float dx = x - c0, dy = y - c0;
                        float d = std::sqrt(dx * dx + dy * dy);
                        float aDot = std::min(1.f, std::max(0.f, dotR + 0.5f - d));
                        float aRing = std::min(1.f, std::max(0.f, ringH + 0.5f - std::fabs(d - ringR)));
                        float ph = std::atan2(dy, dx) / 6.2832f * nDash;  // пунктир: штрих занимает 60% периода
                        ph -= std::floor(ph);
                        if (ph > 0.6f || row == 2) aRing = 0.f;  // строка 2: только точка
                        uint8_t cr = 215, cg = 215, cb = 215;  // светло-серый
                        if (row == 2) { cr = 80; cg = 230; cb = 110; }  // зафиксированная точка: ярко-зелёная
                        uint8_t rr = row ? 255 : 215, rg = row ? 220 : 215, rb = row ? 0 : 215;  // кольцо: серое или жёлтое
                        float a = std::max(aDot, aRing);
                        if (a <= 0.f) continue;
                        bool ring = aRing >= aDot;
                        uint8_t R = ring ? rr : cr, G = ring ? rg : cg, B = ring ? rb : cb;
                        uint8_t* o = &px[(((size_t)row * kZCell + y) * W + b * kZCell + x) * 4];
                        o[0] = g_mbgra ? B : R;
                        o[1] = G;
                        o[2] = g_mbgra ? R : B;
                        o[3] = (uint8_t)(a * 255.f);
                    }
            }
        built = true;
    }
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(n_xrAcquireSwapchainImage(g_zsc, &ai, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    XrResult r = n_xrWaitSwapchainImage(g_zsc, &wi);
    if (XR_SUCCEEDED(r) && idx < g_ztex.size()) g_ctx->UpdateSubresource(g_ztex[idx], 0, nullptr, px.data(), W * 4, 0);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    n_xrReleaseSwapchainImage(g_zsc, &ri);
    return XR_SUCCEEDED(r);
}

// --- маркер: инициализация и отрисовка -------------------------------------------
// Маркер рисуется только в процессе игры Il-2.exe (слой грузится во все OpenXR-приложения; в остальных ничего не рисуем).
bool isIl2Process() {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* name = strrchr(exe, '\\');
    name = name ? name + 1 : exe;
    return _stricmp(name, "Il-2.exe") == 0;
}

void markerInit(XrSession s) {
    g_markerReady = false;
    if (!isIl2Process()) {
        logf_("маркер выключен: процесс %s не Il-2.exe", g_exe.c_str());
        return;
    }
    char env[8] = {};
    if (GetEnvironmentVariableA("IL2CONTACT_MARKER", env, sizeof env) && env[0] == '0') {
        logf_("маркер выключен переменной IL2CONTACT_MARKER=0");
        return;
    }
    if (!g_dev || !n_xrCreateSwapchain || !n_xrEnumerateSwapchainFormats ||
        !n_xrEnumerateSwapchainImages || !n_xrAcquireSwapchainImage || !n_xrWaitSwapchainImage ||
        !n_xrReleaseSwapchainImage) {
        logf_("маркер недоступен: нет D3D11-устройства сессии (игра не на D3D11?) или функций swapchain");
        return;
    }
    uint32_t n = 0;
    n_xrEnumerateSwapchainFormats(s, 0, &n, nullptr);
    std::vector<int64_t> fmts(n);
    if (n == 0 || XR_FAILED(n_xrEnumerateSwapchainFormats(s, n, &n, fmts.data()))) {
        logf_("маркер: не получен список форматов swapchain");
        return;
    }
    const int64_t pref[4] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                             DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    g_mfmt = 0;
    for (int64_t p : pref) {
        for (int64_t f : fmts)
            if (f == p) {
                g_mfmt = p;
                break;
            }
        if (g_mfmt) break;
    }
    if (!g_mfmt) {
        logf_("маркер: подходящий формат swapchain не найден");
        return;
    }
    g_mbgra = (g_mfmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || g_mfmt == DXGI_FORMAT_B8G8R8A8_UNORM);

    XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sci.format = g_mfmt;
    sci.sampleCount = 1;
    sci.width = 2 * kMarkerPx;
    sci.height = kMarkerPx;
    sci.faceCount = 1;
    sci.arraySize = 1;
    sci.mipCount = 1;
    XrResult r = n_xrCreateSwapchain(s, &sci, &g_msc);
    if (XR_FAILED(r)) {
        logf_("маркер: xrCreateSwapchain вернул %d", (int)r);
        g_msc = XR_NULL_HANDLE;
        return;
    }
    uint32_t cnt = 0;
    n_xrEnumerateSwapchainImages(g_msc, 0, &cnt, nullptr);
    std::vector<XrSwapchainImageD3D11KHR> im(cnt);
    for (auto& x : im) x = XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if (cnt == 0 || XR_FAILED(n_xrEnumerateSwapchainImages(
                        g_msc, cnt, &cnt, reinterpret_cast<XrSwapchainImageBaseHeader*>(im.data())))) {
        logf_("маркер: не получены изображения swapchain");
        n_xrDestroySwapchain(g_msc);
        g_msc = XR_NULL_HANDLE;
        return;
    }
    g_mtex.clear();
    for (auto& x : im) g_mtex.push_back(x.texture);
    g_dev->GetImmediateContext(&g_ctx);
    g_markerReady = (g_ctx != nullptr);
    logf_("маркер: готов (формат DXGI %lld, изображений %u)", (long long)g_mfmt, cnt);
    zonesInit(s);
}

// Рисует точку в очередное изображение swapchain. Содержимое одно и то же каждый кадр.
void pollCmd() {
    if (g_cmd != INVALID_SOCKET) {
        uint8_t buf[2000];  // команда маркера до 12 байт; список зон до 5 + 120*14 байт
        for (int i = 0; i < 16; ++i) {  // забираем все накопившиеся команды, действует последняя
            int n = recv(g_cmd, reinterpret_cast<char*>(buf), sizeof buf, 0);
            if (n < 5) break;
            uint32_t m;
            memcpy(&m, buf, 4);
            if (m == kZoneMagic) {  // список зон (отладка)
                int cnt = std::min<int>(buf[4], kZMax);
                if (n >= 5 + cnt * 14) {
                    g_zones.resize(cnt);
                    for (int k = 0; k < cnt; ++k) {
                        const uint8_t* p = buf + 5 + k * 14;
                        memcpy(&g_zones[k].x, p, 4);
                        memcpy(&g_zones[k].y, p + 4, 4);
                        memcpy(&g_zones[k].z, p + 8, 4);
                        g_zones[k].dmm = p[12];
                        g_zones[k].flags = p[13];
                    }
                    g_zonesMs = GetTickCount64();
                }
                continue;
            }
            if (n < 6 || m != kCmdMagic) continue;
            g_mkBase = buf[4] <= 3 ? buf[4] : 0;
            g_mkFrac = buf[5];
            g_mkRing[0] = n >= 8 ? buf[6] : 0;
            g_mkRing[1] = n >= 8 ? buf[7] : 0;
            g_mkScale = (n >= 9 && buf[8] >= 5 && buf[8] <= 18) ? buf[8] / 18.0f : 1.0f;
            if (n >= 12 && (buf[9] | buf[10] | buf[11]) != 0) {
                g_mkMain[0] = buf[9];
                g_mkMain[1] = buf[10];
                g_mkMain[2] = buf[11];
            }
            g_mkStyle = (n >= 13 && buf[12] == 1) ? 1 : 0;
            g_mkCmdMs = GetTickCount64();
        }
    }
    if (g_mkBase != 0 || g_mkFrac != 0 || g_mkRing[0] != 0 || g_mkRing[1] != 0) {
        if (GetTickCount64() - g_mkCmdMs > 5000) {
            g_mkBase = 0;
            g_mkFrac = 0;
            g_mkRing[0] = g_mkRing[1] = 0;
        }
    }
}

bool markerDraw() {
    static uint8_t px[2 * kMarkerPx * kMarkerPx * 4];
    static uint64_t cachedKey = ~0ull;
    pollCmd();
    uint64_t key = ((uint64_t)g_mkStyle << 56) | ((uint64_t)g_mkMain[0] << 48) | ((uint64_t)g_mkMain[1] << 40) | ((uint64_t)g_mkMain[2] << 32) | ((uint64_t)g_mkBase << 24) | ((uint64_t)g_mkFrac << 16) | ((uint64_t)g_mkRing[0] << 8) | g_mkRing[1];
    if (key != cachedKey) {
        // основной цвет по состоянию; доля круга по часовой от верха закрашена зелёным (всегда стандартным, не MARKER_COLOR);
        // снаружи диска кольцо задержки: тусклая дорожка и яркая заполненная часть по часовой от верха
        static const uint8_t kCol[4][3] = {{80, 255, 60}, {255, 220, 0}, {255, 40, 30}, {204, 204, 204}};  // зелёный, жёлтый, красный, серый (коррекция)
        uint8_t kMain[3] = {g_mkMain[0], g_mkMain[1], g_mkMain[2]};
        const uint8_t* base = g_mkBase == 0 ? kMain : kCol[g_mkBase];
        const float c0 = (kMarkerPx - 1) * 0.5f;
        for (int hand = 0; hand < 2; ++hand) {
            float ring = g_mkRing[hand] / 255.0f;
            for (int y = 0; y < kMarkerPx; ++y)
                for (int x = 0; x < kMarkerPx; ++x) {
                    float dx = x - c0, dy = y - c0;
                    float d = std::sqrt(dx * dx + dy * dy);
                    float a = d <= 22.f ? 1.f : (d <= 28.f ? (28.f - d) / 6.f : 0.f);
                    if (g_mkStyle == 1)  // кольцо: внешний радиус 28, внутренний 21 (4:3), центр прозрачный
                        a = d >= 28.f ? 0.f : (d >= 27.f ? 28.f - d : (d <= 20.f ? 0.f : (d < 21.f ? d - 20.f : 1.f)));
                    float ang = std::atan2(dx, -dy);  // 0 = верх, по часовой
                    if (ang < 0) ang += 6.2831853f;
                    float turn = ang / 6.2831853f;
                    bool sector = g_mkBase != 0 && turn < g_mkFrac / 255.0f;
                    const uint8_t* c = sector ? kCol[0] : base;  // сектор прогресса всегда зелёный
                    bool whiteDot = d < 7.f && g_mkStyle == 0;  // у кольца центр прозрачный, белой точки нет
                    uint8_t r = whiteDot ? 255 : c[0], g = whiteDot ? 255 : c[1], b = whiteDot ? 255 : c[2];
                    if (ring > 0.f && d >= 34.f && d <= 42.f) {
                        float edge = std::min(d - 34.f, 42.f - d);  // мягкие края кольца
                        float k = edge >= 1.f ? 1.f : (edge < 0.f ? 0.f : edge);
                        bool on = turn < ring;
                        r = 255; g = on ? 255 : 200; b = on ? 255 : 200;
                        a = k * (on ? 1.f : 0.35f);
                    }
                    uint8_t* o = &px[(y * 2 * kMarkerPx + hand * kMarkerPx + x) * 4];
                    if ((uint8_t)(a * 255.f) == 0) r = g = b = 0;  // у полностью прозрачных пикселей цвет 0: иначе на краю квада при фильтрации проступает тонкая цветная полоска
                    o[0] = g_mbgra ? b : r;
                    o[1] = g;
                    o[2] = g_mbgra ? r : b;
                    o[3] = (uint8_t)(a * 255.f);
                }
        }
        cachedKey = key;
    }
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(n_xrAcquireSwapchainImage(g_msc, &ai, &idx))) return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    XrResult r = n_xrWaitSwapchainImage(g_msc, &wi);
    if (XR_SUCCEEDED(r) && idx < g_mtex.size())
        g_ctx->UpdateSubresource(g_mtex[idx], 0, nullptr, px, 2 * kMarkerPx * 4, 0);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    n_xrReleaseSwapchainImage(g_msc, &ri);
    return XR_SUCCEEDED(r);
}

// Диагностика: что за рантайм и заявляет ли он поддержку трекинга рук.
void logRuntimeInfo() {
    if (n_xrGetInstanceProperties) {
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if (XR_SUCCEEDED(n_xrGetInstanceProperties(g_inst, &ip)))
            logf_("рантайм: %s v%u.%u.%u", ip.runtimeName,
                  (unsigned)XR_VERSION_MAJOR(ip.runtimeVersion),
                  (unsigned)XR_VERSION_MINOR(ip.runtimeVersion),
                  (unsigned)XR_VERSION_PATCH(ip.runtimeVersion));
    }
    if (n_xrGetSystem && n_xrGetSystemProperties) {
        XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO};
        gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId sid = XR_NULL_SYSTEM_ID;
        XrResult r = n_xrGetSystem(g_inst, &gi, &sid);
        if (XR_SUCCEEDED(r)) {
            XrSystemHandTrackingPropertiesEXT ht{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
            XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
            sp.next = &ht;
            r = n_xrGetSystemProperties(g_inst, sid, &sp);
            if (XR_SUCCEEDED(r))
                logf_("система: %s, supportsHandTracking=%d", sp.systemName,
                      (int)ht.supportsHandTracking);
            else
                logf_("xrGetSystemProperties: %d", (int)r);
        } else {
            logf_("xrGetSystem: %d", (int)r);
        }
    }
    if (n_xrEnumExt) {
        uint32_t n = 0;
        if (XR_SUCCEEDED(n_xrEnumExt(nullptr, 0, &n, nullptr)) && n) {
            std::vector<XrExtensionProperties> v(n);
            for (auto& e : v) e = XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES};
            if (XR_SUCCEEDED(n_xrEnumExt(nullptr, n, &n, v.data()))) {
                logf_("расширений рантайма: %u; связанные с руками:", n);
                for (auto& e : v)
                    if (strstr(e.extensionName, "hand") || strstr(e.extensionName, "HAND"))
                        logf_("  %s", e.extensionName);
            }
        } else {
            logf_("список расширений рантайма недоступен");
        }
    }
}

void setupSession(XrSession s) {
    g_session = s;
    logRuntimeInfo();
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};

    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    XrResult r = n_xrCreateReferenceSpace(s, &rs, &g_stage);
    if (XR_FAILED(r)) {
        logf_("STAGE недоступен (%d), пробую LOCAL", (int)r);
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        r = n_xrCreateReferenceSpace(s, &rs, &g_stage);
    } else {
        logf_("базовое пространство: STAGE");
    }
    if (XR_FAILED(r)) {
        logf_("не удалось создать базовое пространство: %d", (int)r);
        g_session = XR_NULL_HANDLE;
        return;
    }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    r = n_xrCreateReferenceSpace(s, &rs, &g_view);
    if (XR_FAILED(r)) logf_("VIEW-пространство не создано: %d (голова не пишется)", (int)r);

    for (int i = 0; i < 2; ++i) {
        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        ci.hand = i == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        r = n_xrCreateHandTrackerEXT(s, &ci, &g_ht[i]);
        logf_("hand tracker %s: результат %d", i == 0 ? "LEFT" : "RIGHT", (int)r);
        if (XR_FAILED(r)) g_ht[i] = XR_NULL_HANDLE;
    }
    markerInit(s);
}

// --- диагностика (пункты 2 и 4 плана): CSV по кадрам --------------------------
// Включается файлом %LOCALAPPDATA%\IL2Contact\diag.on (пустой файл; проверяется раз в ~1 с,
// можно включать и выключать, не перезапуская игру). Файл: diag_<exe>.csv.
// Что пишем: голова в STAGE, позы глаз в STAGE (отсюда расстояние между глазами = масштаб мира
// на уровне нашего слоя), кончики указательных, клавиша NUM5, и то, что получает ПРИЛОЖЕНИЕ
// от xrLocateViews (поза левого вида в его пространстве и расстояние между видами).
struct AppViews {
    bool ok = false;
    XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    float ipd = 0;
};
AppViews g_app;
XrSpace g_lastAppSpace = XR_NULL_HANDLE;
std::ofstream g_diag;
bool g_diagOn = false;

void diagPoll() {
    if (g_dir.empty()) return;
    DWORD a = GetFileAttributesA((g_dir + "\\diag.on").c_str());
    bool want = a != INVALID_FILE_ATTRIBUTES;
    if (want && !g_diagOn) {
        g_diag.open(g_dir + "\\diag_" + g_exe + ".csv", std::ios::trunc);
        if (g_diag.is_open()) {
            g_diag << "t_ms,frame,num5,hv,hx,hy,hz,hqx,hqy,hqz,hqw,ev,e0x,e0y,e0z,e1x,e1y,e1z,"
                      "lt,lx,ly,lz,rt,rx,ry,rz,mkL,mkR,av,a0x,a0y,a0z,a0qx,a0qy,a0qz,a0qw,aipd\n";
            g_diagOn = true;
            logf_("диагностика: CSV включён");
        }
    } else if (!want && g_diagOn) {
        g_diag.close();
        g_diagOn = false;
        logf_("диагностика: CSV выключен");
    }
}

void diagWrite(const Pkt& p) {
    if (p.frame % 90 == 1) diagPoll();
    if (!g_diagOn) return;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    int ev = 0;
    if (n_xrLocateViews) {
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = p.xrTime;
        li.space = g_stage;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t n = 0;
        if (XR_SUCCEEDED(n_xrLocateViews(g_session, &li, &vs, 2, &n, views)) && n >= 2 &&
            (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
            ev = 1;
    }
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    double ms = 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
    int num5 = ((GetAsyncKeyState(VK_NUMPAD5) | GetAsyncKeyState(VK_CLEAR)) & 0x8000) ? 1 : 0;
    char b[900];
    const float* t0 = p.hand[0].j[2];
    const float* t1 = p.hand[1].j[2];
    int n = snprintf(
        b, sizeof b,
        "%.2f,%llu,%d,%u,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
        "%u,%.4f,%.4f,%.4f,%u,%.4f,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.4f\n",
        ms, (unsigned long long)p.frame, num5, p.headValid, p.head[0], p.head[1], p.head[2],
        p.head[3], p.head[4], p.head[5], p.head[6], ev, views[0].pose.position.x,
        views[0].pose.position.y, views[0].pose.position.z, views[1].pose.position.x,
        views[1].pose.position.y, views[1].pose.position.z, p.hand[0].active, t0[0], t0[1], t0[2],
        p.hand[1].active, t1[0], t1[1], t1[2], g_mk[0].show ? 1 : 0, g_mk[1].show ? 1 : 0,
        g_app.ok ? 1 : 0, g_app.pose.position.x, g_app.pose.position.y, g_app.pose.position.z,
        g_app.pose.orientation.x, g_app.pose.orientation.y, g_app.pose.orientation.z,
        g_app.pose.orientation.w, g_app.ipd);
    if (n > 0) g_diag.write(b, n);
    if (p.frame % 90 == 0) g_diag.flush();
}

// --- захват и отправка -----------------------------------------------------
void capture(XrTime t) {
    Pkt p{};
    p.magic = kMagic;
    p.version = kVersion;
    p.frame = ++g_frame;
    p.xrTime = t;

    g_headOk = false;
    if (g_view != XR_NULL_HANDLE) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_SUCCEEDED(n_xrLocateSpace(g_view, g_stage, t, &loc)) &&
            (loc.locationFlags & need) == need) {
            p.headValid = 1;
            g_headOk = true;
            g_headQ = loc.pose.orientation;
            g_headP = loc.pose.position;
            p.head[0] = loc.pose.position.x;
            p.head[1] = loc.pose.position.y;
            p.head[2] = loc.pose.position.z;
            p.head[3] = loc.pose.orientation.x;
            p.head[4] = loc.pose.orientation.y;
            p.head[5] = loc.pose.orientation.z;
            p.head[6] = loc.pose.orientation.w;
        }
    }

    for (int h = 0; h < 2; ++h) {
        g_mk[h].show = false;
        if (g_ht[h] == XR_NULL_HANDLE) continue;
        XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        li.baseSpace = g_stage;
        li.time = t;
        XrHandJointLocationEXT jl[XR_HAND_JOINT_COUNT_EXT];
        XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locs.jointLocations = jl;
        if (XR_FAILED(n_xrLocateHandJointsEXT(g_ht[h], &li, &locs)) || !locs.isActive) {
            g_mk[h].activeFrames = 0;
            g_mk[h].pointing = false;
            continue;
        }

        bool ok = true;
        for (int k = 0; k < 8; ++k) {
            const auto& L = jl[kJoints[k]];
            if (!(L.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                ok = false;
                break;
            }
            p.hand[h].j[k][0] = L.pose.position.x;
            p.hand[h].j[k][1] = L.pose.position.y;
            p.hand[h].j[k][2] = L.pose.position.z;
        }
        p.hand[h].active = ok ? 1 : 0;

        // Маркер: только если рука стабильно отслеживается (первые кадры после появления
        // неточны) и указательный вытянут при согнутых остальных (те же пороги, что в receiver.py).
        g_mk[h].activeFrames = ok ? g_mk[h].activeFrames + 1 : 0;
        if (!ok) g_mk[h].pointing = false;
        if (ok && g_mk[h].activeFrames > 30) {
            auto D = [&](int a, int b) {
                float dx = p.hand[h].j[a][0] - p.hand[h].j[b][0];
                float dy = p.hand[h].j[a][1] - p.hand[h].j[b][1];
                float dz = p.hand[h].j[a][2] - p.hand[h].j[b][2];
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            };
            float di = D(2, 0), others = std::max({D(4, 0), D(5, 0), D(6, 0)});
            // Те же пороги, гистерезис и пауза, что в hands.py: иначе маркер мигал на границе порога, а зона при этом срабатывала.
            bool raw = g_mk[h].pointing ? (di > 0.105f && others < 0.115f) : (di > 0.12f && others < 0.10f);
            g_mk[h].pointing = raw;
            ULONGLONG nowP = GetTickCount64();
            if (raw) g_mk[h].pointMs = nowP;
            if (raw || nowP - g_mk[h].pointMs < 120) {  // короткий разрыв жеста (до 0,12 с) маркер не гасит
                g_mk[h].show = true;
                g_mk[h].pos = {p.hand[h].j[2][0], p.hand[h].j[2][1], p.hand[h].j[2][2]};
            }
        }
    }

    diagWrite(p);

    if (g_sock != INVALID_SOCKET)
        sendto(g_sock, reinterpret_cast<const char*>(&p), sizeof p, 0,
               reinterpret_cast<const sockaddr*>(&g_dst), sizeof g_dst);

    // лог: смена состояния рук + статус примерно раз в 2-3 секунды
    bool changed = false;
    for (int h = 0; h < 2; ++h) {
        bool a = p.hand[h].active != 0;
        if (a != g_lastActive[h]) {
            changed = true;
            g_lastActive[h] = a;
        }
    }
    static ULONGLONG s_lastFrameLog = 0;
    ULONGLONG nowMs = GetTickCount64();
    if ((changed && nowMs - s_lastFrameLog >= 2000) || g_frame % 1800 == 0) {  // не чаще раза в 2 с при смене состояния рук
        s_lastFrameLog = nowMs;
        logf_("кадр %llu голова:%d L:%u R:%u кончик указательного R=(%.3f %.3f %.3f)",
              (unsigned long long)g_frame, (int)p.headValid, p.hand[0].active, p.hand[1].active,
              p.hand[1].j[2][0], p.hand[1].j[2][1], p.hand[1].j[2][2]);
    }
}

// --- перехваченные функции -------------------------------------------------
XrResult XRAPI_CALL layer_xrCreateSession(XrInstance inst, const XrSessionCreateInfo* ci,
                                          XrSession* session) {
    // D3D11-устройство игры (нужно для маркера): берём из цепочки структур сессии.
    ID3D11Device* dev = nullptr;
    for (auto* n = reinterpret_cast<const XrBaseInStructure*>(ci->next); n; n = n->next)
        if (n->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
            dev = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(n)->device;
    XrResult r = n_xrCreateSession(inst, ci, session);
    if (XR_SUCCEEDED(r) && g_handExt && g_session == XR_NULL_HANDLE) {
        if (dev) {
            dev->AddRef();
            g_dev = dev;
        }
        logf_("xrCreateSession OK, настраиваю трекинг рук");
        setupSession(*session);
    }
    return r;
}

XrResult XRAPI_CALL layer_xrDestroySession(XrSession session) {
    if (session == g_session) {
        logf_("xrDestroySession");
        teardownSession();
    }
    return n_xrDestroySession(session);
}

XrResult XRAPI_CALL layer_xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
    if (session != g_session || g_session == XR_NULL_HANDLE || !info)
        return n_xrEndFrame(session, info);

    capture(info->displayTime);

    pollCmd();
    bool wantMk = g_markerReady && g_markerEnabled && g_headOk && (g_mk[0].show || g_mk[1].show);
    bool wantZ = g_zReady && g_headOk && g_cmd != INVALID_SOCKET && !g_zones.empty() && GetTickCount64() - g_zonesMs < 1000;
    if (!wantMk && !wantZ) return n_xrEndFrame(session, info);
    bool mkOk = wantMk && markerDraw();
    bool zOk = wantZ && zonesDraw();
    if (!mkOk && !zOk) return n_xrEndFrame(session, info);

    std::vector<XrCompositionLayerQuad> zq;  // зоны: под маркером
    if (zOk) {
        size_t n = std::min(g_zones.size(), g_zCap);  // Python шлёт самые важные первыми: впереди взгляда и ближе к голове
        for (size_t k = 0; k < n; ++k) {
            const ZoneItem& z = g_zones[k];
            int b = std::min(kZBuckets - 1, std::max(0, (int)std::lround(z.dmm / 5.0) - 1));
            XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            q.space = g_stage;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_zsc;
            q.subImage.imageRect.offset = {b * kZCell, (z.flags & 2) ? 2 * kZCell : ((z.flags & 1) ? kZCell : 0)};  // бит 1 - жёлтое кольцо, бит 2 - точка 5 мм
            q.subImage.imageRect.extent = {kZCell, kZCell};
            q.subImage.imageArrayIndex = 0;
            q.pose.orientation = g_headQ;
            q.pose.position = {z.x, z.y, z.z};
            q.size = {zoneSide(b), zoneSide(b)};  // физический размер зоны: с расстоянием уменьшается по перспективе
            zq.push_back(q);
        }
    }
    XrCompositionLayerQuad mq[2];
    int nq = 0;
    if (mkOk)
        for (int h = 0; h < 2; ++h) {
            if (!g_mk[h].show) continue;
            XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                           XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            q.space = g_stage;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g_msc;
            q.subImage.imageRect.offset = {h * kMarkerPx, 0};
            q.subImage.imageRect.extent = {kMarkerPx, kMarkerPx};
            q.subImage.imageArrayIndex = 0;
            q.pose.orientation = g_headQ;  // квад параллелен плоскости взгляда
            q.pose.position = g_mk[h].pos;
            q.size = {kMarkerSize * g_mkScale, kMarkerSize * g_mkScale};
            mq[nq++] = q;
        }
    static bool first = true;
    if (first) {
        logf_("маркер/зоны: первый кадр с квад-слоем (слоёв игры %u, зон %u)", info->layerCount, (unsigned)zq.size());
        first = false;
    }
    XrResult r = XR_SUCCESS;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::vector<const XrCompositionLayerBaseHeader*> layers(info->layers, info->layers + info->layerCount);
        for (auto& q : zq) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q));
        for (int i = 0; i < nq; ++i) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&mq[i]));
        XrFrameEndInfo mod = *info;
        mod.layerCount = (uint32_t)layers.size();
        mod.layers = layers.data();
        r = n_xrEndFrame(session, &mod);
        if (XR_SUCCEEDED(r)) return r;
        if (!zq.empty()) {  // слоёв слишком много или зоны не приняты: показываем вдвое меньше и повторяем
            g_zCap = std::max<size_t>(4, zq.size() * 3 / 4);  // постепенно: 120 -> 90 -> 67 ..., найти предел рантайма, не теряя лишнего
            zq.resize(std::min(g_zCap, zq.size()));
            logf_("зоны: xrEndFrame вернул %d, число показанных зон уменьшено до %u", (int)r, (unsigned)g_zCap);
            continue;
        }
        break;
    }
    if (nq > 0) {
        logf_("маркер: xrEndFrame с доп. слоем вернул %d, маркер отключён", (int)r);
        g_markerEnabled = false;
    }
    return r;
}

XrResult XRAPI_CALL layer_xrCreateReferenceSpace(XrSession session,
                                                 const XrReferenceSpaceCreateInfo* ci,
                                                 XrSpace* space) {
    XrResult r = n_xrCreateReferenceSpace(session, ci, space);
    static int s_rsLogs = 0;  // игра может создавать пространства постоянно: пишем только первые
    if (ci && s_rsLogs++ < 30)
        logf_("приложение: xrCreateReferenceSpace тип=%d поза=(%.3f %.3f %.3f | %.3f %.3f %.3f %.3f) -> %d",
              (int)ci->referenceSpaceType, ci->poseInReferenceSpace.position.x,
              ci->poseInReferenceSpace.position.y, ci->poseInReferenceSpace.position.z,
              ci->poseInReferenceSpace.orientation.x, ci->poseInReferenceSpace.orientation.y,
              ci->poseInReferenceSpace.orientation.z, ci->poseInReferenceSpace.orientation.w,
              (int)r);
    return r;
}

XrResult XRAPI_CALL layer_xrLocateViews(XrSession session, const XrViewLocateInfo* li,
                                        XrViewState* vs, uint32_t cap, uint32_t* out,
                                        XrView* views) {
    XrResult r = n_xrLocateViews(session, li, vs, cap, out, views);
    if (XR_SUCCEEDED(r) && session == g_session && li && vs && out && views && *out >= 2 &&
        (vs->viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
        g_app.ok = true;
        g_app.pose = views[0].pose;
        float dx = views[0].pose.position.x - views[1].pose.position.x;
        float dy = views[0].pose.position.y - views[1].pose.position.y;
        float dz = views[0].pose.position.z - views[1].pose.position.z;
        g_app.ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
        static int s_lvLogs = 0;
        if (li->space != g_lastAppSpace) {
            g_lastAppSpace = li->space;
            if (s_lvLogs++ < 30) logf_("приложение: xrLocateViews в новом пространстве %p, расстояние между видами %.4f м",
                  (void*)li->space, g_app.ipd);
        }
    }
    return r;
}

XrResult XRAPI_CALL layer_xrDestroyInstance(XrInstance inst) {
    logf_("xrDestroyInstance");
    teardownSession();
    XrResult r = n_xrDestroyInstance(inst);
    netClose();
    return r;
}

XrResult XRAPI_CALL layer_xrGetInstanceProcAddr(XrInstance inst, const char* name,
                                                PFN_xrVoidFunction* fn) {
    if (!g_next) return XR_ERROR_HANDLE_INVALID;
    XrResult r = g_next(inst, name, fn);
    if (XR_FAILED(r) || !name || !fn) return r;
    if (!strcmp(name, "xrCreateSession"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrCreateSession);
    else if (!strcmp(name, "xrDestroySession"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrDestroySession);
    else if (!strcmp(name, "xrEndFrame"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrEndFrame);
    else if (!strcmp(name, "xrCreateReferenceSpace"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrCreateReferenceSpace);
    else if (!strcmp(name, "xrLocateViews"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrLocateViews);
    else if (!strcmp(name, "xrDestroyInstance"))
        *fn = reinterpret_cast<PFN_xrVoidFunction>(layer_xrDestroyInstance);
    return r;
}

#define LOAD(fnName) \
    g_next(*inst, #fnName, reinterpret_cast<PFN_xrVoidFunction*>(&n_##fnName))

XrResult XRAPI_CALL layer_xrCreateApiLayerInstance(const XrInstanceCreateInfo* ci,
                                                   const XrApiLayerCreateInfo* li,
                                                   XrInstance* inst) {
    openLog();
    logf_("xrCreateApiLayerInstance, приложение: %s", ci ? ci->applicationInfo.applicationName : "?");
    if (!ci || !li || !li->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;

    XrApiLayerCreateInfo next = *li;
    next.nextInfo = li->nextInfo->next;
    PFN_xrGetInstanceProcAddr gipa = li->nextInfo->nextGetInstanceProcAddr;
    PFN_xrCreateApiLayerInstance create = li->nextInfo->nextCreateApiLayerInstance;

    // Добавляем XR_EXT_hand_tracking, если приложение (IL-2 через OpenComposite) его не просило.
    std::vector<const char*> exts(ci->enabledExtensionNames,
                                  ci->enabledExtensionNames + ci->enabledExtensionCount);
    bool has = false;
    for (const char* e : exts)
        if (!strcmp(e, XR_EXT_HAND_TRACKING_EXTENSION_NAME)) has = true;

    XrResult r;
    if (has) {
        r = create(ci, &next, inst);
        g_handExt = XR_SUCCEEDED(r);
    } else {
        exts.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        XrInstanceCreateInfo mod = *ci;
        mod.enabledExtensionCount = (uint32_t)exts.size();
        mod.enabledExtensionNames = exts.data();
        r = create(&mod, &next, inst);
        if (XR_SUCCEEDED(r)) {
            g_handExt = true;
        } else {
            logf_("инстанс с XR_EXT_hand_tracking не создан (%d), пробую без него", (int)r);
            r = create(ci, &next, inst);
            g_handExt = false;
        }
    }
    if (XR_FAILED(r)) return r;

    g_next = gipa;
    g_inst = *inst;
    LOAD(xrCreateSession);
    LOAD(xrDestroySession);
    LOAD(xrDestroyInstance);
    LOAD(xrEndFrame);
    LOAD(xrCreateReferenceSpace);
    LOAD(xrDestroySpace);
    LOAD(xrLocateSpace);
    LOAD(xrLocateViews);
    LOAD(xrGetInstanceProperties);
    LOAD(xrGetSystem);
    LOAD(xrGetSystemProperties);
    LOAD(xrCreateSwapchain);
    LOAD(xrDestroySwapchain);
    LOAD(xrEnumerateSwapchainFormats);
    LOAD(xrEnumerateSwapchainImages);
    LOAD(xrAcquireSwapchainImage);
    LOAD(xrWaitSwapchainImage);
    LOAD(xrReleaseSwapchainImage);
    g_next(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
           reinterpret_cast<PFN_xrVoidFunction*>(&n_xrEnumExt));
    if (g_handExt) {
        LOAD(xrCreateHandTrackerEXT);
        LOAD(xrDestroyHandTrackerEXT);
        LOAD(xrLocateHandJointsEXT);
        if (!n_xrCreateHandTrackerEXT || !n_xrLocateHandJointsEXT || !n_xrDestroyHandTrackerEXT) {
            logf_("функции hand tracking не найдены, трекинг рук отключён");
            g_handExt = false;
        }
    }
    logf_("hand tracking расширение: %s", g_handExt ? "ВКЛЮЧЕНО" : "НЕДОСТУПНО в этом рантайме");
    netInit();
    return r;
}

}  // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* /*layerName*/,
    XrNegotiateApiLayerRequest* req) {
    openLog();
    {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        logf_("DLL загружена лоадером (%s, pid %lu, %s)", exe, GetCurrentProcessId(),
              sizeof(void*) == 8 ? "64-бит" : "32-бит");
    }
    if (!loaderInfo || !req || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        req->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
        return XR_ERROR_INITIALIZATION_FAILED;
    req->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    req->layerApiVersion = XR_CURRENT_API_VERSION;
    req->getInstanceProcAddr = layer_xrGetInstanceProcAddr;
    req->createApiLayerInstance = layer_xrCreateApiLayerInstance;
    return XR_SUCCESS;
}
