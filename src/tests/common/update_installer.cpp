// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "frontend_common/update_installer.h"

namespace fs = std::filesystem;

namespace {

const fs::path Exe{"lichii.exe"};

class TempDir {
public:
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("lichii-update-test-" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    fs::path path;
};

void Write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream{file, std::ios::binary | std::ios::trunc} << text;
}

std::string Read(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, {}};
}

// An install of version 4 with user data, and the unpacked version 5 package beside it.
struct Install {
    TempDir dir;
    fs::path root = dir.path / "Fritchii";
    fs::path package = root / UpdateInstaller::StagingName / "unpacked" / "Fritchii-v5";

    Install() {
        Write(root / "lichii.exe", "v4");
        Write(root / "Qt6Core.dll", "v4");
        Write(root / "plugins" / "a.dll", "v4");
        Write(root / "user" / "config" / "qt-config.ini", "settings");
        Write(root / "nx2_switches.txt", "switches");
        Write(root / "notes.txt", "mine");

        Write(package / "lichii.exe", "v5");
        Write(package / "Qt6Core.dll", "v5");
        Write(package / "plugins" / "a.dll", "v5");
        Write(package / "plugins" / "new.dll", "v5");
        Write(package / "user" / "config" / "qt-config.ini", "replaced");
        Write(package / "nx2_switches.txt", "replaced");
    }

    void CheckUserData() const {
        CHECK(Read(root / "user" / "config" / "qt-config.ini") == "settings");
        CHECK(Read(root / "nx2_switches.txt") == "switches");
        CHECK(Read(root / "notes.txt") == "mine");
    }
};

} // namespace

TEST_CASE("Staged and previous files sit beside the target", "[updater]") {
    const fs::path target = "/opt/lichii/lichii-linux-x86_64.AppImage";
    CHECK(UpdateInstaller::StagedPath(target) == "/opt/lichii/lichii-linux-x86_64.AppImage.new");
    CHECK(UpdateInstaller::PreviousPath(target) == "/opt/lichii/lichii-linux-x86_64.AppImage.prev");
}

TEST_CASE("Swap replaces the target and keeps one previous copy", "[updater]") {
    TempDir dir;
    const auto target = dir.path / "lichii.AppImage";
    const auto staged = UpdateInstaller::StagedPath(target);
    const auto previous = UpdateInstaller::PreviousPath(target);
    Write(target, "old");
    fs::permissions(target, fs::perms::owner_all | fs::perms::group_read);
    Write(staged, "new");
    Write(previous, "stale backup");

    std::error_code ec;
    REQUIRE(UpdateInstaller::CanReplace(target));
    REQUIRE(UpdateInstaller::Swap(staged, target, ec));
    CHECK_FALSE(ec);
    CHECK(Read(target) == "new");
    CHECK(Read(previous) == "old");
    CHECK_FALSE(fs::exists(staged));
    const auto permissions = fs::status(target).permissions();
    CHECK((permissions & fs::perms::owner_exec) != fs::perms::none);
    CHECK((permissions & fs::perms::group_read) != fs::perms::none);
}

TEST_CASE("Swap leaves the target untouched when the staged file is missing", "[updater]") {
    TempDir dir;
    const auto target = dir.path / "lichii.AppImage";
    Write(target, "old");

    std::error_code ec;
    CHECK_FALSE(UpdateInstaller::Swap(UpdateInstaller::StagedPath(target), target, ec));
    CHECK(ec);
    CHECK(Read(target) == "old");
    CHECK_FALSE(fs::exists(UpdateInstaller::PreviousPath(target)));
}

TEST_CASE("Rollback exchanges the target with its previous copy", "[updater]") {
    TempDir dir;
    const auto target = dir.path / "lichii.AppImage";
    const auto previous = UpdateInstaller::PreviousPath(target);
    Write(target, "new");
    Write(previous, "old");

    std::error_code ec;
    REQUIRE(UpdateInstaller::Rollback(target, ec));
    CHECK(Read(target) == "old");
    CHECK(Read(previous) == "new");
    REQUIRE(UpdateInstaller::Rollback(target, ec));
    CHECK(Read(target) == "new");
    CHECK(Read(previous) == "old");
    CHECK_FALSE(fs::exists(UpdateInstaller::StagedPath(target)));
}

TEST_CASE("Rollback without a previous copy fails and changes nothing", "[updater]") {
    TempDir dir;
    const auto target = dir.path / "lichii.AppImage";
    Write(target, "current");

    std::error_code ec;
    CHECK_FALSE(UpdateInstaller::Rollback(target, ec));
    CHECK(ec);
    CHECK(Read(target) == "current");
}

TEST_CASE("Only an existing file in a writable folder can be replaced", "[updater]") {
    TempDir dir;
    const auto target = dir.path / "lichii.AppImage";
    CHECK_FALSE(UpdateInstaller::CanReplace(target));
    CHECK_FALSE(UpdateInstaller::CanReplace(dir.path));
    Write(target, "x");
    Write(dir.path / ".lichii-update-write-test", "kept");
    CHECK(UpdateInstaller::CanReplace(target));
    CHECK(Read(dir.path / ".lichii-update-write-test") == "kept");
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator{dir.path}) {
        ++count;
    }
    CHECK(count == 2);
}

TEST_CASE("Only one holder at a time gets the lock", "[updater]") {
    TempDir dir;
    const auto path = UpdateInstaller::LockPath(dir.path / "lichii.AppImage");
    CHECK(path == dir.path / "lichii.AppImage.lock");
    {
        const UpdateInstaller::Lock first{path};
        CHECK(first.Held());
        const UpdateInstaller::Lock second{path};
        CHECK_FALSE(second.Held());
    }
    const UpdateInstaller::Lock third{path};
    CHECK(third.Held());
}

namespace {

void Put(const fs::path& root, const std::string& relative, const std::string& content) {
    fs::create_directories((root / relative).parent_path());
    Write(root / relative, content);
}

std::set<std::string> Files(const fs::path& root) {
    std::set<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            files.insert(fs::relative(entry.path(), root).generic_string());
        }
    }
    return files;
}

} // namespace

TEST_CASE("Install replaces files, adds new ones and keeps the old ones as a backup", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    const auto staging = install / ".update-staging";
    Put(install, "lichii.exe", "old exe");
    Put(install, "Qt6Core.dll", "old qt");
    Put(install, "plugins/a.dll", "old plugin");
    Put(install, "user/config/qt-config.ini", "my settings");
    Put(staging, "lichii.exe", "new exe");
    Put(staging, "Qt6Core.dll", "new qt");
    Put(staging, "plugins/b.dll", "new plugin");
    Put(staging, "user/config/qt-config.ini", "must not be installed");

    std::error_code ec;
    REQUIRE(UpdateInstaller::Install(staging, install, ec));

    CHECK(Read(install / "lichii.exe") == "new exe");
    CHECK(Read(install / "Qt6Core.dll") == "new qt");
    CHECK(Read(install / "plugins/b.dll") == "new plugin");
    CHECK(Read(install / "plugins/a.dll") == "old plugin");
    CHECK(Read(install / "user/config/qt-config.ini") == "my settings");
    CHECK_FALSE(fs::exists(staging));
    const auto backup = install / ".update-backup";
    CHECK(Read(backup / "lichii.exe") == "old exe");
    CHECK(Read(backup / "Qt6Core.dll") == "old qt");
    CHECK_FALSE(fs::exists(backup / "user"));
    CHECK(Files(install / "user") == std::set<std::string>{"config/qt-config.ini"});
    CHECK(Read(install / "install-manifest.txt") ==
          "Qt6Core.dll\nlichii.exe\nplugins/b.dll\n");
    CHECK_FALSE(fs::exists(install / ".update-backup.tmp"));
}

TEST_CASE("A later update removes the files the new version dropped", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    Put(install, "lichii.exe", "v1");
    Put(install, "old.dll", "v1 only");
    Put(install, "keep.dll", "v1");
    Put(install, "user/data.bin", "mine");
    std::error_code ec;

    Put(install, ".update-staging/lichii.exe", "v2");
    Put(install, ".update-staging/old.dll", "v2");
    Put(install, ".update-staging/keep.dll", "v2");
    REQUIRE(UpdateInstaller::Install(install / ".update-staging", install, ec));

    Put(install, ".update-staging/lichii.exe", "v3");
    Put(install, ".update-staging/keep.dll", "v3");
    REQUIRE(UpdateInstaller::Install(install / ".update-staging", install, ec));

    CHECK(Files(install) ==
          std::set<std::string>{"lichii.exe", "keep.dll", "install-manifest.txt",
                                ".update-backup/lichii.exe", ".update-backup/keep.dll",
                                ".update-backup/old.dll", ".update-backup/install-manifest.txt",
                                "user/data.bin"});
    CHECK(Read(install / "lichii.exe") == "v3");
    CHECK(Read(install / ".update-backup/old.dll") == "v2");
    CHECK(Read(install / ".update-backup/lichii.exe") == "v2");
}

TEST_CASE("Restoring the backup brings back the previous version and can be undone", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    Put(install, "lichii.exe", "v1");
    Put(install, "gone.dll", "v1 only");
    Put(install, "user/data.bin", "mine");
    std::error_code ec;

    Put(install, ".update-staging/lichii.exe", "v2");
    Put(install, ".update-staging/new.dll", "v2 only");
    REQUIRE(UpdateInstaller::Install(install / ".update-staging", install, ec));
    REQUIRE(fs::exists(install / "new.dll"));

    REQUIRE(UpdateInstaller::Install(install / ".update-backup", install, ec));
    CHECK(Read(install / "lichii.exe") == "v1");
    CHECK(Read(install / "gone.dll") == "v1 only");
    CHECK_FALSE(fs::exists(install / "new.dll"));
    CHECK(Read(install / "user/data.bin") == "mine");
    CHECK(Read(install / ".update-backup/lichii.exe") == "v2");
    CHECK(Read(install / ".update-backup/new.dll") == "v2 only");

    REQUIRE(UpdateInstaller::Install(install / ".update-backup", install, ec));
    CHECK(Read(install / "lichii.exe") == "v2");
    CHECK(Read(install / "new.dll") == "v2 only");
    CHECK_FALSE(fs::exists(install / "gone.dll"));
    CHECK(Read(install / "user/data.bin") == "mine");
}

#ifndef _WIN32
TEST_CASE("A failed install puts every file back", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    Put(install, "a.dll", "old a");
    Put(install, "locked/b.dll", "old b");
    Put(install, ".update-staging/a.dll", "new a");
    Put(install, ".update-staging/locked/b.dll", "new b");
    if (geteuid() == 0) {
        SKIP("root ignores directory permissions");
    }
    fs::permissions(install / "locked", fs::perms::owner_read | fs::perms::owner_exec);

    std::error_code ec;
    CHECK_FALSE(UpdateInstaller::Install(install / ".update-staging", install, ec));
    CHECK(ec);
    fs::permissions(install / "locked", fs::perms::owner_all);
    CHECK(Read(install / "a.dll") == "old a");
    CHECK(Read(install / "locked/b.dll") == "old b");
    CHECK_FALSE(fs::exists(install / ".update-backup"));
    CHECK_FALSE(fs::exists(install / ".update-backup.tmp"));
    CHECK_FALSE(fs::exists(install / "install-manifest.txt"));
}

TEST_CASE("A failed restore keeps the backup it restores from", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    const auto backup = install / ".update-backup";
    Put(install, "a.dll", "v2 a");
    Put(install, "locked/b.dll", "v2 b");
    Put(backup, "a.dll", "v1 a");
    Put(backup, "locked/b.dll", "v1 b");
    if (geteuid() == 0) {
        SKIP("root ignores directory permissions");
    }
    fs::permissions(install / "locked", fs::perms::owner_read | fs::perms::owner_exec);

    std::error_code ec;
    CHECK_FALSE(UpdateInstaller::Install(backup, install, ec));
    fs::permissions(install / "locked", fs::perms::owner_all);
    CHECK(Read(install / "a.dll") == "v2 a");
    CHECK(Read(install / "locked/b.dll") == "v2 b");
    CHECK(Read(backup / "a.dll") == "v1 a");
    CHECK(Read(backup / "locked/b.dll") == "v1 b");
}
#endif

TEST_CASE("The user folder is protected whatever its letter case", "[updater]") {
    TempDir dir;
    const auto install = dir.path / "app";
    const auto staging = install / ".update-staging";
    Put(install, "lichii.exe", "old exe");
    Put(install, "USER/data.bin", "mine");
    Put(staging, "lichii.exe", "new exe");
    Put(staging, "User/config.ini", "must not be installed");

    std::error_code ec;
    REQUIRE(UpdateInstaller::Install(staging, install, ec));
    CHECK(Read(install / "USER/data.bin") == "mine");
    CHECK_FALSE(fs::exists(install / "User/config.ini"));
    CHECK(Read(install / "install-manifest.txt") == "lichii.exe\n");
    CHECK(Files(install / ".update-backup") ==
          std::set<std::string>{"lichii.exe", "install-manifest.txt"});
}

TEST_CASE("Path list entries inside a folder are removed", "[updater]") {
    using UpdateInstaller::StripPathListEntries;
    CHECK(StripPathListEntries("/tmp/.mount_x/usr/lib:/usr/lib:/tmp/.mount_x:/opt/lib",
                               "/tmp/.mount_x") == "/usr/lib:/opt/lib");
    CHECK(StripPathListEntries("/tmp/.mount_xy/lib:/usr/lib", "/tmp/.mount_x") ==
          "/tmp/.mount_xy/lib:/usr/lib");
    CHECK(StripPathListEntries("", "/tmp/.mount_x").empty());
    CHECK(StripPathListEntries("/a::/b", "/tmp/.mount_x") == "/a:/b");
    CHECK(StripPathListEntries("/a:/b", "") == "/a:/b");
}

TEST_CASE("Updates replace only the package's files and keep one rollback copy", "[updater]") {
    Install install;
    std::error_code ec;
    REQUIRE(UpdateInstaller::Install(install.root, install.package, "4", Exe, ec) ==
            UpdateInstaller::Outcome::Done);
    CHECK_FALSE(ec);

    const auto old_dir = install.root / UpdateInstaller::OldName;
    CHECK(Read(install.root / "lichii.exe") == "v5");
    CHECK(Read(install.root / "Qt6Core.dll") == "v5");
    CHECK(Read(install.root / "plugins" / "new.dll") == "v5");
    CHECK(Read(old_dir / "lichii.exe") == "v4");
    CHECK(Read(old_dir / "plugins" / "a.dll") == "v4");
    CHECK_FALSE(fs::exists(old_dir / "plugins" / "new.dll"));
    CHECK_FALSE(fs::exists(old_dir / "user"));
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::JournalName));
    CHECK(UpdateInstaller::PreviousVersion(install.root, Exe) == "4");
    install.CheckUserData();

    // Rolling back restores version 4, and rolling back again restores version 5.
    REQUIRE(UpdateInstaller::RollBack(install.root, "5", Exe, ec) ==
            UpdateInstaller::Outcome::Done);
    CHECK(Read(install.root / "lichii.exe") == "v4");
    CHECK(Read(install.root / "plugins" / "a.dll") == "v4");
    CHECK_FALSE(fs::exists(install.root / "plugins" / "new.dll"));
    CHECK(UpdateInstaller::PreviousVersion(install.root, Exe) == "5");
    install.CheckUserData();

    REQUIRE(UpdateInstaller::RollBack(install.root, "4", Exe, ec) ==
            UpdateInstaller::Outcome::Done);
    CHECK(Read(install.root / "lichii.exe") == "v5");
    CHECK(Read(install.root / "plugins" / "new.dll") == "v5");
    CHECK(UpdateInstaller::PreviousVersion(install.root, Exe) == "4");
    install.CheckUserData();

    UpdateInstaller::CleanStaging(install.root);
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::StagingName));
}

TEST_CASE("A failed swap puts every file and the old rollback copy back", "[updater]") {
    Install install;
    Write(install.root / UpdateInstaller::OldName / "lichii.exe", "v3");
    Write(install.root / UpdateInstaller::OldName / UpdateInstaller::VersionName, "3");
    // A folder where the package has a file: replacing it fails after Qt6Core.dll was swapped.
    fs::create_directories(install.root / "Qt6Gui.dll");
    Write(install.package / "Qt6Gui.dll", "v5");

    std::error_code ec;
    CHECK(UpdateInstaller::Install(install.root, install.package, "4", Exe, ec) ==
          UpdateInstaller::Outcome::Unchanged);
    CHECK(ec);
    CHECK(Read(install.root / "lichii.exe") == "v4");
    CHECK(Read(install.root / "Qt6Core.dll") == "v4");
    CHECK(Read(install.package / "Qt6Core.dll") == "v5");
    CHECK(fs::is_directory(install.root / "Qt6Gui.dll"));
    CHECK(UpdateInstaller::PreviousVersion(install.root, Exe) == "3");
    CHECK(Read(install.root / UpdateInstaller::OldName / "lichii.exe") == "v3");
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::JournalName));
    install.CheckUserData();
}

TEST_CASE("A package without lichii.exe is refused before anything moves", "[updater]") {
    Install install;
    fs::remove(install.package / "lichii.exe");
    std::error_code ec;
    CHECK(UpdateInstaller::Install(install.root, install.package, "4", Exe, ec) ==
          UpdateInstaller::Outcome::Unchanged);
    CHECK(Read(install.root / "Qt6Core.dll") == "v4");
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::JournalName));
    CHECK_FALSE(UpdateInstaller::PreviousVersion(install.root, Exe));
}

TEST_CASE("A swap interrupted by a crash is undone at the next start", "[updater]") {
    Install install;
    const auto& root = install.root;
    const auto old_dir = root / UpdateInstaller::OldName;
    const auto restore = root / UpdateInstaller::StagingName / "previous-old";
    // The state right after lichii.exe was swapped: the earlier rollback copy is aside in
    // `restore`, the old exe is in OldName, and the journal announces a Qt6Core.dll move that
    // never happened.
    Write(restore / "lichii.exe", "v3");
    fs::create_directories(old_dir);
    fs::rename(root / "lichii.exe", old_dir / "lichii.exe");
    fs::rename(install.package / "lichii.exe", root / "lichii.exe");
    Write(root / UpdateInstaller::JournalName,
          "package update-staging/unpacked/Fritchii-v5\nrestore update-staging/previous-old\n"
          "old lichii.exe\nnew lichii.exe\nold Qt6Core.dll\n");

    std::error_code ec;
    CHECK(UpdateInstaller::Recover(root, ec) == UpdateInstaller::Recovery::Undone);
    CHECK(Read(root / "lichii.exe") == "v4");
    CHECK(Read(root / "Qt6Core.dll") == "v4");
    CHECK(Read(install.package / "lichii.exe") == "v5");
    CHECK(Read(old_dir / "lichii.exe") == "v3");
    CHECK_FALSE(fs::exists(restore));
    CHECK_FALSE(fs::exists(root / UpdateInstaller::JournalName));
    CHECK(UpdateInstaller::Recover(root, ec) == UpdateInstaller::Recovery::None);
    install.CheckUserData();
}

TEST_CASE("A committed swap only has its cleanup finished", "[updater]") {
    Install install;
    const auto restore = install.root / UpdateInstaller::StagingName / "previous-old";
    Write(restore / "lichii.exe", "v3");
    Write(install.root / UpdateInstaller::JournalName,
          "package update-staging/unpacked/Fritchii-v5\nrestore update-staging/previous-old\n"
          "old lichii.exe\nnew lichii.exe\ncommit \n");
    std::error_code ec;
    CHECK(UpdateInstaller::Recover(install.root, ec) == UpdateInstaller::Recovery::Completed);
    CHECK_FALSE(fs::exists(restore));
    CHECK(Read(install.root / "lichii.exe") == "v4");
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::JournalName));
}

TEST_CASE("Journals that point outside the install are not followed", "[updater]") {
    Install install;
    Write(install.root / UpdateInstaller::JournalName,
          "package ../elsewhere\nrestore update-staging/previous-old\nold ../outside.txt\n"
          "new lichii.exe\n");
    std::error_code ec;
    CHECK(UpdateInstaller::Recover(install.root, ec) == UpdateInstaller::Recovery::Failed);
    CHECK(Read(install.root / "lichii.exe") == "v4");
    CHECK(fs::exists(install.root / UpdateInstaller::JournalName));
}

TEST_CASE("A journal cut short in its header is dropped", "[updater]") {
    Install install;
    Write(install.root / UpdateInstaller::JournalName, "package update-staging/unpacked/Fri");
    std::error_code ec;
    CHECK(UpdateInstaller::Recover(install.root, ec) == UpdateInstaller::Recovery::None);
    CHECK_FALSE(fs::exists(install.root / UpdateInstaller::JournalName));
    CHECK(Read(install.root / "lichii.exe") == "v4");
}

TEST_CASE("User data and the updater's own files are protected", "[updater]") {
    for (const auto* path : {"user", "user/config/qt-config.ini", "USER/nand/x", "nx2_switches.txt",
                             "NX2_Switches.TXT", "update-staging/x", "update-old/lichii.exe",
                             "update-journal.txt", "update.lock", "../lichii.exe", "./x", ""}) {
        CHECK(UpdateInstaller::IsProtected(fs::path{path}));
    }
    for (const auto* path : {"lichii.exe", "plugins/a.dll", "users.txt", "plugins/nx2_switches.txt",
                             "plugins/user/a.dll"}) {
        CHECK_FALSE(UpdateInstaller::IsProtected(fs::path{path}));
    }
}

TEST_CASE("Restarts never target the updater's own folders", "[updater]") {
    CHECK(UpdateInstaller::IsUpdaterFolder(fs::path{"C:/Games/Fritchii/update-old"}));
    CHECK(UpdateInstaller::IsUpdaterFolder(fs::path{"C:/Games/Fritchii/Update-Old"}));
    CHECK(UpdateInstaller::IsUpdaterFolder(
        fs::path{"C:/Games/Fritchii/update-staging/unpacked/Fritchii-v5"}));
    CHECK_FALSE(UpdateInstaller::IsUpdaterFolder(fs::path{"C:/Games/Fritchii"}));
    CHECK_FALSE(UpdateInstaller::IsUpdaterFolder(fs::path{"C:/Games/update-older"}));
#if defined(_WIN32) || defined(__linux__)
    CHECK_FALSE(UpdateInstaller::RunningExePath().empty());
#endif
}

TEST_CASE("The package root is the zip's top folder or the unpacked folder", "[updater]") {
    TempDir dir;
    Write(dir.path / "one" / "Fritchii-v5" / "lichii.exe", "v5");
    CHECK(UpdateInstaller::FindPackageRoot(dir.path / "one", Exe) ==
          dir.path / "one" / "Fritchii-v5");
    Write(dir.path / "flat" / "lichii.exe", "v5");
    CHECK(UpdateInstaller::FindPackageRoot(dir.path / "flat", Exe) == dir.path / "flat");
    Write(dir.path / "two" / "a" / "lichii.exe", "v5");
    Write(dir.path / "two" / "b" / "lichii.exe", "v5");
    CHECK_FALSE(UpdateInstaller::FindPackageRoot(dir.path / "two", Exe));
    Write(dir.path / "none" / "a" / "readme.txt", "");
    CHECK_FALSE(UpdateInstaller::FindPackageRoot(dir.path / "none", Exe));
    CHECK(UpdateInstaller::IsWritable(dir.path));
    CHECK_FALSE(UpdateInstaller::IsWritable(dir.path / "missing"));
}

TEST_CASE("Only one updater holds an install's lock", "[updater]") {
    TempDir dir;
    const auto path = dir.path / UpdateInstaller::LockName;
    {
        const UpdateInstaller::Lock first{path};
        const UpdateInstaller::Lock second{path};
        CHECK(first.Held());
        CHECK_FALSE(second.Held());
    }
    const UpdateInstaller::Lock again{path};
    CHECK(again.Held());
}

TEST_CASE("Restarting keeps the frontend options and drops the game", "[updater]") {
    const std::vector<std::string> arguments{"-f",
                                             "-g",
                                             "C:/games/oot.nsp",
                                             "-u",
                                             "1",
                                             "C:/games/other.xci",
                                             "--config=x.ini",
                                             "-qlaunch",
                                             "--pack-nup",
                                             "a",
                                             "-c",
                                             "c.ini",
                                             "--game",
                                             "g.nsp",
                                             "-p",
                                             "args",
                                             "-x",
                                             "*:Info",
                                             "-v",
                                             "--",
                                             "-f"};
    CHECK(
        UpdateInstaller::RestartArguments(arguments) ==
        std::vector<std::string>{"-f", "-u", "1", "--config=x.ini", "-c", "c.ini", "-x", "*:Info"});
    CHECK(UpdateInstaller::RestartArguments({}).empty());
}
