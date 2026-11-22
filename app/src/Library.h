#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QString>
#include <QTimer>
#include <QVector>

// The installed Steam games, read from Steam's own files: steamapps/appmanifest_<appid>.acf (name
// and install state) and appcache/librarycache/<appid>/ (the cover). The same rules as
// tools/ut-probe/steam-library.py (stage 3.8): Steam's runtimes, Proton, the shared
// redistributables and FEX are not games, and only fully installed games are listed. Nothing of the
// account is read.
//
// play() starts a game the way the app drawer entries of stage 3.8 do: a transient systemd user
// unit running "steam-service.sh play APPID", which starts hidden Steam if needed, waits for its
// sign-in and launches the game. The unit runs outside the app, so the game does not end with it.
//
// runningGame and gameInFront (stage 4.1): the game that game-run.sh runs, from the file it keeps in
// $XDG_RUNTIME_DIR/utdeck/game ("LABEL PID FOCUS_FILE"; a Steam game's label is steam-<appid>), and
// whether that game's window has the keyboard focus (Wine patch 0015 writes 1 or 0 to the focus
// file). The window hides the library while the game is in front and shows it again when the file
// goes. The folder is watched; while a game runs, the focus file is read four times a second.
class Library : public QAbstractListModel
{
  Q_OBJECT
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(QString steamFolder READ steamFolder CONSTANT)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(QString runningGame READ runningGame NOTIFY runningGameChanged)
  Q_PROPERTY(bool gameInFront READ gameInFront NOTIFY gameInFrontChanged)
  // The last game ended after Lomiri asked its window to close (the user swiped its card, which is
  // UTDeck's, away): Wine patch 0019 creates "<focus file>.closed". The app then quits too.
  Q_PROPERTY(bool gameClosedFromOutside READ gameClosedFromOutside NOTIFY runningGameChanged)

public:
  enum Role
  {
    AppIdRole = Qt::UserRole + 1,
    TitleRole,
    CoverRole,
    SizeRole,
  };

  explicit Library(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString steamFolder() const { return m_steam; }
  QString lastError() const { return m_last_error; }
  QString runningGame() const { return m_running_game; }
  bool gameInFront() const { return m_game_in_front; }
  bool gameClosedFromOutside() const { return m_game_closed_from_outside; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool play(const QString& app_id);

signals:
  void countChanged();
  void lastErrorChanged();
  void runningGameChanged();
  void gameInFrontChanged();

private:
  struct Game
  {
    QString app_id;
    QString title;
    QString cover;  // file URL, or empty
    qint64 size = 0;
  };

  void setLastError(const QString& error);
  void readGameState();

  QString m_steam;
  QString m_service;
  QString m_last_error;
  QVector<Game> m_games;
  QString m_state_dir;
  QFileSystemWatcher m_state_watcher;
  QTimer m_focus_poll;
  QString m_running_game;
  bool m_game_in_front = false;
  QString m_focus_file;  // the running game's focus file (game-run.sh), for the .closed marker
  bool m_game_closed_from_outside = false;
};
