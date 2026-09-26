// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion of HeadTracking.ini to the canonical
// config format.
//
// Three readings of every input:
//
//   Oracle     v0.1.0's reader and startup code, the newest published build
//              (oracle/oracle_reader.cpp; the repo publishes v* releases only)
//   Import     the frozen reader in src/legacy_config/, and the startup code of
//              the commit that froze it
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it through config::Import into a
//              new CameraUnlock.ini, then this build's startup code on what the
//              session runs on
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.1.0 that change how the file is
// read. There are none. src/ was byte for byte v0.1.0's when the reader was
// frozen, the oracle compiles v0.1.0's reader from byte copies, and every core
// source either reader compiles holds the same bytes at v0.1.0's pin and at this
// repo's (CMakeLists.txt pins them), so the comparison may find no difference at
// all, floats bit for bit.
//
// Comparison 2, import against migration, is the proof for the migration: no
// difference at all. v0.1.0 had no sensitivity, inversion, reticle or other
// setting an approved change drops, the reader keeps every value inside what
// the canonical rows hold, and no default moved, so the import drops nothing
// and nothing may differ, the no-file input included.
//
// Each input migrates three times: over a Defaults.ini the owner creates with
// the built-in values, from a read-only HeadTracking.ini, and over a
// Defaults.ini that differs from the built-in value on every global row the
// table binds. All three give the settings the import read, since the migration
// writes `default` only where the imported value is what `default` gives at that
// launch. After every load HeadTracking.ini keeps its bytes, write time and
// attributes, and the folder holds it and CameraUnlock.ini and nothing else. The
// distinct migrated files are written beside the executable under migrated\,
// for lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: v0.1.0 is the one published build. Its release ZIP and launcher
// manifest shipped no HeadTracking.ini; its first start wrote one
// (WriteDefaultConfigIfMissing), so a player's file is that first-run output,
// edited or not. data/firstrun-v0.1.0.ini holds it, extracted from
// kDefaultIniText at the tag. The HeadTracking.ini the repo kept as the
// documented file has had one version (e7976fd, unchanged through v0.1.0), and
// it is byte for byte the same file, so it is not a second input. Then no file,
// an empty file, core's mutation corpus over the first-run output, and that
// file with ToggleKey and with ChordToggleKey set to every code from 0x01 to
// 0xFE.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace config = wf2_ht::config;
namespace legacy = wf2_ht::legacy;
namespace testing = cameraunlock::config::testing;
using wf2_ht::Config;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- Scratch folders -----------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which both readers sit on,
// is free to cache the file it last read. `game` stands for the folder
// Wreckfest2.exe is in. Every folder lives under one root for the run,
// removed at the end.

void RemoveTree(const fs::path& root) {
    if (!fs::exists(root)) return;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(root);
}

const fs::path& ScratchRoot() {
    static const fs::path root = [] {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        fs::path r = fs::path(temp) / ("wf2_ht_diff_" + std::to_string(GetCurrentProcessId()));
        RemoveTree(r);
        return r;
    }();
    return root;
}

// Each reading's folder goes as soon as the reading is done, so a run holds a
// handful of folders at a time rather than every input's.
class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        root_ = ScratchRoot() / std::to_string(s_next++);
        fs::create_directories(root_ / "game");
    }
    ~Scratch() { RemoveTree(root_); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    fs::path game() const { return root_ / "game"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path canonical() const { return game() / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }

    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        WriteFileBytes(defaults(), bytes);
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(game())) names.insert(entry.path().filename().string());
        return names;
    }

    cfg::ConfigOwnerOptions<Config> Options() const {
        return config::OwnerOptions(game(), cfg::DefaultsFile::At(defaults().wstring()));
    }

private:
    fs::path root_;
};

// ---- What a reading does -----------------------------------------------------------
//
// A Record names everything the running mod acts on after reading the file:
// `field.*` what the pipeline was handed, `start.*` the state the session starts
// in, `hotkey.*` the bindings that can fire, each as `modifiers:code` (Ctrl 1,
// Shift 2, as cameraunlock::input::KeyModifiers numbers them) in ascending
// order. Floats are their bits.
//
// v0.1.0 registered each nav key NavGuarded, which does not fire while Ctrl and
// Shift are both held, and each chord key ChordGuarded, which fires only while
// both are. A canonical binding with no modifiers and one naming Ctrl+Shift fire
// on exactly those conditions, so a record names a binding by its key and
// modifiers alone.

using Record = std::map<std::string, std::string>;

std::string Bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(bits));
    return text;
}

std::string Flag(bool value) { return value ? "1" : "0"; }

const char* const kActionNames[] = {"Toggle", "CycleTrackingMode"};
const char* const kModeNames[] = {"RotationAndPosition", "RotationOnly", "PositionOnly"};

void AddHotkeys(Record& r, const std::vector<wf2_oracle::Registration>& registrations) {
    std::map<int, std::vector<std::pair<unsigned, int>>> byAction;
    for (int action = 0; action < 2; ++action) byAction[action];
    for (const auto& [action, vk, modifiers] : registrations) byAction[action].push_back({modifiers, vk});
    for (auto& [action, items] : byAction) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        std::string text;
        for (const auto& [modifiers, vk] : items) {
            char item[32];
            std::snprintf(item, sizeof(item), "%s%u:0x%02X", text.empty() ? "" : " ", modifiers, static_cast<unsigned>(vk));
            text += item;
        }
        r[std::string("hotkey.") + kActionNames[action]] = text;
    }
}

// No build sets the rotation processor's sensitivity, so the rotation side has
// nothing to record.
void AddPipeline(Record& r, const cameraunlock::PositionSettings& p, float local_smoothing, float remote_smoothing) {
    r["field.pos.sensitivity_x"] = Bits(p.sensitivity_x);
    r["field.pos.sensitivity_y"] = Bits(p.sensitivity_y);
    r["field.pos.sensitivity_z"] = Bits(p.sensitivity_z);
    r["field.pos.invert_x"] = Flag(p.invert_x);
    r["field.pos.invert_y"] = Flag(p.invert_y);
    r["field.pos.invert_z"] = Flag(p.invert_z);
    r["field.pos.limit_x"] = Bits(p.limit_x);
    r["field.pos.limit_y"] = Bits(p.limit_y);
    r["field.pos.limit_y_down"] = Bits(p.limit_y_down);
    r["field.pos.limit_z"] = Bits(p.limit_z);
    r["field.pos.limit_z_back"] = Bits(p.limit_z_back);
    r["field.pos.local_smoothing"] = Bits(p.local_smoothing);
    r["field.pos.remote_smoothing"] = Bits(p.remote_smoothing);
    r["field.local_smoothing"] = Bits(local_smoothing);
    r["field.remote_smoothing"] = Bits(remote_smoothing);
}

Record ObserveOracle(const wf2_oracle::Published& p) {
    Record r;
    r["field.udp_port"] = std::to_string(p.udp_port);
    AddPipeline(r, p.position, p.local_smoothing, p.remote_smoothing);
    r["start.enabled"] = Flag(p.tracking_enabled);
    r["start.mode"] = kModeNames[p.mode];
    AddHotkeys(r, p.hotkeys);
    return r;
}

// The frozen reader's settings through the startup code of the commit that
// froze it: src/config.cpp's LoadConfig copies every field into the runtime
// Config, which ApplyConfigToPipeline and Bindings in src/headtracking_mod.cpp
// and AddBindings in src/hotkeys.cpp consume exactly as v0.1.0 did.
Record ObserveImport(const legacy::Config& c) {
    Record r;
    r["field.udp_port"] = std::to_string(c.udp_port);
    const cameraunlock::PositionSettings p = cameraunlock::PositionSettings::Symmetric(
        1.0f, 1.0f, 1.0f,
        c.limit_x, c.limit_y, c.limit_z, c.limit_z_back,
        c.local_smoothing, c.remote_smoothing,
        false, false, false);
    AddPipeline(r, p, c.local_smoothing, c.remote_smoothing);
    r["start.enabled"] = Flag(c.enable_on_startup);
    r["start.mode"] = kModeNames[c.position_enabled ? wf2_oracle::kRotationAndPosition : wf2_oracle::kRotationOnly];
    AddHotkeys(r, {{wf2_oracle::kToggle, c.toggle_key, 0},
                   {wf2_oracle::kToggle, c.chord_toggle_key, 3},
                   {wf2_oracle::kCycleMode, c.cycle_mode_key, 0},
                   {wf2_oracle::kCycleMode, c.chord_cycle_mode_key, 3}});
    return r;
}

// The settings a session runs on through this build's startup code
// (BootstrapMod): the position settings config::ToPositionSettings builds, the
// smoothing pair, the mode from the pair, and each hotkey list through
// ParseKeyBindings and RegisterKeyBindings.
Record ObserveCanonical(const Config& c) {
    Record r;
    r["field.udp_port"] = std::to_string(c.udp_port);
    AddPipeline(r, config::ToPositionSettings(c), c.local_smoothing, c.remote_smoothing);
    r["start.enabled"] = Flag(c.enable_on_startup);
    r["start.mode"] = kModeNames[static_cast<int>(config::StartupTrackingMode(c))];
    std::vector<wf2_oracle::Registration> registrations;
    const std::pair<int, const std::string*> lists[] = {{wf2_oracle::kToggle, &c.toggle_key},
                                                        {wf2_oracle::kCycleMode, &c.cycle_tracking_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), "the hotkey list '" + *list + "' parses");
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            registrations.emplace_back(action, b.vk, static_cast<unsigned>(b.modifiers));
        }
    }
    AddHotkeys(r, registrations);
    return r;
}

std::vector<std::string> Differences(const Record& a, const Record& b) {
    std::vector<std::string> out;
    for (const auto& [name, value] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            out.push_back(name + " only on the left");
        } else if (it->second != value) {
            out.push_back(name + ": " + value + " / " + it->second);
        }
    }
    for (const auto& [name, value] : b) {
        if (a.find(name) == a.end()) out.push_back(name + " only on the right");
    }
    return out;
}

// ---- The approved differences --------------------------------------------------------
//
// None apply: the import drops nothing, so a session runs on exactly what the
// import read.

void CheckNothingDropped(const cfg::ImportResult& result, const std::string& name) {
    for (const cfg::DroppedValue& d : result.dropped) {
        Check(false, name + ": the import drops [" + d.section + "] " + d.key + ", which no approved change covers here");
    }
    Check(result.pose_shaping.empty(), name + ": v0.1.0 read no pose-shaping setting, so the import reports none");
}

// ---- Inputs ------------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(WF2_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// What v0.1.0 wrote on a first start, the file every player holds.
std::string NewestFirstRun() { return ReadFileBytes(DataPath("firstrun-v0.1.0.ini")); }

const char* const kNewestFirstRunName = "v0.1.0 first-run file, and HeadTracking.ini at e7976fd";

// Every key the frozen reader reads, and how the corpus varies each one. The
// reader refuses a port outside 1024-65535 and a hotkey code that is not a
// bindable key, and clamps a smoothing value into 0-1 and a limit into 0-0.5.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "UdpPort", "5252", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"Hotkeys", "ToggleKey", "0x2D", {"0x11"}, true},
        {"Hotkeys", "CycleModeKey", "0x70", {"0x10"}, true},
        {"Hotkeys", "ChordToggleKey", "0x4B", {"0x12"}, true},
        {"Hotkeys", "ChordCycleModeKey", "0x4A", {"0xA2"}, true},
        {"Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Rotation", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Position", "Enabled", "0", {}},
        {"Position", "LimitX", "0.45", {"-0.5", "0.6"}},
        {"Position", "LimitY", "0.35", {"-0.5", "0.6"}},
        {"Position", "LimitZ", "0.5", {"-0.5", "0.6"}},
        {"Position", "LimitZBack", "0.15", {"-0.5", "0.6"}},
    };
}

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the reader reads.
std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> reads;
    for (const legacy::Key& key : legacy::ReadKeys()) reads.push_back({key.section, key.key});
    return reads;
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::string WithKeyCode(const std::string& base, const std::string& line, int code) {
    const std::size_t at = base.find(line);
    if (at == std::string::npos) throw std::logic_error("no " + line + " in the first-run file");
    char to[48];
    std::snprintf(to, sizeof(to), "%.*s0x%02X", static_cast<int>(line.find('=') + 1), line.c_str(),
                  static_cast<unsigned>(code));
    std::string out = base;
    return out.replace(at, line.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {kNewestFirstRunName, true, NewestFirstRun()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(NewestFirstRun(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    for (const char* line : {"ToggleKey=0x23", "ChordToggleKey=0x59"}) {
        for (int code = 0x01; code <= 0xFE; ++code) {
            const std::string bytes = WithKeyCode(NewestFirstRun(), line, code);
            char name[48];
            std::snprintf(name, sizeof(name), "%.*s0x%02X", static_cast<int>(std::strchr(line, '=') - line + 1), line,
                          static_cast<unsigned>(code));
            inputs.push_back({name, true, bytes});
        }
    }
    return inputs;
}

// ---- Checks on a load ------------------------------------------------------------------

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path);
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    return std::any_of(log.begin(), log.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

// What the reader and the table find in a canonical file, read over the table's
// own defaults, which stand for a Defaults.ini holding the built-in values.
std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<Config> table = config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds (the mode pair as a pair: rotation only), so a migration that
// wrote `default` where the imported value is not what `default` gives would
// read back differently over it. Each value is
// what the corpus's alternate for the legacy key it comes from imports as, so
// the inputs holding that alternate migrate to `default` here and every other
// input to a value.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nRotationEnabled=true\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.3\r\nRemoteSmoothing=0.6\r\n\r\n"
    "[Position]\r\nPositionEnabled=false\r\nPositionLimitX=0.45\r\nPositionLimitY=0.35\r\n"
    "PositionLimitYDown=0.35\r\nPositionLimitZ=0.5\r\nPositionLimitZBack=0.15\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=Insert, Ctrl+Shift+Y\r\nCycleTrackingModeKey=F1, Ctrl+Shift+G\r\n";

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

struct Tally {
    int created = 0;
    int migrated = 0;
    // Migrated files holding `default` on at least one row, and holding a value
    // on at least one.
    int with_default = 0;
    int with_value = 0;
    std::set<std::string> files;
};

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on.
Config Migrate(const Input& input, const Scratch& s, const std::string& label, Tally& tally) {
    const std::optional<FileState> legacy_before = StateOf(s.legacy());
    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(StateOf(s.legacy()) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");

    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) std::printf("  %s: %s, %s\n", label.c_str(), cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    Check(loaded.status == want, label + ": every legacy input imports, and no file gives a created one");
    if (loaded.status != want) return loaded.config;
    ++(input.present ? tally.migrated : tally.created);
    Check(s.Names() == (input.present ? std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}
                                      : std::set<std::string>{"CameraUnlock.ini"}),
          label + ": the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical());
    Check(cfg::HasCanonicalStamp(migrated), label + ": CameraUnlock.ini carries the stamp");
    Check(AsciiCrlf(migrated), label + ": CameraUnlock.ini is ASCII with CRLF line ends");
    Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", label.c_str(), d.c_str());
    Check(diagnostics.empty(), label + ": CameraUnlock.ini reads with no diagnostic");
    if (input.present) {
        tally.files.insert(migrated);
        const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(migrated);
        bool any_default = false;
        bool any_value = false;
        for (const cfg::CanonicalSection& section : doc.sections) {
            if (section.name == "CameraUnlock") continue;
            for (const cfg::CanonicalValue& entry : section.values) {
                (entry.value == "default" ? any_default : any_value) = true;
            }
        }
        if (any_default) ++tally.with_default;
        if (any_value) ++tally.with_value;
    }

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<Config> again = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, label + ": the next start reads CameraUnlock.ini");
    Check(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
          label + ": the next start runs on the same settings");
    Check(StateOf(s.canonical()) == created && StateOf(s.legacy()) == legacy_before,
          label + ": the next start changes neither file");
    Check(!input.present || LogSays(again.log, "is left as it was and is not read"),
          label + ": the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

// ---- Comparisons -------------------------------------------------------------------

void Compare(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(fs::path(WF2_SOURCE_DIR) / "HeadTracking.ini");
    const cfg::ConfigTable<Config> table = config::Table();
    const Record defaults = ObserveImport(legacy::Config{});
    Tally builtin, readonly, skewed;
    int compared = 0;
    int changed = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;

        // Comparison 1. Both read one copy, which neither writes.
        legacy::Config read;
        Record imported;
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            const std::set<std::string> before = s.Names();
            const Record oracle = ObserveOracle(wf2_oracle::Read(s.game().string()));
            const bool present = legacy::LoadConfig(s.legacy().string(), read);
            Check(present == input.present, name + ": the frozen reader finds the file exactly when it is there");
            if (!Differences(oracle, defaults).empty()) ++changed;
            imported = ObserveImport(read);
            const std::vector<std::string> diff = Differences(oracle, imported);
            for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 1, the oracle and the import agree");
            Check(s.Names() == before, name + ": neither reader writes a file");
            Check(!input.present || ReadFileBytes(s.legacy()) == input.bytes, name + ": neither reader changes the file");
        }

        // Comparison 2 over a Defaults.ini the owner creates with the built-in
        // values. The import's own result says it dropped nothing.
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            Config mapped = table.defaults();
            const cfg::ImportResult result =
                config::Import().run({s.legacy().wstring(), s.legacy().string(), false}, mapped);
            Check(result.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
                  name + ": the import reads every input, as the published build did");
            CheckNothingDropped(result, name);
            const Config migrated = Migrate(input, s, name, builtin);
            const std::vector<std::string> diff = Differences(imported, ObserveCanonical(migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2, the session runs as the import read");

            if (fs::exists(s.canonical())) {
                // Over the built-in values the table's own defaults stand for Defaults.ini.
                Config reread;
                CanonicalDiagnostics(ReadFileBytes(s.canonical()), reread);
                Check(Differences(ObserveCanonical(reread), ObserveCanonical(migrated)).empty(),
                      name + ": CameraUnlock.ini reads back as the settings the session runs on");
                // Fresh equals upgrade: the file the newest build wrote on its
                // first start, which is also the defaults it documented, and no
                // file at all, both end as the committed file.
                if (name == kNewestFirstRunName || name == "no file") {
                    Check(ReadFileBytes(s.canonical()) == committed, name + ": gives the committed file, byte for byte");
                }
            }
        }
        if (input.present) {
            // From a read-only HeadTracking.ini, which keeps its attribute. The
            // import run on its own first leaves the folder as it was.
            Scratch s;
            s.WriteLegacy(input.bytes);
            SetFileAttributesW(s.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            const std::set<std::string> before = s.Names();
            Config unused = table.defaults();
            config::Import().run({s.legacy().wstring(), s.legacy().string(), false}, unused);
            Check(s.Names() == before && ReadFileBytes(s.legacy()) == input.bytes,
                  name + ": the import leaves a read-only folder as it was");
            const Config c = Migrate(input, s, name + " (read-only)", readonly);
            Check(Differences(imported, ObserveCanonical(c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesW(s.legacy().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        if (input.present) {
            // Over a Defaults.ini that differs everywhere. With no legacy file
            // the settings are Defaults.ini's own, so only an input with a file
            // is held to the import here.
            Scratch s;
            s.WriteLegacy(input.bytes);
            s.WriteDefaults(kSkewedDefaults);
            const Config c = Migrate(input, s, name + " (skewed Defaults.ini)", skewed);
            const std::vector<std::string> diff = Differences(imported, ObserveCanonical(c));
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": the migration gives the import's settings over a Defaults.ini that differs everywhere");
        }
        ++compared;
    }
    std::printf("comparisons 1 and 2: %d inputs, %d of them read as something other than the defaults\n",
                compared, changed);
    std::printf("over built-in Defaults.ini: %d created, %d migrated; read-only: %d migrated; skewed Defaults.ini: "
                "%d migrated\n", builtin.created, builtin.migrated, readonly.migrated, skewed.migrated);
    std::printf("migrated files holding default on a row, and a value on a row: built-in Defaults.ini %d and %d, "
                "skewed Defaults.ini %d and %d\n", builtin.with_default, builtin.with_value, skewed.with_default,
                skewed.with_value);
    Check(changed > 0, "the inputs reach settings other than the defaults");
    Check(builtin.with_default > 0 && builtin.with_value > 0 && skewed.with_default > 0 && skewed.with_value > 0,
          "over either Defaults.ini the migration writes default on some rows and values on others");

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    std::set<std::string> files = builtin.files;
    files.insert(readonly.files.begin(), readonly.files.end());
    files.insert(skewed.files.begin(), skewed.files.end());
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : files) WriteFileBytes(lint / (std::to_string(n++) + ".ini"), file);
    std::printf("%zu distinct migrated files written to %s\n", files.size(), lint.string().c_str());
}

// v0.1.0's first start wrote the file every player then held, and that is the
// input the corpus is built on.
void FirstRunOutputIsTheCorpusBase() {
    Scratch s;
    wf2_oracle::WriteFirstRunFile(s.game().string());
    Check(ReadFileBytes(s.legacy()) == NewestFirstRun(),
          "data/firstrun-v0.1.0.ini is what v0.1.0 writes on a first start");
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    FirstRunOutputIsTheCorpusBase();
    Compare(Inputs());
    RemoveTree(ScratchRoot());
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
