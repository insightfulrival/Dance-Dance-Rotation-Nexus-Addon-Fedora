// Optional visual check: runs the real DLL under Wine with a simulated Nexus API and writes the
// final ImGui draw data to preview.bin; tools/render_preview.py rasterizes it to a PNG.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Nexus.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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
WNDPROC_CALLBACK wndproc = nullptr;
std::map<std::string, Texture_t> textures;
std::vector<std::string> texture_names; // index + 1 is the fake resource pointer
LinkedMem mumble{};
NexusLinkData_t nexus_link{};

void* allocate(size_t size, void*) { return std::malloc(size); }
void release(void* p, void*) { std::free(p); }
void add_render(ERenderType type, GUI_RENDER render) { callbacks.emplace_back(type, render); }
void remove_render(GUI_RENDER render) {
    callbacks.erase(std::remove_if(callbacks.begin(), callbacks.end(), [render](const auto& p) { return p.second == render; }), callbacks.end());
}
void register_escape(const char*, bool*) {}
void deregister_escape(const char*) {}
const char* addon_path(const char*) { return "preview-ddr"; }
void logger(ELogLevel, const char*, const char* text) { std::printf("log: %s\n", text); }
void alert(const char* text) { std::printf("alert: %s\n", text); }
void register_bind(const char* id, INPUTBINDS_PROCESS handler, const char*) { binds[id] = handler; }
void deregister_bind(const char* id) { binds.erase(id); }
void add_wndproc(WNDPROC_CALLBACK callback) { wndproc = callback; }
void remove_wndproc(WNDPROC_CALLBACK) { wndproc = nullptr; }
void add_shortcut(const char*, const char*, const char*, const char*, const char*) {}
void remove_shortcut(const char*) {}
void add_context(const char*, const char*, GUI_RENDER) {}
void remove_context(const char*) {}
Texture_t* make(const char* id) {
    auto& t = textures[id];
    if (!t.Resource) { texture_names.push_back(id); t = {64, 64, reinterpret_cast<void*>(texture_names.size())}; }
    return &t;
}
Texture_t* texture_memory(const char* id, void*, uint64_t) { return make(id); }
Texture_t* texture_url(const char* id, const char*, const char*) { return make(id); }
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
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.f / 60.f;
    ImGui::NewFrame();
    for (auto c : callbacks) if (c.first == RT_Render) c.second();
    ImGui::Render();
}
void dump(const char* path, const unsigned char* font_pixels, int fw, int fh) {
    std::ofstream out(path, std::ios::binary);
    auto put = [&](const void* p, size_t n) { out.write(static_cast<const char*>(p), static_cast<std::streamsize>(n)); };
    uint32_t count = static_cast<uint32_t>(texture_names.size());
    put(&count, 4);
    for (const auto& name : texture_names) { uint32_t n = static_cast<uint32_t>(name.size()); put(&n, 4); put(name.data(), n); }
    put(&fw, 4); put(&fh, 4); put(font_pixels, static_cast<size_t>(fw) * fh * 4);
    ImDrawData* data = ImGui::GetDrawData();
    uint32_t lists = static_cast<uint32_t>(data->CmdListsCount);
    put(&lists, 4);
    for (int l = 0; l < data->CmdListsCount; ++l) {
        const ImDrawList* list = data->CmdLists[l];
        uint32_t vc = list->VtxBuffer.Size, ic = list->IdxBuffer.Size, cc = list->CmdBuffer.Size;
        put(&vc, 4); put(list->VtxBuffer.Data, vc * sizeof(ImDrawVert));
        put(&ic, 4); put(list->IdxBuffer.Data, ic * sizeof(ImDrawIdx));
        put(&cc, 4);
        for (const auto& cmd : list->CmdBuffer) {
            uint64_t tex = reinterpret_cast<uint64_t>(cmd.TextureId);
            put(&cmd.ClipRect, 16); put(&tex, 8); put(&cmd.IdxOffset, 4); put(&cmd.ElemCount, 4); put(&cmd.VtxOffset, 4);
        }
    }
}

int main(int argc, char** argv) {
    const char* extra = argc > 1 ? argv[1] : "";
    const char* output = argc > 2 ? argv[2] : "preview.bin";
    CreateDirectoryA("preview-ddr", nullptr);
    {
        std::ofstream settings("preview-ddr/settings.txt");
        settings << "version 1\nhelp_shown 1\ninitial_info_shown 1\ndefaults_version \"0.1.0\"\n"
                 << "selected \"Power Weaver [SC]\"\n";
        for (const char* p = extra; *p; ++p) settings << (*p == ';' ? '\n' : *p);
        settings << '\n';
    }
    std::wstring identity = L"{\"name\":\"Preview\",\"profession\":6}";
    std::copy(identity.begin(), identity.end(), mumble.identity);
    ImGui::SetAllocatorFunctions(allocate, release);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* pixels; int fw, fh;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
    ImGui::GetIO().Fonts->TexID = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(0xF0F0));
    AddonAPI_t api{};
    api.ImguiContext = ImGui::GetCurrentContext();
    api.ImguiMalloc = reinterpret_cast<void*>(allocate); api.ImguiFree = reinterpret_cast<void*>(release);
    api.GUI_Register = add_render; api.GUI_Deregister = remove_render;
    api.GUI_RegisterCloseOnEscape = register_escape; api.GUI_DeregisterCloseOnEscape = deregister_escape;
    api.Paths_GetAddonDirectory = addon_path; api.Log = logger; api.GUI_SendAlert = alert;
    api.InputBinds_RegisterWithString = register_bind; api.InputBinds_Deregister = deregister_bind;
    api.WndProc_Register = add_wndproc; api.WndProc_Deregister = remove_wndproc;
    api.QuickAccess_Add = add_shortcut; api.QuickAccess_Remove = remove_shortcut;
    api.QuickAccess_AddContextMenu = add_context; api.QuickAccess_RemoveContextMenu = remove_context;
    api.Textures_Get = texture_get; api.Textures_GetOrCreateFromMemory = texture_memory; api.Textures_GetOrCreateFromURL = texture_url;
    api.DataLink_Get = datalink;
    HMODULE library = LoadLibraryW(L"dance-dance-rotation.dll");
    if (!library) return 1;
    auto* definition = reinterpret_cast<AddonDefinition_t* (*)()>(GetProcAddress(library, "GetAddonDef"))();
    definition->Load(&api);
    frame();
    binds["DDR_TOGGLE_NOTES_WINDOW"]("DDR_TOGGLE_NOTES_WINDOW", false);
    binds["DDR_TOGGLE_SONG_LIST"]("DDR_TOGGLE_SONG_LIST", false);
    binds["DDR_TOGGLE_SONG_INFO"]("DDR_TOGGLE_SONG_INFO", false);
    for (int i = 0; i < 60; ++i) { frame(); Sleep(10); }
    binds["DDR_TOGGLE_PLAY_STOP"]("DDR_TOGGLE_PLAY_STOP", false);
    // Press a few skills while the song plays so hit text and ratings appear.
    for (int i = 0; i < 260; ++i) {
        if (i % 40 == 20) for (int vk : {0x32, 0x33, 0x34, 0x35, 0x70, 0x71, 0x72, 0x73}) { wndproc(nullptr, WM_KEYDOWN, vk, 1); }
        frame(); Sleep(16);
    }
    binds["DDR_PAUSE"]("DDR_PAUSE", false);
    ImGui::GetIO().MousePos = ImVec2(-100, -100);
    for (int i = 0; i < 3; ++i) frame();
    dump(output, pixels, fw, fh);
    definition->Unload();
    FreeLibrary(library);
    ImGui::DestroyContext();
    std::printf("wrote %s\n", output);
}
