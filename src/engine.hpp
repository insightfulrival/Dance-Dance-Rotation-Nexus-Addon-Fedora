// Note highway engine, ported from Views/NotesContainer.cs (Blish HUD module 1.8.1).
//
// Layout arithmetic follows WindowInfo.Recalculate. Note movement is evaluated from the song
// clock instead of per-frame integer-millisecond steps, so note positions, hit windows, misses
// and No Miss Mode pauses are exact and independent of frame rate. Platform independent.
#pragma once
#include "model.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ddr {

constexpr float perfect_fraction = 0.14f;
constexpr int lane_line_thickness = 2;
constexpr double hit_text_speed = 10.0;      // px per second, upwards
constexpr double hit_text_life_ms = 1500.0;
constexpr double fade_in_ms = 700.0;
constexpr double hit_animation_ms = 200.0;
constexpr double miss_fade_ms = 400.0;
constexpr double label_fade_out_ms = 100.0;
constexpr double hit_window_ms[4] = {33, 92, 142, 225}; // Perfect, Great, Good, Boo
constexpr double icon_dispose_extra_ms = 50.0;

enum class Hit { Perfect, Great, Good, Boo, Miss };
inline const char* hit_name(Hit hit) {
    static const char* names[] = {"Perfect", "Great", "Good", "Boo", "Miss"};
    return names[static_cast<int>(hit)];
}

struct Options {
    Orientation orientation = Orientation::RightToLeft;
    CompactStyle compact = CompactStyle::Regular;
    bool auto_hit_weapon1 = true;
    bool start_with_first_skill = true;
    int next_abilities = 0;
    bool operator==(const Options& o) const {
        return orientation == o.orientation && compact == o.compact && auto_hit_weapon1 == o.auto_hit_weapon1 &&
               start_with_first_skill == o.start_with_first_skill && next_abilities == o.next_abilities;
    }
    bool operator!=(const Options& o) const { return !(*this == o); }
};

struct IPoint { int x = 0, y = 0; };

// TimeSpan.FromMilliseconds in .NET Framework rounds half away from zero to whole milliseconds.
inline double timespan_ms(double ms) { return ms >= 0 ? std::floor(ms + 0.5) : std::ceil(ms - 0.5); }

struct Layout {
    Orientation orientation = Orientation::RightToLeft;
    int lanes = 6, note_w = 0, note_h = 0, spacing = 0, vpad = 0, hpad = 0, center = 0;
    IPoint spawn;            // NewNotePosition
    int next_w = 0, next_h = 0;
    IPoint next_pos;         // NextAbilityIconsLocation
    IPoint target;           // TargetLocation
    int perfect = 0, destroy = 0;
    double speed = default_pace; // px per second
    double time_to_end_ms = 0;
    double collision_ms = 0;
    int width = 0, height = 0;

    bool vertical() const { return is_vertical(orientation); }
    // +1 when notes move towards larger coordinates.
    int direction() const {
        return orientation == Orientation::RightToLeft || orientation == Orientation::BottomToTop ? -1 : 1;
    }
    // GetNewNoteLocation: top-left of a full-size note when it spawns.
    IPoint lane_origin(int lane) const {
        switch (orientation) {
            case Orientation::RightToLeft: case Orientation::LeftToRight:
                return {spawn.x, vpad + next_h + lane * (note_h + spacing)};
            case Orientation::TopToBottom: case Orientation::BottomToTop:
                return {hpad + next_w + lane * (note_w + spacing), spawn.y};
            case Orientation::AbilityBar:
                return {lane >= lanes / 2 ? hpad + center + lane * (note_w + spacing) : hpad + lane * (note_w + spacing), spawn.y};
        }
        return {};
    }
    // Axis coordinate of a note center when it spawns.
    double spawn_center() const { return vertical() ? spawn.y + note_h / 2 : spawn.x + note_w / 2; }
};

inline Layout compute_layout(int width, int height, int pace, const Options& options) {
    Layout l;
    l.orientation = options.orientation;
    l.width = width; l.height = height;
    if (l.orientation == Orientation::AbilityBar) l.lanes = 10;
    else l.lanes = options.compact == CompactStyle::Regular ? 6 : options.compact == CompactStyle::Compact ? 3 : 1;
    const int L = l.lanes;
    l.vpad = static_cast<int>(hit_text_speed * (hit_text_life_ms / 1000.0));
    if (l.vertical()) { l.hpad = 10; l.spacing = (height - l.hpad * 2) / 100; }
    else { l.hpad = 0; l.spacing = (height - l.vpad * 2) / 100; }

    const int n = options.next_abilities;
    // The reference reads the previous NoteWidth here; repeated layout passes converge to the
    // final note size, which is used directly.
    if (l.orientation == Orientation::AbilityBar) {
        l.note_w = (width - 2 * l.hpad - l.spacing * (L - 1)) / (L + 2);
        l.note_h = l.note_w;
        l.center = l.note_w * 2;
    } else if (l.vertical()) {
        l.note_w = (width - 2 * l.hpad - l.spacing * (L - 1)) / (L + (n > 0 ? 1 : 0));
        l.note_h = l.note_w;
    } else {
        l.note_h = (height - 2 * l.vpad - l.spacing * (L - 1)) / (L + (n > 0 ? 1 : 0));
        l.note_w = l.note_h;
    }
    if (n > 0) {
        switch (l.orientation) {
            case Orientation::RightToLeft: case Orientation::LeftToRight:
                l.next_w = l.next_h = l.note_h; break;
            case Orientation::TopToBottom: case Orientation::BottomToTop:
                l.next_w = l.next_h = l.note_w; break;
            case Orientation::AbilityBar:
                l.next_w = l.next_h = (height - 2 * l.vpad - l.spacing * (L - 1)) / L; break;
        }
        switch (l.orientation) {
            case Orientation::RightToLeft:
                l.next_pos = {static_cast<int>(width * perfect_fraction) - l.note_w / 2, 0}; break;
            case Orientation::LeftToRight:
                l.next_pos = {static_cast<int>(width * (1 - perfect_fraction)) - l.note_w / 2 - (n - 1) * l.note_w, 0}; break;
            case Orientation::TopToBottom:
                l.next_pos = {0, static_cast<int>(height * (1 - perfect_fraction)) - l.note_h / 2 - (n - 1) * l.note_h}; break;
            case Orientation::BottomToTop:
                l.next_pos = {0, static_cast<int>(height * perfect_fraction) - l.note_h / 2}; break;
            case Orientation::AbilityBar:
                l.next_pos = {l.hpad, 0}; break;
        }
    }

    l.speed = std::max(pace, min_pace);
    int perfect = 0;
    switch (l.orientation) {
        case Orientation::RightToLeft:
            l.spawn = {width, 0};
            perfect = static_cast<int>(std::max(static_cast<double>(perfect_fraction * width), l.note_w * 1.5));
            l.time_to_end_ms = std::abs(l.spawn.x + l.note_w / 2 - perfect) / l.speed * 1000;
            l.target = {static_cast<int>(perfect - l.note_w / 2.0), l.vpad + l.next_h};
            l.destroy = -l.note_w;
            break;
        case Orientation::LeftToRight:
            l.spawn = {-l.note_w, 0};
            perfect = static_cast<int>(std::min(static_cast<double>((1 - perfect_fraction) * width), width - l.note_w * 1.5));
            l.time_to_end_ms = std::abs(l.spawn.x + l.note_w / 2 - perfect) / l.speed * 1000;
            l.target = {static_cast<int>(perfect - l.note_w / 2.0), l.vpad + l.next_h};
            l.destroy = width;
            break;
        case Orientation::TopToBottom: case Orientation::AbilityBar:
            l.spawn = {0, -l.note_h};
            perfect = static_cast<int>(std::min(static_cast<double>((1 - perfect_fraction) * height), height - l.note_h * 1.5));
            l.time_to_end_ms = std::abs(l.spawn.y + l.note_h / 2 - perfect) / l.speed * 1000;
            l.target = {l.orientation == Orientation::AbilityBar ? l.hpad : l.hpad + l.next_w,
                        static_cast<int>(perfect - l.note_h / 2.0)};
            l.destroy = height;
            break;
        case Orientation::BottomToTop:
            l.spawn = {0, height};
            perfect = static_cast<int>(std::max(static_cast<double>(perfect_fraction * height), l.note_h * 1.5));
            l.time_to_end_ms = std::abs(l.spawn.y + l.note_h / 2 - perfect) / l.speed * 1000;
            l.target = {l.hpad + l.next_w, static_cast<int>(perfect - l.note_h / 2.0)};
            l.destroy = -l.note_h;
            break;
    }
    l.perfect = perfect;
    l.collision_ms = timespan_ms((l.vertical() ? l.note_h : l.note_w) / l.speed * 1000);
    return l;
}

struct ActiveNote {
    int index = 0;              // Position in the song
    NoteType type = NoteType::Unknown; // After utility remapping
    int lane = 0;
    bool mini = false;          // Auto-hit Weapon 1
    double spawn_e = 0;         // Song clock (ms) when it spawns
    double created_wall = 0;    // Wall clock (ms) for the fade-in
    bool resolved = false;      // Hit, auto-hit or missed
    bool missed = false;
    double stop_e = 0;          // Hit notes stop moving at this song clock
    double resolved_wall = 0;   // Wall clock when hit/missed, for animations
};

struct HitText {
    Hit hit = Hit::Perfect;
    float x = 0, y = 0;         // Center in notes-area coordinates
    double e0 = 0;              // Song clock when shown (it only ages while playing)
};

class Engine {
public:
    // ---- configuration ----------------------------------------------------------------------
    void configure(const Options& options, int width, int height) {
        options_ = options;
        width_ = std::max(width, 1);
        height_ = std::max(height, 1);
        layout_ = compute_layout(width_, height_, data_.pace, options_);
        compute_lanes();
    }
    void set_song(const std::vector<Note>* notes, const SongData& data) {
        notes_ = notes;
        data_ = data;
        if (!(data_.rate > 0)) data_.rate = 1.0f;
        layout_ = compute_layout(width_, height_, data_.pace, options_);
        compute_lanes();
    }
    const Layout& layout() const { return layout_; }
    const Options& options() const { return options_; }
    const SongData& data() const { return data_; }
    bool has_song() const { return notes_ && !notes_->empty(); }
    const std::vector<Note>& notes() const { static const std::vector<Note> none; return notes_ ? *notes_ : none; }

    // ---- lifecycle (NotesContainer.Reset / AddInitialNotes / Play / Pause / Stop) -------------
    // Returns true if the started state changed (OnStartStop).
    bool reset_and_prepare(double now) {
        bool was_started = started_;
        started_ = false; paused_ = false;
        seq_index_ = first_index_ = 0;
        active_.clear(); texts_.clear();
        last_wall_ = now;
        start_wall_ = paused_wall_ = now;
        if (!has_song()) return was_started;
        const auto& seq = *notes_;
        double initial_start = data_.start_at * 1000.0;
        if (data_.start_at > 0) {
            while (seq_index_ < static_cast<int>(seq.size()) && seq[seq_index_].time_ms < initial_start) ++seq_index_;
            first_index_ = seq_index_;
            if (shift_first_note() && seq_index_ < static_cast<int>(seq.size())) initial_start = seq[seq_index_].time_ms;
        }
        double tte = layout_.time_to_end_ms, total_move;
        if (shift_first_note()) total_move = timespan_ms(tte);
        else if (tte >= 1.0) total_move = timespan_ms(tte - 1000); // first note reaches Perfect 1 s after Play
        else total_move = timespan_ms(tte / 2);
        double e0 = initial_start / data_.rate + total_move;
        start_wall_ = now - e0;
        paused_ = true;
        paused_wall_ = now;
        spawn_until(e0, now);
        return was_started;
    }
    // Returns true if the started state changed.
    bool play(double now) {
        bool changed = false;
        if (!has_song()) return false; // The reference stops again on the next update.
        if (paused_) {
            paused_ = false;
            start_wall_ += now - paused_wall_;
        }
        if (!started_) { started_ = true; changed = true; }
        last_wall_ = now;
        return changed;
    }
    void pause(double now) {
        if (started_ && !paused_) {
            advance(now);
            if (started_ && !paused_) { paused_ = true; paused_wall_ = now; }
        }
    }
    bool stop(double now) { return reset_and_prepare(now); }
    bool started() const { return started_; }
    bool paused() const { return started_ && paused_; }   // NotesContainer.IsPaused
    bool running() const { return started_ && !paused_; }
    double song_clock(double now) const { return (started_ && !paused_ ? now : paused_wall_) - start_wall_; }
    double song_clock() const { return paused_ ? paused_wall_ - start_wall_ : last_wall_ - start_wall_; }

    // ---- per frame (NotesContainer.UpdateNotes) ------------------------------------------------
    // Returns true if the song ended and was reset (started state changed).
    bool advance(double now) {
        if (now < last_wall_) now = last_wall_;
        last_wall_ = now;
        if (!started_ || paused_ || !has_song()) return false;
        double e = now - start_wall_;
        // No Miss Mode: stop exactly when the first hittable note reaches Perfect.
        if (data_.no_miss) {
            double pause_at = next_no_miss_pause();
            if (pause_at <= e) {
                e = pause_at;
                spawn_until(e, now);
                resolve_until(e, now);
                paused_ = true;
                paused_wall_ = start_wall_ + e;
                return false;
            }
        }
        spawn_until(e, now);
        resolve_until(e, now);
        // Remove notes that left the highway, or whose hit animation finished.
        active_.erase(std::remove_if(active_.begin(), active_.end(), [&](const ActiveNote& a) {
            if (a.resolved && !a.missed) return now - a.resolved_wall >= hit_animation_ms;
            return e >= destroy_e(a);
        }), active_.end());
        texts_.erase(std::remove_if(texts_.begin(), texts_.end(), [&](const HitText& t) {
            return e - t.e0 > hit_text_life_ms;
        }), texts_.end());
        if (seq_index_ >= static_cast<int>(notes_->size()) && active_.empty()) {
            reset_and_prepare(now);
            return true;
        }
        return false;
    }

    // ---- input (NotesContainer.OnHotkeyPressed) -------------------------------------------------
    struct PressResult { bool hit = false; Hit grade = Hit::Miss; bool started = false; bool resumed = false; };
    PressResult press(NoteType type, double now) {
        PressResult result;
        if (!has_song()) return result;
        advance(now);
        double e = song_clock(now);
        if (!started_) {
            // The first note waits at Perfect: pressing its hotkey starts the song.
            if (shift_first_note() && !active_.empty() &&
                (active_[0].type == type || active_[0].type == NoteType::Unknown) &&
                try_hit(active_[0], e, now, result)) {
                play(now);
                result.started = true;
            }
            return result;
        }
        if (data_.no_miss) {
            // Unknown notes have no hotkey; any hotkey clears them so No Miss Mode cannot get stuck.
            ActiveNote* first = first_unresolved();
            if (first && first->type == NoteType::Unknown && try_hit(*first, e, now, result)) {
                if (paused_) { play(now); result.resumed = true; }
                return result;
            }
        }
        for (auto& note : active_) {
            if (note.type != type) continue;
            if (try_hit(note, e, now, result)) {
                if (data_.no_miss && paused_) { play(now); result.resumed = true; }
                break;
            }
        }
        return result;
    }

    // ---- queries for rendering -----------------------------------------------------------------
    const std::vector<ActiveNote>& active() const { return active_; }
    const std::vector<HitText>& texts() const { return texts_; }
    double perfect_e(const ActiveNote& a) const { return a.spawn_e + layout_.time_to_end_ms; }
    double destroy_e(const ActiveNote& a) const {
        return a.spawn_e + std::abs(layout_.destroy - layout_.spawn_center()) / layout_.speed * 1000;
    }
    // Axis coordinate of the note center at song clock e.
    double center_at(const ActiveNote& a, double e) const {
        if (a.resolved && !a.missed) e = std::min(e, a.stop_e);
        return layout_.spawn_center() + layout_.direction() * layout_.speed * (e - a.spawn_e) / 1000.0;
    }
    // Rectangle (top-left, size) of a note image at song clock e, before hit-animation scaling.
    void note_rect(const ActiveNote& a, double e, float& x, float& y, float& w, float& h) const {
        IPoint origin = layout_.lane_origin(a.lane);
        w = static_cast<float>(a.mini ? layout_.note_w * 3 / 4 : layout_.note_w);
        h = static_cast<float>(a.mini ? layout_.note_h * 3 / 4 : layout_.note_h);
        double c = center_at(a, e);
        if (layout_.vertical()) {
            x = origin.x + (layout_.note_w - w) / 2.0f;
            y = static_cast<float>(c - h / 2.0);
        } else {
            x = static_cast<float>(c - w / 2.0);
            y = origin.y + (layout_.note_h - h) / 2.0f;
        }
    }
    // Next abilities strip: indexes of the upcoming notes shown, in sequence order.
    std::vector<int> next_ability_indexes() const {
        std::vector<int> result;
        if (options_.next_abilities <= 0 || !has_song()) return result;
        double rotation = song_clock() * data_.rate;
        const auto& seq = *notes_;
        for (int i = first_index_; i < static_cast<int>(seq.size()) && static_cast<int>(result.size()) < options_.next_abilities; ++i)
            if (!(rotation > seq[i].time_ms + layout_.time_to_end_ms * data_.rate + icon_dispose_extra_ms))
                result.push_back(i);
        return result;
    }
    int lane_for_index(int index) const { return lanes_.empty() ? 0 : lanes_[index]; }

private:
    Options options_;
    SongData data_;
    Layout layout_;
    const std::vector<Note>* notes_ = nullptr;
    std::vector<int> lanes_;
    int width_ = 800, height_ = 356;
    bool started_ = false, paused_ = false;
    double start_wall_ = 0, paused_wall_ = 0, last_wall_ = 0;
    int seq_index_ = 0, first_index_ = 0;
    std::vector<ActiveNote> active_;
    std::vector<HitText> texts_;

    bool shift_first_note() const { return data_.no_miss || options_.start_with_first_skill; }
    bool is_auto(const ActiveNote& a) const {
        return options_.auto_hit_weapon1 && a.type == NoteType::Weapon1 && !(*notes_)[a.index].override_auto;
    }
    ActiveNote* first_unresolved() {
        for (auto& a : active_) if (!a.resolved) return &a;
        return nullptr;
    }

    void compute_lanes() {
        lanes_.clear();
        if (!notes_) return;
        const auto& seq = *notes_;
        lanes_.resize(seq.size(), 0);
        for (std::size_t i = 0; i < seq.size(); ++i) {
            NoteType type = data_.remap(seq[i].type);
            if (options_.orientation == Orientation::AbilityBar || options_.compact == CompactStyle::Regular) {
                lanes_[i] = note_lane(options_.orientation, type);
            } else if (options_.compact == CompactStyle::UltraCompact) {
                lanes_[i] = 0;
            } else {
                // GetCompactModeLane: shift down only when overlapping the previous counted note.
                int lane = 0;
                if (!options_.auto_hit_weapon1 || seq[i].type != NoteType::Weapon1) {
                    for (std::size_t j = i; j-- > 0;) {
                        if (options_.auto_hit_weapon1 && seq[j].type == NoteType::Weapon1) continue;
                        if (seq[j].time_ms + layout_.collision_ms > seq[i].time_ms)
                            lane = (lanes_[j] + 1) % layout_.lanes;
                        break;
                    }
                }
                lanes_[i] = lane;
            }
        }
    }

    void spawn_until(double e, double now) {
        const auto& seq = *notes_;
        while (seq_index_ < static_cast<int>(seq.size()) && seq[seq_index_].time_ms < e * data_.rate) {
            const Note& note = seq[seq_index_];
            ActiveNote a;
            a.index = seq_index_;
            a.type = data_.remap(note.type);
            a.lane = lanes_[seq_index_];
            a.mini = options_.auto_hit_weapon1 && a.type == NoteType::Weapon1 && !note.override_auto;
            a.spawn_e = note.time_ms / static_cast<double>(data_.rate);
            a.created_wall = now;
            active_.push_back(a);
            ++seq_index_;
        }
    }

    double next_no_miss_pause() const {
        double best = 1e300;
        for (const auto& a : active_)
            if (!a.resolved && !is_auto(a)) best = std::min(best, perfect_e(a));
        if (best == 1e300 && notes_) {
            for (int i = seq_index_; i < static_cast<int>(notes_->size()); ++i) {
                const Note& n = (*notes_)[i];
                bool auto_note = options_.auto_hit_weapon1 && data_.remap(n.type) == NoteType::Weapon1 && !n.override_auto;
                if (!auto_note) { best = n.time_ms / static_cast<double>(data_.rate) + layout_.time_to_end_ms; break; }
            }
        }
        return best;
    }

    void resolve_until(double e, double now) {
        for (auto& a : active_) {
            if (a.resolved) continue;
            double perfect = perfect_e(a);
            if (started_ && is_auto(a) && e >= perfect) {
                // Auto Hit Weapon 1: cleared at Perfect without hit text.
                a.resolved = true; a.stop_e = perfect; a.resolved_wall = now;
            } else if (e > perfect + hit_window_ms[3]) {
                a.resolved = true; a.missed = true; a.resolved_wall = now;
                add_text(a, perfect + hit_window_ms[3], Hit::Miss);
            }
        }
    }

    bool try_hit(ActiveNote& a, double e, double now, PressResult& result) {
        if (a.resolved) return false;
        if (started_ && is_auto(a)) return false;
        double dt = std::abs(e - perfect_e(a));
        if (dt > hit_window_ms[3]) return false;
        Hit grade = dt <= hit_window_ms[0] ? Hit::Perfect : dt <= hit_window_ms[1] ? Hit::Great
                  : dt <= hit_window_ms[2] ? Hit::Good : Hit::Boo;
        a.resolved = true; a.stop_e = e; a.resolved_wall = now;
        add_text(a, e, grade);
        result.hit = true; result.grade = grade;
        return true;
    }

    void add_text(const ActiveNote& a, double e, Hit hit) {
        if (data_.no_miss) return; // No Miss Mode has no timings to report.
        float x, y, w, h;
        note_rect(a, e, x, y, w, h);
        HitText text;
        text.hit = hit; text.x = x + w / 2; text.y = y + h / 2;
        text.e0 = e;
        texts_.push_back(text);
    }
};

} // namespace ddr
