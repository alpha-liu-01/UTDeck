#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>

// Stage 4.5: adding, editing and removing the user's own games (not Steam's). A game is a folder
// ~/utdeck-probe/wine/own/<id>/ with game.conf (NAME, DIR, EXE, COVER: plain NAME=value lines that
// own-run.sh reads as text), its prefix pfx/ (made at its first start) and an optional utdeck-env.
// Library lists them and starts them (own-run.sh); this class makes and changes them.
//
// Two ways in: a game folder the user copied into Documents/UTDeck/Games (over MTP), or an installer
// (an .exe in Downloads or Documents), run in the new game's prefix as a game is (its window over the
// app); afterwards the programs it installed under the prefix's C: are listed to choose from, with
// what gamecheck.py --programs says of each (machine, size). The app does not check for DRM bypasses
// (the maintainer, 2026-11-23: like Wine, UTDeck does not police what its users run; our own tests
// still use gamecheck.py's full check). The game's own files are never deleted here; removing a game takes it out of
// the library and keeps its prefix (its saved games), unless the user explicitly asks to delete it.
class OwnGames : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool checking READ checking NOTIFY checkedChanged)
  // The last check (check()): executables (list of {path (relative to the folder), machine, bits,
  // kb, unlikely}), the likely game first; error ("" or why the check failed).
  Q_PROPERTY(QVariantList executables READ executables NOTIFY checkedChanged)
  Q_PROPERTY(QString error READ error NOTIFY checkedChanged)
  Q_PROPERTY(QString gamesFolder READ gamesFolder CONSTANT)

public:
  explicit OwnGames(QObject* parent = nullptr);

  bool checking() const { return m_check.state() != QProcess::NotRunning; }
  QVariantList executables() const { return m_executables; }
  QString error() const { return m_error; }
  QString gamesFolder() const;

  // The folders in Documents/UTDeck/Games: {name, path, added}.
  Q_INVOKABLE QVariantList folders() const;
  // Installers: .exe files in Downloads and Documents (and Documents/UTDeck): {name, path, mb}.
  Q_INVOKABLE QVariantList installers() const;
  // Lists a game folder's programs with gamecheck.py --programs (in the background; checkedChanged
  // when done).
  Q_INVOKABLE void check(const QString& folder);
  // Pictures for a cover: from the game's folder (two levels down) and Pictures (one level down).
  Q_INVOKABLE QStringList images(const QString& folder) const;
  // Saves a game; returns its id ("own:<id>"), or "" on failure (error says why).
  Q_INVOKABLE QString add(const QString& name, const QString& folder, const QString& exe, const QString& cover);
  // One game's settings: {name, folder, exe, cover}, from game.conf.
  Q_INVOKABLE QVariantMap game(const QString& app_id) const;
  Q_INVOKABLE bool edit(const QString& app_id, const QString& name, const QString& exe, const QString& cover);
  // Takes a game out of the library. delete_prefix: also deletes its Wine prefix, with any saved
  // games in it (and an installed game's files, which live there). The game's own folder stays.
  Q_INVOKABLE bool remove(const QString& app_id, bool delete_prefix);
  // The installer way: makes the game (no exe yet) and runs INSTALLER in its prefix through
  // own-run.sh --install, as a game; returns its id ("own:<id>") or "".
  Q_INVOKABLE QString startInstall(const QString& name, const QString& installer);
  // Runs a game's installer again in its prefix (one that produced no program); true when started.
  Q_INVOKABLE bool rerunInstall(const QString& app_id);
  // After an installer: the programs under the game's prefix's C:, except Windows' and Wine's own
  // (full paths, the likely game first).
  Q_INVOKABLE QVariantList installedPrograms(const QString& app_id) const;
  // Saves the installed game's program (a full path from installedPrograms()), keeping the game's
  // id and prefix (where the installer put it).
  Q_INVOKABLE bool finishInstall(const QString& app_id, const QString& name, const QString& program,
                                 const QString& cover);
  // Games whose installer ran but whose program was not chosen yet: {appId, name}.
  Q_INVOKABLE QVariantList pending() const;
  // Whether a game has no exe yet (an installer was started but no program chosen).
  Q_INVOKABLE bool pendingInstall(const QString& app_id) const;

signals:
  void checkedChanged();

private:
  QString newId(const QString& name, const QString& folder) const;
  bool writeConf(const QString& id, const QVariantMap& fields);

  QProcess m_check;
  QVariantList m_executables;
  QString m_error;
};
