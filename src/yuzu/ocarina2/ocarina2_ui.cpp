// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/ocarina2_ui.h"

#include <cmath>
#include <map>
#include <string_view>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QImageReader>
#include <QLocale>
#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QUrl>
#include <QScrollArea>
#include <QStyleFactory>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/qt_common.h"
#include "ui_main.h"
#ifdef HAS_RESHADE
#include "video_core/post_processing/fx_preset.h"
#endif

namespace Ocarina2Ui {
namespace {

// Graphite
const QColor Background{0x11, 0x11, 0x13};
const QColor Raised{0x1b, 0x1b, 0x1f};
const QColor Field{0x18, 0x18, 0x1b};
const QColor Button{0x1f, 0x1f, 0x24};
const QColor Text{0xec, 0xec, 0xf1};
const QColor TextSecondary{0x9a, 0x9a, 0xa8};
const QColor TextDisabled{0x55, 0x55, 0x5f};
const QColor IconIdle{0xa4a0bb};
const QColor Accent{0xa7, 0x8b, 0xfa};
const QColor OnAccent{0x1c, 0x14, 0x33};

QPalette GraphitePalette() {
    QPalette p;
    p.setColor(QPalette::Window, Background);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, Field);
    p.setColor(QPalette::AlternateBase, Raised);
    p.setColor(QPalette::Text, Text);
    p.setColor(QPalette::Button, Button);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::BrightText, Text);
    p.setColor(QPalette::ToolTipBase, Button);
    p.setColor(QPalette::ToolTipText, Text);
    p.setColor(QPalette::PlaceholderText, IconIdle);
    p.setColor(QPalette::Highlight, Accent);
    p.setColor(QPalette::HighlightedText, OnAccent);
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::LinkVisited, Accent);
    p.setColor(QPalette::Light, QColor(0x2e, 0x2e, 0x35));
    p.setColor(QPalette::Midlight, QColor(0x26, 0x26, 0x2c));
    p.setColor(QPalette::Mid, QColor(0x22, 0x22, 0x27));
    p.setColor(QPalette::Dark, QColor(0x0c, 0x0c, 0x0e));
    p.setColor(QPalette::Shadow, QColor(0x05, 0x05, 0x06));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, TextDisabled);
    }
    return p;
}

/// Fusion without the always-on mnemonic underlines.
class GraphiteStyle final : public QProxyStyle {
public:
    GraphiteStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* data) const override {
        if (hint == SH_UnderlineShortcut) {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

// The ocarina2 icons as QPainter strokes. The minimal Qt packages the project builds against
// have no SVG module, so the line glyphs are drawn here: each icon is its viewBox coordinates,
// scaled to px (grid is square). Keep the shapes, the round caps and the 1.8 (24-grid) widths —
// the strip, the rail and the play button all use them.
void DrawIconPath(QPainterPath& path, const QString& name) {
    if (name == QStringLiteral("check")) {
        path.moveTo(3.5, 8.5);
        path.lineTo(6.5, 11.5);
        path.lineTo(12.5, 4.5);
    } else if (name == QStringLiteral("chevron-down")) {
        path.moveTo(3, 4.5);
        path.lineTo(6, 7.5);
        path.lineTo(9, 4.5);
    } else if (name == QStringLiteral("arrow-left")) {
        path.moveTo(12.25, 3.5);
        path.lineTo(7.75, 8);
        path.lineTo(12.25, 12.5);
        path.moveTo(8.25, 8);
        path.lineTo(16.25, 8);
    } else if (name == QStringLiteral("fullscreen-exit")) {
        path.moveTo(9, 4);
        path.lineTo(9, 8);
        path.quadTo(QPointF(9, 9), QPointF(8, 9));
        path.lineTo(4, 9);
        path.moveTo(15, 4);
        path.lineTo(19, 4);
        path.quadTo(QPointF(20, 4), QPointF(20, 5));
        path.lineTo(20, 9);
        path.moveTo(20, 15);
        path.lineTo(20, 19);
        path.quadTo(QPointF(20, 20), QPointF(19, 20));
        path.lineTo(15, 20);
        path.moveTo(9, 20);
        path.lineTo(5, 20);
        path.quadTo(QPointF(4, 20), QPointF(4, 19));
        path.lineTo(4, 15);
    } else if (name == QStringLiteral("fullscreen")) {
        path.moveTo(4, 9);
        path.lineTo(4, 5);
        path.quadTo(QPointF(4, 4), QPointF(5, 4));
        path.lineTo(9, 4);
        path.moveTo(15, 4);
        path.lineTo(19, 4);
        path.quadTo(QPointF(20, 4), QPointF(20, 5));
        path.lineTo(20, 9);
        path.moveTo(20, 15);
        path.lineTo(20, 19);
        path.quadTo(QPointF(20, 20), QPointF(19, 20));
        path.lineTo(15, 20);
        path.moveTo(9, 20);
        path.lineTo(5, 20);
        path.quadTo(QPointF(4, 20), QPointF(4, 19));
        path.lineTo(4, 15);
    } else if (name == QStringLiteral("help")) {
        path.addEllipse(QPointF(12, 12), 8.5, 8.5);
        path.moveTo(12, 8.5);
        path.addEllipse(QPointF(12, 12), 3.5, 3.5);
        path.moveTo(6, 6);
        path.lineTo(9.5, 9.5);
        path.moveTo(18, 6);
        path.lineTo(14.5, 9.5);
        path.moveTo(6, 18);
        path.lineTo(9.5, 14.5);
        path.moveTo(18, 18);
        path.lineTo(14.5, 14.5);
    } else if (name == QStringLiteral("library")) {
        for (const QPointF pos : {QPointF(4, 4), QPointF(13, 4), QPointF(4, 13), QPointF(13, 13)}) {
            path.addRoundedRect(QRectF(pos, QSizeF(7, 7)), 2.0, 2.0);
        }
    } else if (name == QStringLiteral("list")) {
        for (const qreal y : {6.0, 12.0, 18.0}) {
            path.moveTo(9, y);
            path.lineTo(20, y);
        }
        path.moveTo(4.5 + 1, 6);
        path.addEllipse(QPointF(4.5, 6), 1, 1);
        path.moveTo(4.5 + 1, 12);
        path.addEllipse(QPointF(4.5, 12), 1, 1);
        path.moveTo(4.5 + 1, 18);
        path.addEllipse(QPointF(4.5, 18), 1, 1);
    } else if (name == QStringLiteral("menu")) {
        for (const qreal y : {7.0, 12.0, 17.0}) {
            path.moveTo(5, y);
            path.lineTo(19, y);
        }
    } else if (name == QStringLiteral("plus")) {
        path.moveTo(12, 5);
        path.lineTo(12, 19);
        path.moveTo(5, 12);
        path.lineTo(19, 12);
    } else if (name == QStringLiteral("restart")) {
        // Arc with a gap at the top right, arrowhead at the gap.
        path.arcMoveTo(QRectF(5, 5, 14, 14), 65);
        path.arcTo(QRectF(5, 5, 14, 14), 65, 310);
        path.moveTo(19, 4.5);
        path.lineTo(19, 8.0);
        path.lineTo(15.5, 8.0);
    } else if (name == QStringLiteral("settings")) {
        path.moveTo(4, 6);   path.lineTo(13, 6);
        path.moveTo(17, 6);  path.lineTo(20, 6);
        path.moveTo(4, 12);  path.lineTo(7, 12);
        path.moveTo(11, 12); path.lineTo(20, 12);
        path.moveTo(4, 18);  path.lineTo(15, 18);
        path.moveTo(19, 18); path.lineTo(20, 18);
        for (const QPointF pos : {QPointF(15, 6), QPointF(9, 12), QPointF(17, 18)}) {
            path.moveTo(pos.x() + 2, pos.y());
            path.addEllipse(pos, 2, 2);
        }
    } else if (name == QStringLiteral("wifi")) {
        path.arcMoveTo(QRectF(4, 7, 16, 16), 40);
        path.arcTo(QRectF(4, 7, 16, 16), 40, 100);
        path.moveTo(7.8, 11.2);
        path.arcMoveTo(QRectF(7.5, 10, 9, 10), 40);
        path.arcTo(QRectF(7.5, 10, 9, 10), 40, 100);
        path.moveTo(10.4, 14.6);
        path.arcMoveTo(QRectF(10.3, 13.3, 3.4, 4.2), 40);
        path.arcTo(QRectF(10.3, 13.3, 3.4, 4.2), 40, 100);
        path.moveTo(12 - 1.2, 18.6);
        path.addEllipse(QPointF(12, 18.6), 1.2, 1.2);
    } else if (name == QStringLiteral("battery")) {
        path.addRoundedRect(QRectF(2.5, 8.5, 15, 7), 1.8, 1.8);
        path.moveTo(19.3, 10.7);
        path.lineTo(20.6, 10.7);
        path.lineTo(20.6, 13.3);
        path.lineTo(19.3, 13.3);
    } else if (name == QStringLiteral("star")) {
        // Centred on (12, 12): the previous one rose above the canvas and was clipped.
        const QPointF t{12, 12.2};
        for (int i = 0; i < 10; ++i) {
            const qreal angle = (qreal(i) * 36.0 - 90.0) * 0.01745329;
            const qreal r = (i % 2 == 0) ? 7.2 : 3.3;
            const QPointF p(t.x() + r * std::cos(angle), t.y() + r * std::sin(angle));
            if (i == 0) {
                path.moveTo(p);
            } else {
                path.lineTo(p);
            }
        }
        path.closeSubpath();
    } else if (name == QStringLiteral("timer")) {
        path.addEllipse(QPointF(12, 13.5), 6.5, 6.5);
        path.moveTo(12, 10.4);
        path.lineTo(12, 13.5);
        path.lineTo(13.8, 15.3);
        path.moveTo(9.5, 3.6);
        path.lineTo(14.5, 3.6);
        path.moveTo(12, 3.6);
        path.lineTo(12, 6.4);
    } else if (name == QStringLiteral("camera")) {
        path.addRoundedRect(QRectF(2.5, 8, 19, 12.5), 2.6, 2.6);
        path.addEllipse(QPointF(12, 14.2), 3.6, 3.6);
        path.moveTo(9, 8);
        path.lineTo(10.2, 5);
        path.lineTo(13.8, 5);
        path.lineTo(15, 8);
    } else if (name == QStringLiteral("home")) {
        path.moveTo(4, 11.2);
        path.lineTo(12, 4.5);
        path.lineTo(20, 11.2);
        path.moveTo(6.5, 9.8);
        path.lineTo(6.5, 20);
        path.lineTo(10.6, 20);
        path.lineTo(10.6, 14);
        path.lineTo(13.4, 14);
        path.lineTo(13.4, 20);
        path.lineTo(17.5, 20);
        path.lineTo(17.5, 9.8);
    } else if (name == QStringLiteral("folder")) {
        path.moveTo(3, 7);
        path.lineTo(8.6, 7);
        path.lineTo(10.8, 9.4);
        path.lineTo(21, 9.4);
        path.lineTo(21, 18.4);
        path.lineTo(3, 18.4);
        path.closeSubpath();
    } else if (name == QStringLiteral("bag")) {
        path.moveTo(5.2, 9.5);
        path.lineTo(18.8, 9.5);
        path.lineTo(20.3, 19.3);
        path.lineTo(3.7, 19.3);
        path.closeSubpath();
        path.moveTo(8.4, 13.4);
        path.moveTo(8.4, 13.4);
        path.moveTo(8.6, 9.5);
        path.arcMoveTo(QRectF(8.1, 5.5, 7.8, 8), 0);
        path.arcTo(QRectF(8.1, 5.5, 7.8, 8), 0, 180);
    } else if (name == QStringLiteral("gamepad")) {
        path.addRoundedRect(QRectF(2.2, 8, 19.6, 9), 4.5, 4.5);
        path.moveTo(7.4, 11);
        path.lineTo(7.4, 14);
        path.moveTo(5.9, 12.5);
        path.lineTo(8.9, 12.5);
        path.addEllipse(QPointF(15.6, 11.4), 0.9, 0.9);
        path.addEllipse(QPointF(17.4, 14), 0.9, 0.9);
    } else if (name == QStringLiteral("user")) {
        path.addEllipse(QPointF(12, 8.3), 3.3, 3.3);
        path.moveTo(4.9, 19.6);
        path.quadTo(QPointF(5.5, 13.8), QPointF(12, 13.8));
        path.quadTo(QPointF(18.5, 13.8), QPointF(19.1, 19.6));
    } else if (name == QStringLiteral("bookmark")) {
        path.moveTo(7, 4.6);
        path.lineTo(7, 19.4);
        path.lineTo(12, 15.3);
        path.lineTo(17, 19.4);
        path.lineTo(17, 4.6);
        path.closeSubpath();
    } else if (name == QStringLiteral("droplet")) {
        path.moveTo(12, 4.2);
        path.cubicTo(QPointF(16.6, 11.2), QPointF(17.9, 13.9), QPointF(17.9, 16.4));
        path.lineTo(6.1, 16.4);
        path.cubicTo(QPointF(6.1, 13.9), QPointF(7.4, 11.2), QPointF(12, 4.2));
    } else if (name == QStringLiteral("community")) {
        path.addEllipse(QRectF(4.5, 3.5, 16, 16));
        path.moveTo(6.3, 17.3);
        path.lineTo(4.0, 20.0);
        path.lineTo(8.7, 18.7);
        path.moveTo(8.5, 11.5);
        path.lineTo(8.51, 11.5);
        path.moveTo(12.5, 11.5);
        path.lineTo(12.51, 11.5);
        path.moveTo(16.5, 11.5);
        path.lineTo(16.51, 11.5);
    } else if (name == QStringLiteral("heart")) {
        path.moveTo(12, 19.3);
        path.cubicTo(QPointF(7.5, 15.4), QPointF(4.3, 12.9), QPointF(4.3, 9.5));
        path.cubicTo(QPointF(4.3, 6.7), QPointF(6.7, 4.8), QPointF(8.9, 4.8));
        path.cubicTo(QPointF(10.5, 4.8), QPointF(11.6, 5.6), QPointF(12, 6.7));
        path.cubicTo(QPointF(12.4, 5.6), QPointF(13.5, 4.8), QPointF(15.1, 4.8));
        path.cubicTo(QPointF(17.3, 4.8), QPointF(19.7, 6.7), QPointF(19.7, 9.5));
        path.cubicTo(QPointF(19.7, 12.9), QPointF(16.5, 15.4), QPointF(12, 19.3));
    }
}

QPixmap RenderIcon(const QString& name, const QColor& color, int px) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(px, px) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    const int grid = (name == QStringLiteral("check") ? 16
                                             : name == QStringLiteral("chevron-down") ? 12
                                                                                      : 24);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale((px * ratio) / grid, (px * ratio) / grid);
    const qreal stroke = (name == QStringLiteral("check") ? 2.0
                              : name == QStringLiteral("chevron-down") ? 1.6
                                                                       : 1.8);
    const QPen pen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPainterPath path;
    if (name == QStringLiteral("stop")) {
        path.addRoundedRect(QRectF(6, 6, 12, 12), 2.0, 2.0);
        painter.fillPath(path, color);
        return pixmap;
    }
    if (name == QStringLiteral("pause")) {
        path.addRoundedRect(QRectF(6.5, 5, 3.5, 14), 1.0, 1.0);
        path.addRoundedRect(QRectF(14, 5, 3.5, 14), 1.0, 1.0);
        painter.fillPath(path, color);
        return pixmap;
    }
    if (name == QStringLiteral("play")) {
        path.moveTo(8.2, 5.4);
        path.lineTo(19.4, 12);
        path.lineTo(8.2, 18.6);
        path.closeSubpath();
        painter.fillPath(path, color);
        return pixmap;
    }
    if (name == QStringLiteral("triforce")) {
        // Three small equilateral triangles around a hollow middle, Zelda's mark.
        constexpr qreal R = 9.8;
        const QPointF A{12, 12 - R}, B{12 - R * 0.866, 12 + R * 0.5}, C{12 + R * 0.866, 12 + R * 0.5};
        const QPointF mAB((A.x() + B.x()) / 2, (A.y() + B.y()) / 2);
        const QPointF mAC((A.x() + C.x()) / 2, (A.y() + C.y()) / 2);
        const QPointF mBC((B.x() + C.x()) / 2, (B.y() + C.y()) / 2);
        QPainterPath tri;
        tri.moveTo(A); tri.lineTo(mAB); tri.lineTo(mAC); tri.closeSubpath();
        tri.moveTo(B); tri.lineTo(mAB); tri.lineTo(mBC); tri.closeSubpath();
        tri.moveTo(C); tri.lineTo(mAC); tri.lineTo(mBC); tri.closeSubpath();
        painter.fillPath(tri, color);
        return pixmap;
    }
    DrawIconPath(path, name);
    painter.strokePath(path, pen);
    return pixmap;
}

void Rename(QAction* action, const QString& text) {
    if (action) {
        action->setText(text);
    }
}

} // namespace

bool Enabled() {
    static const bool enabled = [] {
        if (qEnvironmentVariableIntValue("OCARINA2_CLASSIC_UI") != 0) {
            return false;
        }
        return std::string_view{Common::g_build_name} == "Ocarina2";
    }();
    return enabled;
}

bool ApplyTheme() {
    if (!Enabled()) {
        return false;
    }
    static bool once = false;
    if (!once) {
        once = true;
        QFontDatabase::addApplicationFont(QStringLiteral(":/ocarina2/fonts/Inter-Regular.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral(":/ocarina2/fonts/Inter-Medium.ttf"));
        QApplication::setStyle(new GraphiteStyle);
    }
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(14);
    QApplication::setFont(font);
    QApplication::setPalette(GraphitePalette());
    // Folder and refresh icons come from the dark icon set.
    QIcon::setThemeName(QStringLiteral("colorful_dark"));

    QFile qss(QStringLiteral(":/ocarina2/style.qss"));
    if (qss.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
    return true;
}

bool HidesSetting(const std::string& label) {
    // No frame generation in Ocarina2 (real frames only), and its options are dev tools.
    return Enabled() && label.starts_with("frame_gen");
}

QIcon Icon(const QString& name, int px) {
    QIcon icon;
    const QPixmap idle = RenderIcon(name, IconIdle, px);
    const QPixmap hover = RenderIcon(name, Text, px);
    const QPixmap on = RenderIcon(name, Accent, px);
    icon.addPixmap(idle, QIcon::Normal, QIcon::Off);
    icon.addPixmap(hover, QIcon::Active, QIcon::Off);
    icon.addPixmap(on, QIcon::Normal, QIcon::On);
    icon.addPixmap(on, QIcon::Active, QIcon::On);
    return icon;
}

QIcon Glyph(const QString& name, unsigned rgb, int px) {
    return QIcon(RenderIcon(name, QColor::fromRgb(rgb), px));
}

void RestructureMenus(Ui::MainWindow& ui, QMainWindow& window, const MenuHooks& hooks) {
    const auto tr = [](const char* text) { return QObject::tr(text); };

    // File: open, install, folders, settings, exit.
    ui.menuOpen_Eden_Folders->removeAction(ui.action_Log_Folder);
    QMenu* file = ui.menu_File;
    file->clear();
    file->addAction(ui.action_Load_File);
    file->addAction(ui.action_Load_Folder);
    file->addMenu(ui.menu_recent_files);
    file->addSeparator();
    file->addAction(ui.action_Install_File_NAND);
    file->addSeparator();
    file->addMenu(ui.menuOpen_Eden_Folders);
    file->addSeparator();
    file->addAction(ui.action_Configure);
    file->addSeparator();
    file->addAction(ui.action_Exit);

    // Game (was Emulation): pause, stop, restart, shaders, game settings.
    ui.menu_Emulation->setTitle(tr("&Game"));
    ui.menu_Emulation->removeAction(ui.action_Configure);

    // View: window, bars, game list, window size, effects, debugging.
    QMenu* view = ui.menu_View;
    view->clear();
    view->addAction(ui.action_Fullscreen);
    view->addAction(ui.action_Single_Window_Mode);
    view->addSeparator();
    view->addAction(ui.action_Show_Performance_Overlay);
    view->addAction(ui.action_Show_Status_Bar);
    view->addAction(ui.action_Show_Filter_Bar);
    view->addSeparator();
    ui.menu_Game_List_Mode->addSeparator();
    ui.menu_Game_List_Mode->addMenu(ui.menuGame_Icon_Size);
    ui.menu_Game_List_Mode->addAction(ui.action_Show_Game_Name);
    view->addMenu(ui.menu_Game_List_Mode);
    view->addMenu(ui.menu_Reset_Window_Size);
    view->addSeparator();
    view->addAction(ui.action_Post_Processing_Shaders);
    view->addMenu(ui.menu_View_Debugging);

    // Tools: setup tools first; Amiibo; rarely used things under Advanced.
    QMenu* tools = ui.menu_Tools;
    tools->clear();
    tools->addAction(ui.action_Install_Keys);
    tools->addMenu(ui.menuInstall_Firmware);
    tools->addAction(ui.action_Verify_installed_contents);
    tools->addAction(ui.action_Data_Manager);
    tools->addSeparator();
    tools->addAction(ui.action_Capture_Screenshot);
    tools->addMenu(ui.menu_Create_Shortcuts);
    tools->addSeparator();
    QMenu* amiibo = ui.menu_cabinet_applet;
    QAction* first_cabinet = amiibo->actions().value(0);
    amiibo->insertAction(first_cabinet, ui.action_Load_Amiibo);
    amiibo->insertSeparator(first_cabinet);
    tools->addMenu(amiibo);
    QMenu* advanced = new QMenu(tr("Ad&vanced"), tools);
    advanced->addMenu(ui.menu_Applets);
    advanced->addMenu(ui.menuTAS);
    advanced->addMenu(ui.menu_Multiplayer);
    advanced->addSeparator();
    advanced->addAction(ui.action_Enable_Overlay_Applet);
    tools->addMenu(advanced);
    ui.menubar->removeAction(ui.menu_Multiplayer->menuAction());

    // Help: the log first; Eden's mods page leaves the menu.
    auto* export_log = new QAction(tr("&Export Log..."), &window);
    QObject::connect(export_log, &QAction::triggered, &window, hooks.export_log);
    ui.menu_Help->removeAction(ui.action_Open_Mods_Page);
    QAction* first_help = ui.menu_Help->actions().value(0);
    auto* whats_new = new QAction(tr("What's &New"), &window);
    QObject::connect(whats_new, &QAction::triggered, &window, hooks.whats_new);
    auto* delete_cache = new QAction(tr("&Delete Shader Cache..."), &window);
    QObject::connect(delete_cache, &QAction::triggered, &window, hooks.delete_shader_cache);
    ui.menu_Help->insertAction(first_help, export_log);
    ui.menu_Help->insertAction(first_help, ui.action_Log_Folder);
    ui.menu_Help->insertAction(first_help, delete_cache);
    ui.menu_Help->insertSeparator(first_help);
    ui.menu_Help->insertAction(first_help, whats_new);
    auto* discord = new QAction(Icon(QStringLiteral("community"), 16), tr("Fritchii &Discord"), &window);
    QObject::connect(discord, &QAction::triggered, &window, [] { OpenDiscord(); });
    auto* donate = new QAction(Icon(QStringLiteral("heart"), 16), tr("D&onate"), &window);
    QObject::connect(donate, &QAction::triggered, &window, [] { OpenDonate(); });
    ui.menu_Help->insertAction(first_help, discord);
    ui.menu_Help->insertAction(first_help, donate);

    // Plain names.
    ui.menu_recent_files->setTitle(tr("Open &Recent"));
    ui.menuOpen_Eden_Folders->setTitle(tr("Open &Folder"));
    ui.menu_Game_List_Mode->setTitle(tr("Game &List"));
    ui.menuGame_Icon_Size->setTitle(tr("&Icon Size"));
    ui.menu_Reset_Window_Size->setTitle(tr("&Window Size"));
    ui.menu_Create_Shortcuts->setTitle(tr("Create &Shortcut"));
    Rename(ui.action_Load_File, tr("&Open Game..."));
    Rename(ui.action_Load_Folder, tr("Open &Extracted Game..."));
    Rename(ui.action_Install_File_NAND, tr("&Install Update or DLC..."));
    Rename(ui.action_Configure, tr("&Settings..."));
    Rename(ui.action_Configure_Current_Game, tr("Game &Settings..."));
    Rename(ui.action_Root_Data_Folder, tr("&Data"));
    Rename(ui.action_NAND_Folder, tr("&NAND"));
    Rename(ui.action_SDMC_Folder, tr("&SD Card"));
    Rename(ui.action_Mod_Folder, tr("&Mods"));
    Rename(ui.action_Log_Folder, tr("Open &Log Folder"));
    Rename(ui.action_Reset_Window_Size_720, tr("&720p"));
    Rename(ui.action_Reset_Window_Size_900, tr("&900p"));
    Rename(ui.action_Reset_Window_Size_1080, tr("&1080p"));
    Rename(ui.action_Tree_View, tr("&List"));
    Rename(ui.action_Grid_View, tr("&Grid"));
    Rename(ui.action_Carousel_View, tr("&Carousel"));
    Rename(ui.action_Show_Game_Name, tr("Show Game &Names"));
    Rename(ui.action_Show_Performance_Overlay, tr("&Performance Overlay"));
    Rename(ui.action_Show_Status_Bar, tr("&Status Bar"));
    Rename(ui.action_Show_Filter_Bar, tr("Search &Bar"));
    Rename(ui.action_Single_Window_Mode, tr("Single &Window"));
    Rename(ui.action_Enable_Overlay_Applet, tr("&Overlay Display Applet"));
    Rename(ui.action_Data_Manager, tr("&Data Manager..."));
    Rename(ui.action_Capture_Screenshot, tr("Take &Screenshot"));
    Rename(ui.action_Load_Amiibo, tr("&Load or Remove Amiibo..."));
    Rename(ui.action_Firmware_From_Folder, tr("From &Folder..."));
    Rename(ui.action_Firmware_From_ZIP, tr("From &ZIP..."));
    Rename(ui.action_About, tr("&About Ocarina2"));
    Rename(ui.action_Eden_Dependencies, tr("&Open-Source Components"));
}

QPushButton* MakeGraphicsButton(QWidget* parent, GraphicsHooks hooks) {
    auto* button = new QPushButton(QObject::tr("GRAPHICS"), parent);
    button->setObjectName(QStringLiteral("TogglableStatusBarButton"));
    button->setFocusPolicy(Qt::NoFocus);

    QObject::connect(button, &QPushButton::clicked, button, [button, hooks] {
        QMenu menu(button);
        const auto add_choices = [&menu, &hooks](const QString& title, const auto& texts,
                                                 auto current, auto apply, bool enabled) {
            QMenu* sub = menu.addMenu(title);
            sub->setEnabled(enabled);
            auto* group = new QActionGroup(sub);
            for (const auto& [value, text] : texts) {
                QAction* action = sub->addAction(text);
                action->setCheckable(true);
                action->setChecked(value == current);
                group->addAction(action);
                QObject::connect(action, &QAction::triggered, sub, [apply, value = value, hooks] {
                    apply(value);
                    hooks.refresh();
                });
            }
        };

        std::map<Settings::RendererBackend, QString> renderers;
        for (const auto& [backend, text] : ConfigurationShared::renderer_backend_texts_map) {
            if (backend != Settings::RendererBackend::Null) {
                renderers.emplace(backend, text);
            }
        }
        add_choices(
            QObject::tr("&Renderer"), renderers, Settings::values.renderer_backend.GetValue(),
            [](Settings::RendererBackend backend) {
                Settings::values.renderer_backend.SetValue(backend);
            },
            !hooks.renderer_locked());
        add_choices(
            QObject::tr("GPU &Accuracy"), ConfigurationShared::gpu_accuracy_texts_map,
            Settings::values.gpu_accuracy.GetValue(),
            [](Settings::GpuAccuracy accuracy) {
                Settings::values.gpu_accuracy.SetValue(accuracy);
                QtCommon::system->ApplySettings();
            },
            true);
        add_choices(
            QObject::tr("Upscaling &Filter"), ConfigurationShared::scaling_filter_texts_map,
            Settings::values.scaling_filter.GetValue(),
            [](Settings::ScalingFilter filter) { Settings::values.scaling_filter.SetValue(filter); },
            true);
        add_choices(
            QObject::tr("Anti-&Aliasing"), ConfigurationShared::anti_aliasing_texts_map,
            Settings::values.anti_aliasing.GetValue(),
            [](Settings::AntiAliasing mode) { Settings::values.anti_aliasing.SetValue(mode); },
            true);
#ifdef HAS_RESHADE
        QMenu* effects = menu.addMenu(QObject::tr("&Effects"));
        auto* effects_group = new QActionGroup(effects);
        const bool fx_on = Settings::values.post_shader_enabled.GetValue();
        const std::string active = VideoCore::GetActiveFxPreset();
        QAction* off = effects->addAction(QObject::tr("Off"));
        off->setCheckable(true);
        off->setChecked(!fx_on);
        effects_group->addAction(off);
        QObject::connect(off, &QAction::triggered, effects, [hooks] {
            Settings::values.post_shader_enabled.SetValue(false);
            hooks.refresh();
        });
        for (const auto& preset : VideoCore::GetFxPresetCatalog()) {
            QAction* action = effects->addAction(QString::fromStdString(preset.name));
            action->setCheckable(true);
            action->setChecked(fx_on && preset.name == active);
            effects_group->addAction(action);
            QObject::connect(action, &QAction::triggered, effects, [name = preset.name, hooks] {
                VideoCore::ApplyFxPreset(name);
                Settings::values.post_shader_enabled.SetValue(true);
                hooks.refresh();
            });
        }
        if (hooks.open_effects) {
            effects->addSeparator();
            effects->addAction(QObject::tr("Configure Effects..."), effects, hooks.open_effects);
        }
#endif
        menu.addSeparator();
        menu.addAction(QObject::tr("More Settings..."), &menu, hooks.open_settings);

        const QSize size = menu.sizeHint();
        menu.exec(button->mapToGlobal(QPoint(0, -size.height())));
    });
    return button;
}

QWidget* SettingsPage(QWidget* page) {
    if (!Enabled() || !page) {
        return page;
    }
    // One scroll area per page, kept on the page (it lives as long as the page's dialog).
    if (auto* area = qobject_cast<QScrollArea*>(page->property("Ocarina2Scroll").value<QObject*>())) {
        return area;
    }
    auto* area = new QScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    area->setWidget(page);
    area->setAccessibleName(page->accessibleName());
    page->setProperty("Ocarina2Scroll", QVariant::fromValue<QObject*>(area));
    return area;
}

const char* LoadingBarStyle() {
    return R"(
QProgressBar {
  background-color: rgba(255, 255, 255, 30);
  border: none;
  border-radius: 3px;
  max-height: 6px;
}
QProgressBar::chunk {
  background-color: #a78bfa;
  border-radius: 3px;
})";
}

namespace {
constexpr int KeptSettingsBackups = 10;
constexpr const char* BackupFolderFormat = "yyyy-MM-dd HHmmss";

QString ConfigDirPath() {
    return QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir)));
}

bool CopyDirFiles(const QString& from, const QString& to) {
    const QDir source(from);
    if (!source.exists()) {
        return true;
    }
    if (!QDir().mkpath(to)) {
        return false;
    }
    bool ok = true;
    for (const QString& name : source.entryList(QDir::Files)) {
        QFile::remove(QDir(to).filePath(name));
        ok &= QFile::copy(source.filePath(name), QDir(to).filePath(name));
    }
    return ok;
}
} // namespace

QString BackupSettings() {
    const QDir config(ConfigDirPath());
    const QString folder =
        config.filePath(QStringLiteral("backups/") +
                        QDateTime::currentDateTime().toString(QString::fromLatin1(BackupFolderFormat)));
    if (!QDir().mkpath(folder)) {
        LOG_WARNING(Frontend, "Settings backup: cannot create {}", folder.toStdString());
        return {};
    }
    bool ok = true;
    for (const QString name : {QStringLiteral("qt-config.ini"), QStringLiteral("frizti.ini")}) {
        if (config.exists(name)) {
            ok &= QFile::copy(config.filePath(name), QDir(folder).filePath(name));
        }
    }
    ok &= CopyDirFiles(config.filePath(QStringLiteral("custom")),
                       QDir(folder).filePath(QStringLiteral("custom")));
    LOG_INFO(Frontend, "Settings backup {}: {}", folder.toStdString(), ok ? "saved" : "incomplete");
    // Keep the newest few.
    const auto backups = SettingsBackups();
    for (size_t i = KeptSettingsBackups; i < backups.size(); ++i) {
        QDir(backups[i].first).removeRecursively();
    }
    return ok ? folder : QString{};
}

std::vector<std::pair<QString, QString>> SettingsBackups() {
    const QDir root(QDir(ConfigDirPath()).filePath(QStringLiteral("backups")));
    std::vector<std::pair<QString, QString>> backups;
    // Folder names sort by time.
    const QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QString& name : names) {
        const QDateTime when =
            QDateTime::fromString(name, QString::fromLatin1(BackupFolderFormat));
        if (!when.isValid() || !QFile::exists(QDir(root.filePath(name)).filePath(QStringLiteral("qt-config.ini")))) {
            continue;
        }
        backups.emplace_back(root.filePath(name),
                             QLocale().toString(when, QStringLiteral("d MMM yyyy, HH:mm")));
    }
    return backups;
}

bool RestoreSettingsFiles(const QString& backup) {
    const QDir config(ConfigDirPath());
    const QDir from(backup);
    if (!from.exists(QStringLiteral("qt-config.ini"))) {
        return false;
    }
    bool ok = true;
    for (const QString name : {QStringLiteral("qt-config.ini"), QStringLiteral("frizti.ini")}) {
        if (from.exists(name)) {
            QFile::remove(config.filePath(name));
            ok &= QFile::copy(from.filePath(name), config.filePath(name));
        }
    }
    QDir(config.filePath(QStringLiteral("custom"))).removeRecursively();
    ok &= CopyDirFiles(from.filePath(QStringLiteral("custom")),
                       config.filePath(QStringLiteral("custom")));
    LOG_INFO(Frontend, "Settings restored from {}: {}", backup.toStdString(), ok ? "done" : "incomplete");
    return ok;
}

QString& PendingSettingsRestore() {
    static QString pending;
    return pending;
}

void OpenDiscord() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(DiscordInvite)));
}

void OpenDonate() {
    QDesktopServices::openUrl(QUrl(QString::fromLatin1(DonateLink)));
}

namespace {
QString OcarinaGameConfigPath() {
    return QDir(ConfigDirPath()).filePath(QStringLiteral("custom/0400486024DEC000.ini"));
}
} // namespace

QStringList OcarinaGameOverrides() {
    QFile file(OcarinaGameConfigPath());
    QStringList names;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return names;
    }
    const QString suffix = QStringLiteral("\\use_global=false");
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.endsWith(suffix)) {
            names << line.chopped(suffix.size());
        }
    }
    return names;
}

bool ClearOcarinaGameOverrides() {
    QFile file(OcarinaGameConfigPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray text = file.readAll();
    file.close();
    text.replace("\\use_global=false", "\\use_global=true");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const bool ok = file.write(text) == text.size();
    LOG_INFO(Frontend, "Ocarina of Time's game-specific settings back on the global ones: {}",
             ok ? "done" : "failed");
    return ok;
}

} // namespace Ocarina2Ui
