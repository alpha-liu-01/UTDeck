#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>

class SteamLaunch;

// Stage 4.7: Steam's own tasks from the app. The games of the user's Steam library (appinfo.py
// --library: the apps Steam caches art for, with their public data from appinfo.vdf; only games and
// demos), installing one (steam-game.sh install: for a game that also has a Linux build, its mapping to
// UTDeck's tool first, so Steam installs the Windows build; then Steam's Install window on the "Steam
// is asking" page), the downloads in progress, and uninstalling (steam-game.sh uninstall: Steam's
// question on the same page; the game's Windows folder, compatdata/<appid>, deleted afterwards only if
// the user asked). Steam's own "application shortcut" for an installed game (a second app drawer entry
// next to UTDeck's) is removed once the game is installed.
class SteamGames : public QObject
{
  Q_OBJECT
  // {appId, name, systems, sizeMb, cover, installed, windows (has a Windows build), cloud ("auto",
  // "api" or "": how the game uses Steam Cloud, appinfo.py)}, sorted by name.
  Q_PROPERTY(QVariantList games READ games NOTIFY gamesChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY gamesChanged)
  // The downloads in progress: {appId, name, percent (-1 while unknown), doneMb, totalMb (0 while
  // unknown), speedKb (KB/s, averaged; -1 until known), secondsLeft (-1 while unknown), update (the game
  // was installed already: an update, its .acf has a size on disk)}.
  Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsChanged)
  // What ended while the app ran, newest first: {appId, name, what ("installed", "uninstalled",
  // "uninstalledAll": with its Windows folder and leftover files, "checked": the files checked), detail
  // ("checked": the number of files fetched again; -1 unknown, -3 not checked: the game was running or
  // starting), time ("HH:mm")}.
  Q_PROPERTY(QVariantList finished READ finished NOTIFY finishedChanged)
  // A task started from the app and not yet ended: {appId, verb ("install" or "uninstall"), name}, or empty.
  Q_PROPERTY(QVariantMap task READ task NOTIFY taskChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY taskChanged)

public:
  explicit SteamGames(SteamLaunch* launch, QObject* parent = nullptr);

  QVariantList games() const { return m_games; }
  bool loading() const { return m_list.state() != QProcess::NotRunning; }
  QVariantList downloads() const { return m_downloads; }
  QVariantList finished() const { return m_finished; }
  QVariantMap task() const { return m_task; }
  QString lastError() const { return m_error; }

  Q_INVOKABLE void refresh();
  // Free space where Steam installs games, in MB.
  Q_INVOKABLE int freeMb() const;
  // The game's size on disk, installed (MB, from its .acf), or 0.
  Q_INVOKABLE int installedMb(const QString& app_id) const;
  Q_INVOKABLE bool install(const QString& app_id, const QString& name);
  // Stage 4.7: the game's Steam Cloud state, from Steam's own files (read only, nothing printed):
  // {known (Steam has a cloud list for it), files, sizeKb, inSync (files in sync), lastSync
  // ("yyyy-MM-dd HH:mm", the list's last write: Steam rewrites it at every sync), result (the last
  // "Download/Upload complete" result in Steam's cloud log, "" if none in the log), problem ("conflict",
  // "path", "failed" or ""), backups (the dates of utdeck-saves-<date> folders of phone copies set
  // aside)}. The account's folder (userdata/<account>) is only looked into, never shown.
  Q_INVOKABLE QVariantMap cloudState(const QString& app_id) const;
  // Stage 4.7: Steam's "Verify integrity of game files" (steam-game.sh validate: steam://validate/APPID,
  // no question; Steam hashes every file and downloads again what is missing or damaged). The result
  // comes from Steam's content log into the finished list ("checked": detail = files fetched again).
  Q_INVOKABLE bool verify(const QString& app_id, const QString& name);
  // delete_data: also delete the game's Windows folder (compatdata/<appid>) once Steam is done, and
  // the files the game itself wrote into its install folder (settings, logs), which Steam leaves.
  Q_INVOKABLE bool uninstall(const QString& app_id, const QString& name, bool delete_data);

signals:
  void gamesChanged();
  void downloadsChanged();
  void finishedChanged();
  void taskChanged();
  // An install or uninstall ended (the library should be read again).
  void installedChanged();

private:
  bool start(const QString& verb, const QString& app_id, const QString& name);
  void poll();
  void removeSteamShortcut(const QString& app_id);
  void addFinished(const QString& app_id, const QString& name, const QString& what, int detail = 0);
  // The file check's end in Steam's logs since the task started: -2 still running, -3 not checked (the
  // game was running or starting), else the number of files fetched again.
  int validationResult(const QString& app_id);
  QString nameOf(const QString& app_id) const;

  SteamLaunch* m_launch;
  QProcess m_list;
  QProcess m_script;
  QTimer m_poll;
  QVariantList m_games;
  QVariantList m_downloads;
  QVariantList m_finished;
  QVariantMap m_task;
  QString m_error;
  QHash<QString, QString> m_delete_after;  // uninstalls whose Windows folder goes too: their installdir
  QHash<QString, QString> m_names;          // the names of the downloads seen (the .acf goes at the end)
  // Per download: the bytes and the time of the last poll, and the averaged speed (bytes/s).
  QHash<QString, QPair<qint64, qint64>> m_last_sample;
  QHash<QString, double> m_speed;
  QSet<QString> m_was_downloading;
  int m_task_ticks = 0;
  qint64 m_log_offset = 0;       // the content log's size when the task started
  qint64 m_console_offset = 0;   // the console log's size when the task started
};
