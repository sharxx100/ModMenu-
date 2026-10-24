// language: C++, file: payload.cpp, target: Android ARM64 (injected into standoff2)
#include <android/log.h>
#include <android/input.h>
#include <android/looper.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_android.h"
#include "dobby.h" 

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "s2menu", __VA_ARGS__)

// ============================================================
// IL2CPP API
// ============================================================
struct Il2CppApi {
    void*    (*domain_get)();
    void**   (*domain_get_assemblies)(void* domain, size_t* size);
    void*    (*assembly_get_image)(void* assembly);
    void*    (*class_from_name)(void* image, const char* ns, const char* name);
    void*    (*class_get_method_from_name)(void* klass, const char* name, int argc);
    void*    (*method_get_pointer)(void* method);
    void*    (*class_get_field_from_name)(void* klass, const char* name);
    size_t   (*field_get_offset)(void* field);
    void*    (*object_new)(void* klass);
    void*    (*string_new)(const char* str);
    void*    (*class_get_property_from_name)(void* klass, const char* name);
    void*    (*property_get_get_method)(void* prop);
    void*    (*runtime_invoke)(void* method, void* obj, void** params, void** exc);
    void*    (*class_get_parent)(void* klass);
    void*    (*class_get_namespace)(void* klass);
    const char* (*class_get_name)(void* klass);
};

static Il2CppApi g_api{};
static uintptr_t g_il2cpp_base = 0;
static void* g_assembly_image = nullptr;

static uintptr_t get_module_base(const char* name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name)) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(f);
    return base;
}

template <typename T>
static T sym(const char* name) {
    return (T)dlsym((void*)g_il2cpp_base, name);
}

static bool resolve_il2cpp() {
    g_il2cpp_base = get_module_base("libil2cpp.so");
    if (!g_il2cpp_base) return false;
    g_api.domain_get               = sym<decltype(g_api.domain_get)>("il2cpp_domain_get");
    g_api.domain_get_assemblies    = sym<decltype(g_api.domain_get_assemblies)>("il2cpp_domain_get_assemblies");
    g_api.assembly_get_image       = sym<decltype(g_api.assembly_get_image)>("il2cpp_assembly_get_image");
    g_api.class_from_name          = sym<decltype(g_api.class_from_name)>("il2cpp_class_from_name");
    g_api.class_get_method_from_name = sym<decltype(g_api.class_get_method_from_name)>("il2cpp_class_get_method_from_name");
    g_api.method_get_pointer       = sym<decltype(g_api.method_get_pointer)>("il2cpp_method_get_pointer");
    g_api.class_get_field_from_name= sym<decltype(g_api.class_get_field_from_name)>("il2cpp_class_get_field_from_name");
    g_api.field_get_offset         = sym<decltype(g_api.field_get_offset)>("il2cpp_field_get_offset");
    g_api.object_new               = sym<decltype(g_api.object_new)>("il2cpp_object_new");
    g_api.string_new               = sym<decltype(g_api.string_new)>("il2cpp_string_new");
    g_api.class_get_property_from_name = sym<decltype(g_api.class_get_property_from_name)>("il2cpp_class_get_property_from_name");
    g_api.property_get_get_method  = sym<decltype(g_api.property_get_get_method)>("il2cpp_property_get_get_method");
    g_api.runtime_invoke           = sym<decltype(g_api.runtime_invoke)>("il2cpp_runtime_invoke");
    g_api.class_get_parent         = sym<decltype(g_api.class_get_parent)>("il2cpp_class_get_parent");
    g_api.class_get_namespace      = sym<decltype(g_api.class_get_namespace)>("il2cpp_class_get_namespace");
    g_api.class_get_name           = sym<decltype(g_api.class_get_name)>("il2cpp_class_get_name");
    return g_api.domain_get && g_api.class_from_name && g_api.class_get_method_from_name;
}

static void* find_class(const char* ns, const char* name) {
    size_t n = 0;
    void** asms = g_api.domain_get_assemblies(g_api.domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* img = g_api.assembly_get_image(asms[i]);
        if (!img) continue;
        void* k = g_api.class_from_name(img, ns, name);
        if (k) return k;
    }
    return nullptr;
}

static void* find_method(void* klass, const char* name, int argc) {
    if (!klass) return nullptr;
    return g_api.class_get_method_from_name(klass, name, argc);
}

static void* method_addr(void* klass, const char* name, int argc) {
    void* m = find_method(klass, name, argc);
    if (!m) return nullptr;
    return g_api.method_get_pointer(m);
}

// ============================================================
// Anti-detection
// ============================================================
typedef fopen_t (*fopen_t)(const char*, const char*);
typedef read_t (*read_t)(int, void*, size_t);

static fopen_t  orig_fopen = nullptr;
static read_t   orig_read = nullptr;

static const char* kSelfSoName = "libmenu.so";
static const char* kSelfMapTag = "/data/local/tmp/libmenu.so";

static bool is_proc_target(const char* path) {
    if (!path) return false;
    return strstr(path, "/proc/self/maps")
        || strstr(path, "/proc/self/status")
        || strstr(path, "/proc/self/task/")
        || strstr(path, "/proc/self/smaps");
}

static ssize_t hook_read(int fd, void* buf, size_t count) {
    ssize_t n = orig_read(fd, buf, count);
    if (n <= 0) return n;
    char link[256] = {0};
    char fdpath[64];
    snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
    ssize_t l = readlink(fdpath, link, sizeof(link) - 1);
    if (l <= 0) return n;
    if (!is_proc_target(link)) return n;

    char* data = (char*)buf;
    std::string out;
    out.reserve(n);
    size_t i = 0;
    while (i < (size_t)n) {
        size_t j = i;
        while (j < (size_t)n && data[j] != '\n') j++;
        std::string line(data + i, j - i);
        bool drop = false;
        if (line.find(kSelfSoName) != std::string::npos) drop = true;
        if (line.find(kSelfMapTag) != std::string::npos) drop = true;
        if (line.find("TracerPid:") != std::string::npos) {
            std::string newline = "TracerPid:\t0\n";
            out += newline;
            drop = true;
        }
        if (!drop) { out += line; out += '\n'; }
        i = (j < (size_t)n) ? j + 1 : j;
    }
    if (out.size() <= count) {
        memcpy(buf, out.data(), out.size());
        return (ssize_t)out.size();
    }
    return n;
}

static void install_anti_detect() {
    void* libc = dlopen("libc.so", RTLD_NOW);
    if (!libc) return;
    orig_read = (read_t)dlsym(libc, "read");
    DobbyHook((void*)orig_read, (void*)hook_read, (void**)&orig_read);
    LOG("anti-detect installed");
}

// ============================================================
// ESP / aimbot
// ============================================================
struct Vec3 { float x, y, z; };
struct PlayerSnapshot {
    void* inst;
    Vec3 world;
    Vec3 screen;
    int  team;
    bool alive;
    bool visible;
};

static std::vector<PlayerSnapshot> g_players;
static std::mutex g_players_mtx;

static std::atomic<bool> g_esp_on{false};
static std::atomic<bool> g_aim_on{false};
static std::atomic<float> g_aim_fov{120.0f};
static std::atomic<float> g_aim_smooth{8.0f};
static std::atomic<bool> g_menu_open{true};

struct GameSyms {
    void* local_player;
    void* camera;
    void* player_class;
    void* camera_class;
    void* transform_class;
    int   off_player_list;
    int   off_player_team;
    int   off_player_health;
    int   off_player_transform;
    int   off_cam_matrix;
    int   off_transform_pos;

    void* fn_world_to_screen;
    void* fn_get_main;
    void* fn_get_transform;
    void* fn_get_position;
    void* fn_get_localplayer;
} g_game{};

static bool resolve_game() {
    g_game.player_class   = find_class("", "Player");
    if (!g_game.player_class) g_game.player_class = find_class("Standoff", "Player");
    g_game.camera_class   = find_class("UnityEngine", "Camera");
    g_game.transform_class= find_class("UnityEngine", "Transform");
    if (!g_game.player_class || !g_game.camera_class || !g_game.transform_class) {
        LOG("game classes not found");
        return false;
    }
    g_game.fn_get_main = method_addr(g_game.camera_class, "get_main", 0);
    g_game.fn_world_to_screen = method_addr(g_game.camera_class, "WorldToScreenPoint", 1);
    g_game.fn_get_transform = method_addr(g_game.player_class, "get_transform", 0);
    g_game.fn_get_position  = method_addr(g_game.transform_class, "get_position", 0);
    void* f_team = g_api.class_get_field_from_name(g_game.player_class, "Team");
    void* f_hp   = g_api.class_get_field_from_name(g_game.player_class, "Health");
    void* f_tr   = g_api.class_get_field_from_name(g_game.player_class, "m_CachedTransform");
    g_game.off_player_team = f_team ? (int)g_api.field_get_offset(f_team) : -1;
    g_game.off_player_health = f_hp ? (int)g_api.field_get_offset(f_hp) : -1;
    g_game.off_player_transform = f_tr ? (int)g_api.field_get_offset(f_tr) : -1;
    LOG("game resolved: w2s=%p getmain=%p", g_game.fn_world_to_screen, g_game.fn_get_main);
    return true;
}

static void* invoke_ptr(void* fn, void* self) {
    if (!fn || !self) return nullptr;
    typedef void* (*F)(void*);
    return ((F)fn)(self);
}

static Vec3 world_to_screen(void* cam, Vec3 w) {
    Vec3 out{0,0,0};
    if (!cam || !g_game.fn_world_to_screen) return out;
    void* args[1] = { &w };
    void* exc = nullptr;
    void* ret = g_api.runtime_invoke(g_game.fn_world_to_screen, cam, args, &exc);
    if (ret) {
        memcpy(&out, ret, sizeof(Vec3));
    }
    return out;
}

static Vec3 get_transform_pos(void* transform) {
    if (!transform || !g_game.fn_get_position) return {};
    void* args[0] = {};
    void* exc = nullptr;
    void* ret = g_api.runtime_invoke(g_game.fn_get_position, transform, args, &exc);
    Vec3 out{};
    if (ret) memcpy(&out, ret, sizeof(Vec3));
    return out;
}

static void game_thread() {
    LOG("game_thread start");
    while (!resolve_il2cpp()) { usleep(200000); }
    while (!resolve_game())   { usleep(200000); }

    while (true) {
        if (!g_game.camera && g_game.fn_get_main) {
            void* args[0] = {};
            void* exc = nullptr;
            void* cam = g_api.runtime_invoke(g_game.fn_get_main, nullptr, args, &exc);
            if (cam) g_game.camera = cam;
        }
        if (g_esp_on.load() && g_game.camera) {
            std::lock_guard<std::mutex> lk(g_players_mtx);
            for (auto& p : g_players) {
                if (!p.inst) continue;
                void* tr = g_game.off_player_transform >= 0
                    ? *(void**)((uintptr_t)p.inst + g_game.off_player_transform)
                    : invoke_ptr(g_game.fn_get_transform, p.inst);
                if (!tr) continue;
                p.world = get_transform_pos(tr);
                p.screen = world_to_screen(g_game.camera, p.world);
                p.visible = p.screen.z > 0.01f
                         && p.screen.x >= 0 && p.screen.x <= 2400
                         && p.screen.y >= 0 && p.screen.y <= 1080;
            }
        }
        usleep(16000);
    }
}

static void run_aimbot() {
    if (!g_aim_on.load() || !g_game.camera) return;
    float cx = 1200.0f, cy = 540.0f;
    float best = g_aim_fov.load();
    PlayerSnapshot* target = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_players_mtx);
        for (auto& p : g_players) {
            if (!p.alive || !p.visible || p.team == 0) continue;
            float dx = p.screen.x - cx;
            float dy = p.screen.y - cy;
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < best) { best = d; target = &p; }
        }
    }
    if (!target) return;
}

typedef EGLBoolean (*eglSwapBuffers_t)(EGLDisplay, EGLSurface);
static eglSwapBuffers_t orig_eglSwapBuffers = nullptr;

static void draw_menu() {
    ImGui::SetNextWindowSize(ImVec2(360, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Standoff 2", &g_menu_open, ImGuiWindowFlags_NoCollapse)) {
        static bool esp = false, aim = false;
        static float fov = 120.0f, smooth = 8.0f;
        ImGui::Checkbox("ESP", &esp);
        ImGui::Checkbox("Aimbot", &aim);
        ImGui::SliderFloat("Aimbot FOV", &fov, 10.0f, 360.0f);
        ImGui::SliderFloat("Smooth", &smooth, 1.0f, 30.0f);
        g_esp_on.store(esp);
        g_aim_on.store(aim);
        g_aim_fov.store(fov);
        g_aim_smooth.store(smooth);
        ImGui::Text("Players: %zu", g_players.size());
    }
    ImGui::End();
}

static void draw_esp() {
    if (!g_esp_on.load()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    std::lock_guard<std::mutex> lk(g_players_mtx);
    for (auto& p : g_players) {
        if (!p.visible || !p.alive || p.team == 0) continue;
        float h = 90.0f * (1000.0f / p.world.z);
        if (h < 4 || h > 2000) continue;
        ImU32 col = IM_COL32(255, 60, 60, 220);
        dl->AddRect(ImVec2(p.screen.x - h*0.3f, p.screen.y - h),
                    ImVec2(p.screen.x + h*0.3f, p.screen.y),
                    col, 0, 0, 1.5f);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0fm", p.world.z);
        dl->AddText(ImVec2(p.screen.x - 20, p.screen.y - h - 14), col, buf);
    }
}

static EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    static bool init = false;
    if (!init) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGui_ImplAndroid_Init(nullptr);
        init = true;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    if (g_menu_open.load()) draw_menu();
    draw_esp();
    run_aimbot();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return orig_eglSwapBuffers(dpy, surf);
}

typedef int32_t (*AInputQueue_preDispatchEvent_t)(AInputQueue*, AInputEvent*);
static AInputQueue_preDispatchEvent_t orig_preDispatch = nullptr;

static int32_t hook_preDispatch(AInputQueue* q, AInputEvent* ev) {
    if (ImGui_ImplAndroid_HandleInputEvent(ev)) return 1;
    return orig_preDispatch(q, ev);
}

extern "C" __attribute__((visibility("default")))
void menu_entry() {
    LOG("menu_entry entered");
    install_anti_detect();
    std::thread(game_thread).detach();

    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (egl) {
        void* swap = dlsym(egl, "eglSwapBuffers");
        if (swap) DobbyHook(swap, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    }
    void* android_lib = dlopen("libandroid.so", RTLD_NOW);
    if (android_lib) {
        void* pred = dlsym(android_lib, "AInputQueue_preDispatchEvent");
        if (pred) DobbyHook(pred, (void*)hook_preDispatch, (void**)&orig_preDispatch);
    }
    LOG("hooks installed");
}

__attribute__((constructor))
static void on_load() {
    LOG("libmenu.so loaded");
}