// Dance Dance Rotation data model, ported from the Blish HUD module 1.8.1
// (Model/*.cs and Storage/SongTranslator.cs). Platform independent.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ddr {

enum class NoteType : std::uint8_t {
    Dodge, WeaponSwap, WeaponStow,
    Weapon1, Weapon2, Weapon3, Weapon4, Weapon5,
    Heal, Utility1, Utility2, Utility3, Elite,
    Profession1, Profession2, Profession3, Profession4, Profession5,
    Unknown
};
constexpr int note_type_count = 19;

enum class Orientation { RightToLeft, LeftToRight, TopToBottom, BottomToTop, AbilityBar };
enum class CompactStyle { Regular, Compact, UltraCompact };
enum class Profession { Common, Guardian, Revenant, Warrior, Engineer, Ranger, Thief, Elementalist, Mesmer, Necromancer, Unknown };

inline const char* note_type_name(NoteType type) {
    static const char* names[] = {
        "Dodge", "WeaponSwap", "WeaponStow", "Weapon1", "Weapon2", "Weapon3", "Weapon4", "Weapon5",
        "Heal", "Utility1", "Utility2", "Utility3", "Elite",
        "Profession1", "Profession2", "Profession3", "Profession4", "Profession5", "Unknown"};
    return names[static_cast<int>(type)];
}

inline const char* hotkey_description(NoteType type) {
    static const char* names[] = {
        "Dodge", "Swap Weapons", "Stow Weapons", "Weapon Skill 1", "Weapon Skill 2", "Weapon Skill 3",
        "Weapon Skill 4", "Weapon Skill 5", "Healing Skill", "Utility Skill 1", "Utility Skill 2",
        "Utility Skill 3", "Elite Skill", "Profession Skill 1", "Profession Skill 2", "Profession Skill 3",
        "Profession Skill 4", "Profession Skill 5", "Unknown"};
    return names[static_cast<int>(type)];
}

// Equivalent of C# Enum.TryParse<NoteType>: case-sensitive names, or a defined numeric value.
inline bool parse_note_type(const std::string& text, NoteType& out) {
    for (int i = 0; i < note_type_count; ++i)
        if (text == note_type_name(static_cast<NoteType>(i))) { out = static_cast<NoteType>(i); return true; }
    if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos && text.size() < 4) {
        int value = std::atoi(text.c_str());
        if (value < note_type_count) { out = static_cast<NoteType>(value); return true; }
    }
    return false;
}

// Windows virtual-key codes. The original used XNA Keys, whose values are the same codes.
// Profession Skill 5 defaults to F5; the reference module used F4 twice.
inline int default_hotkey(NoteType type) {
    switch (type) {
        case NoteType::Dodge: return 'V';
        case NoteType::WeaponSwap: return 0xC0; // VK_OEM_3 (`~)
        case NoteType::Weapon1: return '1';
        case NoteType::Weapon2: return '2';
        case NoteType::Weapon3: return '3';
        case NoteType::Weapon4: return '4';
        case NoteType::Weapon5: return '5';
        case NoteType::Heal: return '6';
        case NoteType::Utility1: return '7';
        case NoteType::Utility2: return '8';
        case NoteType::Utility3: return '9';
        case NoteType::Elite: return '0';
        case NoteType::Profession1: return 0x70;
        case NoteType::Profession2: return 0x71;
        case NoteType::Profession3: return 0x72;
        case NoteType::Profession4: return 0x73;
        case NoteType::Profession5: return 0x74;
        default: return 0;
    }
}

inline bool is_vertical(Orientation orientation) {
    return orientation == Orientation::TopToBottom || orientation == Orientation::BottomToTop ||
           orientation == Orientation::AbilityBar;
}

// NoteTypeExtensions.NoteLane
inline int note_lane(Orientation orientation, NoteType type) {
    if (orientation != Orientation::AbilityBar) {
        switch (type) {
            case NoteType::Weapon1: case NoteType::Heal: case NoteType::Profession1: return 0;
            case NoteType::Weapon2: case NoteType::Utility1: case NoteType::Profession2: return 1;
            case NoteType::Weapon3: case NoteType::Utility2: case NoteType::Profession3: return 2;
            case NoteType::Weapon4: case NoteType::Utility3: case NoteType::Profession4: return 3;
            case NoteType::Weapon5: case NoteType::Elite: case NoteType::Profession5: return 4;
            case NoteType::Dodge: case NoteType::WeaponSwap: case NoteType::WeaponStow: case NoteType::Unknown: return 5;
        }
        return 0;
    }
    switch (type) {
        case NoteType::Weapon1: case NoteType::Profession1: case NoteType::WeaponSwap: case NoteType::WeaponStow: return 0;
        case NoteType::Weapon2: case NoteType::Profession2: return 1;
        case NoteType::Weapon3: case NoteType::Profession3: return 2;
        case NoteType::Weapon4: case NoteType::Profession4: return 3;
        case NoteType::Weapon5: case NoteType::Profession5: return 4;
        case NoteType::Heal: return 5;
        case NoteType::Utility1: case NoteType::Dodge: return 6;
        case NoteType::Utility2: return 7;
        case NoteType::Utility3: return 8;
        case NoteType::Elite: case NoteType::Unknown: return 9;
    }
    return 0;
}

enum class NoteColor { Red, Purple, Green };
inline NoteColor note_color(NoteType type) {
    switch (type) {
        case NoteType::Dodge: case NoteType::Heal: case NoteType::Utility1: case NoteType::Utility2:
        case NoteType::Utility3: case NoteType::Elite: return NoteColor::Purple;
        case NoteType::WeaponStow: case NoteType::Profession1: case NoteType::Profession2: case NoteType::Profession3:
        case NoteType::Profession4: case NoteType::Profession5: case NoteType::Unknown: return NoteColor::Green;
        default: return NoteColor::Red;
    }
}

inline Profession profession_from_build_template(int code) {
    switch (code) {
        case 0: return Profession::Common;
        case 1: return Profession::Guardian;
        case 2: return Profession::Warrior;
        case 3: return Profession::Engineer;
        case 4: return Profession::Ranger;
        case 5: return Profession::Thief;
        case 6: return Profession::Elementalist;
        case 7: return Profession::Mesmer;
        case 8: return Profession::Necromancer;
        case 9: return Profession::Revenant;
        default: return Profession::Unknown;
    }
}

// MumbleLink identity uses the same numbering, with 0 meaning no character.
inline Profession profession_from_mumble(int code) {
    return code == 0 ? Profession::Unknown : profession_from_build_template(code);
}

inline const char* profession_name(Profession profession) {
    static const char* names[] = {"Common", "Guardian", "Revenant", "Warrior", "Engineer", "Ranger", "Thief",
                                  "Elementalist", "Mesmer", "Necromancer", "Unknown"};
    return names[static_cast<int>(profession)];
}

inline std::uint32_t profession_color(Profession profession) { // 0xRRGGBB
    static const std::uint32_t colors[] = {0xBBBBBB, 0x72C1D9, 0xD16E5A, 0xFFD166, 0xD09C59, 0x8CDC82,
                                           0xC08F95, 0xF68A87, 0xB679D5, 0x52A76F, 0xBBBBBB};
    return colors[static_cast<int>(profession)];
}

inline std::string elite_name(Profession profession, int specialization) {
    switch (specialization) {
        case 5: return "Druid"; case 7: return "Daredevil"; case 18: return "Berserker";
        case 27: return "Dragonhunter"; case 34: return "Reaper"; case 40: return "Chronomancer";
        case 43: return "Scrapper"; case 48: return "Tempest"; case 52: return "Herald";
        case 55: return "Soulbeast"; case 56: return "Weaver"; case 57: return "Holosmith";
        case 58: return "Deadeye"; case 59: return "Mirage"; case 60: return "Scourge";
        case 61: return "Spellbreaker"; case 62: return "Firebrand"; case 63: return "Renegade";
        case 64: return "Harbinger"; case 65: return "Willbender"; case 66: return "Virtuoso";
        case 67: return "Catalyst"; case 68: return "Bladesworn"; case 69: return "Vindicator";
        case 70: return "Mechanist"; case 71: return "Specter"; case 72: return "Untamed";
        case 73: return "Troubadour"; case 74: return "Paragon"; case 75: return "Amalgam";
        case 76: return "Ritualist"; case 77: return "Antiquary"; case 78: return "Galeshot";
        case 79: return "Conduit"; case 80: return "Evoker"; case 81: return "Luminary";
        default: return profession_name(profession);
    }
}

struct Note {
    NoteType type = NoteType::Unknown;
    std::int32_t time_ms = 0;
    std::int32_t duration_ms = 0;
    std::int32_t ability = 0;
    bool override_auto = false;
};

struct Song {
    std::string name, description, build_url, build_code, elite;
    Profession profession = Profession::Unknown;
    std::int32_t utility[3] = {0, 0, 0}; // Palette ids of the build template utility slots
    std::vector<Note> notes;
    bool is_default = false;
};

constexpr int min_pace = 75, max_pace = 600, default_pace = 300;

// Per-song practice settings (SongData.cs). Mapping values are 0..2 for slots One..Three.
struct SongData {
    int mapping[3] = {0, 1, 2};
    bool no_miss = false;
    float rate = 1.0f;
    int start_at = 0;
    int pace = default_pace;
    bool operator==(const SongData& o) const {
        return std::equal(mapping, mapping + 3, o.mapping) && no_miss == o.no_miss && rate == o.rate &&
               start_at == o.start_at && pace == o.pace;
    }
    bool operator!=(const SongData& o) const { return !(*this == o); }
    NoteType remap(NoteType type) const {
        if (type == NoteType::Utility1 || type == NoteType::Utility2 || type == NoteType::Utility3) {
            int slot = static_cast<int>(type) - static_cast<int>(NoteType::Utility1);
            return static_cast<NoteType>(static_cast<int>(NoteType::Utility1) + mapping[slot]);
        }
        return type;
    }
    // The original "rotate remapping" button cycles the six orders lexicographically.
    void rotate_mapping() {
        if (!std::next_permutation(mapping, mapping + 3)) std::sort(mapping, mapping + 3);
    }
};

} // namespace ddr
