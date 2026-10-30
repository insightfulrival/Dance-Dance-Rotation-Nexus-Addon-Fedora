// Dance Dance Rotation for Nexus: a native port of the Blish HUD module 1.8.1 by Shooper.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Nexus.h"
#include "imgui.h"
#include "engine.hpp"
#include "song_json.hpp"
#include "data.hpp"
#include "images.hpp"
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static_assert(sizeof(void*) == 8, "Nexus requires a Windows x64 addon");
static_assert(sizeof(AddonDefinition_t) == 72);
static_assert(offsetof(AddonAPI_t, GUI_Register) == 32);
static_assert(offsetof(AddonAPI_t, Paths_GetAddonDirectory) == 96);
static_assert(IMGUI_VERSION_NUM == 18000, "Use the ImGui shipped by Nexus");

namespace fs = std::filesystem;
using ddr::NoteType;

namespace {
constexpr const char* version_text = "0.1.0";
constexpr const char* log_name = "Dance Dance Rotation";
constexpr const char* notes_window_h = "Dance Dance Rotation##ddr.notes.horizontal";
constexpr const char* notes_window_v = "Dance Dance Rotation##ddr.notes.vertical";
constexpr const char* notes_window_bar = "Dance Dance Rotation##ddr.notes.abilitybar";
constexpr const char* list_window = "Song List##ddr.songlist";
constexpr const char* info_window = "Song Info##ddr.songinfo";
constexpr const char* help_window = "Welcome to Dance Dance Rotation!##ddr.help";
constexpr const char* settings_window = "Dance Dance Rotation Settings##ddr.settings";
constexpr const char* shortcut_id = "DDR_SHORTCUT";
constexpr const char* context_menu_id = "DDR_SHORTCUT_MENU";
constexpr const char* logo_id = "DDR_IMG_ddr_logo_emblem.png";
constexpr const char* composer_url = "https://campbt.github.io/DanceDanceRotationComposer/create.html";
constexpr const char* help_url = "https://github.com/campbt/DanceDanceRotation#setup-and-usage";
constexpr float toolbar_height = 44.f;
constexpr float button_size = 24.f;

enum Bind { BindNotes, BindList, BindInfo, BindToggle, BindPlay, BindPause, BindStop, BindCount };
struct BindSpec { const char* id; const char* fallback; };
constexpr BindSpec binds[BindCount] = {
    {"DDR_TOGGLE_NOTES_WINDOW", "CTRL+ALT+D"}, {"DDR_TOGGLE_SONG_LIST", "(null)"},
    {"DDR_TOGGLE_SONG_INFO", "(null)"}, {"DDR_TOGGLE_PLAY_STOP", "(null)"}, {"DDR_PLAY", "(null)"},
    {"DDR_PAUSE", "(null)"}, {"DDR_STOP", "(null)"}};

struct Binding { int vk = 0; int mods = 0; }; // mods: 1 Ctrl, 2 Alt, 4 Shift
struct KeyEvent { int vk; int mods; double time; };

struct Settings {
    ddr::Options options;
    float opacity = 0.9f;
    bool show_icons = true, show_hotkeys = true, only_profession = true;
    bool lock_window = false, rating_overlay = true;
    Binding keys[ddr::note_type_count];
    std::string selected;
    bool help_shown = false, initial_info_shown = false;
    std::string defaults_version;
    std::set<std::string> hidden_defaults;
    std::map<std::string, ddr::SongData> song_data;
};

struct SongEntry { ddr::Song song; fs::path file; };

// MumbleLink shared memory and the Guild Wars 2 context block.
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
static_assert(sizeof(wchar_t) == 2, "MumbleLink uses UTF-16");

AddonAPI_t* api = nullptr;
Settings settings;
fs::path addon_dir, settings_path, custom_dir;
std::map<std::string, SongEntry> songs;
ddr::Engine engine;
bool notes_visible = false, list_visible = false, info_visible = false, help_visible = false, settings_visible = false;
std::atomic<bool> bind_pressed[BindCount];
std::mutex input_mutex;
std::vector<KeyEvent> pending_keys;
std::atomic<int> capture_target{-1};
bool capture_done = false;
bool capture_ui_seen = false; // a hotkey list was drawn since the last frame
Binding captured;
std::atomic<bool> block_game_keys{false}; // chat or an ImGui text field has focus
bool chat_focused = false;
ddr::Profession current_profession = ddr::Profession::Unknown;
double last_mumble_check = -1e9;
HANDLE change_handle = INVALID_HANDLE_VALUE;
double reload_at = -1;
std::set<std::string> requested_textures;
std::unordered_map<std::string, Texture_t*> texture_cache;
std::string rating_text;
ImU32 rating_color = 0;
double rating_time = -1e9;
std::string delete_candidate;
double frequency = 0;

// ---- utilities ------------------------------------------------------------------------------
double now_ms() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart * frequency;
}
void log(ELogLevel level, const std::string& message) {
    if (api && api->Log) api->Log(level, log_name, message.c_str());
}
void alert(const char* message) {
    if (api && api->GUI_SendAlert) api->GUI_SendAlert(message);
}
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}
std::string narrow(const wchar_t* text, int length = -1) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), count, nullptr, nullptr);
    if (length < 0 && !out.empty() && out.back() == '\0') out.pop_back();
    return out;
}
fs::path utf8_path(const std::string& text) { return fs::path(widen(text)); }
std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not read file");
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
void write_file_atomic(const fs::path& path, const std::string& text) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) throw std::runtime_error("Could not write file");
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace file");
}
bool copy_to_clipboard(const std::string& text) {
    std::wstring wide = widen(text);
    bool success = false;
    if (OpenClipboard(nullptr)) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (memory) {
            if (auto* target = static_cast<wchar_t*>(GlobalLock(memory))) {
                std::copy(wide.begin(), wide.end(), target);
                target[wide.size()] = L'\0';
                GlobalUnlock(memory);
                success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
            }
            if (!success) GlobalFree(memory);
        }
        CloseClipboard();
    }
    return success;
}
std::string read_clipboard() {
    std::string text;
    if (!OpenClipboard(nullptr)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* wide = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = narrow(wide);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}
bool open_url(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return false;
    std::wstring wide = widen(url);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}
ImU32 rgb(std::uint32_t hex, float alpha = 1.f) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 255));
}
ImVec4 rgb4(std::uint32_t hex) { return ImGui::ColorConvertU32ToFloat4(rgb(hex)); }
NexusLinkData_t* nexus_link() {
    return api && api->DataLink_Get ? static_cast<NexusLinkData_t*>(api->DataLink_Get(DL_NEXUS_LINK)) : nullptr;
}
ImFont* big_font() {
    auto* link = nexus_link();
    return link && link->FontBig ? static_cast<ImFont*>(link->FontBig) : ImGui::GetFont();
}
// Large text is drawn from Nexus' big font so it is downscaled rather than blurred.
ImFont* font_for(float size) {
    auto* link = nexus_link();
    if (link && link->FontBig && size > ImGui::GetFont()->FontSize + 2) return static_cast<ImFont*>(link->FontBig);
    return ImGui::GetFont();
}

// ---- key names (KeysExtensions.NotesString) ----------------------------------------------
std::string key_name(int vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "N" + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
        case VK_MBUTTON: return "M3"; case VK_XBUTTON1: return "M4"; case VK_XBUTTON2: return "M5";
        case VK_BACK: return "Back"; case VK_TAB: return "Tab"; case VK_RETURN: return "Enter";
        case VK_CAPITAL: return "Caps"; case VK_ESCAPE: return "Escape"; case VK_SPACE: return "Space";
        case VK_PRIOR: return "PageUp"; case VK_NEXT: return "PageDown"; case VK_END: return "End";
        case VK_HOME: return "Home"; case VK_LEFT: return "Left"; case VK_UP: return "Up";
        case VK_RIGHT: return "Right"; case VK_DOWN: return "Down"; case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete"; case VK_MULTIPLY: return "*"; case VK_ADD: return "+";
        case VK_SUBTRACT: return "-"; case VK_DECIMAL: return "."; case VK_DIVIDE: return "/";
        case VK_OEM_1: return ";"; case VK_OEM_PLUS: return "+"; case VK_OEM_COMMA: return ",";
        case VK_OEM_MINUS: return "-"; case VK_OEM_PERIOD: return "."; case VK_OEM_2: return "?";
        case VK_OEM_3: return "~"; case VK_OEM_4: return "{"; case VK_OEM_5: return "|";
        case VK_OEM_6: return "}"; case VK_OEM_7: return "\""; case VK_OEM_102: return "\\";
        case VK_SHIFT: return "Shift"; case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt";
        case VK_NUMLOCK: return "NumLock"; case VK_SCROLL: return "Scroll"; case VK_PAUSE: return "Pause";
        case VK_SNAPSHOT: return "PrintScreen";
    }
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%02X", vk);
    return buffer;
}
std::string binding_label(const Binding& binding, bool compact) {
    if (!binding.vk) return compact ? "" : "(not set)";
    std::string text;
    if (binding.mods & 1) text += compact ? "C+" : "Ctrl+";
    if (binding.mods & 2) text += compact ? "A+" : "Alt+";
    if (binding.mods & 4) text += compact ? "S+" : "Shift+";
    return text + key_name(binding.vk);
}

// ---- textures -----------------------------------------------------------------------------
void* texture(const std::string& identifier) {
    auto cached = texture_cache.find(identifier);
    if (cached != texture_cache.end()) return cached->second->Resource;
    if (!api || !api->Textures_Get) return nullptr;
    Texture_t* found = api->Textures_Get(identifier.c_str());
    if (!found || !found->Resource) return nullptr;
    texture_cache.emplace(identifier, found);
    return found->Resource;
}
void* image(const char* path) { return texture(std::string("DDR_IMG_") + path); }
const bundled::AbilityRecord* find_ability(int id) {
    auto begin = std::begin(bundled::abilities), end = std::end(bundled::abilities);
    auto found = std::lower_bound(begin, end, id, [](const bundled::AbilityRecord& r, int v) { return r.id < v; });
    return found != end && found->id == id ? &*found : nullptr;
}
int ability_for_palette(int palette) {
    auto begin = std::begin(bundled::palette), end = std::end(bundled::palette);
    auto found = std::lower_bound(begin, end, palette, [](const bundled::PaletteRecord& r, int v) { return r.palette < v; });
    return found != end && found->palette == palette ? found->ability : -9999999;
}
std::string asset_id(int asset) { return "DDR_ASSET_" + std::to_string(asset); }
// Queue a render.guildwars2.com asset (the Blish DAT asset cache source) once per load.
void request_ability_icon(int ability) {
    const auto* info = find_ability(ability);
    if (!info || info->custom_icon >= 0 || info->asset <= 0 || !api || !api->Textures_GetOrCreateFromURL) return;
    std::string id = asset_id(info->asset);
    if (!requested_textures.insert(id).second) return;
    std::string endpoint = "/" + std::to_string(info->asset) + ".png";
    api->Textures_GetOrCreateFromURL(id.c_str(), "https://assets.gw2dat.com", endpoint.c_str());
}
// Returns the icon texture, or nullptr while it is loading. Unknown abilities use the unknown icon.
void* ability_icon(int ability) {
    const auto* info = find_ability(ability);
    if (!info) return image("abilityIcons/custom/unknownAbilityIcon.png");
    if (info->custom_icon >= 0) return image(bundled::custom_icons[info->custom_icon]);
    return info->asset > 0 ? texture(asset_id(info->asset)) : image("abilityIcons/custom/unknownAbilityIcon.png");
}
const char* ability_name(int ability) {
    const auto* info = find_ability(ability);
    return info ? info->name : "";
}
const char* note_image(NoteType type) {
    switch (ddr::note_color(type)) {
        case ddr::NoteColor::Purple: return "notes/ddr_note_purple.png";
        case ddr::NoteColor::Green: return "notes/ddr_note_green.png";
        default: return "notes/ddr_note_red.png";
    }
}

// ---- settings -----------------------------------------------------------------------------
void reset_keys() {
    for (int i = 0; i < ddr::note_type_count; ++i) settings.keys[i] = {ddr::default_hotkey(static_cast<NoteType>(i)), 0};
}
void save_settings() {
    try {
        std::ostringstream out;
        const auto& o = settings.options;
        out << "version 1\norientation " << static_cast<int>(o.orientation) << "\ncompact " << static_cast<int>(o.compact)
            << "\nopacity " << settings.opacity << "\nauto_hit_weapon1 " << o.auto_hit_weapon1
            << "\nstart_with_first_skill " << o.start_with_first_skill << "\nnext_abilities " << o.next_abilities
            << "\nshow_icons " << settings.show_icons << "\nshow_hotkeys " << settings.show_hotkeys
            << "\nonly_profession " << settings.only_profession << "\nlock_window " << settings.lock_window
            << "\nrating_overlay " << settings.rating_overlay << "\nhelp_shown " << settings.help_shown
            << "\ninitial_info_shown " << settings.initial_info_shown
            << "\ndefaults_version " << std::quoted(settings.defaults_version)
            << "\nselected " << std::quoted(settings.selected) << '\n';
        for (int i = 0; i < ddr::note_type_count - 1; ++i)
            out << "key " << ddr::note_type_name(static_cast<NoteType>(i)) << ' ' << settings.keys[i].vk << ' ' << settings.keys[i].mods << '\n';
        for (const auto& name : settings.hidden_defaults) out << "hidden " << std::quoted(name) << '\n';
        for (const auto& [name, d] : settings.song_data)
            out << "song " << std::quoted(name) << ' ' << d.mapping[0] << ' ' << d.mapping[1] << ' ' << d.mapping[2] << ' '
                << d.no_miss << ' ' << d.rate << ' ' << d.start_at << ' ' << d.pace << '\n';
        write_file_atomic(settings_path, out.str());
    } catch (const std::exception& e) {
        log(LOGL_WARNING, std::string("Settings could not be saved: ") + e.what());
        alert("Dance Dance Rotation: settings could not be saved. Check nexus.log.");
    }
}
void load_settings() {
    settings = Settings();
    reset_keys();
    std::ifstream in(settings_path);
    std::string line;
    auto& o = settings.options;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string key;
        row >> key;
        int value;
        if (key == "orientation" && row >> value && value >= 0 && value <= 4) o.orientation = static_cast<ddr::Orientation>(value);
        else if (key == "compact" && row >> value && value >= 0 && value <= 2) o.compact = static_cast<ddr::CompactStyle>(value);
        else if (key == "opacity") { float v; if (row >> v && std::isfinite(v)) settings.opacity = std::clamp(v, 0.f, 1.f); }
        else if (key == "auto_hit_weapon1") row >> o.auto_hit_weapon1;
        else if (key == "start_with_first_skill") row >> o.start_with_first_skill;
        else if (key == "next_abilities" && row >> value) o.next_abilities = std::clamp(value, 0, 10);
        else if (key == "show_icons") row >> settings.show_icons;
        else if (key == "show_hotkeys") row >> settings.show_hotkeys;
        else if (key == "only_profession") row >> settings.only_profession;
        else if (key == "lock_window") row >> settings.lock_window;
        else if (key == "rating_overlay") row >> settings.rating_overlay;
        else if (key == "help_shown") row >> settings.help_shown;
        else if (key == "initial_info_shown") row >> settings.initial_info_shown;
        else if (key == "defaults_version") row >> std::quoted(settings.defaults_version);
        else if (key == "selected") row >> std::quoted(settings.selected);
        else if (key == "hidden") { std::string name; if (row >> std::quoted(name)) settings.hidden_defaults.insert(name); }
        else if (key == "key") {
            std::string type; int vk, mods;
            if (row >> type >> vk >> mods && vk >= 0 && vk < 256 && mods >= 0 && mods < 8) {
                NoteType parsed;
                if (ddr::parse_note_type(type, parsed) && parsed != NoteType::Unknown) settings.keys[static_cast<int>(parsed)] = {vk, mods};
            }
        } else if (key == "song") {
            std::string name;
            ddr::SongData d;
            if (row >> std::quoted(name) >> d.mapping[0] >> d.mapping[1] >> d.mapping[2] >> d.no_miss >> d.rate >> d.start_at >> d.pace) {
                int sorted[3] = {d.mapping[0], d.mapping[1], d.mapping[2]};
                std::sort(sorted, sorted + 3);
                if (sorted[0] != 0 || sorted[1] != 1 || sorted[2] != 2) { d.mapping[0] = 0; d.mapping[1] = 1; d.mapping[2] = 2; }
                if (!std::isfinite(d.rate)) d.rate = 1.f;
                d.rate = std::clamp(d.rate, 0.1f, 1.f);
                d.start_at = std::max(d.start_at, 0);
                d.pace = std::clamp(d.pace, ddr::min_pace, ddr::max_pace);
                settings.song_data[name] = d;
            }
        }
    }
    // The reference restores deleted default songs when the module version changes.
    if (settings.defaults_version != version_text) {
        settings.hidden_defaults.clear();
        settings.defaults_version = version_text;
    }
}
ddr::SongData song_data(const std::string& name) {
    auto found = settings.song_data.find(name);
    return found == settings.song_data.end() ? ddr::SongData() : found->second;
}

// ---- song repository (SongRepo.cs) ----------------------------------------------------------
void on_start_stop(bool changed);
const SongEntry* selected_song() {
    auto found = songs.find(settings.selected);
    return found == songs.end() ? nullptr : &found->second;
}
void preload_icons(const ddr::Song& song) {
    std::set<int> unique;
    for (const auto& note : song.notes) unique.insert(note.ability);
    for (int palette : song.utility) unique.insert(ability_for_palette(palette));
    for (int ability : unique) request_ability_icon(ability);
}
void apply_selection() {
    const SongEntry* entry = selected_song();
    engine.configure(settings.options, engine.layout().width, engine.layout().height);
    if (entry) {
        engine.set_song(&entry->song.notes, song_data(entry->song.name));
        preload_icons(entry->song);
    } else {
        engine.set_song(nullptr, ddr::SongData());
    }
    on_start_stop(engine.reset_and_prepare(now_ms()));
}
void select_song(const std::string& name) {
    if (name == settings.selected) return;
    settings.selected = name;
    log(LOGL_INFO, "Selected song: " + name);
    save_settings();
    apply_selection();
    if (!settings.initial_info_shown && selected_song()) {
        settings.initial_info_shown = true; // Show Song Info once, when the first song is selected.
        info_visible = true;
        save_settings();
    }
}
void update_song_data(const std::string& name, const ddr::SongData& data) {
    if (song_data(name) == data) return;
    settings.song_data[name] = data;
    if (name == settings.selected) apply_selection();
}
std::string safe_file_name(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += (c < 0x20 || std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos) ? '_' : static_cast<char>(c);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    return out.empty() ? "song" : out;
}
bool same_song(const ddr::Song& a, const ddr::Song& b) {
    if (a.name != b.name || a.description != b.description || a.build_url != b.build_url || a.build_code != b.build_code ||
        a.elite != b.elite || a.profession != b.profession || !std::equal(a.utility, a.utility + 3, b.utility) ||
        a.notes.size() != b.notes.size())
        return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        const auto &x = a.notes[i], &y = b.notes[i];
        if (x.type != y.type || x.time_ms != y.time_ms || x.duration_ms != y.duration_ms || x.ability != y.ability || x.override_auto != y.override_auto)
            return false;
    }
    return true;
}
// Returns true if the selected song changed and the highway must be reset.
bool load_songs() {
    auto previous = songs.extract(settings.selected);
    songs.clear();
    for (const auto& r : bundled::songs) {
        if (settings.hidden_defaults.count(r.name)) continue;
        SongEntry entry;
        auto& s = entry.song;
        s.name = r.name; s.description = r.description; s.build_url = r.build_url; s.build_code = r.build_code;
        s.profession = static_cast<ddr::Profession>(r.profession);
        s.elite = ddr::elite_name(s.profession, r.specialization);
        std::copy(r.utility, r.utility + 3, s.utility);
        s.is_default = true;
        s.notes.reserve(r.note_count);
        for (int i = 0; i < r.note_count; ++i) {
            const auto& n = bundled::notes[r.first_note + i];
            s.notes.push_back({static_cast<NoteType>(n.type), n.time, n.duration, n.ability, n.override_auto != 0});
        }
        songs[s.name] = std::move(entry);
    }
    int custom = 0;
    std::error_code ec;
    for (fs::directory_iterator it(custom_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != L".json") continue;
        try {
            std::string unknown;
            ddr::Song song = ddr::song_from_json_text(read_file(it->path()), &unknown);
            if (!unknown.empty()) log(LOGL_WARNING, "Unknown Note Type: " + unknown + "in " + narrow(it->path().c_str()));
            std::string name = song.name;
            songs[name] = {std::move(song), it->path()}; // Custom songs replace default songs of the same name.
            ++custom;
        } catch (const std::exception& e) {
            log(LOGL_WARNING, "Failed to load song file: " + narrow(it->path().c_str()) + ": " + e.what());
        }
    }
    log(LOGL_INFO, "Loaded " + std::to_string(songs.size()) + " songs (" + std::to_string(custom) + " custom).");
    // Keep the playing song object (and the engine's view of it) when its content is unchanged,
    // for example after the folder watcher reports our own clipboard import.
    auto current = songs.find(settings.selected);
    if (previous && current != songs.end() && same_song(previous.mapped().song, current->second.song) &&
        previous.mapped().file == current->second.file) {
        songs.erase(current);
        songs.insert(std::move(previous));
        return false;
    }
    return true;
}
void reload_songs() {
    if (load_songs()) apply_selection();
}
void add_song_from_clipboard() {
    std::string text = read_clipboard();
    try {
        ddr::Song song = ddr::song_from_json_text(text);
        fs::create_directories(custom_dir);
        fs::path file = custom_dir / utf8_path(safe_file_name(song.name) + ".json");
        write_file_atomic(file, text);
        log(LOGL_INFO, (songs.count(song.name) ? "Successfully replaced song: " : "Successfully added song: ") + song.name);
        std::string name = song.name;
        settings.hidden_defaults.erase(name);
        load_songs();
        settings.selected.clear();
        select_song(name);
        alert("Added Song Successfully");
    } catch (const std::exception& e) {
        log(LOGL_WARNING, std::string("Failed to decode clipboard contents into a song: ") + e.what());
        alert("Failed to decode song.");
    }
}
void delete_song(const std::string& name) {
    auto found = songs.find(name);
    if (found == songs.end()) return;
    log(LOGL_INFO, "Removing song: " + name);
    if (!found->second.file.empty()) {
        std::error_code ec;
        fs::remove(found->second.file, ec);
        if (ec) { alert("Could not delete the song file."); return; }
    } else {
        settings.hidden_defaults.insert(name);
    }
    settings.song_data.erase(name);
    save_settings();
    reload_songs();
}
void watch_custom_songs() {
    std::error_code ec;
    fs::create_directories(custom_dir, ec);
    change_handle = FindFirstChangeNotificationW(custom_dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
}
void poll_custom_songs(double now) {
    if (change_handle != INVALID_HANDLE_VALUE && WaitForSingleObject(change_handle, 0) == WAIT_OBJECT_0) {
        FindNextChangeNotification(change_handle);
        reload_at = now + 500; // Let editors finish writing before reading.
    }
    if (reload_at >= 0 && now >= reload_at) {
        reload_at = -1;
        reload_songs();
    }
}

// ---- input -------------------------------------------------------------------------------------
int current_mods(int primary) {
    int mods = 0;
    if (primary != VK_CONTROL && (GetKeyState(VK_CONTROL) & 0x8000)) mods |= 1;
    if (primary != VK_MENU && (GetKeyState(VK_MENU) & 0x8000)) mods |= 2;
    if (primary != VK_SHIFT && (GetKeyState(VK_SHIFT) & 0x8000)) mods |= 4;
    return mods;
}
// Observes key presses without consuming them, so the game still casts the skill.
UINT wndproc(HWND, UINT message, WPARAM wparam, LPARAM lparam) {
    int vk = 0;
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (lparam & (1 << 30)) return message; // auto-repeat
        vk = static_cast<int>(wparam);
        if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
        if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
        if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
    } else if (message == WM_MBUTTONDOWN) vk = VK_MBUTTON;
    else if (message == WM_XBUTTONDOWN) vk = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    else return message;
    if (vk <= 0 || vk > 255) return message;
    double time = now_ms();
    int mods = current_mods(vk);
    std::lock_guard<std::mutex> lock(input_mutex);
    if (capture_target.load() >= 0) {
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT) return 0; // wait for the main key
        if (vk != VK_ESCAPE) captured = {vk, mods};
        capture_done = vk != VK_ESCAPE;
        if (vk == VK_ESCAPE) capture_target.store(-1);
        return 0; // A key being assigned should not also reach the game.
    }
    if (!block_game_keys.load() && pending_keys.size() < 256) pending_keys.push_back({vk, mods, time});
    return message;
}
void on_bind(const char* identifier, bool released) {
    if (released || !identifier) return;
    for (int i = 0; i < BindCount; ++i)
        if (std::strcmp(identifier, binds[i].id) == 0) bind_pressed[i].store(true);
}
void show_rating(ddr::Hit hit) {
    static const std::uint32_t colors[] = {0x00FFFF, 0x6495ED, 0x00FF7F, 0xFF4500, 0xFF0000};
    rating_text = ddr::hit_name(hit);
    rating_color = rgb(colors[static_cast<int>(hit)]);
    rating_time = now_ms();
}
void on_start_stop(bool changed) {
    if (changed) log(LOGL_DEBUG, engine.started() ? "Song started" : "Song stopped");
}
void handle_press(NoteType type, double time) {
    bool was_started = engine.started();
    auto result = engine.press(type, time);
    if (result.hit && !engine.data().no_miss) show_rating(result.grade);
    on_start_stop(was_started != engine.started());
}
void process_input(double now) {
    std::vector<KeyEvent> events;
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        events.swap(pending_keys);
        if (capture_done) {
            int target = capture_target.exchange(-1);
            capture_done = false;
            if (target >= 0 && target < ddr::note_type_count) { settings.keys[target] = captured; save_settings(); }
        }
    }
    block_game_keys.store(ImGui::GetIO().WantTextInput || chat_focused);
    for (const auto& event : events) {
        if (!notes_visible) continue;
        for (int i = 0; i < ddr::note_type_count - 1; ++i)
            if (settings.keys[i].vk == event.vk && settings.keys[i].mods == event.mods)
                handle_press(static_cast<NoteType>(i), std::min(event.time, now));
    }
    if (bind_pressed[BindNotes].exchange(false)) notes_visible = !notes_visible;
    if (bind_pressed[BindList].exchange(false)) list_visible = !list_visible;
    if (bind_pressed[BindInfo].exchange(false)) info_visible = !info_visible;
    if (bind_pressed[BindToggle].exchange(false)) {
        if (engine.started()) { alert("Stopped"); on_start_stop(engine.reset_and_prepare(now)); }
        else { alert("Starting"); engine.reset_and_prepare(now); on_start_stop(engine.play(now)); }
    }
    if (bind_pressed[BindPlay].exchange(false)) on_start_stop(engine.play(now));
    if (bind_pressed[BindPause].exchange(false)) engine.pause(now);
    if (bind_pressed[BindStop].exchange(false)) on_start_stop(engine.stop(now));
}
void poll_mumble(double now) {
    if (!api || !api->DataLink_Get) return;
    auto* link = static_cast<const LinkedMem*>(api->DataLink_Get(DL_MUMBLE_LINK));
    if (!link) return;
    uint32_t ui_state;
    std::memcpy(&ui_state, link->context + 48, sizeof ui_state);
    chat_focused = (ui_state & 0x20) != 0; // GW2 chat or another game text box has focus
    if (now - last_mumble_check < 1000) return;
    last_mumble_check = now;
    std::wstring identity(link->identity, wcsnlen(link->identity, 256));
    auto key = identity.find(L"\"profession\":");
    ddr::Profession profession = ddr::Profession::Unknown;
    if (key != std::wstring::npos) profession = ddr::profession_from_mumble(static_cast<int>(std::wcstol(identity.c_str() + key + 13, nullptr, 10)));
    current_profession = profession;
}

// ---- widgets -------------------------------------------------------------------------------
void tooltip(const char* text) {
    if (text && *text && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
// ControlExtensions.ConvertToButton: images at 70% opacity that brighten when hovered.
bool image_button(const char* id, void* tex, float size, const char* tip, bool enabled = true) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size)) && enabled;
    bool hovered = ImGui::IsItemHovered();
    float alpha = !enabled ? 0.3f : hovered ? 1.f : 0.7f;
    if (tex) ImGui::GetWindowDrawList()->AddImage(tex, pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)));
    else ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)));
    tooltip(tip);
    return clicked;
}
void outlined_text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, bool stroke) {
    ImU32 shadow = IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xFF);
    if (stroke)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx || dy) dl->AddText(font, size, ImVec2(pos.x + dx, pos.y + dy), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1.5f, pos.y + 1.5f), shadow, text);
    dl->AddText(font, size, pos, color, text);
}
std::string clock_text(int seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%d : %02d", seconds / 60, seconds % 60);
    return buffer;
}

// ---- notes window (NotesView + NotesContainer rendering) -------------------------------------
void draw_target(ImDrawList* dl, ImVec2 o, const ddr::Layout& l) {
    const ImU32 tint = IM_COL32(255, 255, 255, 128); // TargetOpacity 0.5
    auto put = [&](const char* path, float x, float y, float w, float h) {
        if (void* t = image(path)) dl->AddImage(t, ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + w, o.y + y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
    };
    if (l.vertical()) {
        float y = static_cast<float>(l.target.y), h = static_cast<float>(l.note_h), cap = static_cast<float>(l.hpad);
        float first = static_cast<float>(l.lane_origin(0).x);
        put("notes/ddr_target_left.png", first - cap, y, cap, h);
        for (int lane = 0; lane < l.lanes; ++lane) {
            float x = static_cast<float>(l.lane_origin(lane).x);
            put("notes/ddr_target_circle.png", x, y, static_cast<float>(l.note_w), h);
            bool health_gap = l.center > 0 && lane == l.lanes / 2 - 1;
            if (lane + 1 < l.lanes && !health_gap) put("notes/ddr_target_spacer.png", x + l.note_w, y, static_cast<float>(l.spacing), h);
        }
        put("notes/ddr_target_right.png", l.lane_origin(l.lanes - 1).x + static_cast<float>(l.note_w), y, cap, h);
    } else {
        float x = static_cast<float>(l.target.x), w = static_cast<float>(l.note_w);
        float cap = static_cast<float>(std::min(l.vpad - 4.0, w * 0.375));
        cap = std::floor(cap);
        float y = static_cast<float>(l.target.y);
        put("notes/ddr_target_top.png", x, y - cap, w, cap);
        for (int lane = 0; lane < l.lanes; ++lane) {
            put("notes/ddr_target_circle.png", x, y, w, static_cast<float>(l.note_h));
            y += l.note_h;
            if (lane + 1 < l.lanes) { put("notes/ddr_target_spacer.png", x, y, w, static_cast<float>(l.spacing)); y += l.spacing; }
        }
        put("notes/ddr_target_bottom.png", x, y, w, cap);
    }
}
void draw_lane_lines(ImDrawList* dl, ImVec2 o, const ddr::Layout& l) {
    const ImU32 color = IM_COL32(255, 255, 255, 26); // white, opacity 0.1
    for (int lane = 0; lane < l.lanes; ++lane) {
        auto origin = l.lane_origin(lane);
        if (l.vertical()) {
            float x = o.x + origin.x + l.note_w / 2 - 1;
            float y0 = l.orientation == ddr::Orientation::BottomToTop ? l.target.y + static_cast<float>(l.note_h) : 0.f;
            float y1 = l.orientation == ddr::Orientation::BottomToTop ? static_cast<float>(l.height) : static_cast<float>(l.target.y);
            dl->AddRectFilled(ImVec2(x, o.y + y0), ImVec2(x + ddr::lane_line_thickness, o.y + y1), color);
        } else {
            float y = o.y + origin.y + l.note_h / 2 - 1;
            float x0 = l.orientation == ddr::Orientation::RightToLeft ? l.target.x + static_cast<float>(l.note_w) : 0.f;
            float x1 = l.orientation == ddr::Orientation::RightToLeft ? static_cast<float>(l.width) : static_cast<float>(l.target.x);
            dl->AddRectFilled(ImVec2(o.x + x0, y), ImVec2(o.x + x1, y + ddr::lane_line_thickness), color);
        }
    }
}
void draw_next_abilities(ImDrawList* dl, ImVec2 o, const ddr::Layout& l) {
    auto indexes = engine.next_ability_indexes();
    if (indexes.empty()) return;
    const auto& seq = engine.notes();
    float size = static_cast<float>(l.note_w), x = static_cast<float>(l.next_pos.x), y = static_cast<float>(l.next_pos.y);
    bool reversed = l.orientation == ddr::Orientation::LeftToRight || l.orientation == ddr::Orientation::TopToBottom;
    bool down = l.orientation == ddr::Orientation::TopToBottom || l.orientation == ddr::Orientation::BottomToTop;
    if (reversed) std::reverse(indexes.begin(), indexes.end());
    for (int index : indexes) {
        void* t = ability_icon(seq[index].ability);
        if (!t) t = image("abilityIcons/custom/unknownAbilityIcon.png");
        if (t) dl->AddImage(t, ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + size, o.y + y + size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 179));
        (down ? y : x) += size;
    }
}
float label_size(float height, std::size_t length) {
    if (length < 3) return height > 34 ? 32.f : height > 20 ? 18.f : height > 16 ? 14.f : 12.f;
    return height > 48 ? 32.f : height > 32 ? 18.f : height > 24 ? 14.f : 12.f;
}
void draw_notes(ImDrawList* dl, ImVec2 o, double now) {
    const auto& active = engine.active();
    const auto& seq = engine.notes();
    double e = engine.song_clock(now);
    const ddr::ActiveNote* hovered = nullptr;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool hover_allowed = ImGui::IsWindowHovered() && !engine.running();
    // Earlier notes are drawn above later ones; auto-hit Weapon 1 icons stay behind everything.
    std::vector<int> order;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = static_cast<int>(active.size()) - 1; i >= 0; --i)
            if (active[i].mini == (pass == 0)) order.push_back(i);
    for (int i : order) {
        const auto& a = active[i];
        float x, y, w, h;
        engine.note_rect(a, e, x, y, w, h);
        float alpha = static_cast<float>(std::clamp((now - a.created_wall) / ddr::fade_in_ms, 0.0, 1.0));
        float label_alpha = alpha;
        float scale = 1.f;
        if (a.resolved) {
            double t = now - a.resolved_wall;
            label_alpha *= static_cast<float>(std::max(0.0, 1 - t / ddr::label_fade_out_ms));
            if (a.missed) alpha *= static_cast<float>(std::max(0.0, 1 - t / ddr::miss_fade_ms));
            else {
                double k = std::min(1.0, t / ddr::hit_animation_ms);
                alpha *= static_cast<float>(1 - k);
                scale = static_cast<float>(1 - 0.75 * k);
            }
        }
        if (alpha <= 0.003f) continue;
        float cx = o.x + x + w / 2, cy = o.y + y + h / 2;
        ImVec2 p0(cx - w * scale / 2, cy - h * scale / 2), p1(cx + w * scale / 2, cy + h * scale / 2);
        if (a.missed) dl->AddRectFilled(p0, p1, IM_COL32(255, 0, 0, static_cast<int>(alpha * 255)));
        const ddr::Note& note = seq[a.index];
        void* t = settings.show_icons ? ability_icon(note.ability) : nullptr;
        if (!t) t = image(note_image(a.type));
        if (t) dl->AddImage(t, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)));
        if (settings.show_hotkeys && !a.mini && label_alpha > 0.003f) {
            std::string text = binding_label(settings.keys[static_cast<int>(a.type)], true);
            if (!text.empty()) {
                float size = label_size(h, text.size());
                ImFont* font = font_for(size);
                ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0, text.c_str());
                outlined_text(dl, font, size, ImVec2(cx - extent.x / 2, cy - extent.y / 2),
                              IM_COL32(255, 255, 255, static_cast<int>(label_alpha * 255)), text.c_str(), true);
            }
        }
        if (hover_allowed && !hovered && mouse.x >= p0.x && mouse.x < p1.x && mouse.y >= p0.y && mouse.y < p1.y) hovered = &a;
    }
    if (hovered) {
        const ddr::Note& note = seq[hovered->index];
        std::string name = ability_name(note.ability);
        if (!name.empty()) name += ", ";
        ImGui::SetTooltip("%sTime: %.3fs, Dur: %d ms", name.c_str(), note.time_ms / 1000.0, note.duration_ms);
    }
}
void draw_hit_texts(ImDrawList* dl, ImVec2 o, double now) {
    static const std::uint32_t colors[] = {0x00FFFF, 0x6495ED, 0x00FF7F, 0xFF4500, 0xFF0000};
    double e = engine.song_clock(now);
    for (const auto& text : engine.texts()) {
        double age = std::max(0.0, e - text.e0);
        float alpha = static_cast<float>(std::max(0.0, 1 - age / ddr::hit_text_life_ms));
        const char* label = ddr::hit_name(text.hit);
        ImFont* font = font_for(18.f);
        ImVec2 extent = font->CalcTextSizeA(18.f, 1e9f, 0, label);
        float y = text.y - static_cast<float>(age / 1000.0 * ddr::hit_text_speed);
        outlined_text(dl, font, 18.f, ImVec2(o.x + text.x - extent.x / 2, o.y + y - extent.y / 2),
                      rgb(colors[static_cast<int>(text.hit)], alpha), label, false);
    }
}
void draw_toolbar(double now) {
    const SongEntry* entry = selected_song();
    ddr::SongData data = entry ? song_data(entry->song.name) : ddr::SongData();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    if (image_button("##songlist", image("buttons/menuIcon.png"), button_size, "Song List")) list_visible = !list_visible;
    ImGui::SameLine();
    if (image_button("##songinfo", image("buttons/menuSettings.png"), button_size, "Song Info")) info_visible = !info_visible;
    ImGui::SameLine();
    if (image_button("##reset", image("buttons/stopIcon.png"), button_size, "Reset")) on_start_stop(engine.stop(now));
    ImGui::SameLine();
    bool running = engine.running();
    const char* tip = data.no_miss ? "Play Disabled in No Miss Mode. Press note key." : running ? "Pause" : "Play";
    if (image_button("##playpause", image(running && !data.no_miss ? "buttons/pauseIcon.png" : "buttons/playIcon.png"), button_size, tip, !data.no_miss)) {
        if (!engine.started() || engine.paused()) on_start_stop(engine.play(now));
        else engine.pause(now);
    }
    ImGui::SameLine();
    float right = 118.f;
    float title_width = std::max(40.f, ImGui::GetContentRegionAvail().x - right);
    std::string title = entry ? entry->song.name + (data.no_miss ? "  (No Miss Mode)" : "") : std::string("--");
    ImVec2 title_pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##title", ImVec2(title_width, button_size))) info_visible = !info_visible;
    ImGui::GetWindowDrawList()->PushClipRect(title_pos, ImVec2(title_pos.x + title_width, title_pos.y + button_size), true);
    ImFont* font = font_for(18.f);
    ImGui::GetWindowDrawList()->AddText(font, 18.f, ImVec2(title_pos.x, title_pos.y + 3), ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 230, 230), title.c_str());
    ImGui::GetWindowDrawList()->PopClipRect();
    ImGui::SameLine();
    if (ImGui::SmallButton(settings.lock_window ? "Unlock" : "Lock")) { settings.lock_window = !settings.lock_window; save_settings(); }
    tooltip("Lock hides the title bar and prevents moving or resizing the notes window");
    ImGui::SameLine();
    if (ImGui::SmallButton("Settings")) settings_visible = !settings_visible;
    ImGui::PopStyleVar();
}
void draw_notes_window(double now) {
    if (!notes_visible) return;
    bool vertical = ddr::is_vertical(settings.options.orientation);
    bool bar = settings.options.orientation == ddr::Orientation::AbilityBar;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    // Each window shape keeps its own saved size, like the per-orientation Blish window ids.
    ImVec2 size = bar ? ImVec2(800, 460) : vertical ? ImVec2(400, 800) : ImVec2(800, 400);
    ImGui::SetNextWindowPos(ImVec2(std::max(0.f, display.x / 2 - size.x / 2), std::max(0.f, display.y - size.y - 180)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    if (vertical) ImGui::SetNextWindowSizeConstraints(ImVec2(280, 460), ImVec2(800, 2048));
    else ImGui::SetNextWindowSizeConstraints(ImVec2(400, 280), ImVec2(2048, 800));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (settings.lock_window) flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin(bar ? notes_window_bar : vertical ? notes_window_v : notes_window_h, &notes_visible, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (void* bg = image("notes/notesBg.png"))
            dl->AddImage(bg, p0, ImVec2(p0.x + avail.x, p0.y + avail.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(settings.opacity * 255)));
        if (void* bar = image("notes/notesControlsBg.png"))
            dl->AddImage(bar, p0, ImVec2(p0.x + avail.x, p0.y + toolbar_height), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>((1 - settings.opacity) * 255)));
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 10, p0.y + 10));
        ImGui::PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + toolbar_height), true);
        draw_toolbar(now);
        ImGui::PopClipRect();
        ImVec2 o(p0.x, p0.y + toolbar_height);
        int width = std::max(1, static_cast<int>(avail.x)), height = std::max(1, static_cast<int>(avail.y - toolbar_height));
        if (width != engine.layout().width || height != engine.layout().height || engine.options() != settings.options) {
            engine.configure(settings.options, width, height); // OnResized: Reset + AddInitialNotes
            on_start_stop(engine.reset_and_prepare(now));
        }
        const ddr::Layout& l = engine.layout();
        dl->PushClipRect(o, ImVec2(o.x + width, o.y + height), true);
        draw_lane_lines(dl, o, l);
        draw_target(dl, o, l);
        draw_next_abilities(dl, o, l);
        draw_notes(dl, o, now);
        dl->PopClipRect();
        draw_hit_texts(dl, o, now);
        if (engine.paused())
            dl->AddText(ImGui::GetFont(), 14.f, ImVec2(o.x + 10, o.y + 10), IM_COL32(211, 211, 211, 255),
                        clock_text(static_cast<int>(engine.song_clock(now) / 1000)).c_str());
        if (!engine.has_song()) {
            const char* hint = "Select a song in the Song List";
            ImVec2 extent = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(o.x + (width - extent.x) / 2, o.y + (height - extent.y) / 2), IM_COL32(220, 220, 220, 220), hint);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
void draw_rating_overlay(double now) {
    if (!settings.rating_overlay || rating_text.empty()) return;
    double age = now - rating_time;
    if (age > 1500) return;
    float alpha = static_cast<float>(age < 1000 ? 1.0 : 1 - (age - 1000) / 500);
    ImFont* font = big_font();
    float size = 34.f;
    ImVec2 extent = font->CalcTextSizeA(size, 1e9f, 0, rating_text.c_str());
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImU32 color = (rating_color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha * 255) << IM_COL32_A_SHIFT);
    outlined_text(ImGui::GetForegroundDrawList(), font, size, ImVec2(display.x / 2 - extent.x / 2, display.y * 0.12f), color, rating_text.c_str(), true);
}

// ---- song list (SongListView) ----------------------------------------------------------------
std::vector<const SongEntry*> visible_songs() {
    std::vector<const SongEntry*> list;
    bool filter = settings.only_profession && current_profession != ddr::Profession::Unknown;
    for (const auto& [name, entry] : songs) {
        auto p = entry.song.profession;
        if (!filter || p == ddr::Profession::Common || p == ddr::Profession::Unknown || p == current_profession) list.push_back(&entry);
    }
    std::sort(list.begin(), list.end(), [](const SongEntry* a, const SongEntry* b) {
        if (a->song.profession != b->song.profession) return a->song.profession < b->song.profession;
        if (a->song.elite != b->song.elite) return a->song.elite < b->song.elite;
        return a->song.name < b->song.name;
    });
    return list;
}
void draw_song_list() {
    if (!list_visible) return;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(0.f, display.x / 2 - 400), std::max(0.f, display.y - 400 - 180 - 420)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 280), ImVec2(500, 800));
    if (ImGui::Begin(list_window, &list_visible, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::Button("Add From Clipboard")) add_song_from_clipboard();
        tooltip("Copy a song's JSON (for example from the Dance Dance Rotation Composer), then press this");
        ImGui::SameLine();
        if (ImGui::Button("Create Song") && !open_url(composer_url)) alert("Failed to open link.");
        tooltip(composer_url);
        auto list = visible_songs();
        ImGui::SameLine();
        if (settings.only_profession && current_profession != ddr::Profession::Unknown)
            ImGui::TextDisabled("%zu / %zu songs (%s)", list.size(), songs.size(), ddr::profession_name(current_profession));
        else ImGui::TextDisabled("%zu songs", songs.size());
        ImGui::Separator();
        if (ImGui::BeginChild("##songs")) {
            std::string pending_select, pending_delete;
            for (const auto* entry : list) {
                const auto& s = entry->song;
                ImGui::PushID(s.name.c_str());
                bool selected = s.name == settings.selected;
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
                if (ImGui::RadioButton("##select", selected) && !selected) pending_select = s.name;
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TextColored(rgb4(ddr::profession_color(s.profession)), "%s%s", s.elite.c_str(), entry->file.empty() ? "" : "  (custom)");
                ImGui::EndGroup();
                if (ImGui::IsItemClicked() && !selected) pending_select = s.name;
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - button_size - 8);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
                if (image_button("##delete", image("buttons/deleteIcon.png"), button_size, entry->file.empty() ? "Hide this default song (restored on addon update)" : "Delete this custom song file"))
                    pending_delete = s.name;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (list.empty()) ImGui::TextWrapped("No songs for this profession. Disable 'Only show current profession songs' in Settings to see all songs.");
            if (!pending_select.empty()) select_song(pending_select);
            if (!pending_delete.empty()) { delete_candidate = pending_delete; ImGui::OpenPopup("Delete song?"); }
            if (ImGui::BeginPopupModal("Delete song?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Remove \"%s\"?", delete_candidate.c_str());
                if (ImGui::Button("Delete")) { delete_song(delete_candidate); ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// ---- song info (SongInfoView) ----------------------------------------------------------------
void read_only_field(const char* id, const std::string& value) {
    std::vector<char> buffer(value.begin(), value.end());
    buffer.push_back('\0');
    ImGui::SetNextItemWidth(-button_size - 12);
    ImGui::InputText(id, buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
}
void draw_song_info(double now) {
    if (!info_visible) return;
    (void)now;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(0.f, display.x / 2 - 400 - 390), std::max(0.f, display.y - 400 - 180 - 420)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(333, 676), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(info_window, &info_visible, ImGuiWindowFlags_NoCollapse)) {
        const SongEntry* entry = selected_song();
        if (!entry) {
            ImGui::TextUnformatted("--");
            ImGui::TextDisabled("Select a song in the Song List.");
            ImGui::End();
            return;
        }
        const auto& s = entry->song;
        ddr::SongData data = song_data(s.name);
        ImGui::PushTextWrapPos(0);
        ImGui::PushFont(big_font());
        ImGui::TextWrapped("%s", s.name.c_str());
        ImGui::PopFont();
        ImGui::TextColored(rgb4(ddr::profession_color(s.profession)), "%s", s.elite.c_str());
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Description", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::TextWrapped("%s", s.description.empty() ? "--" : s.description.c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextUnformatted("Build URL");
        read_only_field("##buildurl", s.build_url.empty() ? "--" : s.build_url);
        if (!s.build_url.empty()) {
            ImGui::SameLine();
            if (image_button("##openurl", image("buttons/openLinkIcon.png"), button_size, "Open the build page") && !open_url(s.build_url)) alert("Failed to open link.");
        }
        if (!s.build_code.empty()) {
            ImGui::TextUnformatted("Build Template");
            read_only_field("##buildcode", s.build_code);
            ImGui::SameLine();
            if (image_button("##copybuild", image("buttons/copyIcon.png"), button_size, "Copy the build template chat code"))
                alert(copy_to_clipboard(s.build_code) ? "Copied Build to Clipboard" : "No Build Template to Copy");
        }
        ImGui::Spacing();
        bool changed = false, commit = false;
        if (ImGui::CollapsingHeader("Practice Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("No Miss Mode", &data.no_miss)) { changed = true; commit = true; }
            ImGui::TextWrapped("Notes will automatically pause in the perfect position and resume when hit. Great for learning the rotation keys without worrying about timing.");
            int speed = static_cast<int>(std::lround(data.rate * 100));
            ImGui::TextUnformatted("Note Speed");
            tooltip("Setting under 100% slows down the song, adding more time in between notes.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##speed", &speed, 10, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) { data.rate = speed / 100.f; changed = true; }
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            tooltip("Setting under 100% slows down the song, adding more time in between notes.");
            ImGui::TextUnformatted("Note Pace");
            tooltip("Sets how fast notes move. This does not affect how fast you have to press buttons.");
            ImGui::SetNextItemWidth(-1);
            char pace_label[16];
            std::snprintf(pace_label, sizeof pace_label, "%d%%%%", data.pace * 100 / ddr::default_pace);
            if (ImGui::SliderInt("##pace", &data.pace, ddr::min_pace, ddr::max_pace, pace_label, ImGuiSliderFlags_AlwaysClamp)) changed = true;
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            tooltip("Sets how fast notes move. This does not affect how fast you have to press buttons.");
            int last = s.notes.empty() ? 0 : s.notes.back().time_ms / 1000;
            ImGui::TextUnformatted("Start At");
            tooltip("Sets what time the notes start");
            ImGui::SetNextItemWidth(-1);
            data.start_at = std::min(data.start_at, last);
            std::string start_label = clock_text(data.start_at);
            if (ImGui::SliderInt("##start", &data.start_at, 0, std::max(last, 0), start_label.c_str(), ImGuiSliderFlags_AlwaysClamp)) changed = true;
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            tooltip("Sets what time the notes start");
        }
        if (ImGui::CollapsingHeader("Remap Utility Skills", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (s.profession == ddr::Profession::Common) {
                ImGui::TextColored(rgb4(0xD3D3D3), "Utility remapping is disabled.");
            } else if (s.profession == ddr::Profession::Revenant) {
                ImGui::PushStyleColor(ImGuiCol_Text, rgb(0xFF6347));
                ImGui::TextWrapped("Remapping utilities is currently disabled for Revenant. Utility skills on both Legends must match the song's build.");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, rgb(0xD3D3D3));
                ImGui::TextWrapped("Set the utility icon positions you use if you prefer utility icons in different positions than the song's build template.");
                ImGui::PopStyleColor();
                // Slot k shows the build utility whose mapping is k.
                int slot_ability[3] = {-9999999, -9999999, -9999999};
                for (int i = 0; i < 3; ++i) slot_ability[data.mapping[i]] = ability_for_palette(s.utility[i]);
                static const char* tips[] = {"This should match in-game utility slot 1", "This should match in-game utility slot 2", "This should match in-game utility slot 3"};
                for (int slot = 0; slot < 3; ++slot) {
                    void* t = ability_icon(slot_ability[slot]);
                    if (!t) t = image("abilityIcons/custom/unknownAbilityIcon.png");
                    if (t) ImGui::Image(t, ImVec2(72, 72));
                    else ImGui::Dummy(ImVec2(72, 72));
                    if (ImGui::IsItemHovered()) {
                        std::string name = ability_name(slot_ability[slot]);
                        ImGui::SetTooltip("%s%s%s", tips[slot], name.empty() ? "" : "\n", name.c_str());
                    }
                    ImGui::SameLine();
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
                if (image_button("##rotate", image("buttons/reloadIcon.png"), 48, "Change the remapping ordering.")) {
                    data.rotate_mapping();
                    changed = true; commit = true;
                }
            }
        }
        if (changed) update_song_data(s.name, data);
        if (commit) save_settings();
    }
    ImGui::End();
}

// ---- help (HelpView) ----------------------------------------------------------------------------
void draw_help() {
    if (!help_visible) return;
    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(std::max(0.f, display.x / 2 - 415), std::max(100.f, display.y - 400 - 180 - 420 - 360)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(830, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(help_window, &help_visible, ImGuiWindowFlags_NoCollapse)) {
        struct Step { const char* image; const char* title; const char* subtitle; const char* button; };
        const Step steps[] = {{"help/setHotkeys.png", "Set Hotkeys", "in Settings", "Open##settings"},
                              {"help/selectSong.png", "Select Song", "", "Open##songlist"},
                              {"help/pressPlay.png", "Press Play", "", "Open##notes"}};
        ImFont* font = big_font();
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(10 + i * 270.f);
            ImGui::BeginGroup();
            if (void* t = image(steps[i].image)) ImGui::Image(t, ImVec2(250, 150)); else ImGui::Dummy(ImVec2(250, 150));
            ImVec2 extent = font->CalcTextSizeA(30.f, 1e9f, 0, steps[i].title);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddText(font, 30.f, ImVec2(pos.x + 125 - extent.x / 2, pos.y), IM_COL32(0xC7, 0xAE, 0x78, 255), steps[i].title);
            ImGui::Dummy(ImVec2(250, 34));
            if (*steps[i].subtitle) {
                ImVec2 sub = ImGui::CalcTextSize(steps[i].subtitle);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 125 - sub.x / 2);
                ImGui::TextUnformatted(steps[i].subtitle);
            }
            bool enabled = i != 2 || selected_song();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 125 - 40);
            if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
            if (ImGui::Button(steps[i].button, ImVec2(80, 0)) && enabled) {
                if (i == 0) settings_visible = true;
                else if (i == 1) list_visible = true;
                else notes_visible = true;
            }
            if (!enabled) { ImGui::PopStyleVar(); tooltip("Select a song first"); }
            ImGui::EndGroup();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Hotkeys must match your in-game skill hotkeys. Keys are observed, never blocked or sent to the game.");
    }
    ImGui::End();
}

// ---- settings (ModuleSettings) --------------------------------------------------------------------
void draw_settings_contents() {
    auto& o = settings.options;
    bool save = false;
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* orientations[] = {"Right to Left", "Left to Right", "Top to Bottom", "Bottom to Top", "Ability Bar"};
        int orientation = static_cast<int>(o.orientation);
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Notes Orientation", &orientation, orientations, 5)) { o.orientation = static_cast<ddr::Orientation>(orientation); save = true; }
        tooltip("Sets the direction notes will travel while playing.");
        int opacity = static_cast<int>(std::lround(settings.opacity * 100));
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Background Transparency", &opacity, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp)) settings.opacity = opacity / 100.f;
        save |= ImGui::IsItemDeactivatedAfterEdit();
        tooltip("Sets the transparency of the notes background. Min=0% Max=100%");
        save |= ImGui::Checkbox("Start with first skill", &o.start_with_first_skill);
        tooltip("If enabled, the song can be started by pressing the hotkey for the first ability.\nNotes will be shifted so the first note is already in the 'Perfect' location.\nIf start time is after 0s, the first note after the start time is shifted to be the start position.");
        save |= ImGui::Checkbox("Auto Hit Weapon 1", &o.auto_hit_weapon1);
        tooltip("If enabled, Weapon1 skills will automatically clear, instead of requiring hotkey presses, since they are probably on auto-cast.");
        save |= ImGui::Checkbox("Show ability icon as note", &settings.show_icons);
        tooltip("If enabled, notes will use the actual ability icon instead of the generic.");
        save |= ImGui::Checkbox("Show ability hotkeys", &settings.show_hotkeys);
        tooltip("If enabled, notes will have the hotkeys displayed on top of them.");
        save |= ImGui::Checkbox("Only show current profession songs", &settings.only_profession);
        tooltip("If enabled, the song list will only show songs for the current profession");
        const char* styles[] = {"Regular", "Compact", "Ultra Compact"};
        int compact = static_cast<int>(o.compact);
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Compact Mode", &compact, styles, 3)) { o.compact = static_cast<ddr::CompactStyle>(compact); save = true; }
        tooltip("Adjusts how notes are distributed in the lanes.\nRegular - Notes will go to lane based on their hotkey.\nCompact - Notes will try to go to one lane, but be shifted down to prevent overlap.\nUltra Compact - All notes will be in one lane. Overlap may be significant, so use Show next abilities.\n\nThis has no effect in Ability Bar Orientation.");
        ImGui::SetNextItemWidth(200);
        ImGui::SliderInt("Show next abilities", &o.next_abilities, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
        save |= ImGui::IsItemDeactivatedAfterEdit();
        tooltip("If enabled, the next X ability icons will be shown above the notes section, with no animations.");
        save |= ImGui::Checkbox("Show hit rating at top of screen", &settings.rating_overlay);
        tooltip("Shows Perfect / Great / Good / Boo at the top of the screen, like the original screen notifications.");
        save |= ImGui::Checkbox("Lock notes window", &settings.lock_window);
        tooltip("Hides the title bar and prevents moving or resizing, for overlaying the ability bar.");
    }
    if (ImGui::CollapsingHeader("Ability Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("Each hotkey must match your in-game hotkeys to work! Presses are observed and still reach the game. Mouse buttons 3-5 can be assigned.");
        int capturing = capture_target.load();
        capture_ui_seen = true;
        static const NoteType order[] = {NoteType::WeaponSwap, NoteType::Dodge, NoteType::Weapon1, NoteType::Weapon2, NoteType::Weapon3,
                                         NoteType::Weapon4, NoteType::Weapon5, NoteType::Heal, NoteType::Utility1, NoteType::Utility2,
                                         NoteType::Utility3, NoteType::Elite, NoteType::Profession1, NoteType::Profession2,
                                         NoteType::Profession3, NoteType::Profession4, NoteType::Profession5, NoteType::WeaponStow};
        for (NoteType type : order) {
            int i = static_cast<int>(type);
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ddr::hotkey_description(type));
            ImGui::SameLine(160);
            if (capturing == i) ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Press a key...  (Esc cancels)");
            else ImGui::TextUnformatted(binding_label(settings.keys[i], false).c_str());
            ImGui::SameLine(360);
            if (ImGui::SmallButton("Set")) capture_target.store(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) { settings.keys[i] = {}; if (capturing == i) capture_target.store(-1); save = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Default")) { settings.keys[i] = {ddr::default_hotkey(type), 0}; save = true; }
            ImGui::PopID();
        }
    }
    if (ImGui::CollapsingHeader("Control and Window Hotkeys")) {
        ImGui::TextWrapped("Toggle Play/Stop, Play, Pause, Stop and the window toggles are Nexus keybinds: set them in Nexus Options > Keybinds under Dance Dance Rotation. The notes window toggle defaults to Ctrl+Alt+D.");
    }
    if (ImGui::Button("Song List")) list_visible = true;
    ImGui::SameLine();
    if (ImGui::Button("Song Info")) info_visible = true;
    ImGui::SameLine();
    if (ImGui::Button("Help")) help_visible = true;
    ImGui::SameLine();
    if (ImGui::Button("Online help") && !open_url(help_url)) alert("Failed to open link.");
    if (save) save_settings();
}
void draw_settings_window() {
    if (!settings_visible) return;
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(settings_window, &settings_visible, ImGuiWindowFlags_NoCollapse)) draw_settings_contents();
    ImGui::End();
    if (!settings_visible && capture_target.load() >= 0) capture_target.store(-1);
}

// ---- Nexus callbacks ---------------------------------------------------------------------------------
void render() {
    try {
        double now = now_ms();
        // Cancel a pending key assignment when its settings panel is no longer shown.
        if (capture_target.load() >= 0 && !capture_ui_seen) capture_target.store(-1);
        capture_ui_seen = false;
        poll_mumble(now);
        process_input(now);
        poll_custom_songs(now);
        bool was_started = engine.started();
        engine.advance(now);
        on_start_stop(was_started != engine.started());
        draw_notes_window(now);
        draw_song_list();
        draw_song_info(now);
        draw_help();
        draw_settings_window();
        draw_rating_overlay(now);
    } catch (const std::exception& e) { log(LOGL_WARNING, e.what()); }
}
void render_options() {
    try {
        if (ImGui::Button("Open Dance Dance Rotation")) notes_visible = true;
        draw_settings_contents();
    } catch (const std::exception& e) { log(LOGL_WARNING, e.what()); }
}
void render_context_menu() {
    if (ImGui::Selectable("Notes window")) notes_visible = !notes_visible;
    if (ImGui::Selectable("Song List")) list_visible = !list_visible;
    if (ImGui::Selectable("Song Info")) info_visible = !info_visible;
    if (ImGui::Selectable("Settings")) settings_visible = !settings_visible;
    if (ImGui::Selectable("Help")) help_visible = !help_visible;
}
void load(AddonAPI_t* provided) {
    api = provided;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency = 1000.0 / static_cast<double>(f.QuadPart);
    notes_visible = list_visible = info_visible = help_visible = settings_visible = false;
    requested_textures.clear();
    texture_cache.clear();
    rating_text.clear();
    pending_keys.clear();
    capture_target.store(-1);
    capture_done = false;
    for (auto& flag : bind_pressed) flag.store(false);
    try {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
        ImGui::SetAllocatorFunctions(reinterpret_cast<void* (*)(size_t, void*)>(api->ImguiMalloc),
                                     reinterpret_cast<void (*)(void*, void*)>(api->ImguiFree));
        const char* folder = api->Paths_GetAddonDirectory("dance-dance-rotation");
        if (!folder || !*folder) throw std::runtime_error("Nexus did not provide an addon directory");
        addon_dir = fs::path(folder);
        fs::create_directories(addon_dir);
        settings_path = addon_dir / "settings.txt";
        custom_dir = addon_dir / "customSongs";
        fs::create_directories(custom_dir);
        load_settings();
        if (api->Textures_GetOrCreateFromMemory)
            for (const auto& img : bundled_images::images)
                api->Textures_GetOrCreateFromMemory(img.identifier, const_cast<unsigned char*>(img.data), img.size);
        load_songs();
        engine.configure(settings.options, 800, 356);
        apply_selection();
        watch_custom_songs();
        if (!settings.help_shown) { help_visible = true; settings.help_shown = true; } // first run
        save_settings();
        for (const auto& bind : binds) api->InputBinds_RegisterWithString(bind.id, on_bind, bind.fallback);
        api->WndProc_Register(wndproc);
        api->QuickAccess_Add(shortcut_id, logo_id, logo_id, binds[BindNotes].id, "Dance Dance Rotation");
        if (api->QuickAccess_AddContextMenu) api->QuickAccess_AddContextMenu(context_menu_id, shortcut_id, render_context_menu);
        api->GUI_RegisterCloseOnEscape(list_window, &list_visible);
        api->GUI_RegisterCloseOnEscape(info_window, &info_visible);
        api->GUI_RegisterCloseOnEscape(settings_window, &settings_visible);
        api->GUI_Register(RT_Render, render);
        api->GUI_Register(RT_OptionsRender, render_options);
        log(LOGL_INFO, std::string("Loaded Dance Dance Rotation ") + version_text + " (Blish HUD module 1.8.1 port).");
    } catch (const std::exception& e) { log(LOGL_CRITICAL, e.what()); }
}
void unload() {
    if (!api) return;
    api->GUI_Deregister(render);
    api->GUI_Deregister(render_options);
    api->GUI_DeregisterCloseOnEscape(list_window);
    api->GUI_DeregisterCloseOnEscape(info_window);
    api->GUI_DeregisterCloseOnEscape(settings_window);
    if (api->QuickAccess_RemoveContextMenu) api->QuickAccess_RemoveContextMenu(context_menu_id);
    api->QuickAccess_Remove(shortcut_id);
    api->WndProc_Deregister(wndproc);
    for (const auto& bind : binds) api->InputBinds_Deregister(bind.id);
    if (change_handle != INVALID_HANDLE_VALUE) { FindCloseChangeNotification(change_handle); change_handle = INVALID_HANDLE_VALUE; }
    // No addon threads or texture callbacks exist; Nexus owns the textures.
    engine.set_song(nullptr, ddr::SongData());
    engine.reset_and_prepare(0);
    songs.clear();
    texture_cache.clear();
    requested_textures.clear();
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        pending_keys.clear();
    }
    api = nullptr;
}
} // namespace

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    // Local signature, not registered in the public Nexus library.
    static AddonDefinition_t definition = {
        0x44445250, NEXUS_API_VERSION, "Dance Dance Rotation (Fedora port)",
        {0, 1, 0, 0}, "Shooper (original) / community Nexus port",
        "Learn class rotations to the beat! Native port of the Blish HUD module 1.8.1.",
        load, unload, AF_None, UP_None, nullptr
    };
    return &definition;
}
