// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "common/logging.h"
#include "yuzu/ocarina2/ocarina2_ui.h"
#include "yuzu/ocarina2/sound.h"
#include "yuzu/ocarina2/tile_strip.h"

namespace Ocarina2Ui {

namespace {

struct Palette {
    QColor top;
    QColor bottom;
    QColor accent;
};

const Palette kPalettes[] = {
    {QColor{0x1d3b2a}, QColor{0x0e2418}, QColor{0x63e2a2}},  // Kokiri green
    {QColor{0x332048}, QColor{0x1b1130}, QColor{0xb18cff}},  // Majora violet
    {QColor{0x274a63}, QColor{0x122031}, QColor{0x6ec3ff}},  // Zora blue
    {QColor{0x54321d}, QColor{0x2b1a0d}, QColor{0xffc46b}},  // Goron ember
    {QColor{0x1f3a4a}, QColor{0x0c1b24}, QColor{0x7de3e3}},  // Zora ice
    {QColor{0x462a3f}, QColor{0x221119}, QColor{0xff8fb1}},  // Gerudo rose
    {QColor{0x223a2c}, QColor{0x101f15}, QColor{0x9ae6b4}},  // Sci-fi forest
    {QColor{0x3a2a1f}, QColor{0x1c130c}, QColor{0xffd166}},  // Deku gold
    {QColor{0x2c2f4a}, QColor{0x131530}, QColor{0x9aa0ff}},  // Twilight indigo
    {QColor{0x402f2f}, QColor{0x1e1010}, QColor{0xff6b6b}},  // Lava red
    {QColor{0x27394a}, QColor{0x111c28}, QColor{0x7ad0fd}},  // Hylia steel
    {QColor{0x3c3c3c}, QColor{0x1d1d1d}, QColor{0xd8d8d8}},  // Shadow grey
};

constexpr int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

// KPalettes is constexpr-qualified as a table of aggregates of colours; QColor is not a
// structural literal type, so the table is runtime const instead of constexpr.
static_assert(kPaletteCount > 0);

/// Stable hash of a string (FNV-1a) so a game lands on the same palette every boot.
u32 Hash(const QString& text) {
    u64 hash = 14695981039346656037ULL;
    for (const QChar c : text) {
        hash ^= static_cast<u64>(c.unicode());
        hash *= 1099511628211ULL;
    }
    return static_cast<u32>((hash >> 32) ^ hash);
}

/// Which glyph a game gets from its name, so "what type of game is this" is visible at a glance.
/// Zelda gets a triforce; the rest rotate through the fantasy glyphs by hash.
int GlyphIndex(const QString& title) {
    const QString lower = title.toLower();
    if (lower.contains(QStringLiteral("zelda")) || lower.contains(QStringLiteral("ocarina"))) {
        return 0;  // triforce
    }
    if (lower.contains(QStringLiteral("majora"))) {
        return 1;  // heart
    }
    if (lower.contains(QStringLiteral("wind waker")) || lower.contains(QStringLiteral("skyward")) ||
        lower.contains(QStringLiteral("twilight"))) {
        return 2;  // leaf
    }
    if (lower.contains(QStringLiteral("fire"))) {
        return 3;  // sword
    }
    if (lower.contains(QStringLiteral("pokemon")) || lower.contains(QStringLiteral("pok\u00e9mon"))) {
        return 4;  // bolt-ish (star)
    }
    if (lower.contains(QStringLiteral("mario"))) {
        return 5;  // crown
    }
    if (lower.contains(QStringLiteral("metroid"))) {
        return 6;  // gem
    }
    if (lower.contains(QStringLiteral("animal")) || lower.contains(QStringLiteral("crossing"))) {
        return 7;  // sun
    }
    return 3 + static_cast<int>(Hash(title) % 7);  // the rest rotate
}

QPixmap RenderGlyph(int index, const QColor& color, int px) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(px, px) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(px / 2.0, px / 2.0);
    painter.scale(px / 24.0, px / 24.0);
    QPainterPath path;
    const double r = 9.0;
    switch (index) {
    case 0:  // triforce
        path.moveTo(0, -r);
        path.lineTo(-r * 0.87, r * 0.5);
        path.lineTo(r * 0.87, r * 0.5);
        path.closeSubpath();
        path.moveTo(-r * 0.87, r * 0.5);
        path.moveTo(-r * 0.42, r * 0.07);
        path.lineTo(-r * 0.87, r * 0.5);
        path.lineTo(-r * 1.17, r * 0.05);
        path.closeSubpath();
        path.moveTo(r * 0.87, r * 0.5);
        path.moveTo(r * 0.42, r * 0.07);
        path.lineTo(r * 0.87, r * 0.5);
        path.lineTo(r * 1.17, r * 0.05);
        path.closeSubpath();
        painter.fillPath(path, color);
        return pixmap;
    case 1:  // heart
        path.moveTo(0, r * 0.75);
        path.cubicTo(-r * 1.1, -r * 0.1, -r * 0.8, -r * 0.9, 0, -r * 0.35);
        path.cubicTo(r * 0.8, -r * 0.9, r * 1.1, -r * 0.1, 0, r * 0.75);
        painter.fillPath(path, color);
        return pixmap;
    case 2:  // leaf
        path.moveTo(0, -r);
        path.cubicTo(r, -r * 0.4, r, r * 0.7, 0, r);
        path.cubicTo(-r, r * 0.7, -r, -r * 0.4, 0, -r);
        path.moveTo(0, r * 0.55);
        path.lineTo(0, -r * 0.55);
        painter.strokePath(path, QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        return pixmap;
    case 3:  // sword
        path.moveTo(0, -r);
        path.lineTo(r * 0.24, -r * 0.2);
        path.lineTo(r * 0.24, r * 0.22);
        path.lineTo(r * 0.52, r * 0.48);
        path.lineTo(0, r * 0.76);
        path.lineTo(-r * 0.52, r * 0.48);
        path.lineTo(-r * 0.24, r * 0.22);
        path.lineTo(-r * 0.24, -r * 0.2);
        path.closeSubpath();
        painter.fillPath(path, color);
        return pixmap;
    case 4:  // bolt/star
        path.moveTo(r * 0.2, -r);
        path.lineTo(-r * 0.55, r * 0.12);
        path.lineTo(r * 0.05, r * 0.1);
        path.lineTo(-r * 0.2, r);
        path.lineTo(r * 0.55, -r * 0.12);
        path.lineTo(-r * 0.05, -r * 0.1);
        path.closeSubpath();
        painter.fillPath(path, color);
        return pixmap;
    case 5:  // crown
        path.moveTo(-r * 0.85, r * 0.55);
        path.lineTo(-r * 0.85, -r * 0.35);
        path.lineTo(-r * 0.35, r * 0.15);
        path.lineTo(0, -r * 0.85);
        path.lineTo(r * 0.35, r * 0.15);
        path.lineTo(r * 0.85, -r * 0.35);
        path.lineTo(r * 0.85, r * 0.55);
        path.closeSubpath();
        painter.fillPath(path, color);
        return pixmap;
    case 6:  // gem
        path.moveTo(0, -r * 0.85);
        path.lineTo(r * 0.75, 0);
        path.lineTo(0, r * 0.85);
        path.lineTo(-r * 0.75, 0);
        path.closeSubpath();
        path.moveTo(-r * 0.75, 0);
        path.lineTo(r * 0.75, 0);
        path.moveTo(0, -r * 0.85);
        path.lineTo(0, r * 0.85);
        painter.strokePath(path, QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        return pixmap;
    case 7:  // sun
        path.addEllipse(QPointF(0, 0), r * 0.5, r * 0.5);
        painter.fillPath(path, color);
        path = QPainterPath();
        for (int i = 0; i < 8; ++i) {
            const double a = i * M_PI / 4.0;
            path.moveTo(std::cos(a) * r * 0.68, std::sin(a) * r * 0.68);
            path.lineTo(std::cos(a) * r, std::sin(a) * r);
        }
        painter.strokePath(path, QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap));
        return pixmap;
    case 8:  // moon
        path.moveTo(r * 0.35, -r * 0.85);
        path.cubicTo(-r * 0.75, -r * 0.35, -r * 0.75, r * 0.35, r * 0.35, r * 0.85);
        path.cubicTo(-r * 0.15, r * 0.42, -r * 0.15, -r * 0.42, r * 0.35, -r * 0.85);
        painter.fillPath(path, color);
        return pixmap;
    default:  // shield
        path.moveTo(-r * 0.72, -r * 0.62);
        path.lineTo(r * 0.72, -r * 0.62);
        path.lineTo(r * 0.72, 0);
        path.cubicTo(r * 0.72, r * 0.5, r * 0.35, r * 0.75, 0, r * 0.95);
        path.cubicTo(-r * 0.35, r * 0.75, -r * 0.72, r * 0.5, -r * 0.72, 0);
        path.closeSubpath();
        path.moveTo(0, -r * 0.42);
        path.lineTo(0, r * 0.55);
        painter.strokePath(path, QPen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        return pixmap;
    }
}

} // namespace

GameTile::GameTile(QWidget* parent) : QWidget(parent) {
    setFixedSize(QSize(228, 228));
    setFocusPolicy(Qt::NoFocus);  ///< the strip owns focus and the arrows
    setAttribute(Qt::WA_Hover);
}

void GameTile::SetGame(const QString& title_, const QString& path_, u64 program_id_,
                       const QPixmap& icon_, bool ghost_) {
    title = title_;
    path = path_;
    program_id = program_id_;
    icon = icon_;
    ghost = ghost_;
    if (ghost) {
        return;  // placeholder slot: painted from qss-free translucent shapes only
    }
    const u32 hash = Hash(title);
    palette = static_cast<int>(hash % kPaletteCount);
    glyph = GlyphIndex(title);
}

void GameTile::AnimateFocus(bool focused) {
    if (selected == focused) {
        return;
    }
    selected = focused;
    update();
}

void GameTile::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect r = rect();

    QPainterPath clip;
    clip.addRoundedRect(QRectF(r), 16, 16);
    painter.setClipPath(clip);

    if (ghost) {
        // An empty slot, like the Switch 2's unfilled tile positions: a faint rounded "card"
        // with a soft aqua glow and plus sign. Pressing A on it adds a games folder.
        painter.fillRect(r, QColor(255, 255, 255, 7));
        const QPointF gc(r.width() / 2.0, r.height() / 2.0);
        QRadialGradient aura(gc, r.width() * 0.72);
        aura.setColorAt(0.0, QColor(63, 216, 255, 26));
        aura.setColorAt(0.62, QColor(63, 216, 255, 8));
        aura.setColorAt(1.0, QColor(63, 216, 255, 0));
        painter.fillRect(r, aura);
        // Dashed card edge: unmistakably a slot to drop a game into, not a broken tile.
        QPen pen(QColor(122, 220, 255, 46), 1.5, Qt::DashLine, Qt::RoundCap);
        pen.setDashPattern({5, 5});
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 14, 14);
        painter.setPen(QPen(QColor(150, 232, 255, 96), 2.6, Qt::SolidLine, Qt::RoundCap));
        const qreal s = 11;
        painter.drawLine(QPointF(gc.x() - s, gc.y()), QPointF(gc.x() + s, gc.y()));
        painter.drawLine(QPointF(gc.x(), gc.y() - s), QPointF(gc.x(), gc.y() + s));
    } else {
        const Palette& pal = kPalettes[palette];

        if (icon.isNull()) {
            // Background: two-stop vertical gradient plus a faint radial glow top-right.
            QLinearGradient gradient(0, 0, 0, r.height());
            gradient.setColorAt(0.0, pal.top);
            gradient.setColorAt(1.0, pal.bottom);
            painter.fillRect(r, gradient);
            QRadialGradient glow(QPointF(r.width() * 0.75, r.height() * 0.18), r.width() * 0.9);
            glow.setColorAt(0.0, QColor(pal.accent.red(), pal.accent.green(), pal.accent.blue(), 90));
            glow.setColorAt(1.0, QColor(0, 0, 0, 0));
            painter.fillRect(r, glow);
            const QPixmap glyph_image = RenderGlyph(glyph, pal.accent, 84);
            painter.drawPixmap((r.width() - 84) / 2, (r.height() - 84) / 2, glyph_image);
        } else {
            // Fill the tile edge-to-edge: cover scale, centered; the rounded clip trims the
            // overhang so the picture always fits the card.
            const QPixmap cover =
                icon.scaled(r.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            painter.drawPixmap((r.width() - cover.width()) / 2, (r.height() - cover.height()) / 2,
                               cover);
        }
    }

    const bool strip_focused = [this] {
        QWidget* w = parentWidget();
        for (int hops = 0; w && hops < 5; ++hops) {
            if (auto* strip = qobject_cast<TileStrip*>(w)) {
                return strip->HasFocus();
            }
            w = w->parentWidget();
        }
        return false;
    }();

    // A soft diagonal sheen for depth.
    {
        QLinearGradient sheen(0, 0, r.width(), r.height());
        sheen.setColorAt(0.0, QColor(255, 255, 255, selected && strip_focused ? 38 : 26));
        sheen.setColorAt(0.22, QColor(255, 255, 255, 0));
        sheen.setColorAt(0.78, QColor(255, 255, 255, 0));
        sheen.setColorAt(1.0, QColor(0, 0, 0, 54));
        painter.fillRect(r, sheen);
    }

    // The selection ring - the Switch 2 aqua focus ring. Only while the strip owns the focus
    // chain; otherwise the dock owns the highlight and the tile stays quiet.
    if (selected && strip_focused) {
        painter.setPen(QPen(QColor(0x3f, 0xd8, 0xff), 3.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(r).adjusted(1.75, 1.75, -1.75, -1.75), 15, 15);
        // A soft outer glow.
        QRadialGradient ring(QPointF(r.width() / 2.0, r.height() / 2.0), r.width() * 0.9);
        ring.setColorAt(0.0, QColor(63, 216, 255, 0));
        ring.setColorAt(0.8, QColor(63, 216, 255, 30));
        ring.setColorAt(1.0, QColor(63, 216, 255, 60));
        painter.fillRect(r, ring);
    }
}

void GameTile::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (ghost) {
            // Empty slot: straight into "add a game", quiet tick instead of a launch chime.
            SoundPlayer::Instance().Play(SoundPlayer::Effect::TabSwitch);
            emit Activated(QString(), 0);
            return;
        }
        SoundPlayer::Instance().Play(SoundPlayer::Effect::Confirm);
        const int idx = parentWidget() ? parentWidget()->property("tile_index").toInt() : -1;
        if (parentWidget()) {
            if (auto* strip = qobject_cast<TileStrip*>(parentWidget()->parentWidget())) {
                // move selection to this tile, then activate
                if (idx >= 0 && idx != strip->SelectedIndex()) {
                    strip->MoveSelection(idx - strip->SelectedIndex());
                }
            }
        }
        emit Activated(path, program_id);
        return;
    }
    QWidget::mousePressEvent(event);
}

void GameTile::enterEvent(QEnterEvent*) {
    AnimateFocus(true);
}

bool GameTile::focusNextPrevChild(bool) {
    return false;
}

// ------------------------------------------------------------------ TileStrip
TileStrip::TileStrip(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(280);
    setAttribute(Qt::WA_StyledBackground);
    setObjectName(QStringLiteral("Ocarina2TileStrip"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("Ocarina2TileScroll"));
    scroll->setWidgetResizable(false);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("Ocarina2TileContent"));
    content->setFixedHeight(240);
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(288, 0, 0, 0);
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
}

void TileStrip::SetGames(const std::vector<TileRow>& games) {
    LOG_INFO(Frontend, "TileStrip: SetGames with {} games", games.size());
    auto* scroll = findChild<QScrollArea*>(QStringLiteral("Ocarina2TileScroll"));
    auto* content = scroll ? scroll->widget() : nullptr;
    auto* row = content ? qobject_cast<QHBoxLayout*>(content->layout()) : nullptr;
    if (!row || !content) {
        return;
    }
    content->setFixedHeight(280);
    // Rebuild the tiles only when the game list actually changed - otherwise keep the existing
    // tiles (and the running focus animations) intact.
    size_t signature = 14695981039346656037ULL;
    for (const TileRow& game : games) {
        const QString key = game.path + QLatin1Char('|') + QString::number(game.program_id);
        for (const QChar c : key) {
            signature ^= c.unicode();
            signature *= 1099511628211ULL;
        }
    }
    if (signature == last_signature_ && games.size() == tiles_.size()) {
        return;
    }
    last_signature_ = signature;

    for (GameTile* tile : tiles_) {
        row->removeWidget(tile);
        tile->deleteLater();
    }
    for (GameTile* ghost : ghosts_) {
        row->removeWidget(ghost);
        ghost->deleteLater();
    }
    tiles_.clear();
    ghosts_.clear();
    games_ = games;
    selected = -1;

    for (size_t i = 0; i < games.size(); ++i) {
        auto* tile = new GameTile(content);
        tile->SetGame(games[i].title, games[i].path, games[i].program_id, games[i].icon);
        tile->setProperty("tile_index", static_cast<int>(i));
        connect(tile, &GameTile::Activated, this, [this](const QString& path, u64 program_id) {
            emit Activated(path, program_id);
        });
        row->addWidget(tile);
        tiles_.push_back(tile);
    }
    // The empty slots keep the row wider than the screen, so they run off the right edge
    // like the Switch's home menu.
    const int min_slots = std::max(9, (width() + 241) / 242 + 1);
    const int ghost_count = (std::max)(0, min_slots - static_cast<int>(tiles_.size()));
    for (int i = 0; i < ghost_count; ++i) {
        auto* ghost_tile = new GameTile(content);
        ghost_tile->SetGame(QString(), QString(), 0, QPixmap(), /*ghost=*/true);
        connect(ghost_tile, &GameTile::Activated, this,
                [this](const QString& path, u64 program_id) { emit Activated(path, program_id); });
        row->addWidget(ghost_tile);
        ghosts_.push_back(ghost_tile);
    }
    content->setFixedWidth(288 + static_cast<int>(games.size() + ghost_count) * 242 + 96);
    if (SlotCount() > 0) {
        selected = 0;
        SlotTile(0)->AnimateFocus(true);
        SyncToSelection();
        if (!games.empty()) {
            emit SelectionChanged(games_[0].title, games_[0].program_id);
        } else {
            emit SelectionChanged(QString(), 0);
        }
    }
}

GameTile* TileStrip::SlotTile(int index) const {
    const int real = static_cast<int>(tiles_.size());
    if (index < real) {
        return tiles_[index];
    }
    return ghosts_[index - real];
}

void TileStrip::SyncToSelection() {
    if (selected < 0 || selected >= SlotCount()) {
        return;
    }
    auto* scroll = findChild<QScrollArea*>(QStringLiteral("Ocarina2TileScroll"));
    if (!scroll) {
        return;
    }
    GameTile* tile = SlotTile(selected);
    const int target = tile->x() - (width() / 2) + (tile->width() / 2);
    QPropertyAnimation* anim = new QPropertyAnimation(scroll->horizontalScrollBar(), "value", this);
    anim->setDuration(240);
    anim->setStartValue(scroll->horizontalScrollBar()->value());
    anim->setEndValue(std::clamp(target, 0, scroll->horizontalScrollBar()->maximum()));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void TileStrip::MoveSelection(int delta) {
    const int slot_count = SlotCount();
    if (slot_count == 0) {
        return;
    }
    const int next = (selected + delta + slot_count) % slot_count;
    if (next == selected) {
        return;
    }
    if (selected >= 0) {
        SlotTile(selected)->AnimateFocus(false);
    }
    selected = next;
    SlotTile(selected)->AnimateFocus(true);
    SoundPlayer::Instance().Play(SoundPlayer::Effect::Navigate);
    SyncToSelection();
    if (selected < static_cast<int>(games_.size())) {
        emit SelectionChanged(games_[selected].title, games_[selected].program_id);
    } else {
        emit SelectionChanged(QString(), 0);
    }
}

void TileStrip::ActivateSelected() {
    const int slot_count = SlotCount();
    if (selected < 0 || selected >= slot_count) {
        return;
    }
    const bool is_ghost = selected >= static_cast<int>(games_.size());
    SoundPlayer::Instance().Play(is_ghost ? SoundPlayer::Effect::TabSwitch
                                          : SoundPlayer::Effect::Confirm);
    emit Activated(is_ghost ? QString() : games_[selected].path,
                   is_ghost ? 0 : games_[selected].program_id);
}

void TileStrip::SelectFirst() {
    const int slot_count = SlotCount();
    if (slot_count == 0) {
        return;
    }
    if (selected >= 0) {
        SlotTile(selected)->AnimateFocus(false);
    }
    selected = 0;
    SlotTile(0)->AnimateFocus(true);
    SyncToSelection();
    if (!games_.empty()) {
        emit SelectionChanged(games_[0].title, games_[0].program_id);
    } else {
        emit SelectionChanged(QString(), 0);
    }
}

void TileStrip::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
        MoveSelection(-1);
        return;
    case Qt::Key_Right:
        MoveSelection(1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        ActivateSelected();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void TileStrip::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    has_focus_ = true;
    if (selected < 0 && SlotCount() > 0) {
        SelectFirst();
    }
    for (GameTile* tile : tiles_) {
        tile->update();
    }
    for (GameTile* ghost : ghosts_) {
        ghost->update();
    }
}

void TileStrip::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    has_focus_ = false;
    for (GameTile* tile : tiles_) {
        tile->update();
    }
    for (GameTile* ghost : ghosts_) {
        ghost->update();
    }
}

bool TileStrip::focusNextPrevChild(bool) {
    return false;
}

} // namespace Ocarina2Ui