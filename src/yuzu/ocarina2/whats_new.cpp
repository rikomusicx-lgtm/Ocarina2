// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/whats_new.h"

#include <algorithm>

#include <QScreen>
#include <QGuiApplication>
#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include "common/scm_rev.h"
#include "qt_common/config/uisettings.h"

namespace Ocarina2Ui {

QString CleanReleaseNotes(const QString& markdown) {
    QString text = markdown;
    text.remove(QRegularExpression(QStringLiteral("<!--.*?-->"),
                                   QRegularExpression::DotMatchesEverythingOption));
    if (const auto packages = text.indexOf(QStringLiteral("# Packages")); packages >= 0) {
        text.truncate(packages);
    }
    // A Ocarina2 release starts with "Download this file ..." and a rule; the app has
    // already downloaded it.
    QStringList lines = text.split(QLatin1Char('\n'));
    const int head = std::min<int>(static_cast<int>(lines.size()), 20);
    for (int i = 0; i < head; ++i) {
        if (lines[i].trimmed() == QStringLiteral("---")) {
            const QString before = lines.mid(0, i).join(QLatin1Char('\n'));
            if (before.contains(QStringLiteral("Download"), Qt::CaseInsensitive)) {
                lines = lines.mid(i + 1);
            }
            break;
        }
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString BundledNotes() {
    // OCARINA2_NOTES_PREVIEW=<notes.md> shows a draft before it is bundled (for the release notes).
    const QString preview = qEnvironmentVariable("OCARINA2_NOTES_PREVIEW");
    QFile file(preview.isEmpty() ? QStringLiteral(":/ocarina2/whats-new.md") : preview);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    return CleanReleaseNotes(QString::fromUtf8(file.readAll()));
}

void SetNotes(QTextBrowser* browser, const QString& markdown) {
    browser->setMarkdown(markdown);
    QTextDocument* document = browser->document();
    document->setDocumentMargin(4);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        format.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
        format.setBottomMargin(std::max<qreal>(format.bottomMargin(), 8));
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat chars = fragment.charFormat();
            if (fragment.isValid() && chars.fontWeight() > QFont::Medium) {
                chars.setFontWeight(QFont::Medium);
                QTextCursor span(document);
                span.setPosition(fragment.position());
                span.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                span.setCharFormat(chars);
            }
        }
    }
    cursor.endEditBlock();
}

QString VersionName() {
    // The version string carries its own prefix ("build-122-dev", "build-121"); just hand it over.
    const QString version = QString::fromUtf8(Common::g_build_version);
    return QStringLiteral("Ocarina2 %1").arg(version).trimmed();
}

bool ShouldShowWhatsNew(bool used_before) {
    const std::string version{Common::g_build_version};
    if (version.empty()) {
        return false;
    }
    const std::string& seen = UISettings::values.ocarina2_whats_new_seen.GetValue();
    if (seen == version) {
        return false;
    }
    if (seen.empty() && !used_before) {
        // A first install: nothing is new to this player.
        MarkWhatsNewSeen();
        return false;
    }
    return !BundledNotes().isEmpty();
}

void MarkWhatsNewSeen() {
    UISettings::values.ocarina2_whats_new_seen.SetValue(std::string{Common::g_build_version});
}

void ShowWhatsNew(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("What's New"));
    // Never taller or wider than the screen: at 175% a 1080p screen is ~600 units tall.
    const QRect screen = (parent ? parent->screen() : QGuiApplication::primaryScreen())
                             ->availableGeometry();
    dialog.resize(std::min(660, screen.width() * 9 / 10), std::min(640, screen.height() * 85 / 100));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(36, 30, 36, 24);
    layout->setSpacing(18);

    auto* heading = new QLabel(QObject::tr("What's new in %1").arg(VersionName()));
    heading->setObjectName(QStringLiteral("Ocarina2DialogTitle"));
    layout->addWidget(heading);

    auto* notes = new QTextBrowser;
    notes->setObjectName(QStringLiteral("Ocarina2Notes"));
    notes->setFrameShape(QFrame::NoFrame);
    notes->setOpenExternalLinks(true);
    const QString text = BundledNotes();
    SetNotes(notes, text.isEmpty() ? QObject::tr("No notes for this version yet.") : text);
    layout->addWidget(notes, 1);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* close = new QPushButton(QObject::tr("Got it"));
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    row->addWidget(close);
    layout->addLayout(row);

    dialog.exec();
}

} // namespace Ocarina2Ui
