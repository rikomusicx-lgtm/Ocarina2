// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QIcon>
#include <QTextBrowser>
#include <fmt/ranges.h>
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"
#include "yuzu/about_dialog.h"
#include "yuzu/ocarina2/ocarina2_ui.h"

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent), ui{std::make_unique<Ui::AboutDialog>()} {
    static const std::string build_id = std::string{Common::g_build_id};
    static const std::string yuzu_build = fmt::format(
        "Ocarina2 {} ({})", std::string{Common::g_build_version},
        std::string{Common::g_scm_rev}.substr(0, 10));

    const auto override_build =
        fmt::format(fmt::runtime(std::string(Common::g_title_bar_format_idle)), build_id);
    const auto yuzu_build_version = override_build.empty() ? yuzu_build : override_build;

    ui->setupUi(this);
    // Try and request the icon from Qt theme (Linux?)
    const QIcon yuzu_logo = QIcon::fromTheme(QStringLiteral("io.github.rikomusicx-lgtm.ocarina2"),
                                           QIcon(QStringLiteral(":/img/ocarina2.ico")));
    if (!yuzu_logo.isNull()) {
        ui->labelLogo->setPixmap(yuzu_logo.pixmap(200));
    }
    ui->labelBuildInfo->setText(
        ui->labelBuildInfo->text().arg(QString::fromStdString(yuzu_build_version),
                                       QString::fromUtf8(Common::g_build_date).left(10)));
    if (Ocarina2Ui::Enabled()) {
        setWindowTitle(tr("About Ocarina2"));
        ui->labelYuzu->setText(QStringLiteral("<span style='font-size:30px'>Ocarina2</span>"));
    }

    // Credits: everyone whose work is in this build. Keep names accurate from the git history.
    auto* credits = new QTextBrowser(this);
    credits->setObjectName(QStringLiteral("Ocarina2Credits"));
    credits->setFrameShape(QFrame::NoFrame);
    credits->setReadOnly(true);
    credits->setOpenExternalLinks(true);
    credits->setFixedHeight(150);
    credits->setMarkdown(QStringLiteral(
        "**Credits** \u2014 unofficial fan project\n\n"
        "- **Eglantine** \u2014 Camille LaVey (original developer) and contributors\n"
        "- **Eden** \u2014 the Eden team, continuation of yuzu; **yuzu** \u2014 yuzu-emu; "
        "**Sudachi** \u2014 Man Sorahia and contributors\n"
        "- **Lichii** \u2014 zortos293 (maintainer) and contributors; Djihads80\n"
        "- **Fritchii / the Ocarina2 line** \u2014 Frizti (lead: AMD shader preparation, the "
        "Graphite UI), leonardochappuis (Linux, water), CheezerWheezer (AMD), koriko\n"
        "- **Community** \u2014 janvitos, ProphetWild, 101Leafy, nyakuoff, monsterlag, "
        "Dark (Dynamic FPS), EvilOOT (DMA snapshot)\n"
        "- **Artwork** \u2014 YOB (yobslab); **Ocarina of Forks Discord**\n"
        "- **This fork** \u2014 rikomusicx-lgtm/Ocarina2, with its testers and bug "
        "reporters"));
    ui->verticalLayout_3->insertWidget(1, credits);
}

AboutDialog::~AboutDialog() = default;
