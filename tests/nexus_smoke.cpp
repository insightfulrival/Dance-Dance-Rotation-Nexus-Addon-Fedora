// Wine integration harness: loads the real addon DLL with real ImGui 1.80 and a simulated Nexus API.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Nexus.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: REQUIRE failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

struct LinkedMem {
    uint32_t ui_version, ui_tick;
    float avatar_position[3], avatar_front[3], avatar_top[3];
    wchar_t name[256];
    float camera_position[3], camera_front[3], camera_top[3];
    wchar_t identity[256];
    uint32_t context_length;
    unsigned char context[256];
    wchar_t description[2048];
};

std::vector<std::pair<ERenderType, GUI_RENDER>> callbacks;
std::map<std::string, INPUTBINDS_PROCESS> binds;
std::map<std::string, std::string> bind_defaults;
std::map<std::string, bool*> escape;
WNDPROC_CALLBACK wndproc = nullptr;
GUI_RENDER context_menu = nullptr;
bool shortcut = false;
std::map<std::string, Texture_t> textures;
std::set<std::string> memory_ids, url_ids;
std::vector<std::string> logs, alerts;
LinkedMem mumble{};
NexusLinkData_t nexus_link{};

void* allocate(size_t size, void*) { return std::malloc(size); }
void release(void* p, void*) { std::free(p); }
void add_render(ERenderType type, GUI_RENDER render) { callbacks.emplace_back(type, render); }
void remove_render(GUI_RENDER render) {
    callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(), [render](const auto& p) { return p.second == render; }), callbacks.end());
}
void register_escape(const char* name, bool* p) { escape[name] = p; }
void deregister_escape(const char* name) { escape.erase(name); }
const char* addon_path(const char*) { return "smoke-ddr"; }
void logger(ELogLevel level, const char*, const char* text) {
    std::printf("addon log %d: %s\n", level, text);
    REQUIRE(level != LOGL_CRITICAL);
    logs.emplace_back(text);
}
void alert(const char* text) { std::printf("alert: %s\n", text); alerts.emplace_back(text); }
void register_bind(const char* id, INPUTBINDS_PROCESS handler, const char* fallback) { binds[id] = handler; bind_defaults[id] = fallback; }
void deregister_bind(const char* id) { binds.erase(id); }
void add_wndproc(WNDPROC_CALLBACK callback) { REQUIRE(!wndproc); wndproc = callback; }
void remove_wndproc(WNDPROC_CALLBACK callback) { REQUIRE(wndproc == callback); wndproc = nullptr; }
void add_shortcut(const char*, const char* texture, const char*, const char* bind, const char*) {
    REQUIRE(memory_ids.count(texture) && binds.count(bind));
    shortcut = true;
}
void remove_shortcut(const char*) { shortcut = false; }
void add_context(const char*, const char*, GUI_RENDER render) { context_menu = render; }
void remove_context(const char*) { context_menu = nullptr; }
Texture_t* texture_memory(const char* id, void* data, uint64_t size) {
    REQUIRE(memory_ids.insert(id).second);
    REQUIRE(size > 8 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0);
    auto& t = textures[id];
    t = {64, 64, reinterpret_cast<void*>(1)};
    return &t;
}
Texture_t* texture_url(const char* id, const char* remote, const char* endpoint) {
    REQUIRE(url_ids.insert(id).second); // Each ability icon is requested once per load.
    REQUIRE(std::strcmp(remote, "https://assets.gw2dat.com") == 0);
    REQUIRE(endpoint[0] == '/' && std::strstr(endpoint, ".png"));
    auto& t = textures[id];
    t = {64, 64, reinterpret_cast<void*>(2)};
    return &t;
}
Texture_t* texture_get(const char* id) {
    auto found = textures.find(id);
    return found == textures.end() ? nullptr : &found->second;
}
void* datalink(const char* id) {
    if (std::strcmp(id, DL_MUMBLE_LINK) == 0) return &mumble;
    if (std::strcmp(id, DL_NEXUS_LINK) == 0) return &nexus_link;
    return nullptr;
}

void frame() {
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1600, 1000);
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    for (auto callback : callbacks) {
        if (callback.first == RT_OptionsRender) {
            ImGui::SetNextWindowPos(ImVec2(1290, 0), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_Always);
            ImGui::Begin("Mock Nexus Options"); callback.second(); ImGui::End();
        } else callback.second();
    }
    if (context_menu) {
        ImGui::SetNextWindowPos(ImVec2(1380, 780), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_Always);
        ImGui::Begin("Mock QuickAccess menu"); context_menu(); ImGui::End();
    }
    ImGui::Render();
    REQUIRE(ImGui::GetDrawData());
}
void frames(int count, int sleep_ms = 0) {
    for (int i = 0; i < count; ++i) { frame(); if (sleep_ms) Sleep(sleep_ms); }
}
ImGuiWindow* window(const char* name) {
    ImGuiWindow* w = ImGui::FindWindowByName(name);
    return w && w->Active ? w : nullptr;
}
// Locate an item by hovering along a line inside a window, then click it.
bool click_id(ImGuiWindow* w, ImGuiID id, float x_offset, float step = 3.f) {
    auto& io = ImGui::GetIO();
    for (float y = w->Pos.y; y < w->Pos.y + w->Size.y; y += step) {
        io.MousePos = ImVec2(w->Pos.x + x_offset, y);
        frame();
        if (ImGui::GetCurrentContext()->HoveredId == id) {
            io.MouseDown[0] = true; frame();
            io.MouseDown[0] = false; frame();
            return true;
        }
    }
    return false;
}
UINT key(int vk) {
    UINT result = wndproc(nullptr, WM_KEYDOWN, vk, 1);
    wndproc(nullptr, WM_KEYUP, vk, 0xC0000001);
    return result;
}
bool logged(const char* text) {
    return std::any_of(logs.begin(), logs.end(), [text](const std::string& s) { return s.find(text) != std::string::npos; });
}
bool alerted(const char* text) { return std::find(alerts.begin(), alerts.end(), text) != alerts.end(); }
std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out; out << in.rdbuf();
    return out.str();
}
void set_clipboard(const std::string& text) {
    REQUIRE(OpenClipboard(nullptr));
    EmptyClipboard();
    std::wstring wide(text.begin(), text.end());
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    std::copy(wide.begin(), wide.end(), target);
    target[wide.size()] = 0;
    GlobalUnlock(memory);
    REQUIRE(SetClipboardData(CF_UNICODETEXT, memory));
    CloseClipboard();
}

const char* custom_song = R"({"name":"Smoke Test Song","description":"Three notes","buildUrl":"https://example.invalid/build",
"buildChatCode":"[&DQYfFRomOBV0AAAAcwAAAMsAAAA1FwAAEhcAAAAAAAAAAAAAAAAAAAAAAAA=]",
"decodedBuildTemplate":{"profession":6,"specializations":[{"id":31},{"id":56}],"skills":{"terrestrial":{"heal":0,"utilities":[115,145,203],"elite":0}}},
"notes":[{"time":0,"duration":0,"noteType":"Weapon2","abilityId":10},{"time":400,"duration":0,"noteType":"Weapon3","abilityId":11},
{"time":800,"duration":0,"noteType":"Weapon4","abilityId":"12"}]})";

int main() {
    namespace fs = std::filesystem;
    fs::remove_all("smoke-ddr");
    std::wstring identity = L"{\"name\":\"Smoke\",\"profession\":6,\"spec\":56}";
    std::copy(identity.begin(), identity.end(), mumble.identity);
    ImGui::SetAllocatorFunctions(allocate, release);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    AddonAPI_t api{};
    api.ImguiContext = ImGui::GetCurrentContext();
    api.ImguiMalloc = reinterpret_cast<void*>(allocate);
    api.ImguiFree = reinterpret_cast<void*>(release);
    api.GUI_Register = add_render; api.GUI_Deregister = remove_render;
    api.GUI_RegisterCloseOnEscape = register_escape; api.GUI_DeregisterCloseOnEscape = deregister_escape;
    api.Paths_GetAddonDirectory = addon_path; api.Log = logger; api.GUI_SendAlert = alert;
    api.InputBinds_RegisterWithString = register_bind; api.InputBinds_Deregister = deregister_bind;
    api.WndProc_Register = add_wndproc; api.WndProc_Deregister = remove_wndproc;
    api.QuickAccess_Add = add_shortcut; api.QuickAccess_Remove = remove_shortcut;
    api.QuickAccess_AddContextMenu = add_context; api.QuickAccess_RemoveContextMenu = remove_context;
    api.Textures_Get = texture_get; api.Textures_GetOrCreateFromMemory = texture_memory; api.Textures_GetOrCreateFromURL = texture_url;
    api.DataLink_Get = datalink;
    std::size_t preselected_urls = 0;
    const char* orientation_settings[] = {nullptr, "orientation 2\ncompact 1\nnext_abilities 3\n", "orientation 4\nnext_abilities 2\n"};
    for (int cycle = 0; cycle < 3; ++cycle) {
        memory_ids.clear(); url_ids.clear(); textures.clear(); logs.clear(); alerts.clear();
        if (cycle > 0) {
            // Keep the recorded settings (custom key, selected song) and change only the layout.
            std::string saved = read("smoke-ddr/settings.txt");
            std::ofstream("smoke-ddr/settings.txt", std::ios::app) << orientation_settings[cycle];
            REQUIRE(saved.find("key Weapon1 81 0") != std::string::npos);
        }
        auto library = LoadLibraryW(L"dance-dance-rotation.dll");
        if (!library) { std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError()); return 1; }
        auto get_definition = reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(library, "GetAddonDef"));
        REQUIRE(get_definition);
        auto* definition = get_definition();
        REQUIRE(definition && definition->APIVersion == 6 && definition->Signature == 0x44445250);
        definition->Load(&api);
        REQUIRE(callbacks.size() == 2 && wndproc && shortcut && context_menu && binds.size() == 7 && escape.size() == 3);
        REQUIRE(bind_defaults["DDR_TOGGLE_NOTES_WINDOW"] == "CTRL+ALT+D" && bind_defaults["DDR_PLAY"] == "(null)");
        REQUIRE(memory_ids.size() == 32); // Every bundled PNG once, before any window opens.
        REQUIRE(logged("Loaded 102 songs (") || logged("Loaded 103 songs ("));
        frames(3);
        if (cycle == 0) {
            REQUIRE(url_ids.empty());                         // No song selected yet: no downloads.
            REQUIRE(window("Welcome to Dance Dance Rotation!##ddr.help")); // First run shows Help.
            // Song List -> Add From Clipboard selects and saves a custom song.
            binds["DDR_TOGGLE_SONG_LIST"]("DDR_TOGGLE_SONG_LIST", false);
            frames(2);
            ImGuiWindow* list = window("Song List##ddr.songlist");
            REQUIRE(list);
            set_clipboard(custom_song);
            REQUIRE(click_id(list, list->GetID("Add From Clipboard"), 20));
            REQUIRE(alerted("Added Song Successfully"));
            REQUIRE(fs::exists("smoke-ddr/customSongs/Smoke Test Song.json"));
            REQUIRE(url_ids.size() >= 3); // Note and utility-remap icons, each queued once
            REQUIRE(window("Song Info##ddr.songinfo"));            // First selected song opens Song Info.
            // Keys are observed, not consumed. The first note waits at Perfect; its key starts the song.
            binds["DDR_TOGGLE_NOTES_WINDOW"]("DDR_TOGGLE_NOTES_WINDOW", false);
            frames(5);
            REQUIRE(window("Dance Dance Rotation##ddr.notes.horizontal"));
            REQUIRE(key('3') == WM_KEYDOWN);
            frames(2);
            REQUIRE(!logged("Song started"));
            REQUIRE(key('2') == WM_KEYDOWN);
            frames(2);
            REQUIRE(logged("Song started"));
            for (int i = 0; i < 360 && !logged("Song stopped"); ++i) { frame(); Sleep(16); }
            REQUIRE(logged("Song stopped")); // Song end resets to the prepared state.
            // Settings: capture a new Weapon 1 key; the assigned key is consumed.
            ImGuiWindow* menu = window("Mock QuickAccess menu");
            REQUIRE(menu && click_id(menu, menu->GetID("Settings"), 20));
            frames(2);
            ImGuiWindow* s = window("Dance Dance Rotation Settings##ddr.settings");
            REQUIRE(s);
            int weapon1 = 3;
            ImGuiID seed = ImHashData(&weapon1, sizeof weapon1, s->IDStack.back());
            ImGuiID set_id = ImHashStr("Set", 0, seed);
            bool found = false;
            for (float y = 0; y < 900 && !found; y += 300) {
                ImGui::SetScrollY(s, y);
                frames(2);
                found = click_id(s, set_id, 375);
            }
            REQUIRE(found);
            REQUIRE(key(VK_SHIFT) == 0); // Modifier alone waits for the main key
            REQUIRE(key('Q') == 0);
            frames(2);
            REQUIRE(read("smoke-ddr/settings.txt").find("key Weapon1 81 0") != std::string::npos);
            REQUIRE(key('Q') == WM_KEYDOWN); // Capture finished: keys reach the game again.
        } else {
            // The selected custom song is restored and its icons queued once per load.
            REQUIRE(!url_ids.empty());
            if (cycle == 1) preselected_urls = url_ids.size(); else REQUIRE(url_ids.size() == preselected_urls);
            binds["DDR_TOGGLE_NOTES_WINDOW"]("DDR_TOGGLE_NOTES_WINDOW", false);
            frames(5);
            REQUIRE(window(cycle == 1 ? "Dance Dance Rotation##ddr.notes.vertical" : "Dance Dance Rotation##ddr.notes.abilitybar"));
            REQUIRE(!window("Welcome to Dance Dance Rotation!##ddr.help"));
            // Toggle Play/Stop starts without a key press, then the bind stops it again.
            binds["DDR_TOGGLE_PLAY_STOP"]("DDR_TOGGLE_PLAY_STOP", false);
            frames(30, 16);
            REQUIRE(alerted("Starting") && logged("Song started"));
            binds["DDR_TOGGLE_PLAY_STOP"]("DDR_TOGGLE_PLAY_STOP", false);
            frames(2);
            REQUIRE(alerted("Stopped") && logged("Song stopped"));
            // Typing in GW2 chat must not hit notes: '2' would otherwise start the song.
            auto starts = std::count_if(logs.begin(), logs.end(), [](const std::string& l) { return l == "Song started"; });
            mumble.context[48] = 0x20;
            frames(2);
            REQUIRE(key('2') == WM_KEYDOWN);
            frames(2);
            REQUIRE(std::count_if(logs.begin(), logs.end(), [](const std::string& l) { return l == "Song started"; }) == starts);
            mumble.context[48] = 0;
            frames(2);
            REQUIRE(key('2') == WM_KEYDOWN);
            frames(2);
            REQUIRE(std::count_if(logs.begin(), logs.end(), [](const std::string& l) { return l == "Song started"; }) == starts + 1);
        }
        definition->Unload();
        REQUIRE(callbacks.empty() && !wndproc && !shortcut && !context_menu && binds.empty() && escape.empty());
        REQUIRE(FreeLibrary(library));
        std::printf("cycle %d passed: %zu memory textures, %zu URL textures\n", cycle, memory_ids.size(), url_ids.size());
    }
    ImGui::DestroyContext();
    std::puts("Nexus smoke passed: load/render/unload x3, clipboard import, key-start and song end, key capture, Toggle Play/Stop, chat blocking, all orientations.");
}
