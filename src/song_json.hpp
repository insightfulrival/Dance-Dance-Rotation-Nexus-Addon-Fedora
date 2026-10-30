// Song JSON decoding, ported from Storage/SongTranslator.cs (Blish HUD module 1.8.1).
#pragma once
#include "json.hpp"
#include "model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ddr {

namespace detail {
inline const json::Value& require(const json::Value& object, const char* key) {
    const json::Value* value = object.find(key);
    if (!value || value->kind == json::Value::Null) throw std::runtime_error(std::string("Missing ") + key);
    return *value;
}
inline std::string optional_string(const json::Value& object, const char* key) {
    const json::Value* value = object.find(key);
    return value && value->kind == json::Value::String ? value->text : std::string();
}
// Newtonsoft accepts JSON numbers and numeric strings for numeric properties.
inline double number(const json::Value* value, double fallback = 0) {
    if (!value || value->kind == json::Value::Null) return fallback;
    double result = 0;
    if (value->kind == json::Value::Number) result = value->number;
    else if (value->kind == json::Value::String) {
        const char* begin = value->text.c_str();
        while (*begin == ' ') ++begin;
        char* end = nullptr;
        result = std::strtod(begin, &end);
        while (end && *end == ' ') ++end;
        if (end == begin || !end || *end) throw std::runtime_error("Expected a number");
    } else throw std::runtime_error("Expected a number");
    if (!std::isfinite(result)) throw std::runtime_error("Expected a finite number");
    return result;
}
inline int integer(const json::Value* value) {
    double v = number(value);
    if (v < -2147483648.0 || v > 2147483647.0 || v != std::floor(v)) throw std::runtime_error("Expected an integer");
    return static_cast<int>(v);
}
// C# Math.Round(double) rounds half to even.
inline std::int32_t round_ms(double value) {
    if (value < -2147483648.0 || value > 2147483647.0) throw std::runtime_error("Time out of range");
    return static_cast<std::int32_t>(std::nearbyint(value));
}
} // namespace detail

// Returns the decoded song; throws std::exception when the original module would fail too.
inline Song song_from_json(const json::Value& root, std::string* unknown_types = nullptr) {
    using namespace detail;
    if (root.kind != json::Value::Object) throw std::runtime_error("Song must be a JSON object");
    Song song;
    const json::Value& name = require(root, "name");
    if (name.kind != json::Value::String || name.text.empty()) throw std::runtime_error("Song name is missing");
    song.name = name.text;
    song.description = optional_string(root, "description");
    song.build_url = optional_string(root, "buildUrl");
    song.build_code = optional_string(root, "buildChatCode");

    const json::Value& build = require(root, "decodedBuildTemplate");
    song.profession = profession_from_build_template(integer(build.find("profession")));
    const json::Value& specializations = require(build, "specializations");
    if (specializations.kind != json::Value::Array || specializations.items.empty())
        throw std::runtime_error("Build template has no specializations");
    song.elite = elite_name(song.profession, integer(specializations.items.back().find("id")));
    if (const json::Value* skills = build.find("skills"))
        if (const json::Value* terrestrial = skills->find("terrestrial"))
            if (const json::Value* utilities = terrestrial->find("utilities"); utilities && utilities->kind == json::Value::Array)
                for (std::size_t i = 0; i < 3 && i < utilities->items.size(); ++i)
                    song.utility[i] = integer(&utilities->items[i]);

    const json::Value& notes = require(root, "notes");
    if (notes.kind != json::Value::Array) throw std::runtime_error("Song notes must be an array");
    song.notes.reserve(notes.items.size());
    for (const auto& item : notes.items) {
        if (item.kind != json::Value::Object) throw std::runtime_error("Note must be an object");
        Note note;
        const json::Value* type = item.find("noteType");
        std::string type_name = type && type->kind == json::Value::String ? type->text : std::string();
        if (!parse_note_type(type_name, note.type)) {
            note.type = NoteType::Unknown;
            if (unknown_types) *unknown_types += "'" + type_name + "' ";
        }
        note.time_ms = round_ms(number(item.find("time")));
        note.duration_ms = round_ms(number(item.find("duration")));
        note.ability = integer(item.find("abilityId"));
        const json::Value* override_auto = item.find("overrideAuto");
        note.override_auto = override_auto && override_auto->kind == json::Value::Bool && override_auto->boolean;
        song.notes.push_back(note);
    }
    // Songs are played in time order; the composer and bundled songs are already sorted.
    std::stable_sort(song.notes.begin(), song.notes.end(),
                     [](const Note& a, const Note& b) { return a.time_ms < b.time_ms; });
    return song;
}

inline Song song_from_json_text(const std::string& text, std::string* unknown_types = nullptr) {
    return song_from_json(json::parse(text), unknown_types);
}

} // namespace ddr
