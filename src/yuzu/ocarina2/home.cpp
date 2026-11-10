// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/ocarina2/home.h"

#include <algorithm>
#include <set>
#include <vector>

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
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
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVariant>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/whats_new.h"

namespace Ocarina2Ui {
namespace {

constexpr u64 OcarinaOfTime = 0x0400486024DEC000ULL;
constexpr int TileIcon = 104;

struct Game {
    QString title;
    QString path;
    u64 program_id = 0;
    QPixmap icon;
};

QPixmap Rounded(const QPixmap& source, int size, int radius) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap out(QSize(size, size) * ratio);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
        painter.setClipPath(clip);
        if (source.isNull()) {
            painter.fillRect(QRect(0, 0, size, size), QColor(0x24, 0x23, 0x2c));
        } else {
            painter.drawPixmap(QRect(0, 0, size, size), source);
        }
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

Home::Home(QMainWindow* window_, QBoxLayout* central, QWidget* game_list_, QWidget* placeholder_,
           HomeHooks hooks_)
    : QObject(window_), window{window_}, game_list{game_list_}, placeholder{placeholder_},
      hooks{std::move(hooks_)} {
    // The classic menus live behind the dock's More button. A zero-height bar keeps every
    // shortcut working and stays out of the way when fullscreen shows the bar again.
    window->menuBar()->setFixedHeight(0);

    rail = MakeRail();
    page = MakePage();
    central->addWidget(page, 1);
    central->addWidget(rail, 0, Qt::AlignHCenter);

    game_list->installEventFilter(this);
    placeholder->installEventFilter(this);

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
    auto* layout = new QHBoxLayout(rail);
    layout->setContentsMargins(0, 0, 0, 2);
    layout->setSpacing(22);
    layout->setAlignment(Qt::AlignHCenter);

    const auto make = [this](const QString& icon, const QString& caption, const QString& tip) {
        auto* button = new QToolButton(rail);
        button->setObjectName(QStringLiteral("Ocarina2DockButton"));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(Icon(icon, 22));
        button->setIconSize(QSize(22, 22));
        button->setText(caption);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
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
         {home_button, library_button, folder_button, settings_button, update_button, menu_button}) {
        layout->addWidget(button);
        button->installEventFilter(this);
    }
    return rail;
}

QWidget* Home::MakePage() {
    page = new QWidget;
    page->setObjectName(QStringLiteral("Ocarina2Home"));
    page->setAttribute(Qt::WA_StyledBackground);
    page->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(page, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        ShowGameMenu(hero_path, hero_id, page->mapToGlobal(pos));
    });

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setSpacing(0);

    // The Switch 2 top bar: player avatar, Home/Games switch, network glyphs and the clock.
    topbar = new QWidget(page);
    topbar->setObjectName(QStringLiteral("Ocarina2TopBar"));
    auto* topb = new QHBoxLayout(topbar);
    topb->setContentsMargins(40, 0, 40, 0);
    topb->setSpacing(14);

    avatar_button = new QToolButton(topbar);
    avatar_button->setObjectName(QStringLiteral("Ocarina2Avatar"));
    avatar_button->setIcon(Icon(QStringLiteral("user"), 19));
    avatar_button->setIconSize(QSize(19, 19));
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

    // The Switch 2 segmented control in the middle of the top bar: which view we are in.
    auto* pills = new QWidget(topbar);
    pills->setObjectName(QStringLiteral("Ocarina2Pills"));
    auto* pill_layout = new QHBoxLayout(pills);
    pill_layout->setContentsMargins(2, 2, 2, 2);
    pill_layout->setSpacing(2);
    auto* group = new QButtonGroup(pills);
    group->setExclusive(true);
    const auto makePill = [pills, pill_layout, group](const QString& text) {
        auto* button = new QToolButton(pills);
        button->setObjectName(QStringLiteral("Ocarina2Pill"));
        button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        group->addButton(button);
        pill_layout->addWidget(button);
        return button;
    };
    pill_home = makePill(tr("Home"));
    pill_games = makePill(tr("Games"));
    connect(pill_home, &QToolButton::clicked, this, &Home::ShowHome);
    connect(pill_games, &QToolButton::clicked, this, &Home::ShowList);
    topb->addWidget(pills, 0, Qt::AlignVCenter);
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
    topb->addWidget(makeTopIcon(QStringLiteral("battery"), tr("Battery charged")), 0,
                    Qt::AlignVCenter);
    topb->addWidget(makeTopIcon(QStringLiteral("gamepad"), tr("Pro Controller")), 0,
                    Qt::AlignVCenter);
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

    // The Switch 2 "current game" row: a bookmark and the highlighted title, above the tiles.
    game_row = new QWidget(page);
    game_row->setObjectName(QStringLiteral("Ocarina2CurrentGameRow"));
    auto* rowl = new QHBoxLayout(game_row);
    rowl->setContentsMargins(44, 8, 44, 0);
    rowl->setSpacing(8);
    auto* bookmark = new QLabel(game_row);
    bookmark->setObjectName(QStringLiteral("Ocarina2CurrentGameIcon"));
    bookmark->setPixmap(Glyph(QStringLiteral("bookmark"), 0x3fd8ff, 15).pixmap(15, 15));
    rowl->addWidget(bookmark, 0, Qt::AlignVCenter);
    game_row_title = new QLabel(game_row);
    game_row_title->setObjectName(QStringLiteral("Ocarina2CurrentGame"));
    rowl->addWidget(game_row_title, 0, Qt::AlignVCenter);
    rowl->addStretch(1);
    layout->addWidget(game_row);

    auto* middle = new QWidget(page);
    auto* mid = new QVBoxLayout(middle);
    mid->setSpacing(0);
    mid->setAlignment(Qt::AlignHCenter);
    layout->addWidget(middle, 1);
    mid->addStretch(1);

    play = new QPushButton(middle);
    play->setObjectName(QStringLiteral("Ocarina2Play"));
    play->setText(tr("Play"));
    play->setCursor(Qt::PointingHandCursor);
    play->setIconSize(QSize(20, 20));
    play->setFocusPolicy(Qt::StrongFocus);
    play->installEventFilter(this);
    connect(play, &QPushButton::clicked, this, [this] {
        if (hero_path.isEmpty()) {
            hooks.add_folder();
        } else {
            hooks.play(hero_path, hero_id);
        }
    });
    auto* play_effect = new QGraphicsDropShadowEffect;
    play_effect->setBlurRadius(44);
    play_effect->setOffset(0, 6);
    play_effect->setColor(QColor(63, 216, 255, 120));
    play->setGraphicsEffect(play_effect);
    mid->addWidget(play, 0, Qt::AlignHCenter);
    mid->addSpacing(36);

    row_box = new QWidget(middle);
    row = new QHBoxLayout(row_box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(22);
    row->setAlignment(Qt::AlignHCenter);
    mid->addWidget(row_box);
    mid->addStretch(2);

    // The thin strip at the bottom edge of the screen: theme on the left, button hints on the right.
    auto* strip = new QWidget(page);
    strip->setObjectName(QStringLiteral("Ocarina2BottomBar"));
    auto* stripl = new QHBoxLayout(strip);
    stripl->setContentsMargins(40, 0, 40, 0);
    stripl->setSpacing(12);
    theme_button = new QToolButton(strip);
    theme_button->setObjectName(QStringLiteral("Ocarina2ThemeButton"));
    theme_button->setIcon(Icon(QStringLiteral("droplet"), 15));
    theme_button->setIconSize(QSize(15, 15));
    theme_button->setText(tr("Ocarina Dark"));
    theme_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    theme_button->setToolTip(tr("Appearance - open the configuration dialog"));
    theme_button->setCursor(Qt::PointingHandCursor);
    connect(theme_button, &QToolButton::clicked, this, [this] { hooks.open_settings(); });
    stripl->addWidget(theme_button, 0, Qt::AlignVCenter);
    stripl->addStretch(1);
    auto* hints = new QLabel(strip);
    hints->setObjectName(QStringLiteral("Ocarina2Hints"));
    hints->setText(
        tr("<span style='color:#3fd8ff;font-weight:800'>A</span> Select&nbsp;&nbsp;&nbsp;"
           "<span style='color:#3fd8ff;font-weight:800'>B</span> Back&nbsp;&nbsp;&nbsp;"
           "<span style='color:#3fd8ff;font-weight:800'>X</span> Options&nbsp;&nbsp;&nbsp;"
           "<span style='color:#3fd8ff;font-weight:800'>L/R</span> Switch game"));
    stripl->addWidget(hints, 0, Qt::AlignVCenter);
    layout->addWidget(strip);

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
        SelectGame(hero->path, hero->program_id, hero->title);
        play->setText(tr("Play"));
        play->setIcon(Glyph(QStringLiteral("play"), 0xffffff, 20));
    } else {
        SelectGame(QString(), 0, tr("Ocarina2"));
        play->setText(tr("Add games"));
        play->setIcon(Glyph(QStringLiteral("plus"), 0xffffff, 20));
    }

    // The Switch 2 row of game tiles: every game. Activating a tile boots the game;
    // right click (or the controller's X) opens the game menu.
    // deleteLater: a tile's own click can land here.
    while (QLayoutItem* item = row->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    for (const Game& game : games) {
        QToolButton* button = new QToolButton(row_box);
        button->setObjectName(QStringLiteral("Ocarina2Tile"));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(QIcon(Rounded(game.icon, TileIcon, 20)));
        button->setIconSize(QSize(TileIcon, TileIcon));
        button->setFixedWidth(TileIcon + 42);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        button->installEventFilter(this);
        button->setProperty("path", game.path);
        button->setProperty("id", QVariant::fromValue(game.program_id));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setToolTip(game.title);
        button->setFixedWidth(TileIcon + 16);
        button->setFixedHeight(TileIcon + 16);
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, path = game.path, id = game.program_id, title = game.title] {
                    SelectGame(path, id, title);
                    hooks.play(path, id);
                });
        connect(button, &QToolButton::customContextMenuRequested, this,
                [this, path = game.path, id = game.program_id, button] {
                    ShowGameMenu(path, id, button->mapToGlobal(button->rect().center()));
                });
    }
    RebuildNavOrder();
}

void Home::ShowHome() {
    list_mode = false;
    game_list->hide();
    placeholder->hide();
    rail->show();
    page->show();
    pill_home->setChecked(true);
    home_button->setFocus(Qt::OtherFocusReason);
    nav_index_ = 0;
    Refresh();
}

void Home::ShowList() {
    list_mode = true;
    page->hide();
    rail->show();
    pill_games->setChecked(true);
    library_button->setFocus(Qt::OtherFocusReason);
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
}

bool Home::HandleNavKey(QKeyEvent* event) {
    const int key = event->key();
    if (nav_order_.isEmpty()) {
        RebuildNavOrder();
    }
    const int count = nav_order_.size();
    if (count == 0) {
        return false;
    }

    // The controller's Home button goes straight to the home screen, like the console.
    if (key == Qt::Key_Home) {
        ShowHome();
        return true;
    }

    // L/R switches the highlighted game, console style.
    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        const int first_tile = 7;  // dock (6) + play (1) come first
        if (count <= first_tile) {
            return false;
        }
        int next = nav_index_ >= first_tile ? nav_index_ : first_tile;
        next += (key == Qt::Key_PageDown ? 1 : -1);
        if (next >= count) {
            next = first_tile;
        } else if (next < first_tile) {
            next = count - 1;
        }
        nav_index_ = next;
        nav_order_[nav_index_]->setFocus(Qt::OtherFocusReason);
        return true;
    }

    int next = nav_index_;
    const int dock_end = 5;  // home, games, add-games, settings, what's-new, more
    const int play_index = 6;
    const int last_tile = count - 1;

    switch (key) {
        case Qt::Key_Up:
            if (nav_index_ <= dock_end) {
                return false;  // nothing above the dock
            }
            if (nav_index_ == play_index) {
                next = last_tile >= play_index + 1 ? last_tile : dock_end;
            } else {  // tiles: up is the Play button
                next = play_index;
            }
            break;
        case Qt::Key_Down:
            if (nav_index_ <= dock_end) {
                next = play_index;  // down from the dock is the Play button
            } else if (nav_index_ == play_index) {
                next = play_index + 1 < count ? play_index + 1 : nav_index_;
            } else {
                next = dock_end;  // down from a tile is the dock's last button
            }
            break;
        case Qt::Key_Left:
            if (nav_index_ <= dock_end) {
                --next;
            } else if (nav_index_ > play_index) {
                --next;
            }
            break;
        case Qt::Key_Right:
            if (nav_index_ <= dock_end) {
                ++next;
            } else if (nav_index_ > play_index) {
                ++next;
            }
            break;
        default:
            return false;
    }

    next = std::clamp(next, 0, count - 1);
    if (next != nav_index_) {
        nav_index_ = next;
        nav_order_[nav_index_]->setFocus(Qt::OtherFocusReason);
        return true;
    }
    return false;
}

void Home::RebuildNavOrder() {
    nav_order_.clear();
    nav_order_ = {home_button, library_button, folder_button,
                  settings_button, update_button, menu_button, play};
    if (row != nullptr) {
        for (int i = 0; i < row->count(); ++i) {
            if (QLayoutItem* item = row->itemAt(i)) {
                if (QWidget* widget = item->widget()) {
                    nav_order_.append(widget);
                }
            }
        }
    }
    nav_index_ = std::clamp(nav_index_, 0, static_cast<int>(nav_order_.size()) - 1);
}

bool Home::eventFilter(QObject* watched, QEvent* event) {
    // The main window shows its game list (start up, a game stopped, the list filled): show
    // the home page instead, unless the player asked for the full list.
    if (event->type() == QEvent::Show && (watched == game_list || watched == placeholder)) {
        QTimer::singleShot(0, this, &Home::Restore);
    }
    if (event->type() == QEvent::FocusIn) {
        // Focus (controller or mouse) is what selects the game, console style: the bookmark
        // row and the Play button follow the focused tile.
        if (row != nullptr) {
            for (int i = 0; i < row->count(); ++i) {
                QLayoutItem* item = row->itemAt(i);
                if (item && item->widget() == watched) {
                    auto* button = qobject_cast<QToolButton*>(watched);
                    SelectGame(button->property("path").toString(),
                               button->property("id").toULongLong(), button->toolTip());
                    nav_index_ = 7 + i;  // dock (6) + play (1) come first
                    break;
                }
            }
        }
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
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
