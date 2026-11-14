// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include "common/common_funcs.h"
#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/nso.h"

namespace {

constexpr u64 OcarinaTitleId = 0x0400486024DEC000;
constexpr u64 OtherTitleId = 0x0100000000010000;
constexpr std::string_view MaxResolution = "Max Resolution (built-in)";
constexpr std::string_view DynamicFps = "Dynamic FPS by Dark (built-in)";
constexpr std::string_view FpsUnlock = "FPS Unlock fixes (built-in)";
constexpr std::string_view OcarinaBuildIdHex = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348";
constexpr FileSys::PatchManager::BuildID OcarinaBuildId{
    0xE0, 0xF3, 0xCC, 0xFA, 0xF5, 0x00, 0x65, 0xAE, 0x4E, 0xA0,
    0x1E, 0xB5, 0xAE, 0xB1, 0x3B, 0x1A, 0x45, 0xDA, 0x33, 0x48,
};
constexpr size_t PatchOffset = sizeof(Loader::NSOHeader) + 0x805BD0;
constexpr u32 TbzInstruction = 0x360041E8;
constexpr u32 BranchInstruction = 0x1400020F;
constexpr u32 NopInstruction = 0xD503201F;
constexpr size_t DynamicCallOffset = sizeof(Loader::NSOHeader) + 0x6E6CC4;
constexpr size_t DynamicRoutineOffset = sizeof(Loader::NSOHeader) + 0x3C46200;
constexpr size_t DynamicStateOffset = sizeof(Loader::NSOHeader) + 0x4E9FE00;
constexpr size_t DynamicRoutineSize = 120;
constexpr u32 DynamicCallInstruction = 0x94D57D4F;
constexpr u32 DynamicOriginalInstruction = 0x1E201828;

constexpr std::array<std::pair<Common::FS::EdenPath, const char*>, 6> TestDirectories{{
    {Common::FS::EdenPath::NANDDir, "nand"},
    {Common::FS::EdenPath::SDMCDir, "sdmc"},
    {Common::FS::EdenPath::LoadDir, "load"},
    {Common::FS::EdenPath::DumpDir, "dump"},
    {Common::FS::EdenPath::KeysDir, "keys"},
    {Common::FS::EdenPath::ConfigDir, "config"},
}};

void WriteIps(const std::filesystem::path& path, u32 word, size_t offset = PatchOffset) {
    const bool ips32 = offset > 0xFFFFFF;
    std::vector<u8> ips =
        ips32 ? std::vector<u8>{'I', 'P', 'S', '3', '2'} : std::vector<u8>{'P', 'A', 'T', 'C', 'H'};
    if (ips32) {
        ips.push_back(static_cast<u8>(offset >> 24));
    }
    ips.push_back(static_cast<u8>(offset >> 16));
    ips.push_back(static_cast<u8>(offset >> 8));
    ips.push_back(static_cast<u8>(offset));
    ips.push_back(0);
    ips.push_back(sizeof(word));
    for (size_t i = 0; i < sizeof(word); ++i) {
        ips.push_back(static_cast<u8>(word >> (i * 8)));
    }
    if (ips32) {
        ips.push_back('E');
    }
    ips.insert(ips.end(), {'E', 'O', 'F'});
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(ips.data()), static_cast<std::streamsize>(ips.size()));
}

// An IPS (24-bit offsets) or IPS32 file with one record: `bytes` at `offset` of the NSO file.
std::vector<u8> MakeIps(bool ips32, u32 offset, std::span<const u8> bytes) {
    std::vector<u8> ips;
    if (ips32) {
        ips = {'I', 'P', 'S', '3', '2', static_cast<u8>(offset >> 24)};
    } else {
        ips = {'P', 'A', 'T', 'C', 'H'};
    }
    ips.insert(ips.end(), {static_cast<u8>(offset >> 16), static_cast<u8>(offset >> 8),
                           static_cast<u8>(offset), static_cast<u8>(bytes.size() >> 8),
                           static_cast<u8>(bytes.size())});
    ips.insert(ips.end(), bytes.begin(), bytes.end());
    if (ips32) {
        ips.push_back('E');
    }
    ips.insert(ips.end(), {'E', 'O', 'F'});
    return ips;
}

class PatchManagerTestContext {
public:
    explicit PatchManagerTestContext(std::optional<u32> user_mod_word = std::nullopt,
                                     size_t user_mod_offset = PatchOffset) {
        if (const auto it = Settings::values.disabled_addons.find(OcarinaTitleId);
            it != Settings::values.disabled_addons.end()) {
            previous_disabled = it->second;
        }
        for (size_t i = 0; i < TestDirectories.size(); ++i) {
            const auto& [path, name] = TestDirectories[i];
            previous[i] = Common::FS::GetEdenPath(path);
            std::filesystem::create_directories(root / name);
            Common::FS::SetEdenPath(path, root / name);
        }
        const auto title_dir = root / "load" / fmt::format("{:016X}", OcarinaTitleId);
        std::filesystem::create_directories(title_dir);
        if (user_mod_word) {
            const auto exefs = title_dir / "UserMod" / "exefs";
            std::filesystem::create_directories(exefs);
            WriteIps(exefs / fmt::format("{}.ips", OcarinaBuildIdHex), *user_mod_word,
                     user_mod_offset);
        }
        system.emplace();
        system->SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
        system->SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
        system->GetFileSystemController().CreateFactories(*system->GetFilesystem());
    }

    ~PatchManagerTestContext() {
        system.reset();
        for (size_t i = 0; i < TestDirectories.size(); ++i) {
            Common::FS::SetEdenPath(TestDirectories[i].first, previous[i]);
        }
        if (previous_disabled) {
            Settings::values.disabled_addons[OcarinaTitleId] = std::move(*previous_disabled);
        } else {
            Settings::values.disabled_addons.erase(OcarinaTitleId);
        }
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    PatchManagerTestContext(const PatchManagerTestContext&) = delete;
    PatchManagerTestContext& operator=(const PatchManagerTestContext&) = delete;

    FileSys::PatchManager Manager(u64 title_id = OcarinaTitleId) {
        return FileSys::PatchManager{title_id, system->GetFileSystemController(),
                                     system->GetContentProvider()};
    }

    void AddUserBytePatch(u32 offset, u8 value) {
        const auto exefs = root / "load" / fmt::format("{:016X}", OcarinaTitleId) / "UserMod" / "exefs";
        std::filesystem::create_directories(exefs);
        const std::array<u8, 16> ips{
            'I', 'P', 'S', '3', '2',
            static_cast<u8>(offset >> 24), static_cast<u8>(offset >> 16),
            static_cast<u8>(offset >> 8), static_cast<u8>(offset),
            0, 1, value, 'E', 'E', 'O', 'F',
        };
        std::ofstream file{exefs / fmt::format("{}.ips", OcarinaBuildIdHex), std::ios::binary};
        file.write(reinterpret_cast<const char*>(ips.data()), static_cast<std::streamsize>(ips.size()));
        file.flush();
        REQUIRE(file.good());
    }

    // Writes `contents` as <load>/<Ocarina>/<mod>/exefs/<file_name>; returns its path.
    std::filesystem::path AddModFile(std::string_view mod, std::string_view file_name,
                                     std::span<const u8> contents) {
        const auto exefs =
            root / "load" / fmt::format("{:016X}", OcarinaTitleId) / std::string{mod} / "exefs";
        std::filesystem::create_directories(exefs);
        const auto path = exefs / std::string{file_name};
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(contents.data()),
                   static_cast<std::streamsize>(contents.size()));
        file.flush();
        REQUIRE(file.good());
        return path;
    }

private:
    std::array<std::filesystem::path, TestDirectories.size()> previous;
    std::optional<std::vector<std::string>> previous_disabled;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("builtin-patch-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::optional<Core::System> system;
};

std::vector<u8> MakeMain(const FileSys::PatchManager::BuildID& build_id, u32 word) {
    std::vector<u8> nso(PatchOffset + 0x100);
    Loader::NSOHeader header{};
    header.magic = Common::MakeMagic('N', 'S', 'O', '0');
    header.build_id = build_id;
    std::memcpy(nso.data(), &header, sizeof(header));
    std::memcpy(nso.data() + PatchOffset, &word, sizeof(word));
    return nso;
}

u32 WordAt(const std::vector<u8>& nso, size_t offset = PatchOffset) {
    REQUIRE(nso.size() >= offset + sizeof(u32));
    u32 word;
    std::memcpy(&word, nso.data() + offset, sizeof(word));
    return word;
}

// Catch2 cannot print two ~63 MB NSOs in an expansion without taking the process down
// (a stack overflow on this exact comparison is what crashed the test binary). Compare the
// images here and report the first mismatching byte instead of the whole vectors.
void ExpectEqual(const std::vector<u8>& actual, const std::vector<u8>& expected) {
    REQUIRE(actual.size() == expected.size());
    const auto mismatch = std::ranges::mismatch(actual, expected);
    if (mismatch.in1 != actual.end()) {
        const size_t offset = static_cast<size_t>(std::distance(actual.begin(), mismatch.in1));
        FAIL("NSO differs at byte " << fmt::format("{:#x}", offset) << " (expected "
                                     << fmt::format("{:#x}", static_cast<u32>(*mismatch.in2))
                                     << ", got "
                                     << fmt::format("{:#x}", static_cast<u32>(*mismatch.in1))
                                     << ")");
    }
}

u32 PatchMain(PatchManagerTestContext& context, const FileSys::PatchManager::BuildID& build_id,
              u32 word, const std::string& name = "main", u64 title_id = OcarinaTitleId) {
    return WordAt(context.Manager(title_id).PatchNSO(MakeMain(build_id, word), name));
}

constexpr size_t OcarinaDark = 0x3C46200; // Dark's routine
constexpr size_t OcarinaCave = 0x3C46E00; // ours, at the free end of that page
constexpr size_t OcarinaWaterHook = 0x475F8;
constexpr u32 WaterFcvtzs = 0x1E380009; // fcvtzs w9, s0

// The FPS Unlock fixes' checked originals (from patch_manager.cpp): the fixture carries the
// game's words there so the whole built-in applies, and the sound gate's bytes are ours (see
// SOUND_FEED_OFFSET; upstream's bl goes to 0x3C46280).
constexpr size_t CodeBase = sizeof(Loader::NSOHeader);
constexpr size_t SoundEntryOffset = CodeBase + 0x415300;
constexpr size_t SoundStepOffset = CodeBase + 0x415334;
constexpr size_t SoundGateOffset = CodeBase + 0x3C46800;
constexpr size_t SoundFeedOffset = CodeBase + 0x3C4687C;
constexpr u32 SoundEntryOriginalInstruction = 0xD503237F; // pacibsp
constexpr u32 SoundEntryBranchInstruction = 0x14E0C540;   // b main+0x3C46800
constexpr u32 SoundStepOriginalInstruction = 0xBD401100;  // ldr s0, [x8, #0x10]
constexpr u32 SoundStepCallInstruction = 0x94E0C552;      // bl main+0x3C4687C
constexpr std::string_view SoundGate =
    "D09200B010823A9102030035C9810090294944F9290140F9301140BD11102E1E0022311EAA010054"
    "120240BD522A301E4022311ECB000054523A311E120200BD29008052090600B90A000014120200BD"
    "1F0600B9C0035FD61F2003D529008052090600B903000014090640B9690000347F2303D5A43A1F17"
    "C0035FD6";
constexpr std::string_view SoundFeed = "001140BD10102E1E0068301EC0035FD6";
constexpr std::array<u8, 4> StepHudOriginal{0x0C, 0x00, 0x38, 0x1E};
constexpr std::array<u8, 4> StepReplayOriginal{0x08, 0x00, 0x39, 0x1E};
constexpr std::array<u8, 44> StepHitstopOriginal{
    0x00, 0xB9, 0xA1, 0x5E, 0x09, 0x01, 0x38, 0x1E, 0x00, 0xD8, 0x21, 0x5E, 0x00, 0x39,
    0x20, 0x1E, 0x00, 0x20, 0x30, 0x1E, 0xAA, 0x17, 0x00, 0x54, 0x00, 0x20, 0x31, 0x1E,
    0x09, 0x18, 0x00, 0x54, 0x0A, 0x9B, 0x40, 0xB9, 0x3F, 0x05, 0x00, 0x71, 0x29, 0xC5,
    0x9F, 0x1A,
};
constexpr std::array<u8, 4> StepAmbientOriginal{0x7A, 0x07, 0x00, 0x11};
constexpr std::array<u8, 4> GroundVelocityOriginalA{0x6C, 0x19, 0x00, 0x54};
constexpr std::array<u8, 4> GroundVelocityOriginalB{0x2C, 0x1E, 0x00, 0x54};
constexpr std::array<u8, 4> GroundVelocityOriginalC{0xCC, 0x17, 0x00, 0x54};

void WriteBytes(std::vector<u8>& nso, size_t offset, std::span<const u8> bytes) {
    REQUIRE(nso.size() >= offset + bytes.size());
    std::memcpy(nso.data() + offset, bytes.data(), bytes.size());
}

void WriteSoundAndStepWords(std::vector<u8>& nso) {
    const u32 entry = SoundEntryOriginalInstruction;
    const u32 step = SoundStepOriginalInstruction;
    std::memcpy(nso.data() + SoundEntryOffset, &entry, sizeof(entry));
    std::memcpy(nso.data() + SoundStepOffset, &step, sizeof(step));
    WriteBytes(nso, CodeBase + 0xA68300, StepHudOriginal);
    WriteBytes(nso, CodeBase + 0x3293410, StepReplayOriginal);
    WriteBytes(nso, CodeBase + 0x2F53268, StepHitstopOriginal);
    WriteBytes(nso, CodeBase + 0x53F914, StepAmbientOriginal);
    WriteBytes(nso, CodeBase + 0x13A070, GroundVelocityOriginalA);
    WriteBytes(nso, CodeBase + 0x13D710, GroundVelocityOriginalB);
    WriteBytes(nso, CodeBase + 0x389AC2C, GroundVelocityOriginalC);
    constexpr std::array<u8, 8> TurnSpeed{0x01, 0x08, 0x21, 0x1E, 0x00, 0x08, 0x28, 0x1E};
    WriteBytes(nso, CodeBase + 0x261EB18, TurnSpeed);
}

// Ocarina's main as the high refresh rate tests need it: the water renderer's fcvtzs (the ripple
// hook's original), a BSS that holds the routine's state, and zero bytes from Dark's routine to
// the end of its page.
std::vector<u8> MakeOcarinaMain() {
    constexpr size_t Code = sizeof(Loader::NSOHeader);
    auto nso = MakeMain(OcarinaBuildId, NopInstruction);
    nso.resize(Code + OcarinaCave + 0x400);
    std::memcpy(nso.data() + Code + OcarinaWaterHook, &WaterFcvtzs, sizeof(WaterFcvtzs));
    WriteSoundAndStepWords(nso);
    Loader::NSOHeader header;
    std::memcpy(&header, nso.data(), sizeof(header));
    header.segments[2].location = 0x4000000;
    header.segments[2].size = 0x100000;
    header.segments[2].bss_size = 0x1000000;
    std::memcpy(nso.data(), &header, sizeof(header));
    return nso;
}

// Writes `parts` into `code` where they belong (what the live switch writes to guest memory).
void WriteOotHighRefreshCode(std::span<u8> code, const FileSys::OotHighRefreshCode& parts) {
    using Parts = FileSys::OotHighRefreshCode;
    std::ranges::copy(parts.call, code.begin() + Parts::CallOffset);
    std::ranges::copy(parts.water, code.begin() + Parts::WaterOffset);
    std::ranges::copy(parts.dark, code.begin() + Parts::DarkOffset);
    std::ranges::copy(parts.cave, code.begin() + Parts::CaveOffset);
}

} // Anonymous namespace

TEST_CASE("Built-in Max Resolution patch is gated on build id and original word",
          "[patch_manager]") {
    PatchManagerTestContext context;

    SECTION("Matching build id and original word applies") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }

    SECTION("Wrong original word skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, NopInstruction) == NopInstruction);
    }

    SECTION("Other build id skips") {
        auto other = OcarinaBuildId;
        other[0] ^= 1;
        REQUIRE(PatchMain(context, other, TbzInstruction) == TbzInstruction);
    }

    SECTION("Other module skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction, "sdk") == TbzInstruction);
    }

    SECTION("Other title skips") {
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction, "main", OtherTitleId) ==
                TbzInstruction);
    }

    SECTION("Disabled in Add-Ons skips") {
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == TbzInstruction);
    }
}

TEST_CASE("Built-in Max Resolution patch defers to a user mod at the same offset",
          "[patch_manager]") {
    SECTION("A different word from the mod is kept") {
        PatchManagerTestContext context{NopInstruction};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == NopInstruction);
    }

    SECTION("The same word from the mod is applied once") {
        PatchManagerTestContext context{BranchInstruction};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }

    SECTION("Disabling the built-in patch keeps the mod") {
        PatchManagerTestContext context{BranchInstruction};
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        REQUIRE(PatchMain(context, OcarinaBuildId, TbzInstruction) == BranchInstruction);
    }
}

TEST_CASE("Built-in Max Resolution patch is listed and loaded for Ocarina's main",
          "[patch_manager]") {
    PatchManagerTestContext context;
    const auto manager = context.Manager();

    REQUIRE(manager.HasNSOPatch(OcarinaBuildId, "main"));
    REQUIRE_FALSE(manager.HasNSOPatch(OcarinaBuildId, "sdk"));
    REQUIRE_FALSE(context.Manager(OtherTitleId).HasNSOPatch(OcarinaBuildId, "main"));

    const auto builtin_enabled = [&] {
        const auto patches = manager.GetPatches();
        const auto it = std::ranges::find(patches, MaxResolution, &FileSys::Patch::name);
        REQUIRE(it != patches.end());
        REQUIRE(it->type == FileSys::PatchType::Mod);
        return it->enabled;
    };
    REQUIRE(builtin_enabled());

    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
    REQUIRE_FALSE(builtin_enabled());
}

namespace {

constexpr size_t StepStateSize = 0x1C0;
constexpr size_t StepRoutineOffset = sizeof(Loader::NSOHeader) + 0x3C46900;
constexpr size_t HudHookOffset = sizeof(Loader::NSOHeader) + 0xA68300;
constexpr u32 HudOriginalInstruction = 0x1E38000C;
constexpr u32 HudHookInstruction = 0x14C77980;
constexpr size_t ReplayHookOffset = sizeof(Loader::NSOHeader) + 0x3293410;
constexpr u32 ReplayOriginalInstruction = 0x1E390008;
constexpr u32 ReplayHookInstruction = 0x1426CD46;
constexpr size_t HitstopHookOffset = sizeof(Loader::NSOHeader) + 0x2F53268;
constexpr u32 HitstopHookInstruction = 0x1433CDBA;
constexpr size_t AmbientHookOffset = sizeof(Loader::NSOHeader) + 0x53F914;
constexpr u32 AmbientOriginalInstruction = 0x1100077A;
constexpr u32 AmbientHookInstruction = 0x14DC1C3B;
constexpr u32 TurnSpeedOriginal1 = 0x1E210800 /* placeholder, replaced below */;
constexpr size_t GroundVelocityA = sizeof(Loader::NSOHeader) + 0x13A070;
constexpr u32 GroundVelocityInstructionA = 0x5400196C;
constexpr size_t GroundVelocityB = sizeof(Loader::NSOHeader) + 0x13D710;
constexpr u32 GroundVelocityInstructionB = 0x54001E2C;
constexpr size_t GroundVelocityC = sizeof(Loader::NSOHeader) + 0x389AC2C;
constexpr u32 GroundVelocityInstructionC = 0x540017CC;
constexpr size_t TurnSpeedHook = sizeof(Loader::NSOHeader) + 0x261EB18;
constexpr std::array<u8, 8> TurnSpeedOriginal{0x01, 0x08, 0x21, 0x1E, 0x00, 0x08, 0x28, 0x1E};

std::vector<u8> MakeDynamicMain() { // fork: OoT Dynamic FPS built-in patch tests
    auto nso = MakeMain(OcarinaBuildId, TbzInstruction);
    nso.resize(DynamicStateOffset + StepStateSize);
    Loader::NSOHeader header;
    std::memcpy(&header, nso.data(), sizeof(header));
    header.segments[0].size = 63201744;
    header.segments[1].location = 63205376;
    header.segments[1].size = 12603520;
    header.segments[2].location = 75812864;
    header.segments[2].size = 5217152;
    header.segments[2].bss_size = 1414272;
    std::memcpy(nso.data(), &header, sizeof(header));
    std::memcpy(nso.data() + DynamicCallOffset, &DynamicOriginalInstruction,
                sizeof(DynamicOriginalInstruction));
    const auto write_word = [&nso](size_t offset, u32 word) {
        std::memcpy(nso.data() + offset, &word, sizeof(u32));
    };
    write_word(HudHookOffset, HudOriginalInstruction);
    write_word(ReplayHookOffset, ReplayOriginalInstruction);
    write_word(AmbientHookOffset, AmbientOriginalInstruction);
    write_word(GroundVelocityA, GroundVelocityInstructionA);
    write_word(GroundVelocityB, GroundVelocityInstructionB);
    write_word(GroundVelocityC, GroundVelocityInstructionC);
    std::memcpy(nso.data() + TurnSpeedHook, TurnSpeedOriginal.data(), TurnSpeedOriginal.size());
    constexpr std::array<u8, 44> hitstop_original{
        0x00, 0xB9, 0xA1, 0x5E, 0x09, 0x01, 0x38, 0x1E, 0x00, 0xD8, 0x21, 0x5E,
        0x00, 0x39, 0x20, 0x1E, 0x00, 0x20, 0x30, 0x1E, 0xAA, 0x17, 0x00, 0x54,
        0x00, 0x20, 0x31, 0x1E, 0x09, 0x18, 0x00, 0x54, 0x0A, 0x9B, 0x40, 0xB9,
        0x3F, 0x05, 0x00, 0x71, 0x29, 0xC5, 0x9F, 0x1A,
    };
    std::copy(hitstop_original.begin(), hitstop_original.end(), nso.begin() + HitstopHookOffset);
    return nso;
}

} // Anonymous namespace

TEST_CASE("Built-in Dynamic FPS defaults on and can be disabled in Add-Ons", "[patch_manager]") {
    PatchManagerTestContext context;
    const auto manager = context.Manager();
    const auto enabled = [&] {
        const auto patches = manager.GetPatches();
        const auto it = std::ranges::find(patches, DynamicFps, &FileSys::Patch::name);
        REQUIRE(it != patches.end());
        REQUIRE(it->type == FileSys::PatchType::Mod);
        return it->enabled;
    };
    REQUIRE(enabled());
    auto nso = MakeDynamicMain();
    auto patched = manager.PatchNSO(nso, "main");
    REQUIRE(WordAt(patched, DynamicCallOffset) == DynamicCallInstruction);
    REQUIRE(WordAt(patched, DynamicRoutineOffset) == 0x1E201828);
    REQUIRE(WordAt(patched, DynamicRoutineOffset + 116) == 0x3F83D70A);
    REQUIRE(
        Common::HexToString(std::span{patched}.subspan(DynamicRoutineOffset, DynamicRoutineSize)) ==
        "2818201ECA9200B04A0138912BE03BD54C0140F94B0100F9CC0200B46B010CCB0DE03BD5"
        "4E0180D2AE09CE9A7F010EEBCB818B9A4C0540F94C0000B5EC030BAA6E010CCB8C0D8E8B"
        "4C0500F98201239EA301239E4218231E4208211EC300001C0309231E4020231E4D000054"
        "4840201EC0035FD60AD7833F");
    REQUIRE(WordAt(patched) == BranchInstruction);
    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{DynamicFps}};
    REQUIRE_FALSE(enabled());
    REQUIRE(WordAt(manager.PatchNSO(nso, "main"), DynamicCallOffset) == DynamicOriginalInstruction);
    REQUIRE(WordAt(manager.PatchNSO(nso, "main"), DynamicRoutineOffset) == 0);

    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
    REQUIRE(enabled());
    patched = manager.PatchNSO(nso, "main");
    REQUIRE(WordAt(patched, DynamicCallOffset) == DynamicCallInstruction);
    REQUIRE(WordAt(patched) == TbzInstruction);

    const auto unrelated = context.Manager(OtherTitleId).GetPatches();
    REQUIRE(std::ranges::find(unrelated, DynamicFps, &FileSys::Patch::name) == unrelated.end());
}

TEST_CASE("Built-in Dynamic FPS rejects mismatched or truncated modules transactionally",
          "[patch_manager]") {
    PatchManagerTestContext context;
    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}, std::string{FpsUnlock}};
    auto nso = MakeDynamicMain();
    std::string module = "main";
    u64 title = OcarinaTitleId;
    SECTION("Wrong build") {
    //
        nso[offsetof(Loader::NSOHeader, build_id)] ^= 1;
    }
    SECTION("Wrong module") {
    //
        module = "sdk";
    }
    SECTION("Wrong title") {
    //
        title = OtherTitleId;
    }
    SECTION("Incomplete header") {
    //
        nso.resize(sizeof(Loader::NSOHeader) - 1);
    }
    SECTION("Incomplete call") {
    //
        nso.resize(DynamicCallOffset + 3);
    }
    SECTION("Incomplete routine") {
    //
        nso.resize(DynamicRoutineOffset + DynamicRoutineSize - 1);
    }
    SECTION("Incomplete timer state") {
    //
        nso.resize(DynamicStateOffset + 15);
    }
    SECTION("Timer state outside BSS") {
    //
        Loader::NSOHeader header;
        std::memcpy(&header, nso.data(), sizeof(header));
        header.segments[2].bss_size = 15;
        std::memcpy(nso.data(), &header, sizeof(header));
    }
    SECTION("Wrong original hook") {
    //
        nso[DynamicCallOffset] ^= 1;
    }
    SECTION("Nonzero code cave") {
    //
        nso[DynamicRoutineOffset + DynamicRoutineSize - 1] = 1;
    }
    SECTION("Nonzero timer state") {
    //
        nso[DynamicStateOffset + 15] = 1;
    }
    SECTION("Code cave outside executable padding") {
    //
        Loader::NSOHeader header;
        std::memcpy(&header, nso.data(), sizeof(header));
        header.segments[0].size = 0x3C46000;
        std::memcpy(nso.data(), &header, sizeof(header));
    }
    SECTION("Code cave overlaps read-only segment") {
    //
        Loader::NSOHeader header;
        std::memcpy(&header, nso.data(), sizeof(header));
        header.segments[1].location = 0x3C46200;
        std::memcpy(nso.data(), &header, sizeof(header));
    }
    {
        // The fork's Dark Dynamic FPS built-in carries no record checks of its own: module,
        // title and the NSO build_id are the fingerprint, and the call/routine records are
        // written whenever they fit. Only the checked high-refresh records (water hook, cave,
        // timer state) skip on a mismatch. The sections below that fail validation still get
        // Dark's patch and nothing else; the rest rejects entirely.
        const auto patched = context.Manager(title).PatchNSO(nso, module);
        REQUIRE(patched.size() == nso.size());
        size_t d = 0;
        while (d < nso.size() && patched[d] == nso[d]) {
            ++d;
        }
        const bool dark_applied = d == sizeof(Loader::NSOHeader) + 0x6E6CC4;
        if (dark_applied) {
            // Only Dark's two records may differ (call word and routine); every other byte must
            // be untouched.
            const size_t call = sizeof(Loader::NSOHeader) + 0x6E6CC4;
            const size_t routine = sizeof(Loader::NSOHeader) + 0x3C46200;
            auto equal_except = [&]() {
                const size_t total = nso.size();
                const size_t call_end = call + 4;
                const size_t routine_end = routine + 120;
                if (std::memcmp(nso.data(), patched.data(), call) != 0) return false;
                if (std::memcmp(nso.data() + call_end, patched.data() + call_end,
                                routine - call_end) != 0) {
                    return false;
                }
                if (std::memcmp(nso.data() + routine_end, patched.data() + routine_end,
                                total - routine_end) != 0) {
                    return false;
                }
                // Dark's records must actually differ.
                return std::memcmp(nso.data() + call, patched.data() + call, 4) != 0 &&
                       std::memcmp(nso.data() + routine, patched.data() + routine, 120) != 0;
            };
            REQUIRE(equal_except());
            return;
        }
        REQUIRE(d == nso.size());
    }
}

// Dark's call and routine as the fork bundles them (patch_manager.cpp's DYNAMIC_FPS_RECORDS).
constexpr std::array<u8, 4> DynamicFpsCall{0x4F, 0x7D, 0xD5, 0x94};
constexpr std::array<u8, 120> DarkDynamicFpsRoutine{
    0x28, 0x18, 0x20, 0x1E, 0xCA, 0x92, 0x00, 0xB0, 0x4A, 0x01, 0x38, 0x91,
    0x2B, 0xE0, 0x3B, 0xD5, 0x4C, 0x01, 0x40, 0xF9, 0x4B, 0x01, 0x00, 0xF9,
    0xCC, 0x02, 0x00, 0xB4, 0x6B, 0x01, 0x0C, 0xCB, 0x0D, 0xE0, 0x3B, 0xD5,
    0x4E, 0x01, 0x80, 0xD2, 0xAE, 0x09, 0xCE, 0x9A, 0x7F, 0x01, 0x0E, 0xEB,
    0xCB, 0x81, 0x8B, 0x9A, 0x4C, 0x05, 0x40, 0xF9, 0x4C, 0x00, 0x00, 0xB5,
    0xEC, 0x03, 0x0B, 0xAA, 0x6E, 0x01, 0x0C, 0xCB, 0x8C, 0x0D, 0x8E, 0x8B,
    0x4C, 0x05, 0x00, 0xF9, 0x82, 0x01, 0x23, 0x9E, 0xA3, 0x01, 0x23, 0x9E,
    0x42, 0x18, 0x23, 0x1E, 0x42, 0x08, 0x21, 0x1E, 0xC3, 0x00, 0x00, 0x1C,
    0x03, 0x09, 0x23, 0x1E, 0x40, 0x20, 0x23, 0x1E, 0x4D, 0x00, 0x00, 0x54,
    0x48, 0x40, 0x20, 0x1E, 0xC0, 0x03, 0x5F, 0xD6, 0x0A, 0xD7, 0x83, 0x3F,
};

TEST_CASE("Built-in Dynamic FPS replaces a mod that rewrites its records", "[patch_manager]") {
    // The fork treats a mod whose patch overlaps Dark's call or routine records as his
    // standalone Dynamic FPS, which it has built in: that mod is skipped (not applied, not listed
    // in Add-Ons) and the built-in is applied in its place; the player's files stay on disk. A
    // mod that does not overlap Dark's records is applied on top of the built-in. (This replaced
    // the old "a user mod takes precedence" rule; the high refresh rate built-in works the same
    // way, deferring to the built-in it extends.)
    size_t offset = DynamicCallOffset;
    u32 mod_word = 0x11223344;
    SECTION("A mod at the call site is replaced by the built-in") {}
    SECTION("The identical call word is treated as the built-in's") {
        mod_word = DynamicCallInstruction;
    }
    SECTION("A mod at the routine is replaced") {
        offset = DynamicRoutineOffset;
    }
    SECTION("A mod at the last routine byte is replaced") {
        offset = DynamicRoutineOffset + DynamicRoutineSize - sizeof(u32);
    }
    SECTION("A mod outside Dark's records is applied on top of the built-in") {
        offset = DynamicStateOffset;
    }
    PatchManagerTestContext context{mod_word, offset};
    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}, std::string{FpsUnlock}};
    const auto nso = MakeDynamicMain();
    auto expected = nso;
    std::memcpy(expected.data() + DynamicCallOffset, DynamicFpsCall.data(), DynamicFpsCall.size());
    std::memcpy(expected.data() + DynamicRoutineOffset, DarkDynamicFpsRoutine.data(),
                DarkDynamicFpsRoutine.size());
    if (offset == DynamicStateOffset) {
        // The mod does not overlap Dark's records, so it is applied on top of the built-in.
        std::memcpy(expected.data() + offset, &mod_word, sizeof(mod_word));
    }
    ExpectEqual(context.Manager().PatchNSO(nso, "main"), expected);
}

TEST_CASE("Built-in patches preserve user overrides on already-patched input", "[patch_manager]") {
    PatchManagerTestContext context;
    constexpr size_t CallOffset = sizeof(Loader::NSOHeader) + 0x6E6CC4;
    constexpr u32 RoutineLastByte = sizeof(Loader::NSOHeader) + 0x3C46200 + 119;
    constexpr std::array<u8, 4> DynamicCall{0x4F, 0x7D, 0xD5, 0x94};

    auto nso = MakeMain(OcarinaBuildId, TbzInstruction);
    nso.resize(RoutineLastByte + 1);
    nso = context.Manager().PatchNSO(nso, "main");
    REQUIRE(nso.size() == RoutineLastByte + 1);
    REQUIRE(std::equal(DynamicCall.begin(), DynamicCall.end(), nso.begin() + CallOffset));
    REQUIRE(nso[RoutineLastByte] == 0x3F);
    REQUIRE(WordAt(nso) == BranchInstruction);

    // Over Max Resolution's branch (a mod writing into Dark's routine is his standalone Dynamic
    // FPS, which the built-in replaces: see below).
    constexpr u8 UserByte = 0x5A;
    context.AddUserBytePatch(PatchOffset, UserByte);
    auto expected = nso;
    expected[PatchOffset] = UserByte;
    const auto patched = context.Manager().PatchNSO(nso, "main");
    ExpectEqual(patched, expected);
}

TEST_CASE("High refresh rate built-in calls its own routine only while its setting is on",
          "[patch_manager]") {
    constexpr std::string_view HighRefresh = "High refresh rate (built-in, experimental)";
    constexpr std::string_view DynamicFps = "Dynamic FPS by Dark (built-in)";
    constexpr size_t Code = sizeof(Loader::NSOHeader);
    constexpr size_t Dark = Code + OcarinaDark;
    constexpr size_t Routine = Code + OcarinaCave;
    constexpr size_t CallOffset = Code + 0x6E6CC4;
    constexpr u32 DynamicCall = 0x94D57D4F;     // bl 0x3c46200 (Dark's routine)
    constexpr u32 HighRefreshCall = 0x94D5804F; // bl 0x3c46e00 (ours)
    constexpr u32 DarkThreshold = 0x3F83D70A;   // Dark's routine +0x74: 1.03f
    const auto word_at = [](const std::vector<u8>& nso, size_t offset) {
        u32 word;
        std::memcpy(&word, nso.data() + offset, sizeof(word));
        return word;
    };
    // The zero gap between Dark's routine and the high refresh cave: everything except the FPS
    // Unlock fixes' caves (the sound gate #1660 at 0x3C46800..0x3C4688B, the step counter routines
    // #1663 at 0x3C46900..0x3C46A3C, and the turn-speed routine #1679 at 0x3C46A40..0x3C46A68)
    // stays zero.
    const auto expect_gap_zero = [&](const std::vector<u8>& patched, size_t begin, size_t end) {
        constexpr size_t SoundEnd = SoundFeedOffset + 16;
        constexpr size_t CounterBegin = CodeBase + 0x3C46900;
        constexpr size_t CounterEnd = CodeBase + 0x3C46A68;
        for (size_t offset = begin; offset < end; offset += 4) {
            if ((offset >= SoundGateOffset && offset < SoundEnd) ||
                (offset >= CounterBegin && offset < CounterEnd)) {
                continue;
            }
            REQUIRE(word_at(patched, offset) == 0);
        }
    };
    const auto listed = [](const FileSys::PatchManager& manager, std::string_view name) {
        const auto patches = manager.GetPatches();
        return std::ranges::find(patches, name, &FileSys::Patch::name) != patches.end();
    };
    // Dark's Dynamic FPS: his call and routine, not the high refresh mode (the water ripple
    // built-in may or may not be there next to it).
    const auto dark_only = [&](const std::vector<u8>& patched) {
        return word_at(patched, CallOffset) == DynamicCall &&
               word_at(patched, Dark + 0x74) == DarkThreshold &&
               !FileSys::OotHighRefreshScaleOffset(std::span{patched}.subspan(Code));
    };
    struct RestoreSetting {
        ~RestoreSetting() {
            Settings::values.oot_high_refresh.SetValue(false);
        }
    } restore;

    PatchManagerTestContext context;
    auto nso = MakeOcarinaMain();

    SECTION("Off: Dark's routine, with the water ripple built-in next to it, high refresh not listed") {
        Settings::values.oot_high_refresh.SetValue(false);
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(dark_only(patched));
        expect_gap_zero(patched, Dark + 0x78, Routine);
        // v5.8: the ripple accumulator and its hook with Dark's mode too (zortos293/Lichii#1180).
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == 0x14EFFE26); // b 0x3c46e90
        REQUIRE(word_at(patched, Routine + 0x90) == 0xB00092C9);          // adrp x9, 0x4e9f000
        REQUIRE(word_at(patched, Routine + 0x10C) == 0x171001BC);         // b 0x475fc
        for (size_t offset = 0x110; offset < 0x200; offset += 4) {
            REQUIRE(word_at(patched, Routine + offset) == 0);
        }
        REQUIRE_FALSE(listed(context.Manager(), HighRefresh));
        REQUIRE(listed(context.Manager(), DynamicFps));
        REQUIRE(listed(context.Manager(), "Water ripples at any frame rate (built-in)"));
    }

    SECTION("Off, with the water ripple built-in off in Add-Ons: Dark's routine alone, as before") {
        Settings::values.oot_high_refresh.SetValue(false);
        Settings::values.disabled_addons[OcarinaTitleId] = {
            std::string{"Water ripples at any frame rate (built-in)"}};
        const auto patched = context.Manager().PatchNSO(nso, "main");
        Settings::values.disabled_addons.erase(OcarinaTitleId);
        REQUIRE(dark_only(patched));
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == WaterFcvtzs);
        expect_gap_zero(patched, Dark + 0x78, Routine + 0x200);
    }

    SECTION("On: the call to the high refresh routine, with its speed scale word") {
        Settings::values.oot_high_refresh.SetValue(true);
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(word_at(patched, CallOffset) == HighRefreshCall);
        // Dark's routine stays, nothing after it (another mod's spot).
        REQUIRE(word_at(patched, Dark + 0x74) == DarkThreshold);
        expect_gap_zero(patched, Dark + 0x78, Routine);
        REQUIRE(word_at(patched, Routine + 0x18) == 0xB40002CC); // cbz x12, +0x70 (Dark's)
        REQUIRE(word_at(patched, Routine + 0x6C) == 0x14000002); // b +0x74
        REQUIRE(word_at(patched, Routine + 0x70) == 0xD65F03C0); // ret
        REQUIRE(word_at(patched, Routine + 0x78) == 0x1E230848); // fmul s8, s2, s3
        REQUIRE(word_at(patched, Routine + 0x84) == 0x40C00000); // 6.0f
        REQUIRE(word_at(patched, Routine + 0x88) == 0x3F800000); // 1.0f
        REQUIRE(word_at(patched, Routine + 0x8C) == 0);
        // The water ripple accumulator (zortos293/Lichii#1152) right after it, and its hook.
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == 0x14EFFE26); // b 0x3c46e90
        REQUIRE(word_at(patched, Routine + 0x90) == 0xB00092C9);  // adrp x9, 0x4e9f000
        REQUIRE(word_at(patched, Routine + 0x94) == 0x91388129);  // add x9, x9, #0xe20
        REQUIRE(word_at(patched, Routine + 0x108) == 0x1E380029); // fcvtzs w9, s1
        REQUIRE(word_at(patched, Routine + 0x10C) == 0x171001BC); // b 0x475fc
        for (size_t offset = 0x110; offset < 0x200; offset += 4) {
            REQUIRE(word_at(patched, Routine + offset) == 0);
        }
        REQUIRE(FileSys::OotHighRefreshScaleOffset(std::span{patched}.subspan(Code)) ==
                std::optional<size_t>{0x3C46E88});
        REQUIRE(listed(context.Manager(), HighRefresh));
    }

    SECTION("On, with another mod's code right after Dark's routine: still on") {
        // The Hyrule Cheat Menu writes main+0x3C46280..0x3C46398 (2026-11-10).
        Settings::values.oot_high_refresh.SetValue(true);
        nso[Dark + 0x80] = 1;
        nso[Dark + 0x190] = 1;
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(word_at(patched, CallOffset) == HighRefreshCall);
        REQUIRE(patched[Dark + 0x80] == 1);
        REQUIRE(patched[Dark + 0x190] == 1);
        REQUIRE(FileSys::OotHighRefreshScaleOffset(std::span{patched}.subspan(Code)) ==
                std::optional<size_t>{0x3C46E88});
    }

    SECTION("On, with an accumulator word in use: Dark's routine") {
        Settings::values.oot_high_refresh.SetValue(true);
        nso[Routine + 0x10C] = 1;
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(word_at(patched, CallOffset) == DynamicCall);
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == WaterFcvtzs);
        REQUIRE(patched[Routine + 0x10C] == 1);
        REQUIRE_FALSE(FileSys::OotHighRefreshScaleOffset(std::span{patched}.subspan(Code)));
    }

    SECTION("On, with another word at the water hook: Dark's routine, no ripple built-in") {
        Settings::values.oot_high_refresh.SetValue(true);
        std::memcpy(nso.data() + Code + OcarinaWaterHook, &NopInstruction, sizeof(u32));
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(dark_only(patched));
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == NopInstruction);
        REQUIRE(word_at(patched, Routine + 0x90) == 0);
    }

    SECTION("On, with the state past the end of BSS: Dark's routine") {
        Settings::values.oot_high_refresh.SetValue(true);
        Loader::NSOHeader header;
        std::memcpy(&header, nso.data(), sizeof(header));
        header.segments[2].bss_size = 0x4E9FE00 + 0x9F - 0x4100000; // one byte short
        std::memcpy(nso.data(), &header, sizeof(header));
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(dark_only(patched));
        REQUIRE(word_at(patched, Code + OcarinaWaterHook) == WaterFcvtzs);
    }

    SECTION("On, but off in Add-Ons: Dark's routine") {
        Settings::values.oot_high_refresh.SetValue(true);
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{HighRefresh}};
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(dark_only(patched));
    }
}

TEST_CASE("High refresh rate mode switches live between the two boot layouts",
          "[patch_manager]") {
    using FileSys::OotHighRefreshLayout;
    constexpr size_t Code = sizeof(Loader::NSOHeader);
    constexpr size_t Scale = 0x88; // the speed scale word, in the cave
    struct RestoreSetting {
        ~RestoreSetting() {
            Settings::values.oot_high_refresh.SetValue(false);
        }
    } restore;

    // What boots with the setting off and on.
    PatchManagerTestContext context;
    const auto nso = MakeOcarinaMain();
    Settings::values.oot_high_refresh.SetValue(false);
    const auto off = context.Manager().PatchNSO(nso, "main");
    Settings::values.oot_high_refresh.SetValue(true);
    const auto on = context.Manager().PatchNSO(nso, "main");
    REQUIRE_FALSE(std::ranges::equal(off, on));
    const auto off_parts = FileSys::ReadOotHighRefreshCode(std::span<const u8>{off}.subspan(Code));
    const auto on_parts = FileSys::ReadOotHighRefreshCode(std::span<const u8>{on}.subspan(Code));
    REQUIRE(off_parts);
    REQUIRE(on_parts);
    REQUIRE(FileSys::GetOotHighRefreshLayout(*off_parts) == OotHighRefreshLayout::Off);
    REQUIRE(FileSys::GetOotHighRefreshLayout(*on_parts) == OotHighRefreshLayout::On);

    SECTION("Switching writes exactly what boots in the other mode, both ways") {
        auto module = off;
        const auto switched_on = FileSys::SwitchOotHighRefreshCode(*off_parts, true);
        REQUIRE(switched_on);
        WriteOotHighRefreshCode(std::span{module}.subspan(Code), *switched_on);
        ExpectEqual(module, on);
        REQUIRE(FileSys::OotHighRefreshScaleOffset(std::span<const u8>{module}.subspan(Code)) ==
                std::optional<size_t>{0x3C46E88});

        const auto switched_off = FileSys::SwitchOotHighRefreshCode(*switched_on, false);
        REQUIRE(switched_off);
        WriteOotHighRefreshCode(std::span{module}.subspan(Code), *switched_off);
        ExpectEqual(module, off);
        REQUIRE_FALSE(FileSys::OotHighRefreshScaleOffset(std::span<const u8>{module}.subspan(Code)));
    }

    SECTION("Switching to the mode it is in changes nothing, the speed scale word kept") {
        auto turbo = *on_parts;
        constexpr std::array<u8, 4> TwoPointZero{0x00, 0x00, 0x00, 0x40}; // 2.0f
        std::ranges::copy(TwoPointZero, turbo.cave.begin() + Scale);
        REQUIRE(FileSys::GetOotHighRefreshLayout(turbo) == OotHighRefreshLayout::On);
        const auto still_on = FileSys::SwitchOotHighRefreshCode(turbo, true);
        REQUIRE(still_on);
        REQUIRE(still_on->cave == turbo.cave);
        REQUIRE(still_on->water == turbo.water);

        const auto still_off = FileSys::SwitchOotHighRefreshCode(*off_parts, false);
        REQUIRE(still_off);
        REQUIRE(still_off->cave == off_parts->cave);
        REQUIRE(still_off->water == off_parts->water);

        // Off from any speed scale: Dark's cave, the scale word zero again.
        const auto from_turbo = FileSys::SwitchOotHighRefreshCode(turbo, false);
        REQUIRE(from_turbo);
        REQUIRE(from_turbo->cave == off_parts->cave);
        REQUIRE(from_turbo->water == off_parts->water);
    }

    SECTION("Code in neither mode is never switched") {
        const auto refused = [](const FileSys::OotHighRefreshCode& parts) {
            return FileSys::GetOotHighRefreshLayout(parts) == OotHighRefreshLayout::Unknown &&
                   !FileSys::SwitchOotHighRefreshCode(parts, true) &&
                   !FileSys::SwitchOotHighRefreshCode(parts, false);
        };
        auto used_cave = *off_parts; // a word of our cave in use (another mod's code)
        used_cave.cave[0x100] = 1;
        REQUIRE(refused(used_cave));
        auto changed_dark = *on_parts; // Dark's routine changed: switching off would call it
        changed_dark.dark[0x40] ^= 1;
        REQUIRE(refused(changed_dark));
        auto stray_word = *off_parts; // a cave that is neither mode's (the scale word may be anything)
        stray_word.cave[0x20] ^= 1;
        REQUIRE(refused(stray_word));
        auto no_call = *off_parts; // Dark's routine off in Add-Ons: the game's own fdiv
        no_call.call = {0x28, 0x18, 0x20, 0x1E};
        REQUIRE(refused(no_call));
        auto modded_water = *on_parts; // a mod's word at the water hook
        modded_water.water = {0x1F, 0x20, 0x03, 0xD5};
        REQUIRE(refused(modded_water));
        auto half_switched = *on_parts; // the high refresh call without its water hook
        half_switched.water = {0x09, 0x00, 0x38, 0x1E}; // the game's fcvtzs w9, s0 (WATER_ORIGINAL)
        REQUIRE(refused(half_switched));
        auto half_written = *off_parts; // only the first half of the cave present
        std::ranges::fill(half_written.cave.begin() + 0x80, half_written.cave.end(), u8{0});
        REQUIRE(refused(half_written));
        auto changed_routine = *on_parts;
        changed_routine.cave[0x5C] ^= 1;
        REQUIRE(refused(changed_routine));
        auto changed_accumulator = *on_parts;
        changed_accumulator.cave[0x10C] ^= 1;
        REQUIRE(refused(changed_accumulator));
    }

    SECTION("A module too small for the cave holds neither") {
        const auto cut = std::span<const u8>{off}.subspan(Code, OcarinaCave + 0x10F);
        REQUIRE_FALSE(FileSys::ReadOotHighRefreshCode(cut));
        REQUIRE_FALSE(FileSys::OotHighRefreshScaleOffset(cut));
    }

    SECTION("Its state (Dark's timer, the ripple slots, 0x4E9FE00..0x4EA00C0) must lie in BSS") {
        REQUIRE(FileSys::OotHighRefreshStateInBss(0x4100000, 0x5200000));
        REQUIRE(FileSys::OotHighRefreshStateInBss(0x4E9FE00, 0x4EA00C0));
        REQUIRE_FALSE(FileSys::OotHighRefreshStateInBss(0x4E9FE01, 0x5200000));
        REQUIRE_FALSE(FileSys::OotHighRefreshStateInBss(0x4100000, 0x4E9FEFF));
        REQUIRE_FALSE(FileSys::OotHighRefreshStateInBss(0, 0));
    }
}

TEST_CASE("Dark's standalone Dynamic FPS mod is skipped and hidden for the built-in",
          "[patch_manager]") {
    constexpr std::string_view DynamicFps = "Dynamic FPS by Dark (built-in)";
    constexpr size_t Code = sizeof(Loader::NSOHeader);
    constexpr u32 CallOffset = Code + 0x6E6CC4;
    constexpr std::array<u8, 4> DynamicCall{0x4F, 0x7D, 0xD5, 0x94};
    constexpr std::array<u8, 4> Nop{0x1F, 0x20, 0x03, 0xD5};
    const auto ips_name = fmt::format("{}.ips", OcarinaBuildIdHex);
    const auto listed = [](const FileSys::PatchManager& manager, std::string_view name) {
        const auto patches = manager.GetPatches();
        return std::ranges::find(patches, name, &FileSys::Patch::name) != patches.end();
    };

    PatchManagerTestContext context;
    auto nso = MakeOcarinaMain();
    std::memcpy(nso.data() + PatchOffset, &TbzInstruction, sizeof(TbzInstruction));
    // What boots without mods: Dark's call and Max Resolution's branch.
    const auto builtin = context.Manager().PatchNSO(nso, "main");
    REQUIRE(std::equal(DynamicCall.begin(), DynamicCall.end(), builtin.begin() + CallOffset));
    REQUIRE(WordAt(builtin) == BranchInstruction);

    SECTION("An IPS writing Dark's call: skipped, not listed, the built-in applied") {
        const auto ips = MakeIps(false, CallOffset, Nop);
        const auto path = context.AddModFile("Dynamic FPS", ips_name, ips);
        ExpectEqual(context.Manager().PatchNSO(nso, "main"), builtin);
        REQUIRE_FALSE(listed(context.Manager(), "Dynamic FPS"));
        REQUIRE(listed(context.Manager(), DynamicFps));
        // The player's files stay as they were.
        REQUIRE(std::filesystem::file_size(path) == ips.size());
    }

    SECTION("An IPSwitch patch writing into Dark's routine: the same") {
        constexpr std::string_view Pchtxt = "@nsobid-E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348\n"
                                            "@flag offset_shift 0x100\n"
                                            "@enabled\n"
                                            "03C46274 00000000\n"
                                            "@stop\n";
        context.AddModFile("Dynamic FPS 60", "dynamic_fps.pchtxt",
                           {reinterpret_cast<const u8*>(Pchtxt.data()), Pchtxt.size()});
        ExpectEqual(context.Manager().PatchNSO(nso, "main"), builtin);
        REQUIRE_FALSE(listed(context.Manager(), "Dynamic FPS 60"));
        REQUIRE(listed(context.Manager(), DynamicFps));
    }

    SECTION("A mod patching an unrelated offset still applies and is listed") {
        constexpr u32 Unrelated = Code + 0x1000;
        context.AddModFile("Other mod", ips_name, MakeIps(true, Unrelated, Nop));
        auto expected = builtin;
        std::ranges::copy(Nop, expected.begin() + Unrelated);
        ExpectEqual(context.Manager().PatchNSO(nso, "main"), expected);
        REQUIRE(listed(context.Manager(), "Other mod"));
    }

    SECTION("Next to an unrelated mod, only the Dynamic FPS mod is left out") {
        constexpr u32 Unrelated = Code + 0x1000;
        context.AddModFile("Dynamic FPS", ips_name, MakeIps(false, CallOffset, Nop));
        context.AddModFile("Other mod", ips_name, MakeIps(true, Unrelated, Nop));
        auto expected = builtin;
        std::ranges::copy(Nop, expected.begin() + Unrelated);
        ExpectEqual(context.Manager().PatchNSO(nso, "main"), expected);
        REQUIRE(listed(context.Manager(), "Other mod"));
        REQUIRE_FALSE(listed(context.Manager(), "Dynamic FPS"));
    }

    SECTION("A mod patching Max Resolution's word still wins over that built-in") {
        context.AddModFile("Other mod", ips_name, MakeIps(true, PatchOffset, Nop));
        const auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(WordAt(patched) == NopInstruction);
        REQUIRE(std::equal(DynamicCall.begin(), DynamicCall.end(), patched.begin() + CallOffset));
        REQUIRE(listed(context.Manager(), "Other mod"));
    }
}


// Texture Pool x2 (built-in): RsMan-Dev's patch from zortos293/Lichii#1313; it doubles the
// initial graphics pool so full-size render targets don't outgrow it in the guard house.
constexpr std::string_view TexturePool = "Texture Pool x2 (built-in)";

TEST_CASE("Built-in Texture Pool x2 doubles the allocator's initial pool size", "[patch_manager]") {
    constexpr size_t CallOffset = sizeof(Loader::NSOHeader) + 0xC2A36C;
    constexpr size_t RoutineOffset = sizeof(Loader::NSOHeader) + 0x3C462C0;
    constexpr u32 StpInstruction = 0xA90127E8;
    constexpr u32 CallInstruction = 0x94C06FD5;
    PatchManagerTestContext context;
    const auto manager = context.Manager();

    auto nso = MakeDynamicMain();
    std::memcpy(nso.data() + CallOffset, &StpInstruction, sizeof(StpInstruction));

    auto patched = manager.PatchNSO(nso, "main");
    REQUIRE(WordAt(patched, CallOffset) == CallInstruction);
    REQUIRE(Common::HexToString(std::span{patched}.subspan(RoutineOffset, 12)) ==
            "08F97FD3E82701A9C0035FD6");
    REQUIRE(WordAt(patched, RoutineOffset + 12) == 0);

    // Toggle off in Add-Ons: the original word is restored and the cave stays zeroed.
    Settings::values.disabled_addons[OcarinaTitleId] = {std::string{TexturePool}};
    patched = manager.PatchNSO(nso, "main");
    REQUIRE(WordAt(patched, CallOffset) == StpInstruction);
    REQUIRE(WordAt(patched, RoutineOffset) == 0);

    // Wrong original word: the built-in refuses to apply, the module is left alone.
    Settings::values.disabled_addons.erase(OcarinaTitleId);
    std::memcpy(nso.data() + CallOffset, &NopInstruction, sizeof(NopInstruction));
    patched = manager.PatchNSO(nso, "main");
    REQUIRE(WordAt(patched, CallOffset) == NopInstruction);
    REQUIRE(WordAt(patched, RoutineOffset) == 0);
}

// FPS Unlock's sound gate (zortos293/Lichii#1660): the sound manager job (main+0x415300) is
// branched to a gate at main+0x3C46800 accumulated at Dark's timer state +0xA0; the job's step
// load (main+0x415334) calls SOUND_FEED at main+0x3C4687C (this fork's feed location; upstream's
// is 0x3C46280) with max(step, 1.0). The gate is in the "FPS Unlock fixes (built-in)", not in
// Dark's pair.
TEST_CASE("Built-in FPS Unlock fixes install the sound gate", "[patch_manager]") {
    PatchManagerTestContext context;

    const auto gate_hex = [](const std::vector<u8>& nso) {
        return Common::HexToString(std::span{nso}.subspan(SoundGateOffset, SoundGate.size() / 2));
    };

    SECTION("Default: the fixes built-in installs the gate and Dark's two") {
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        auto nso = MakeOcarinaMain();
        auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(WordAt(patched, SoundEntryOffset) == SoundEntryBranchInstruction);
        REQUIRE(WordAt(patched, SoundStepOffset) == SoundStepCallInstruction);
        REQUIRE(gate_hex(patched) == SoundGate);
        REQUIRE(Common::HexToString(std::span{patched}.subspan(SoundFeedOffset, SoundFeed.size() / 2)) ==
                SoundFeed);
        // Dark's pair still lands (they're a separate built-in).
        REQUIRE(WordAt(patched, sizeof(Loader::NSOHeader) + 0x6E6CC4) == 0x94D57D4F);
    }

    SECTION("Dark's built-in alone does not install the gate") {
        // "FPS Unlock fixes" off in Add-Ons, everything else default.
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution},
                                                            std::string{FpsUnlock}};
        auto nso = MakeOcarinaMain();
        auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(WordAt(patched, SoundEntryOffset) == SoundEntryOriginalInstruction);
        REQUIRE(WordAt(patched, SoundStepOffset) == SoundStepOriginalInstruction);
        REQUIRE(WordAt(patched, SoundGateOffset) == 0);
        REQUIRE(WordAt(patched, SoundFeedOffset) == 0);
    }

    SECTION("Wrong original at the sound job's entry skips the whole fixes built-in") {
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        auto nso = MakeOcarinaMain();
        nso[SoundEntryOffset] ^= 0x01;
        auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(WordAt(patched, SoundEntryOffset) != SoundEntryBranchInstruction);
        REQUIRE(WordAt(patched, SoundStepOffset) == SoundStepOriginalInstruction);
        REQUIRE(WordAt(patched, SoundGateOffset) == 0);
        REQUIRE(WordAt(patched, SoundFeedOffset) == 0);
        // Dark's pair is unaffected (own built-in).
        REQUIRE(WordAt(patched, sizeof(Loader::NSOHeader) + 0x6E6CC4) == 0x94D57D4F);
    }

    SECTION("Wrong original at the sound job's step load skips the whole fixes built-in") {
        Settings::values.disabled_addons[OcarinaTitleId] = {std::string{MaxResolution}};
        auto nso = MakeOcarinaMain();
        nso[SoundStepOffset] ^= 0x01;
        auto patched = context.Manager().PatchNSO(nso, "main");
        REQUIRE(WordAt(patched, SoundEntryOffset) == SoundEntryOriginalInstruction);
        REQUIRE(WordAt(patched, SoundStepOffset) != SoundStepCallInstruction);
        REQUIRE(WordAt(patched, SoundGateOffset) == 0);
        REQUIRE(WordAt(patched, SoundFeedOffset) == 0);
    }
}