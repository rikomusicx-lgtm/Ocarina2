// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

class QTextBrowser;
class QWidget;

// Ocarina2's "What's new" window: the release notes of this version, bundled in the exe
// (ocarina2/whats-new.md), shown once after an update and from Help > What's New.
namespace Ocarina2Ui {

/// Release notes as players read them in the app: the download instructions at the top of a
/// GitHub release (they are for the release page) and the "# Packages" tail are dropped.
[[nodiscard]] QString CleanReleaseNotes(const QString& markdown);

/// The notes bundled in this build; empty while ocarina2/whats-new.md holds only the placeholder.
/// OCARINA2_NOTES_PREVIEW=<file> reads a draft instead.
[[nodiscard]] QString BundledNotes();

/// Shows Markdown notes in Ocarina2's reading style: Inter 500 for bold (the 700 weight isn't
/// bundled), roomier lines and paragraphs.
void SetNotes(QTextBrowser* browser, const QString& markdown);

/// "Ocarina2 v5"
[[nodiscard]] QString VersionName();

/// True once per version, after an update: the version differs from the last one shown and the
/// bundled notes are not empty. A first install (nothing seen yet and Ocarina2 never ran) only
/// records the version.
[[nodiscard]] bool ShouldShowWhatsNew(bool used_before);

/// Records this version as seen (the caller saves the config).
void MarkWhatsNewSeen();

void ShowWhatsNew(QWidget* parent);

} // namespace Ocarina2Ui
