// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <QWidget>
#include <QString>

#include "common/common_types.h"

namespace Ocarina2Ui {

/// One game tile in the Switch-2-style horizontal strip: a rounded card with its own palette
/// and glyph, drawn in paintEvent - no assets. Selection is a focus ring (the aqua Switch 2
/// ring) plus a subtle scale-up via the fixed size animating between states.
class GameTile : public QWidget {
    Q_OBJECT
public:
    explicit GameTile(QWidget* parent = nullptr);

    void SetGame(const QString& title, const QString& path, u64 program_id, const QPixmap& icon,
                 bool ghost = false);
    const QString& GamePath() const { return path; }
    u64 ProgramId() const { return program_id; }
    bool IsGhost() const { return ghost; }
    void AnimateFocus(bool focused);

    QSize sizeHint() const override { return {252, 168}; }

signals:
    void Activated(const QString& path, u64 program_id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    QString title;
    QString path;
    u64 program_id = 0;
    QPixmap icon;
    int palette = 0;     ///< which of the background palettes
    int glyph = 0;       ///< which glyph
    bool selected = false;
    bool ghost = false;  ///< an empty "add a game" slot, not a real tile
};

/// A horizontal scrolling strip of game tiles: pressing Left/Right moves the selection (the
/// strip scrolls so the selected tile stays on screen, like the Switch home menu), Enter or
/// A activates the selected game. Honest to the Switch design - tiles slide left as you move
/// right, nothing ever jumps off the screen.
class TileStrip : public QWidget {
    Q_OBJECT
public:
    struct TileRow {
        QString title;
        QString path;
        u64 program_id = 0;
        QPixmap icon;
    };

    explicit TileStrip(QWidget* parent = nullptr);

    /// All (title, path, program_id, icon) rows.
    void SetGames(const std::vector<TileRow>& games);

    int SelectedIndex() const { return selected; }
    QString SelectedPath() const { return selected >= 0 ? games_[selected].path : QString(); }
    u64 SelectedProgramId() const { return selected >= 0 ? games_[selected].program_id : 0; }
    QString SelectedTitle() const { return selected >= 0 ? games_[selected].title : QString(); }

signals:
    void Activated(const QString& path, u64 program_id);
    void SelectionChanged(const QString& title, u64 program_id);

public slots:
    void MoveSelection(int delta);
    void ActivateSelected();
    void SelectFirst();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    void SyncToSelection();

    std::vector<TileRow> games_;
    std::vector<GameTile*> tiles_;   ///< one per game, in strip order
    std::vector<GameTile*> ghosts_;  ///< empty "add game" slots filling the row
    int selected = -1;
    size_t last_signature_ = 0;  ///< FNV of the games (path|program_id) to avoid rebuilding tiles
};

} // namespace Ocarina2Ui
