// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/home.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QModelIndex>
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
#include "yuzu/ocarina2/home_panel.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/saves_window.h"
#include "yuzu/ocarina2/sound.h"
#include "yuzu/ocarina2/tile_strip.h"
#include "yuzu/ocarina2/whats_new.h"
#include <QStyle>
#include <QStyleOption>
#include <QTextBrowser>

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

/// The page background: either a user-supplied image in the profile folder — drop a
/// "background.png" (or .jpg / .jpeg, max 16 MB) into the ocarina2 profile folder and the home
/// screen uses it, fitted to fill the page — or the built-in Hyrule scene baked in as a qrc
/// resource. The painted art never gets mouse events, so it can never get in the way.
static QPixmap ResolveHomeBg() {
    const QString profile =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::EdenDir));
    for (const char* name : {"background.png", "background.jpg", "background.jpeg"}) {
        const QFileInfo candidate(profile + QLatin1Char('/') + QLatin1String(name));
        if (!candidate.isFile() || candidate.size() > 16 * 1024 * 1024) {
            continue;
        }
        QPixmap pixmap(candidate.filePath());
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }
    return QPixmap(QStringLiteral(":/ocarina2/assets/zelda-bg.png"));
}

class Ocarina2HomePage : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QStyleOption option{};
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter);
        const QPixmap& art = ZeldaBg();
        const QPixmap art_scaled =
            art.scaled(rect().size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint pos(rect().center().x() - art_scaled.width() / 2,
                         rect().center().y() - art_scaled.height() / 2);
        painter.drawPixmap(QRect(pos, art_scaled.size()), art_scaled);
    }

private:
    static const QPixmap& ZeldaBg() {
        static const QPixmap art = ResolveHomeBg();
        return art;
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

    // One filter, one handler per event: it sees the whole tree exactly once, so no double-
    // handling between the leaf widgets and their parents.
    window->installEventFilter(this);

    // Fix the hero placement once we know our real size.
    QTimer::singleShot(0, this, [this] { FixHomeLayout(); });

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
    connect(back_button, &QPushButton::clicked, this, [this] {
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Back);
        ShowHome();
    });

    // The game list builds itself on a worker after the UI comes up, so the first Refresh() can
    // see an empty model. Poll a few times over the first seconds so the strip fills itself in.
    auto* warmup = new QTimer(this);
    warmup->setInterval(1500);
    int calls = 0;
    connect(warmup, &QTimer::timeout, this, [this, warmup, calls]() mutable {
        if (++calls > 15 || (hooks.model && hooks.model->rowCount() > 0 && calls > 3)) {
            warmup->stop();
            if (hooks.model && hooks.model->rowCount() > 0) {
                Refresh();
            }
            return;
        }
        Refresh();
    });
    warmup->start();

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
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignHCenter);

    const auto make = [this](const QString& icon, const QString& caption, const QString& tip,
                             bool home) {
        auto* button = new BounceToolButton(rail);
        button->setObjectName(home ? QStringLiteral("Ocarina2DockHome")
                                   : QStringLiteral("Ocarina2DockCircle"));
        button->setProperty("band", 4);
        button->setProperty("caption", caption);
        button->setFixedSize(home ? 64 : 56, home ? 64 : 56);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(Icon(icon, home ? 26 : 22));
        button->setIconSize(QSize(home ? 26 : 22, home ? 26 : 22));
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->installEventFilter(this);
        connect(button, &QToolButton::clicked, this,
                [] { SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm); });
        return button;
    };

    home_button = make(QStringLiteral("home"), tr("Home"), tr("Home screen"), /*home=*/true);
    connect(home_button, &QToolButton::clicked, this, &Home::ShowHome);

    library_button = make(QStringLiteral("list"), tr("Games"), tr("Show every game"), false);
    connect(library_button, &QToolButton::clicked, this, &Home::ShowList);

    folder_button = make(QStringLiteral("folder"), tr("Add games"), tr("Add games to the list"), false);
    connect(folder_button, &QToolButton::clicked, this, [this] { ShowAddGamesPanel(); });

    settings_button =
        make(QStringLiteral("settings"), tr("Settings"), tr("Open settings"), false);
    connect(settings_button, &QToolButton::clicked, this, [this] { hooks.open_settings(); });

    update_button =
        make(QStringLiteral("star"), tr("What's new"), tr("See what's new in this build"), false);
    connect(update_button, &QToolButton::clicked, this, [this] { ShowWhatsNewPanel(); });

    auto* classic = new QMenu(rail);
    for (QMenu* menu : hooks.classic_menus) {
        classic->addMenu(menu);
    }
    menu_button = make(QStringLiteral("menu"), tr("More"), tr("Show all menus"), false);
    connect(menu_button, &QToolButton::clicked, this, [classic, this] {
        const QSize size = classic->sizeHint();
        classic->exec(menu_button->mapToGlobal(QPoint(0, menu_button->height() - size.height())));
    });

    for (QWidget* button :
         {home_button, library_button, folder_button, settings_button, update_button,
          menu_button}) {
        layout->addWidget(button);
    }
    return rail;
}

QWidget* Home::MakePage() {
    page = new Ocarina2HomePage;
    page->setObjectName(QStringLiteral("Ocarina2Home"));
    page->setAttribute(Qt::WA_StyledBackground);
    page->setContextMenuPolicy(Qt::CustomContextMenu);
    page->installEventFilter(this);
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
    topbar->setFixedHeight(46);
    auto* topb = new QHBoxLayout(topbar);
    topb->setContentsMargins(16, 3, 16, 3);
    topb->setSpacing(10);

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
    wordfont.setLetterSpacing(QFont::AbsoluteSpacing, 2.6);
    wordfont.setPointSize(9);
    wordfont.setBold(true);
    word->setFont(wordfont);
    topb->addWidget(word, 0, Qt::AlignVCenter);
    topb->addStretch(1);

    const auto makeTopIcon = [&](const QString& icon, const QString& tip) {
        auto* label = new QLabel(topbar);
        label->setObjectName(QStringLiteral("Ocarina2TopIcon"));
        label->setPixmap(Icon(icon, 16).pixmap(16, 16));
        label->setToolTip(tip);
        return label;
    };
    topb->addWidget(makeTopIcon(QStringLiteral("wifi"), tr("Wi-Fi connected")), 0,
                    Qt::AlignVCenter);
    topb->addWidget(makeTopIcon(QStringLiteral("gamepad"), tr("Pro Controller")), 0,
                    Qt::AlignVCenter);
    // Fritchii v5.8.3: the Saves window (export, import, copy saves between Ocarina2, Eden and
    // Lichii). In the top bar, next to the other system buttons.
    auto* saves = new QPushButton(Icon(QStringLiteral("list"), 14), tr("Saves"), topbar);
    saves->setObjectName(QStringLiteral("Ocarina2Link"));
    saves->setIconSize(QSize(14, 14));
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

    tile_strip = new TileStrip(page);
    tile_strip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tile_strip->setProperty("band", 1);
    tile_strip->installEventFilter(this);
    connect(tile_strip, &TileStrip::Activated, this, [this](const QString& path, u64 program_id) {
        if (path.isEmpty()) {
            hooks.add_folder();
        } else {
            hooks.play(path, program_id);
        }
    });
    connect(tile_strip, &TileStrip::SelectionChanged, this,
            [this](const QString& title, u64 program_id) {
                hero_path = tile_strip->SelectedPath();
                hero_id = program_id;
                hero_title = title;
                if (game_row_title) {
                    game_row_title->setText(title);
                    game_row_title->setVisible(!title.isEmpty());
                }
            });
    layout->addWidget(tile_strip);

    // The dock rides in the middle of the page, like the Switch 2 home screen: a rounded
    // plate of round buttons; the focused item's name rides underneath it.
    layout->addWidget(MakeRail(), 0, Qt::AlignHCenter);

    // The focused item's name below the dock (the Switch 2 puts it exactly there).
    game_row = new QWidget(page);
    game_row->setObjectName(QStringLiteral("Ocarina2CurrentGameRow"));
    auto* rowl = new QHBoxLayout(game_row);
    rowl->setContentsMargins(0, 6, 0, 0);
    rowl->setSpacing(0);
    rowl->setAlignment(Qt::AlignHCenter);
    game_row_title = new QLabel(game_row);
    game_row_title->setObjectName(QStringLiteral("Ocarina2CurrentGame"));
    game_row_title->setAlignment(Qt::AlignHCenter);
    QFont title_font = game_row_title->font();
    title_font.setFamily(QStringLiteral("Inter"));
    title_font.setWeight(QFont::DemiBold);
    title_font.setPixelSize(17);
    game_row_title->setFont(title_font);
    rowl->addWidget(game_row_title, 0, Qt::AlignHCenter);
    layout->addWidget(game_row, 0, Qt::AlignHCenter);

    // The very bottom hint row, above the status bar: the connected controller on the left,
    // the console hints on the right (Switch 2 style).
    auto* hint_bar = new QWidget(page);
    hint_bar->setObjectName(QStringLiteral("Ocarina2HomeHintBar"));
    auto* hint_layout = new QHBoxLayout(hint_bar);
    hint_layout->setContentsMargins(24, 10, 24, 0);
    hint_layout->setSpacing(8);
    auto* pad = new QLabel(hint_bar);
    pad->setPixmap(Glyph(QStringLiteral("gamepad"), 0x8f8ca6, 16).pixmap(16, 16));
    pad->setToolTip(tr("Pro Controller - Player 1"));
    hint_layout->addWidget(pad, 0, Qt::AlignVCenter);
    auto* pad_label = new QLabel(QStringLiteral("Pro Controller"), hint_bar);
    pad_label->setObjectName(QStringLiteral("Ocarina2HomeHintText"));
    hint_layout->addWidget(pad_label, 0, Qt::AlignVCenter);
    hint_layout->addStretch(1);
    auto* hints = new QLabel(hint_bar);
    hints->setObjectName(QStringLiteral("Ocarina2HomeHintText"));
    hints->setText(QStringLiteral(
        "<span style='color:#e6e3ef;font-weight:800'>+</span>&nbsp; Options&nbsp;&nbsp;&nbsp;"
        "<span style='color:#e6e3ef;font-weight:800'>A</span>&nbsp; OK"));
    hint_layout->addWidget(hints, 0, Qt::AlignVCenter);
    hint_bar->setFixedHeight(38);
    layout->addWidget(hint_bar);
    layout->setContentsMargins(0, 0, 0, 4);
    return page;
}

void Home::FixHomeLayout() {
    if (!page || !tile_strip) {
        return;
    }
    // The strip and title are laid out by the page; only the back button and dock need pinning.
    if (game_row) {
        game_row->adjustSize();
    }
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

    // The strip holds every game, like the Switch 2 home menu: tiles slide left as you move
    // right, the focused tile gets the aqua ring and the game title under it.
    std::vector<TileStrip::TileRow> rows;
    rows.reserve(games.size());
    for (const Game& game : games) {
        rows.push_back({game.title, game.path, game.program_id,
                        RoundedCard(game.icon, QSize(176, 176), 18)});
    }
    tile_strip->SetGames(rows);

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
    if (tile_strip) {
        tile_strip->setFocus(Qt::OtherFocusReason);
    } else {
        home_button->setFocus(Qt::OtherFocusReason);
    }
    nav_index_ = 0;
    Refresh();
    FixHomeLayout();
    QTimer::singleShot(0, this, [this] { FixHomeLayout(); });
}

void Home::ShowList() {
    list_mode = true;
    page->hide();
    rail->show();
    PositionBackButton();
    back_button->show();
    back_button->raise();
    // The dock hides with the page; put focus on the games.
    QWidget* focus_target = hooks.game_view ? hooks.game_view() : nullptr;
    if (focus_target) {
        focus_target->setFocus(Qt::OtherFocusReason);
        if (auto* item_view = qobject_cast<QAbstractItemView*>(focus_target);
            item_view && item_view->model() && !item_view->currentIndex().isValid()) {
            item_view->setCurrentIndex(item_view->model()->index(0, 0));
        }
        if (const QWidget* fw = QApplication::focusWidget()) {
            LOG_INFO(Frontend, "Ocarina2 ln: ShowList focused; focus widget now {}",
                     fw->metaObject()->className());
        }
    } else if (back_button) {
        back_button->setFocus(Qt::OtherFocusReason);
        LOG_INFO(Frontend, "Ocarina2 ln: ShowList focused; focus is the back button");
    }
    nav_index_ = 1;  // the "Games" button in the dock
    RebuildNavOrder();
    if (hooks.game_list_empty()) {
        placeholder->show();
    } else {
        game_list->show();
    }
}

void Home::ShowAddGamesPanel() {
    HomePanel panel(window, QStringLiteral("A D D   T O   Y O U R   L I B R A R Y"),
                    tr("Add games"));
    panel.AddRow(QStringLiteral("folder"), tr("Add games folder…"), [this] { hooks.add_folder(); });
    if (hooks.add_file) {
        panel.AddRow(QStringLiteral("plus"), tr("Add a game file…"),
                     [this] { hooks.add_file(); });
    }
    panel.SetHints(QStringLiteral(
        "<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
        "<span style='color:#3fd8ff;font-weight:800'>B</span> Back"));
    panel.exec();
}

void Home::ShowWhatsNewPanel() {
    HomePanel panel(window, QStringLiteral("W H A T ' S   N E W"), VersionName());
    auto* notes = new QTextBrowser;
    notes->setObjectName(QStringLiteral("Ocarina2Notes"));
    notes->setFrameShape(QFrame::NoFrame);
    notes->setOpenExternalLinks(true);
    notes->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const QString text = BundledNotes();
    SetNotes(notes, text.isEmpty() ? tr("No notes for this version yet.") : text);
    panel.SetBodyWidget(notes);
    panel.SetHints(QStringLiteral(
        "<span style='color:#3fd8ff;font-weight:800'>B</span> Back"));
    panel.exec();
}

void Home::Restore() {
    // A restart shows the list for a moment, and nothing may cover a running game.
    if (hooks.single_window() && hooks.emulation_running()) {
        LOG_INFO(Frontend, "Ocarina2 restore: skipped (emulation running)");
        return;
    }
    LOG_INFO(Frontend, "Ocarina2 restore: list_mode={} back_button_valid={}",
             list_mode, back_button != nullptr);
    if (list_mode) {
        // Restoring after the game stops must bring back the back button too, otherwise the
        // user is stranded on the games pane with no way home. ShowList is the same state the
        // Games dock button produces: games pane up, back button visible, focus on the list.
        ShowList();
    } else {
        ShowHome();
    }
    FixHomeLayout();
}

void Home::OnEmulationStarting() {
    if (!hooks.single_window()) {
        return;
    }
    rail->hide();
    page->hide();
    back_button->hide();
}

bool Home::HandleListNavKey(QKeyEvent* event) {
    const int key = event->key();

    // B/Escape is the console's Back: leave the games screen.
    if (key == Qt::Key_Escape || key == Qt::Key_Home) {
        ShowHome();
        return true;
    }

    QWidget* view = hooks.game_view ? hooks.game_view() : nullptr;
    auto* item_view = qobject_cast<QAbstractItemView*>(view);
    QWidget* edit = hooks.filter_edit ? hooks.filter_edit() : nullptr;
    const QWidget* current = QApplication::focusWidget();
    {
        QString tag = QStringLiteral("null");
        if (const QWidget* fw = current) {
            tag = QString::fromUtf8(fw->metaObject()->className());
            if (!fw->objectName().isEmpty()) {
                tag += QStringLiteral("/%1").arg(fw->objectName());
            }
        }
        LOG_INFO(Frontend, "Ocarina2 ln: key={} focus={}", key, tag.toStdString());
    }

    const auto ensure_index = [&] {
        if (item_view && item_view->model()) {
            const QModelIndex idx = item_view->currentIndex();
            if (!idx.isValid() && item_view->model()->rowCount(idx.parent()) > 0) {
                item_view->setCurrentIndex(item_view->model()->index(0, 0));
            }
        }
    };

    // A/confirm: play the highlighted game (or activate the back button if it has focus).
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (auto* button = qobject_cast<QAbstractButton*>(const_cast<QWidget*>(current))) {
            button->click();
            return true;
        }
        if (item_view && item_view->model() && item_view->model()->rowCount() > 0) {
            QModelIndex idx = item_view->currentIndex();
            if (!idx.isValid()) {
                idx = item_view->model()->index(0, 0);
                item_view->setCurrentIndex(idx);
            }
            item_view->activated(idx);
            return true;
        }
        return false;
    }

    if (key == Qt::Key_Up || key == Qt::Key_Down) {
        const bool on_back = back_button && current == back_button;
        const bool in_filter = edit && current == edit;
        const bool in_view = view && current == view;
        const bool empty = !item_view || !item_view->model() || item_view->model()->rowCount() == 0;
        if (in_view && !empty) {
            ensure_index();
            const QModelIndex idx = item_view->currentIndex();
            const int row = idx.isValid() ? idx.row() : 0;
            const int last = item_view->model()->rowCount(idx.isValid() ? idx.parent() : QModelIndex()) - 1;
            if (key == Qt::Key_Up && row == 0) {
                return true;  // ceiling: stay on the games
            }
            if (key == Qt::Key_Down && row == last) {
                if (back_button) {
                    back_button->setFocus(Qt::OtherFocusReason);
                    return true;
                }
                if (edit) {
                    edit->setFocus(Qt::OtherFocusReason);
                    return true;
                }
                return true;
            }
            return false;  // native selection movement inside the games view
        }
        if (on_back) {
            if (key == Qt::Key_Up && view) {
                view->setFocus(Qt::OtherFocusReason);
                ensure_index();
                return true;
            }
            if (key == Qt::Key_Down && edit) {
                edit->setFocus(Qt::OtherFocusReason);
                return true;
            }
            return true;  // don't fall through to the pane route
        }
        if (in_filter) {
            if (key == Qt::Key_Up && back_button) {
                back_button->setFocus(Qt::OtherFocusReason);
                return true;
            }
            return false;  // cursor movement native to the line edit
        }
        // Focus wandered somewhere unexpected: heal it to the games, or the back button
        // when there isn't a games list.
        if (view && !empty) {
            view->setFocus(Qt::OtherFocusReason);
            ensure_index();
            return true;
        }
        if (back_button) {
            back_button->setFocus(Qt::OtherFocusReason);
        }
        return true;
    }

    // Left/Right: always the view's native behavior (grid/cursor).
    return false;
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
    // position), Left/Right walks the row. Rows: 0 = top bar, 1 = games list / hero, 2 = back
    // button, 3 = filter, 4 = dock. Empty rows are skipped automatically.
    const auto band_of = [](QWidget* w) {
        bool ok = false;
        const int band = w->property("band").toInt(&ok);
        return ok ? band : 4;
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

    // The games view keeps its native arrows; only at the very first or last row does Up and
    // Down yield so the controller can move on to the next band. Left/Right always stay native.
    if (hooks.game_view) {
        if (QWidget* view = hooks.game_view(); view && current == view) {
            if (dx != 0) {
                return false;  // move the selection natively
            }
            bool at_edge = true;
            if (auto* item_view = qobject_cast<QAbstractItemView*>(view);
                item_view && item_view->model()) {
                const QModelIndex idx = item_view->currentIndex();
                if (idx.isValid()) {
                    const int row = idx.row();
                    const int last = item_view->model()->rowCount(idx.parent()) - 1;
                    at_edge = dy < 0 ? row <= 0 : row >= last;
                }
            }
            if (!at_edge) {
                return false;  // move the selection natively
            }
        }
    }
    // The filter keeps Left/Right for editing the text.
    if (dx != 0 && qobject_cast<QLineEdit*>(current)) {
        return false;
    }

    const auto row_at = [&](int b) {
        QList<QWidget*> row;
        for (QWidget* w : nav_order_) {
            if (band_of(w) == b && w->isVisible() && w->isEnabled()) {
                row.append(w);
            }
        }
        return row;
    };
    if (dx != 0) {
        QList<QWidget*> row = row_at(band);
        if (row.isEmpty()) {
            return false;
        }
        const int i = row.indexOf(current);
        const int step = dx > 0 ? 1 : -1;
        const int next = ((i < 0 ? 0 : i) + step + row.size()) % row.size();
        row[next]->setFocus(Qt::OtherFocusReason);
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        return true;
    }
    // Up/Down: nearest non-empty row in that direction.
    int target_band = -1;
    for (int b = band + dy; b >= 0 && b <= 4; b += dy) {
        if (!row_at(b).isEmpty()) {
            target_band = b;
            break;
        }
    }
    if (target_band < 0) {
        return false;
    }
    const QList<QWidget*> row = row_at(target_band);
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
    // Jumping into the games view: make sure a row is selected so the arrows keep working.
    if (hooks.game_view && best == hooks.game_view()) {
        if (auto* item_view = qobject_cast<QAbstractItemView*>(best);
            item_view && item_view->model()) {
            if (!item_view->currentIndex().isValid()) {
                item_view->setCurrentIndex(item_view->model()->index(0, 0));
            }
        }
    }
    if (best) {
        best->setFocus(Qt::OtherFocusReason);
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
        return true;
    }
    return false;
}

void Home::RebuildNavOrder() {
    nav_order_.clear();
    if (list_mode) {
        // Games screen: the list's active view (band 1), then the floating Back button
        // (band 2), then the filter's line edit (band 3). The dock is hidden with the page.
        if (hooks.game_view) {
            if (QWidget* view = hooks.game_view()) {
                view->setProperty("band", 1);
                nav_order_.append(view);
            }
        }
        if (back_button) {
            back_button->setProperty("band", 2);
            nav_order_.append(back_button);
        }
        if (hooks.filter_edit) {
            if (QWidget* edit = hooks.filter_edit()) {
                edit->setProperty("band", 3);
                nav_order_.append(edit);
            }
        }
    } else {
        if (avatar_button) {
            nav_order_.append(avatar_button);
        }
        if (tile_strip) {
            tile_strip->setProperty("band", 1);
            nav_order_.append(tile_strip);
        }
        nav_order_.append(home_button);
        nav_order_.append(library_button);
        nav_order_.append(folder_button);
        nav_order_.append(settings_button);
        nav_order_.append(update_button);
        nav_order_.append(menu_button);
    }
    // Focus stays on the dock's Home button at rest; clamps handle list mode.
    if (nav_index_ < 0 || nav_index_ >= nav_order_.size()) {
        nav_index_ = std::max(0, static_cast<int>(nav_order_.size()) - 8);
    }
}

bool Home::eventFilter(QObject* watched, QEvent* event) {
    // Focus follows the dock/strip, same as the console: the name under the dock is the
    // focused item's name.
    if (event->type() == QEvent::FocusIn) {
        if (const auto* button = qobject_cast<const QToolButton*>(watched)) {
            const QString caption = button->property("caption").toString();
            if (!caption.isEmpty() && game_row_title) {
                game_row_title->setText(caption);
                game_row_title->setVisible(true);
            }
        }
        return false;
    }
    if (watched == window && event->type() == QEvent::Resize) {
        PositionBackButton();
        return false;
    }
    if (watched == page && event->type() == QEvent::Resize) {
        FixHomeLayout();
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
        // One filter, every path once. The window filter already sees everything; no need
        // for the page filter to run the same checks (that used to double-step focus).
        if (list_mode && key->key() != Qt::Key_Menu) {
            // The games pane drives its own rows for the controller; arrow keys elsewhere
            // wander back into the pane so the screen is never a dead end.
            if (HandleListNavKey(key)) {
                return true;
            }
        }
        // Key menu opens the options for the focused game.
        if (key->key() == Qt::Key_Menu) {
            if (auto* button = qobject_cast<QToolButton*>(watched);
                button && button->property("path").isValid()) {
                ShowGameMenu(button->property("path").toString(),
                             button->property("id").toULongLong(),
                             button->mapToGlobal(button->rect().center()));
                return true;
            }
            if (watched == tile_strip) {
                ShowGameMenu(tile_strip->SelectedPath(), tile_strip->SelectedProgramId(),
                             tile_strip->mapToGlobal(tile_strip->rect().center()));
                return true;
            }
        }
        // Home-screen navigation (avatar, strip, dock) outside the games pane. The tile strip
        // keeps the arrow keys for the tiles themselves.
        if (!list_mode) {
            if (auto* widget = qobject_cast<QWidget*>(watched)) {
                if (nav_order_.contains(widget)) {
                    if (widget == tile_strip &&
                        (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
                         key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
                         key->key() == Qt::Key_Space)) {
                        return false;  // the strip handles arrows and confirm itself
                    }
                    if (HandleNavKey(key)) {
                        return true;
                    }
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Ocarina2Ui
