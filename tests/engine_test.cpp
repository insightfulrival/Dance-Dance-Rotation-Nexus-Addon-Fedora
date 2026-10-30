// Native tests for the platform-independent port: JSON, song decoding, layout and gameplay.
#include "engine.hpp"
#include "song_json.hpp"
#include "data.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace ddr;

static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)
static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }

static Note note(NoteType type, int time, int ability = 1, bool override_auto = false) {
    Note n; n.type = type; n.time_ms = time; n.ability = ability; n.override_auto = override_auto; return n;
}

static void test_json() {
    auto v = json::parse("\xEF\xBB\xBF {\"a\": [1, -2.5e2, true, null, \"x\\u00e9\\ud83d\\ude00\\n\"], \"B\": {}}");
    CHECK(v.kind == json::Value::Object && v.find("a")->items.size() == 5);
    CHECK(near(v.find("a")->items[1].number, -250));
    CHECK(v.find("a")->items[4].text == "x\xC3\xA9\xF0\x9F\x98\x80\n");
    CHECK(v.find("b") && v.find("b")->kind == json::Value::Object); // case-insensitive fallback
    const char* bad[] = {"", "{", "[1,]", "{\"a\" 1}", "01", "\"\\x\"", "[1] 2", "nul", "\"a\nb\""};
    for (const char* text : bad) {
        bool threw = false;
        try { json::parse(text); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

static std::string read(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out; out << in.rdbuf();
    return out.str();
}

static void test_default_songs(const char* path) {
    auto root = json::parse(read(path));
    CHECK(root.kind == json::Value::Array && static_cast<int>(root.items.size()) == bundled::raw_song_count);
    std::map<std::string, Song> songs;
    for (const auto& item : root.items) {
        Song song = song_from_json(item);
        songs[song.name] = song; // Later definitions replace earlier ones.
    }
    constexpr int count = sizeof(bundled::songs) / sizeof(bundled::songs[0]);
    CHECK(static_cast<int>(songs.size()) == count);
    CHECK(count == 102);
    int notes = 0;
    for (const auto& record : bundled::songs) {
        auto found = songs.find(record.name);
        CHECK(found != songs.end());
        const Song& song = found->second;
        CHECK(song.description == record.description && song.build_url == record.build_url && song.build_code == record.build_code);
        CHECK(static_cast<int>(song.profession) == record.profession);
        CHECK(song.elite == elite_name(song.profession, record.specialization));
        for (int i = 0; i < 3; ++i) CHECK(song.utility[i] == record.utility[i]);
        CHECK(static_cast<int>(song.notes.size()) == record.note_count);
        for (int i = 0; i < record.note_count; ++i) {
            const auto& r = bundled::notes[record.first_note + i];
            const Note& n = song.notes[i];
            CHECK(n.time_ms == r.time && n.duration_ms == r.duration && n.ability == r.ability);
            CHECK(static_cast<int>(n.type) == r.type && n.override_auto == (r.override_auto != 0));
        }
        notes += record.note_count;
    }
    CHECK(notes == 22194);
    // "Utility" is not a NoteType; the reference decodes it as Unknown.
    std::string unknown;
    Song tempest = song_from_json_text(R"({"name":"t","notes":[{"time":1.5,"duration":2.5,"noteType":"Utility","abilityId":"7"}],
        "decodedBuildTemplate":{"profession":9,"specializations":[{"id":1},{"id":63}],"skills":{"terrestrial":{"utilities":[4]}}}})", &unknown);
    CHECK(tempest.notes[0].type == NoteType::Unknown && unknown == "'Utility' ");
    CHECK(tempest.notes[0].time_ms == 2 && tempest.notes[0].duration_ms == 2); // half to even
    CHECK(tempest.notes[0].ability == 7 && tempest.profession == Profession::Revenant && tempest.elite == "Renegade");
    CHECK(tempest.utility[0] == 4 && tempest.utility[1] == 0 && tempest.utility[2] == 0);
    const char* failing[] = {
        R"({"notes":[],"decodedBuildTemplate":{"specializations":[{"id":1}]}})",
        R"({"name":"x","decodedBuildTemplate":{"specializations":[{"id":1}]}})",
        R"({"name":"x","notes":[],"decodedBuildTemplate":{"specializations":[]}})",
        R"({"name":"x","notes":[],"decodedBuildTemplate":{}})",
        R"({"name":"x","notes":[{"time":"soon"}],"decodedBuildTemplate":{"specializations":[{"id":1}]}})",
    };
    for (const char* text : failing) {
        bool threw = false;
        try { song_from_json_text(text); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

static void test_layout() {
    Options o;
    Layout l = compute_layout(800, 356, 300, o);
    CHECK(l.lanes == 6 && l.vpad == 15 && l.hpad == 0 && l.spacing == 3);
    CHECK(l.note_w == 51 && l.note_h == 51 && l.perfect == 112);
    CHECK(l.spawn.x == 800 && l.target.x == 86 && l.target.y == 15 && l.destroy == -51);
    CHECK(near(l.time_to_end_ms, 713.0 / 300 * 1000) && l.collision_ms == 170);
    CHECK(l.lane_origin(2).y == 15 + 2 * 54);
    o.next_abilities = 3;
    l = compute_layout(800, 356, 300, o);
    CHECK(l.note_h == 311 / 7 && l.next_h == l.note_h && l.target.y == 15 + l.note_h);
    CHECK(l.next_pos.x == 112 - l.note_w / 2 && l.next_pos.y == 0);
    o = Options(); o.orientation = Orientation::TopToBottom;
    l = compute_layout(400, 800, 600, o);
    CHECK(l.hpad == 10 && l.spacing == 7 && l.note_w == (400 - 20 - 35) / 6);
    CHECK(l.perfect == static_cast<int>(std::min(0.86f * 800.0, 800 - l.note_h * 1.5)) && l.direction() == 1);
    o.orientation = Orientation::AbilityBar;
    l = compute_layout(1000, 400, 300, o);
    CHECK(l.lanes == 10 && l.note_w == (1000 - 20 - 3 * 9) / 12 && l.center == 2 * l.note_w);
    CHECK(l.lane_origin(5).x == 10 + l.center + 5 * (l.note_w + 3));
    o.orientation = Orientation::LeftToRight; o.compact = CompactStyle::Compact;
    l = compute_layout(800, 356, 75, o);
    CHECK(l.lanes == 3 && l.spawn.x == -l.note_w && l.destroy == 800 && l.direction() == 1 && l.speed == 75);
    o.compact = CompactStyle::UltraCompact;
    CHECK(compute_layout(800, 356, 10, o).lanes == 1 && compute_layout(800, 356, 10, o).speed == 75);
}

struct Rig {
    std::vector<Note> notes;
    Engine engine;
    Rig(std::vector<Note> n, Options o = Options(), SongData d = SongData()) : notes(std::move(n)) {
        engine.configure(o, 800, 356);
        engine.set_song(&notes, d);
        engine.reset_and_prepare(0);
    }
    double perfect(int index) { return notes[index].time_ms / engine.data().rate + engine.layout().time_to_end_ms; }
};

static void test_start_with_first_skill() {
    Rig rig({note(NoteType::Weapon2, 0), note(NoteType::Weapon2, 150), note(NoteType::Weapon3, 500)});
    auto& e = rig.engine;
    CHECK(!e.started() && e.active().size() == 3); // Everything due within the travel time is visible
    CHECK(near(e.center_at(e.active()[0], e.song_clock()), e.layout().perfect, 0.2));
    CHECK(!e.press(NoteType::Weapon3, 10).hit);      // Wrong hotkey does not start
    auto r = e.press(NoteType::Weapon2, 20);
    CHECK(r.hit && r.started && r.grade == Hit::Perfect && e.running());
    // Song clock continues from the prepared position, so note 1 reaches Perfect 500 ms later.
    double start_wall = 20 - e.song_clock(20);
    // One press starts the song and clears only the first note; the reference also cleared the
    // Weapon 2 note 150 ms later with the same press.
    CHECK(!e.active()[1].resolved);
    double p2 = start_wall + rig.perfect(2);
    CHECK(near(p2, 20 + 500, 1.0));
    e.advance(p2 - 100);
    CHECK(e.press(NoteType::Weapon3, p2 - 60).grade == Hit::Great);
}

static void test_play_after_one_second() {
    Options o; o.start_with_first_skill = false;
    Rig rig({note(NoteType::Heal, 0)}, o);
    auto& e = rig.engine;
    CHECK(e.active().empty() || e.center_at(e.active()[0], e.song_clock()) > e.layout().perfect);
    CHECK(!e.press(NoteType::Heal, 5).hit && !e.started()); // Cannot start by key in this mode
    e.play(100);
    double start_wall = 100 - e.song_clock(100);
    CHECK(near(start_wall + rig.perfect(0), 1100, 0.6));
    e.advance(1100 - 34);
    CHECK(e.press(NoteType::Heal, 1100 - 34).grade == Hit::Great);
}

static void test_windows_and_miss() {
    Options o; o.start_with_first_skill = false;
    std::vector<Note> notes;
    for (int i = 0; i < 6; ++i) notes.push_back(note(NoteType::Weapon5, i * 1000));
    Rig rig(notes, o);
    auto& e = rig.engine;
    e.play(0);
    double sw = -e.song_clock(0);
    double offsets[] = {-30, 90, -140, 220, -260};
    Hit expected[] = {Hit::Perfect, Hit::Great, Hit::Good, Hit::Boo};
    for (int i = 0; i < 4; ++i) {
        double t = sw + rig.perfect(i) + offsets[i];
        e.advance(t);
        auto r = e.press(NoteType::Weapon5, t);
        CHECK(r.hit && r.grade == expected[i]);
    }
    double early = sw + rig.perfect(4) + offsets[4];
    e.advance(early);
    CHECK(!e.press(NoteType::Weapon5, early).hit); // Too early: ignored, note stays hittable
    e.advance(sw + rig.perfect(4) + 226);
    bool missed = false;
    for (const auto& a : e.active()) if (a.index == 4) missed = a.missed;
    CHECK(missed);
    bool miss_text = false;
    for (const auto& t : e.texts()) miss_text |= t.hit == Hit::Miss;
    CHECK(miss_text);
    // Hit texts only age while playing and disappear after 1.5 s of song time.
    e.advance(sw + rig.perfect(5) - 300);
    e.pause(sw + rig.perfect(5) - 300);
    size_t texts = e.texts().size();
    e.advance(sw + 60000);
    CHECK(e.texts().size() == texts && e.paused());
    // Song end resets to the prepared state.
    e.play(sw + 60000);
    e.advance(sw + 70000);
    e.advance(sw + 80000);
    CHECK(!e.started() && !e.active().empty());
}

static void test_auto_hit_weapon1() {
    Options o; o.start_with_first_skill = false;
    Rig rig({note(NoteType::Weapon1, 0), note(NoteType::Weapon1, 400, 1, true), note(NoteType::Weapon2, 2000)}, o);
    auto& e = rig.engine;
    e.play(0);
    double sw = -e.song_clock(0);
    e.advance(sw + rig.perfect(0) - 10);
    CHECK(!e.press(NoteType::Weapon1, sw + rig.perfect(0) - 10).hit); // Auto notes ignore presses
    CHECK(e.active()[0].mini && !e.active()[0].resolved);
    e.advance(sw + rig.perfect(0) + 1);
    CHECK(e.active()[0].resolved && !e.active()[0].missed && e.texts().empty());
    CHECK(!e.active()[1].mini);
    auto r = e.press(NoteType::Weapon1, sw + rig.perfect(1) + 5);           // overrideAuto needs a press
    CHECK(r.hit && r.grade == Hit::Perfect);
}

static void test_no_miss_mode() {
    SongData d; d.no_miss = true;
    Rig rig({note(NoteType::Elite, 0), note(NoteType::Utility2, 700), note(NoteType::Unknown, 1500), note(NoteType::Weapon4, 2500)}, Options(), d);
    auto& e = rig.engine;
    auto r = e.press(NoteType::Elite, 50);
    CHECK(r.started && r.hit && e.texts().empty());
    double sw = 50 - e.song_clock(50);
    e.advance(sw + rig.perfect(1) + 5000);  // Waits at Perfect instead of missing
    CHECK(e.paused() && near(e.song_clock(), rig.perfect(1)));
    CHECK(!e.press(NoteType::Weapon4, 6000).hit && e.paused());
    r = e.press(NoteType::Utility2, 7000);
    CHECK(r.hit && r.grade == Hit::Perfect && r.resumed && e.running());
    sw = 7000 - e.song_clock(7000);
    e.advance(sw + rig.perfect(2) + 100);
    CHECK(e.paused());
    r = e.press(NoteType::Dodge, 9000);  // Any hotkey clears an Unknown note
    CHECK(r.hit && r.resumed);
    CHECK(e.texts().empty());
}

static void test_rate_start_and_remap() {
    SongData d; d.rate = 0.5f; d.start_at = 2; d.mapping[0] = 1; d.mapping[1] = 0;
    Rig rig({note(NoteType::Weapon2, 0), note(NoteType::Weapon3, 1000), note(NoteType::Utility1, 2500), note(NoteType::Utility2, 3000)}, Options(), d);
    auto& e = rig.engine;
    CHECK(e.active().size() == 2 && e.active()[0].index == 2);   // Notes before 2 s are skipped
    CHECK(e.active()[0].type == NoteType::Utility2);             // Utility1 remapped to slot 2
    CHECK(e.active()[1].type == NoteType::Utility1);
    CHECK(near(e.center_at(e.active()[0], e.song_clock()), e.layout().perfect, 0.2));
    CHECK(!e.press(NoteType::Utility1, 0).hit);
    auto r = e.press(NoteType::Utility2, 0);
    CHECK(r.started && r.grade == Hit::Perfect);
    // At half speed, 500 ms of rotation time takes 1000 ms.
    double sw = -e.song_clock(0);
    CHECK(near(sw + rig.perfect(3), 1000, 0.6));
}

static void test_compact_lanes() {
    Options o; o.compact = CompactStyle::Compact;
    std::vector<Note> notes = {note(NoteType::Weapon2, 0), note(NoteType::Weapon1, 50), note(NoteType::Weapon3, 100),
                               note(NoteType::Weapon4, 200), note(NoteType::Weapon5, 300), note(NoteType::Heal, 2000)};
    Engine e;
    e.configure(o, 800, 356);
    e.set_song(&notes, SongData());
    // Three lanes: (356 - 30 - 6) / 3 = 106 px notes, 106 px at 300 px/s -> 353 ms.
    CHECK(e.layout().note_w == 106 && e.layout().collision_ms == 353);
    CHECK(e.lane_for_index(0) == 0 && e.lane_for_index(1) == 0);   // Auto Weapon1 always lane 0
    CHECK(e.lane_for_index(2) == 1 && e.lane_for_index(3) == 2);   // Weapon1 is skipped when checking overlap
    CHECK(e.lane_for_index(4) == 0 && e.lane_for_index(5) == 0);   // Wraps after the third lane
    o.compact = CompactStyle::UltraCompact;
    e.configure(o, 800, 356);
    CHECK(e.lane_for_index(3) == 0);
    o.compact = CompactStyle::Regular;
    e.configure(o, 800, 356);
    CHECK(e.lane_for_index(5) == 0 && e.lane_for_index(4) == 4);
}

static void test_next_abilities_and_pause() {
    Options o; o.next_abilities = 2; o.start_with_first_skill = false;
    std::vector<Note> notes;
    for (int i = 0; i < 5; ++i) notes.push_back(note(NoteType::Weapon2, i * 500));
    Rig rig(notes, o);
    auto& e = rig.engine;
    CHECK((e.next_ability_indexes() == std::vector<int>{0, 1}));
    e.play(0);
    double sw = -e.song_clock(0);
    e.advance(sw + rig.perfect(0) + 51);
    CHECK((e.next_ability_indexes() == std::vector<int>{1, 2}));
    // A long pause does not change the grade of the next press.
    e.pause(sw + rig.perfect(1) - 100);
    e.advance(sw + 100000);
    e.play(sw + 100000);
    double sw2 = sw + 100000 - (rig.perfect(1) - 100);
    e.advance(sw2 + rig.perfect(1) - 20);
    CHECK(e.press(NoteType::Weapon2, sw2 + rig.perfect(1) - 20).grade == Hit::Perfect);
}

static void test_rotate_mapping() {
    SongData d;
    int expected[6][3] = {{0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}, {0, 1, 2}};
    for (auto& row : expected) {
        d.rotate_mapping();
        CHECK(d.mapping[0] == row[0] && d.mapping[1] == row[1] && d.mapping[2] == row[2]);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: engine_test defaultSongs.json\n"); return 2; }
    test_json();
    test_default_songs(argv[1]);
    test_layout();
    test_start_with_first_skill();
    test_play_after_one_second();
    test_windows_and_miss();
    test_auto_hit_weapon1();
    test_no_miss_mode();
    test_rate_start_and_remap();
    test_compact_lanes();
    test_next_abilities_and_pause();
    test_rotate_mapping();
    std::printf("engine tests passed: %d checks\n", checks);
}
