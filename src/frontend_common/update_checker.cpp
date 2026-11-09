// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fmt/format.h>
#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_checker.h"

namespace UpdateChecker {

namespace {

std::optional<u64> ParseBuildNumber(std::string_view tag) {
    constexpr std::string_view prefix = "build-";
    if (!tag.starts_with(prefix)) {
        return std::nullopt;
    }
    tag.remove_prefix(prefix.size());
    u64 number{};
    const auto [end, error] = std::from_chars(tag.data(), tag.data() + tag.size(), number);
    if (error != std::errc{} || end != tag.data() + tag.size() || number == 0) {
        return std::nullopt;
    }
    return number;
}

constexpr std::string_view ReleaseRepo = "Frizti/Lichii";
// Download URLs name the repository by its current name; GitHub redirects the old one.
constexpr std::array RepoNames{std::string_view{"Lichii"}, std::string_view{"Ocarina2"}};

std::optional<u32> ParseNumber(std::string_view text) {
    if (text.empty() || (text.size() > 1 && text.front() == '0') ||
        !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    u32 number{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return number;
}

CheckResult CheckOcarina2([[maybe_unused]] const Ocarina2Version& current) {
#ifndef _WIN32
    // Ocarina2 only publishes Windows packages.
    return {};
#else
    auto source = DefaultSource();
    if (const char* override_url = std::getenv("EDEN_UPDATE_URL");
        override_url != nullptr && *override_url != '\0') {
        const auto parsed = ParseSourceOverride(override_url);
        if (!parsed) {
            LOG_ERROR(Frontend,
                      "Ignoring EDEN_UPDATE_URL {}: it must be https, or http on a loopback host",
                      override_url);
            return {.failure = Failure::Network};
        }
        LOG_WARNING(Frontend, "Checking for updates at {} (EDEN_UPDATE_URL, for testing only)",
                    override_url);
        source = *parsed;
    }

    const auto response = Common::Net::Fetch(source.origin, source.api_path);
    if (!response.body) {
        LOG_WARNING(Frontend, "Update check failed with status {}", response.status);
        return {.failure = ClassifyFailure(response.status), .status = response.status};
    }
    const auto releases = Common::Net::Release::ListFromJson(
        std::string_view{*response.body}, source.origin, std::string{ReleaseRepo});
    auto update = SelectOcarina2Update(releases, current, source);
    if (!update) {
        return {};
    }
    auto asset = SelectOcarina2Asset(*update, source);
    LOG_INFO(Frontend, "Received update {} ({})", update->title, update->tag);
    return {.update = std::move(update), .asset = std::move(asset)};
#endif
}

} // namespace

Failure ClassifyFailure(int status) {
    if (status == 403 || status == 429) {
        return Failure::RateLimited;
    }
    return status >= 400 ? Failure::Http : Failure::Network;
}

std::optional<Common::Net::Release> SelectUpdate(std::span<const Common::Net::Release> releases,
                                                 std::string_view build_tag,
                                                 std::string_view platform,
                                                 Common::Net::LinuxPackage package) {
    auto newest = ParseBuildNumber(build_tag);
    if (!newest) {
        return std::nullopt;
    }

    const Common::Net::Release* selected = nullptr;
    for (const auto& release : releases) {
        const auto number = ParseBuildNumber(release.tag);
        if (number && *number > *newest && !release.GetPlatformAssets(platform, package).empty()) {
            newest = number;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

CheckResult Check() {
    if (const auto running = RunningVersion()) {
        return CheckOcarina2(*running);
    }
    if (!ParseBuildNumber(Common::g_build_release_tag) ||
        std::string_view{Common::g_build_release_platform}.empty()) {
        return {};
    }

    const auto list = Common::Net::GetReleases();
    if (!list.ok) {
        const auto failure = ClassifyFailure(list.status);
        LOG_WARNING(Frontend, "Update check failed with status {}", list.status);
        return {.failure = failure, .status = list.status};
    }

    auto update =
        SelectUpdate(list.releases, Common::g_build_release_tag, Common::g_build_release_platform,
                     Common::Net::CurrentLinuxPackage());
    if (update) {
        LOG_INFO(Frontend, "Received update {}", update->title);
    }
    return {.update = std::move(update)};
}

std::optional<Common::Net::Release> GetUpdate() {
    return Check().update;
}

std::string Ocarina2Version::ToString() const {
    if (patch != 0) {
        return fmt::format("{}.{}.{}", major, minor, patch);
    }
    return minor == 0 ? fmt::format("{}", major) : fmt::format("{}.{}", major, minor);
}

std::optional<Ocarina2Version> ParseOcarina2Version(std::string_view text) {
    const auto dot = text.find('.');
    const auto major = ParseNumber(text.substr(0, dot));
    if (!major || *major == 0) {
        return std::nullopt;
    }
    if (dot == std::string_view::npos) {
        return Ocarina2Version{.major = *major};
    }
    // N, N.M or N.M.P without leading zeros, and N.0 is spelled N, so each version has one tag
    // and one package name (N.0.P is allowed: a fix of N). Builds before v5.6 read N.M with one
    // digit only and skip the others.
    const auto rest = text.substr(dot + 1);
    const auto dot2 = rest.find('.');
    const std::string_view minor_text = rest.substr(0, dot2);
    const auto plain = [](std::string_view digits) {
        return !digits.empty() && digits.size() <= 3 && (digits.size() == 1 || digits[0] != '0');
    };
    const auto minor = ParseNumber(minor_text);
    if (!plain(minor_text) || !minor) {
        return std::nullopt;
    }
    if (dot2 == std::string_view::npos) {
        if (*minor == 0) {
            return std::nullopt;
        }
        return Ocarina2Version{.major = *major, .minor = *minor};
    }
    const std::string_view patch_text = rest.substr(dot2 + 1);
    const auto patch = ParseNumber(patch_text);
    if (!plain(patch_text) || !patch || *patch == 0) {
        return std::nullopt;
    }
    return Ocarina2Version{.major = *major, .minor = *minor, .patch = *patch};
}

std::optional<Ocarina2Version> ParseBuildVersion(std::string_view build_version) {
    for (const std::string_view prefix : {"Ocarina2 v", "Frizti v"}) {
        if (build_version.starts_with(prefix)) {
            return ParseOcarina2Version(build_version.substr(prefix.size()));
        }
    }
    return std::nullopt;
}

std::optional<Ocarina2Version> RunningVersion() {
    // Ocarina2 builds carry the name apart from the version ("Ocarina2" + "v4").
    if (std::string_view{Common::g_build_name} == "Ocarina2") {
        return ParseBuildVersion(fmt::format("Ocarina2 {}", Common::g_build_version));
    }
    return ParseBuildVersion(Common::g_build_version);
}

std::optional<Ocarina2Version> ParseOcarina2Tag(std::string_view tag) {
    for (const std::string_view prefix : {"ocarina2-v", "frizti-v"}) {
        if (tag.starts_with(prefix)) {
            return ParseOcarina2Version(tag.substr(prefix.size()));
        }
    }
    return std::nullopt;
}

std::string Ocarina2AssetName(const Ocarina2Version& version) {
    return fmt::format("Ocarina2-v{}-Windows-x64.zip", version.ToString());
}

Source DefaultSource() {
    return {.origin = "https://api.github.com",
            .api_path = fmt::format("/repos/{}/releases?per_page=100", ReleaseRepo)};
}

std::optional<Source> ParseSourceOverride(std::string_view url) {
    bool https = false;
    if (url.starts_with("https://")) {
        https = true;
        url.remove_prefix(8);
    } else if (url.starts_with("http://")) {
        url.remove_prefix(7);
    } else {
        return std::nullopt;
    }
    if (std::ranges::any_of(url, [](unsigned char c) { return c <= ' ' || c >= 0x7f; })) {
        return std::nullopt;
    }
    const auto slash = url.find('/');
    const auto authority = url.substr(0, slash);
    if (authority.empty() || authority.find_first_of("@?#\\") != std::string_view::npos) {
        return std::nullopt;
    }
    if (!https) {
        std::string host{authority.starts_with('[') ? authority.substr(0, authority.find(']') + 1)
                                                    : authority.substr(0, authority.find(':'))};
        std::ranges::transform(host, host.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (host != "127.0.0.1" && host != "localhost" && host != "[::1]") {
            return std::nullopt;
        }
    }
    return Source{.origin = fmt::format("{}://{}", https ? "https" : "http", authority),
                  .api_path = slash == std::string_view::npos ? std::string{"/"}
                                                              : std::string{url.substr(slash)},
                  .test = true};
}

std::optional<Common::Net::Asset> SelectOcarina2Asset(const Common::Net::Release& release,
                                                      const Source& source) {
    const auto version = ParseOcarina2Tag(release.tag);
    if (!version) {
        return std::nullopt;
    }
    const auto filename = Ocarina2AssetName(*version);
    for (const auto& url : release.assets) {
        std::string origin;
        if (source.test) {
            if (url.starts_with(source.origin + "/") && url.ends_with("/" + filename) &&
                url.find("..") == std::string::npos &&
                url.find_first_of("?#\\") == std::string::npos) {
                origin = source.origin;
            }
        } else {
            for (const auto repo : RepoNames) {
                if (url == fmt::format("https://github.com/Frizti/{}/releases/download/{}/{}", repo,
                                       release.tag, filename)) {
                    origin = "https://github.com";
                }
            }
        }
        if (origin.empty()) {
            continue;
        }
        Common::Net::Asset asset{.name = "Ocarina2",
                                 .url = origin,
                                 .path = url.substr(origin.size()),
                                 .filename = filename};
        if (const auto info = release.asset_info.find(url); info != release.asset_info.end()) {
            asset.sha256 = info->second.sha256;
            asset.size = info->second.size;
        }
        return asset;
    }
    return std::nullopt;
}

bool HiddenFromUpdater(const Common::Net::Release& release) {
    return release.body.find(HiddenReleaseMarker) != std::string::npos;
}

std::optional<Common::Net::Release> SelectOcarina2Update(
    std::span<const Common::Net::Release> releases, const Ocarina2Version& current,
    const Source& source) {
    const Common::Net::Release* selected = nullptr;
    Ocarina2Version newest = current;
    for (const auto& release : releases) {
        if (HiddenFromUpdater(release)) {
            LOG_INFO(Frontend, "Update check skips {}: its notes hide it from the updater",
                     release.tag);
            continue;
        }
        const auto version = ParseOcarina2Tag(release.tag);
        if (version && *version > newest && SelectOcarina2Asset(release, source)) {
            newest = *version;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

} // namespace UpdateChecker
