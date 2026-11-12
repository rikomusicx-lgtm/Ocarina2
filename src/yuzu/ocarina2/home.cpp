// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/home.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

#include <QApplication>
#include <QBoxLayout>

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QVariant>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "qt_common/game_list/game_list_p.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/saves_window.h"
#include "yuzu/ocarina2/whats_new.h"
#include <QStyle>
#include <QStyleOption>

namespace Ocarina2Ui {
namespace {

constexpr u64 OcarinaOfTime = 0x0400486024DEC000ULL;

/// A dock button whose icon swells with an OutBack bounce on hover and shrinks back the
/// same way when the pointer leaves - the "bubbly" feedback every Switch 2 button has.
class BounceToolButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            auto* bounce = new QPropertyAnimation(this, "iconSize", this);
            bounce->setDuration(240);
            bounce->setEasingCurve(QEasingCurve::OutBack);
            bounce->setStartValue(iconSize());
            bounce->setEndValue(event->type() == QEvent::Enter ? QSize(40, 40)
                                                               : QSize(32, 32));
            bounce->start(QAbstractAnimation::DeleteWhenStopped);
            return true;
        }
        return QToolButton::event(event);
    }
};

struct Game {
    QString title;
    QString path;
    u64 program_id = 0;
    QPixmap icon;
};

QPixmap RoundedCard(const QPixmap& source, const QSize& size, int radius) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap out(size * ratio);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    if (source.isNull()) {
        QPainter painter(&out);
        painter.fillRect(QRect(0, 0, size.width(), size.height()), QColor(0x24, 0x23, 0x2c));
        return out;
    }
    const QPixmap big = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QSize grown = big.size();
    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
        painter.setClipPath(clip);
        painter.drawPixmap((size.width() - grown.width()) / 2,
                           (size.height() - grown.height()) / 2, big);
    }
    return out;
}

/// True for a missing or fully transparent icon (some dumps have none).
bool IsBlank(const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return true;
    }
    const QImage image = pixmap.toImage();
    const int step_x = std::max(1, image.width() / 16);
    const int step_y = std::max(1, image.height() / 16);
    for (int y = 0; y < image.height(); y += step_y) {
        for (int x = 0; x < image.width(); x += step_x) {
            if (qAlpha(image.pixel(x, y)) != 0) {
                return false;
            }
        }
    }
    return true;
}

/// A cover the player put next to the game file: "<file name>.png" or "icon.png" (the same
/// files Game Settings > Detect Icon looks at first). Read only; the game file is never changed.
QPixmap LocalCover(const QString& game_path) {
    const QFileInfo info(game_path);
    const QDir dir = info.absoluteDir();
    for (const QString& base : {info.completeBaseName(), QStringLiteral("icon")}) {
        for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
            const QFileInfo candidate(dir.filePath(base + QLatin1String(ext)));
            if (!candidate.isFile() || candidate.size() > 16 * 1024 * 1024) {
                continue;
            }
            QPixmap pixmap;
            if (pixmap.load(candidate.filePath())) {
                return pixmap;
            }
        }
    }
    return {};
}

std::vector<Game> CollectGames(QStandardItemModel* model, const QPixmap& fallback) {
    std::vector<Game> games;
    std::set<QString> seen;
    if (!model) {
        return games;
    }
    const auto add = [&](const QStandardItem* item) {
        if (item->data(GameListItem::TypeRole).toInt() !=
            static_cast<int>(GameListItemType::Game)) {
            return;
        }
        const QString path = item->data(GameListItemPath::FullPathRole).toString();
        if (path.isEmpty() || !seen.insert(path).second) {
            return;
        }
        Game game;
        game.path = path;
        game.program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
        game.title = item->data(GameListItemPath::TitleRole).toString();
        if (game.title.isEmpty()) {
            game.title = QFileInfo(path).completeBaseName();
        }
        const QByteArray icon_data = item->data(GameListItemPath::IconDataRole).toByteArray();
        if (!game.icon.loadFromData(icon_data)) {
            game.icon = item->data(Qt::DecorationRole).value<QPixmap>();
        }
        if (IsBlank(game.icon)) {
            game.icon = LocalCover(path);
        }
        if (IsBlank(game.icon)) {
            // No icon in the dump and none next to it: Ocarina2's own icon, not an empty square.
            game.icon = fallback;
        }
        games.push_back(std::move(game));
    };
    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem* top = model->item(row);
        if (!top) {
            continue;
        }
        if (top->hasChildren()) {
            for (int child = 0; child < top->rowCount(); ++child) {
                add(top->child(child));
            }
        } else {
            add(top);
        }
    }
    return games;
}

} // namespace

/// A background page that paints the ring of black-outlined green vines behind the home
/// content. The vines never get mouse events (children answer clicks), so they can never
/// get in the way.
class Ocarina2HomePage : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QStyleOption option{};
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter);
        DrawVines(painter, rect());
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static QPainterPath Leaf(double length, double width) {
        QPainterPath leaf;
        leaf.moveTo(0, 0);
        leaf.quadTo(width * 0.5, -length * 0.42, 0, -length);
        leaf.quadTo(-width * 0.5, -length * 0.42, 0, 0);
        leaf.closeSubpath();
        return leaf;
    }

    static void DrawLeaf(QPainter& p, const QPointF& pos, double angle, double length,
                         double width, const QColor& fill) {
        p.save();
        p.translate(pos);
        p.rotate(angle);
        const QPainterPath leaf = Leaf(length, width);
        p.fillPath(leaf, QBrush(fill));
        p.setPen(QPen(QColor("#0d0d0d"), 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.strokePath(leaf, p.pen());
        p.drawLine(QPointF(0, -2), QPointF(0, -length + 3));
        p.restore();
    }

    static double Wobble(double degrees) {
        return std::sin(degrees * 4.0 * kPi / 180.0) * 6.0 +
               std::sin(degrees * 9.0 * kPi / 180.0 + 1.3) * 2.5;
    }

    static void DrawVines(QPainter& p, const QRect& area) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const double cx = area.center().x();
        const double cy = area.top() + area.height() * 0.34;
        const double radius =
            std::min(static_cast<double>(area.width()), static_cast<double>(area.height())) * 0.23;

        // The vine ring around the hero.
        QPainterPath ring;
        for (double deg = 0; deg <= 360.0; deg += 6.0) {
            const double a = deg * kPi / 180.0;
            const double rr = radius + Wobble(deg);
            const QPointF pt{cx + rr * std::cos(a), cy + rr * std::sin(a)};
            deg == 0.0 ? ring.moveTo(pt) : ring.lineTo(pt);
        }
        ring.closeSubpath();
        const QPen outline(QColor("#0d0d0d"), 7.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const QPen vine(QColor("#2f7a2f"), 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const QPen vine_hi(QColor("#57a157"), 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.strokePath(ring, outline);
        p.strokePath(ring, vine);
        p.strokePath(ring, vine_hi);

        // Leaves on the ring, alternating in and out.
        for (double deg = 10; deg < 360; deg += 26) {
            const double a = deg * kPi / 180.0;
            const double rr = radius + Wobble(deg);
            const QPointF pos(cx + rr * std::cos(a), cy + rr * std::sin(a));
            const bool outer = static_cast<int>(deg / 26) % 2 == 0;
            const double radial = std::atan2(pos.y() - cy, pos.x() - cx) * 180.0 / kPi;
            DrawLeaf(p, pos, radial + (outer ? 90 : -90), 17, 9,
                     outer ? QColor("#3f8f3f") : QColor("#57a157"));
        }

        // Tendrils crawling outward from the ring.
        struct Tendril {
            double deg;
            double curl;
            double length;
        };
        const std::vector<Tendril> tendrils{{-90, 70, 125}, {-35, -85, 150}, {30, 75, 140},
                                            {75, -65, 120}, {155, 95, 155}, {205, -75, 135},
                                            {245, 60, 145}, {290, -55, 120}, {325, 65, 140}};
        for (const Tendril& t : tendrils) {
            const double a = t.deg * kPi / 180.0;
            const QPointF dir_out{std::cos(a), std::sin(a)};
            const QPointF dir_curl{-std::sin(a), std::cos(a)};
            const QPointF start{cx + radius * std::cos(a), cy + radius * std::sin(a)};
            const QPointF c1 = start + dir_out * (t.length * 0.42) + dir_curl * (t.curl * 0.35);
            const QPointF c2 = start + dir_out * (t.length * 0.75) - dir_curl * (t.curl * 0.55);
            const QPointF end = start + dir_out * t.length + dir_curl * (t.curl * 0.5);
            QPainterPath tp;
            tp.moveTo(start);
            tp.cubicTo(c1, c2, end);
            p.strokePath(tp, QPen(QColor("#0d0d0d"), 6.0, Qt::SolidLine, Qt::RoundCap,
                                  Qt::RoundJoin));
            p.strokePath(tp, vine);
            p.strokePath(tp, vine_hi);
            for (const double f : {0.3, 0.62, 0.92}) {
                const QPointF pt = tp.pointAtPercent(f);
                const double tangent = tp.angleAtPercent(f);
                DrawLeaf(p, pt, tangent - 90.0, 13, 7.5, QColor("#3f8f3f"));
            }
        }
    }
};

Home::Home(QMainWindow* window_, QBoxLayout* central, QWidget* game_list_, QWidget* placeholder_,
           HomeHooks hooks_)
    : QObject(window_), window{window_}, game_list{game_list_}, placeholder{placeholder_},
      hooks{std::move(hooks_)} {
    // The classic menus live behind the dock's More button. A zero-height bar keeps every
    // shortcut working and stays out of the way when fullscreen shows the bar again.
    window->menuBar()->setFixedHeight(0);

    page = MakePage();
    central->addWidget(page, 1);

    game_list->installEventFilter(this);
    placeholder->installEventFilter(this);

    window->installEventFilter(this);  // keep the floating Back button centered at the bottom

    back_button = new QPushButton(window);
    back_button->setObjectName(QStringLiteral("Ocarina2BackButton"));
    back_button->setText(tr("Back"));
    back_button->setIcon(Glyph(QStringLiteral("arrow-left"), 0xcfc9e3, 18));
    back_button->setIconSize(QSize(18, 18));
    back_button->setCursor(Qt::PointingHandCursor);
    back_button->setFocusPolicy(Qt::StrongFocus);
    back_button->setFixedHeight(38);
    back_button->setFixedWidth(120);
    back_button->hide();
    back_button->installEventFilter(this);
    connect(back_button, &QPushButton::clicked, this, &Home::ShowHome);

    refresh_timer = new QTimer(this);
    refresh_timer->setSingleShot(true);
    refresh_timer->setInterval(150);
    connect(refresh_timer, &QTimer::timeout, this, &Home::Refresh);
    if (hooks.model) {
        const auto later = [this] { refresh_timer->start(); };
        connect(hooks.model, &QAbstractItemModel::rowsInserted, this, later);
        connect(hooks.model, &QAbstractItemModel::rowsRemoved, this, later);
        connect(hooks.model, &QAbstractItemModel::modelReset, this, later);
        connect(hooks.model, &QAbstractItemModel::dataChanged, this, later);
    }
    QTimer::singleShot(0, this, &Home::Restore);
}

// The dock is the Switch 2-style home bar: a row of round system buttons centered at the
// bottom of the window, each with a caption. It drives the same actions as before.
QWidget* Home::MakeRail() {
    rail = new QWidget;
    rail->setObjectName(QStringLiteral("Ocarina2Dock"));
    rail->setAttribute(Qt::WA_StyledBackground);
    rail->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* layout = new QHBoxLayout(rail);
    layout->setContentsMargins(26, 14, 26, 14);
    layout->setSpacing(18);
    layout->setAlignment(Qt::AlignHCenter);

    const auto make = [this](const QString& icon, const QString& caption, const QString& tip) {
        auto* cell = new QWidget(rail);
        cell->setObjectName(QStringLiteral("Ocarina2DockCell"));
        auto* cell_layout = new QVBoxLayout(cell);
        cell_layout->setContentsMargins(0, 6, 0, 0);
        cell_layout->setSpacing(4);
        cell_layout->setAlignment(Qt::AlignHCenter);

        auto* button = new BounceToolButton(cell);
        button->setObjectName(QStringLiteral("Ocarina2DockButton"));
        button->setProperty("band", 3);
        button->setFixedSize(84, 84);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(Icon(icon, 32));
        button->setIconSize(QSize(32, 32));
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        auto* shadow = new QGraphicsDropShadowEffect(button);
        shadow->setBlurRadius(20);
        shadow->setOffset(0, 3);
        shadow->setColor(QColor(0, 0, 0, 140));
        button->setGraphicsEffect(shadow);
        cell_layout->addWidget(button, 0, Qt::AlignHCenter);

        auto* label = new QLabel(cell);
        label->setObjectName(QStringLiteral("Ocarina2DockLabel"));
        label->setText(caption);
        label->setAlignment(Qt::AlignHCenter);
        cell_layout->addWidget(label, 0, Qt::AlignHCenter);
        return button;
    };

    home_button = make(QStringLiteral("home"), tr("Home"), tr("Home screen"));
    connect(home_button, &QToolButton::clicked, this, &Home::ShowHome);

    library_button = make(QStringLiteral("list"), tr("Games"), tr("Show every game"));
    connect(library_button, &QToolButton::clicked, this, &Home::ShowList);

    folder_button = make(QStringLiteral("folder"), tr("Add games"), tr("Add a games folder"));
    connect(folder_button, &QToolButton::clicked, this, [this] { hooks.add_folder(); });

    settings_button = make(QStringLiteral("settings"), tr("Settings"),
                           tr("Open the configuration dialog"));
    connect(settings_button, &QToolButton::clicked, this, [this] { hooks.open_settings(); });

    update_button =
        make(QStringLiteral("star"), tr("What's new"), tr("See what's new in this build"));
    connect(update_button, &QToolButton::clicked, this, [this] { ShowWhatsNew(window); });

    auto* classic = new QMenu(rail);
    for (QMenu* menu : hooks.classic_menus) {
        classic->addMenu(menu);
    }
    menu_button = make(QStringLiteral("menu"), tr("More"), tr("Show all menus"));
    connect(menu_button, &QToolButton::clicked, this, [classic, this] {
        const QSize size = classic->sizeHint();
        classic->exec(menu_button->mapToGlobal(QPoint(0, menu_button->height() - size.height())));
    });

    for (QWidget* button :
         {home_button, library_button, folder_button, settings_button, update_button,
          menu_button}) {
        layout->addWidget(button->parentWidget());
        button->installEventFilter(this);
    }
    return rail;
}

QWidget* Home::MakePage() {
    page = new Ocarina2HomePage;
    page->setObjectName(QStringLiteral("Ocarina2Home"));
    page->setAttribute(Qt::WA_StyledBackground);
    page->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(page, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        ShowGameMenu(hero_path, hero_id, page->mapToGlobal(pos));
    });

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The Switch 2 top bar: player avatar, Home/Games switch, network glyphs and the clock.
    topbar = new QWidget(page);
    topbar->setObjectName(QStringLiteral("Ocarina2TopBar"));
    topbar->setAttribute(Qt::WA_StyledBackground);
    auto* topb = new QHBoxLayout(topbar);
    topb->setContentsMargins(40, 0, 40, 0);
    topb->setSpacing(14);

    avatar_button = new QToolButton(topbar);
    avatar_button->setObjectName(QStringLiteral("Ocarina2Avatar"));
    avatar_button->setProperty("band", 0);
    avatar_button->setIcon(Glyph(QStringLiteral("triforce"), 0x3fd8ff, 12));
    avatar_button->setIconSize(QSize(12, 12));
    avatar_button->setToolTip(tr("Player 1 - toggle fullscreen"));
    avatar_button->setCursor(Qt::PointingHandCursor);
    avatar_button->setFocusPolicy(Qt::StrongFocus);
    connect(avatar_button, &QToolButton::clicked, this, [this] { hooks.toggle_fullscreen(); });
    topb->addWidget(avatar_button, 0, Qt::AlignVCenter);

    auto* word = new QLabel(topbar);
    word->setObjectName(QStringLiteral("Ocarina2Wordmark"));
    word->setText(QObject::tr("OCARINA2"));
    QFont wordfont = word->font();
    wordfont.setLetterSpacing(QFont::AbsoluteSpacing, 4.5);
    wordfont.setPointSize(11);
    wordfont.setBold(true);
    word->setFont(wordfont);
    topb->addWidget(word, 0, Qt::AlignVCenter);
    topb->addStretch(1);

    const auto makeTopIcon = [&](const QString& icon, const QString& tip) {
        auto* label = new QLabel(topbar);
        label->setObjectName(QStringLiteral("Ocarina2TopIcon"));
        label->setPixmap(Icon(icon, 18).pixmap(18, 18));
        label->setToolTip(tip);
        return label;
    };
    topb->addWidget(makeTopIcon(QStringLiteral("wifi"), tr("Wi-Fi connected")), 0,
                    Qt::AlignVCenter);
    topb->addWidget(makeTopIcon(QStringLiteral("gamepad"), tr("Pro Controller")), 0,
                    Qt::AlignVCenter);
    // Fritchii v5.8.3: the Saves window (export, import, copy saves between Ocarina2, Eden and
    // Lichii). In the top bar, next to the other system buttons.
    auto* saves = new QPushButton(Icon(QStringLiteral("list"), 16), tr("Saves"), topbar);
    saves->setObjectName(QStringLiteral("Ocarina2Link"));
    saves->setIconSize(QSize(16, 16));
    saves->setCursor(Qt::PointingHandCursor);
    saves->setFocusPolicy(Qt::NoFocus);
    connect(saves, &QPushButton::clicked, this,
            [this] { ShowSavesWindow(window, hooks.emulation_running); });
    topb->addWidget(saves, 0, Qt::AlignVCenter);
    clock_label = new QLabel(topbar);
    clock_label->setObjectName(QStringLiteral("Ocarina2Clock"));
    clock_label->setText(QDateTime::currentDateTime().toString(QStringLiteral("h:mm AP")));
    topb->addWidget(clock_label, 0, Qt::AlignVCenter);
    layout->addWidget(topbar);

    clock_timer = new QTimer(this);
    clock_timer->setInterval(1000);
    connect(clock_timer, &QTimer::timeout, this, [this] {
        if (clock_label) {
            clock_label->setText(QDateTime::currentDateTime().toString(
                QStringLiteral("h:mm AP")));
        }
    });
    clock_timer->start();

    // The "current game" row sits inside the middle column, directly below the hero image,
    // centered.
    auto* middle = new QWidget(page);
    auto* mid = new QVBoxLayout(middle);
    mid->setSpacing(0);
    mid->setAlignment(Qt::AlignHCenter);
    layout->addWidget(middle, 1);
    mid->addStretch(3);

    play = new QToolButton(middle);
    play->setObjectName(QStringLiteral("Ocarina2Hero"));
    play->setProperty("band", 1);
    play->setCursor(Qt::PointingHandCursor);
    play->setToolButtonStyle(Qt::ToolButtonIconOnly);
    play->setFocusPolicy(Qt::StrongFocus);
    play->setIconSize(QSize(240, 240));
    play->setFixedSize(252, 252);
    play->installEventFilter(this);
    connect(play, &QToolButton::clicked, this, [this] {
        if (hero_path.isEmpty()) {
            hooks.add_folder();
        } else {
            hooks.play(hero_path, hero_id);
        }
    });
    auto* play_effect = new QGraphicsDropShadowEffect;
    play_effect->setBlurRadius(44);
    play_effect->setOffset(0, 8);
    play_effect->setColor(QColor(63, 216, 255, 120));
    play->setGraphicsEffect(play_effect);
    mid->addWidget(play, 0, Qt::AlignHCenter);
    mid->addSpacing(16);

    // The highlighted game's name sits directly below the hero image, centered with its bookmark.
    game_row = new QWidget(middle);
    game_row->setObjectName(QStringLiteral("Ocarina2CurrentGameRow"));
    auto* rowl = new QHBoxLayout(game_row);
    rowl->setContentsMargins(0, 0, 0, 0);
    rowl->setSpacing(8);
    rowl->setAlignment(Qt::AlignHCenter);
    auto* bookmark = new QLabel(game_row);
    bookmark->setObjectName(QStringLiteral("Ocarina2CurrentGameIcon"));
    bookmark->setPixmap(Glyph(QStringLiteral("bookmark"), 0x3fd8ff, 15).pixmap(15, 15));
    rowl->addWidget(bookmark, 0, Qt::AlignVCenter);
    game_row_title = new QLabel(game_row);
    game_row_title->setObjectName(QStringLiteral("Ocarina2CurrentGame"));
    auto* name_glow = new QGraphicsDropShadowEffect(game_row_title);
    name_glow->setBlurRadius(18);
    name_glow->setOffset(0, 0);
    name_glow->setColor(QColor(167, 139, 250, 235));  // violet glow behind the text
    game_row_title->setGraphicsEffect(name_glow);
    rowl->addWidget(game_row_title, 0, Qt::AlignVCenter);
    mid->addWidget(game_row, 0, Qt::AlignHCenter);
    mid->addStretch(1);

    // The dock rides inside the page; a floating rounded plate carries the buttons, like the
    // Switch 2 home screen.
    layout->addWidget(MakeRail(), 0, Qt::AlignHCenter);
    layout->setContentsMargins(0, 0, 0, 18);
    return page;
}

void Home::SelectGame(const QString& path, u64 program_id, const QString& title_text) {
    hero_path = path;
    hero_id = program_id;
    hero_title = title_text;
    if (game_row) {
        game_row->setVisible(!path.isEmpty());
    }
    if (game_row_title) {
        game_row_title->setText(hero_title);
    }
}

void Home::ShowGameMenu(const QString& path, u64 program_id, const QPoint& global_pos) {
    if (path.isEmpty()) {
        return;
    }
    QMenu menu;
    if (hooks.game_menu) {
        hooks.game_menu(menu, program_id, path.toStdString());
    } else {
        menu.addAction(tr("Game Settings..."), &menu, [this, path, program_id] {
            hooks.game_settings(program_id, path.toStdString());
        });
    }
    menu.addSeparator();
    menu.addAction(tr("Delete Shader Cache..."), &menu,
                   [this, program_id] { hooks.delete_shader_cache(program_id); });
    menu.exec(global_pos);
}

void Home::Refresh() {
    const std::vector<Game> games = CollectGames(hooks.model, window->windowIcon().pixmap(256));

    // The hero: the game picked last, else Ocarina of Time, else the first game.
    const Game* hero = nullptr;
    for (const Game& game : games) {
        if (!hero_path.isEmpty() && game.path == hero_path) {
            hero = &game;
            break;
        }
    }
    if (!hero) {
        for (const Game& game : games) {
            if (game.program_id == OcarinaOfTime) {
                hero = &game;
                break;
            }
        }
    }
    if (!hero && !games.empty()) {
        hero = &games.front();
    }

    if (hero) {
        play->setIcon(QIcon(RoundedCard(hero->icon, QSize(240, 240), 18)));
        SelectGame(hero->path, hero->program_id, hero->title);
    } else {
        SelectGame(QString(), 0, tr("Ocarina2"));
        play->setIcon(Glyph(QStringLiteral("plus"), 0xffffff, 72));
    }

    // The games live behind the dock's Games button (a full list); there is no tile row
    // duplicating the hero on the home screen.
    RebuildNavOrder();
}

void Home::PositionBackButton() {
    if (!back_button || !window) {
        return;
    }
    // The games list keeps a filter bar at the very bottom; float the button above it.
    constexpr int kBottomMargin = 90;
    back_button->setGeometry((window->width() - back_button->width()) / 2,
                             window->height() - back_button->height() - kBottomMargin,
                             back_button->width(), back_button->height());
}

void Home::ShowHome() {
    list_mode = false;
    game_list->hide();
    placeholder->hide();
    rail->show();
    page->show();
    back_button->hide();
    home_button->setFocus(Qt::OtherFocusReason);
    nav_index_ = 0;
    Refresh();
}

void Home::ShowList() {
    list_mode = true;
    page->hide();
    rail->show();
    library_button->setFocus(Qt::OtherFocusReason);
    PositionBackButton();
    back_button->show();
    back_button->raise();
    nav_index_ = 1;  // the "Games" button in the dock
    if (hooks.game_list_empty()) {
        placeholder->show();
    } else {
        game_list->show();
    }
}

void Home::Restore() {
    // A restart shows the list for a moment, and nothing may cover a running game.
    if (hooks.single_window() && hooks.emulation_running()) {
        return;
    }
    if (list_mode) {
        rail->show();
        page->hide();
    } else {
        ShowHome();
    }
}

void Home::OnEmulationStarting() {
    if (!hooks.single_window()) {
        return;
    }
    rail->hide();
    page->hide();
    back_button->hide();
}

bool Home::HandleNavKey(QKeyEvent* event) {
    const int key = event->key();
    QString focus_tag;
    if (QWidget* fw = QApplication::focusWidget()) {
        focus_tag = QString::fromUtf8(fw->metaObject()->className()) + QStringLiteral("/") +
                    fw->objectName();
        if (auto* t = qobject_cast<QToolButton*>(fw)) {
            focus_tag += QStringLiteral(" tip=") + t->toolTip();
        }
    }
    LOG_INFO(Frontend, "Ocarina2 nav: key={} focus={}", key, focus_tag.toStdString());
    if (nav_order_.isEmpty()) {
        RebuildNavOrder();
    }

    // The controller's Home button goes straight to the home screen, like the console.
    if (key == Qt::Key_Home) {
        ShowHome();
        return true;
    }

    // B is the console's Back: on the games screen, it returns home.
    if (key == Qt::Key_Escape && list_mode) {
        ShowHome();
        return true;
    }

    // A activates whatever button has focus, like the console's confirm key.
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (auto* button = qobject_cast<QAbstractButton*>(QApplication::focusWidget())) {
            button->click();
            return true;
        }
        return false;
    }

    int dx = 0;
    int dy = 0;
    switch (key) {
        case Qt::Key_Left:
            dx = -1;
            break;
        case Qt::Key_Right:
            dx = 1;
            break;
        case Qt::Key_Up:
            dy = -1;
            break;
        case Qt::Key_Down:
            dy = 1;
            break;
        default:
            return false;
    }

    // Explicit rows, like the Switch 2: Up/Down jumps between rows (keeping rough horizontal
    // position), Left/Right walks the row. Rows: 0 = top bar, 1 = hero image, 3 = dock.
    const auto band_of = [](QWidget* w) {
        bool ok = false;
        const int band = w->property("band").toInt(&ok);
        return ok ? band : 3;
    };
    QWidget* current = QApplication::focusWidget();
    if (current == nullptr || !nav_order_.contains(current)) {
        nav_order_[0]->setFocus(Qt::OtherFocusReason);
        return true;
    }
    const int count = nav_order_.size();
    if (count == 0) {
        return false;
    }
    const int band = band_of(current);
    const int target_band = std::clamp(band + dy, 0, 3);
    if (dx != 0) {
        QList<QWidget*> row;
        for (QWidget* w : nav_order_) {
            if (band_of(w) == band && w->isVisible() && w->isEnabled()) {
                row.append(w);
            }
        }
        if (row.isEmpty()) {
            return false;
        }
        const int i = row.indexOf(current);
        const int step = dx > 0 ? 1 : -1;
        const int next = ((i < 0 ? 0 : i) + step + row.size()) % row.size();
        row[next]->setFocus(Qt::OtherFocusReason);
        return true;
    }
    if (target_band == band) {
        return false;
    }
    QList<QWidget*> row;
    for (QWidget* w : nav_order_) {
        if (band_of(w) == target_band && w->isVisible() && w->isEnabled()) {
            row.append(w);
        }
    }
    if (row.isEmpty()) {
        return false;
    }
    const double from_x = current->mapToGlobal(current->rect().center()).x();
    QWidget* best = row.front();
    double best_distance = std::numeric_limits<double>::infinity();
    for (QWidget* w : row) {
        const double distance = std::abs(w->mapToGlobal(w->rect().center()).x() - from_x);
        if (distance < best_distance) {
            best_distance = distance;
            best = w;
        }
    }
    if (best) {
        best->setFocus(Qt::OtherFocusReason);
        return true;
    }
    return false;
}

void Home::RebuildNavOrder() {
    nav_order_.clear();
    if (avatar_button) {
        nav_order_.append(avatar_button);
    }
    nav_order_.append(play);
    if (list_mode && back_button) {
        back_button->setProperty("band", 3);
        nav_order_.append(back_button);
    }
    nav_order_.append(home_button);
    nav_order_.append(library_button);
    nav_order_.append(folder_button);
    nav_order_.append(settings_button);
    nav_order_.append(update_button);
    nav_order_.append(menu_button);
    // Focus stays on the dock's Home button at rest; clamps handle list mode.
    if (nav_index_ < 0 || nav_index_ >= nav_order_.size()) {
        nav_index_ = std::max(0, static_cast<int>(nav_order_.size()) - 8);
    }
}

bool Home::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window && event->type() == QEvent::Resize) {
        PositionBackButton();
        return false;
    }
    // The main window shows its game list (start up, a game stopped, the list filled): show
    // the home page instead, unless the player asked for the full list.
    if (event->type() == QEvent::Show && (watched == game_list || watched == placeholder)) {
        QTimer::singleShot(0, this, &Home::Restore);
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        auto* t = qobject_cast<const QToolButton*>(watched);
        LOG_INFO(Frontend, "Ocarina2 keypress on widget={} obj={} tip={} key={}",
                 watched ? watched->metaObject()->className() : "null",
                 t ? t->objectName().toStdString() : std::string{},
                 t ? t->toolTip().toStdString() : std::string{},
                 key->key());
        // X (and the keyboard's Menu key) opens the options for the focused game.
        if (key->key() == Qt::Key_Menu) {
            if (auto* button = qobject_cast<QToolButton*>(watched);
                button && button->property("path").isValid()) {
                ShowGameMenu(button->property("path").toString(),
                             button->property("id").toULongLong(),
                             button->mapToGlobal(button->rect().center()));
                return true;
            }
            if (watched == play) {
                ShowGameMenu(hero_path, hero_id, play->mapToGlobal(play->rect().center()));
                return true;
            }
        }
        if (auto* widget = qobject_cast<QWidget*>(watched);
            watched && nav_order_.contains(widget) && HandleNavKey(key)) {
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Ocarina2Ui
