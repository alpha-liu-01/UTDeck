#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantMap>

// Stage 4.6: one game's settings, Steam's or the user's own, as its utdeck-env file holds them
// (steamapps/compatdata/<appid>/utdeck-env, read by utdeck-compat; wine/own/<id>/utdeck-env, read by
// own-run.sh): one NAME=value per line, read at each start, winning over defaults.env.
//
// load() gives the settings the page shows: runtime ("utdeck" or "proton"), args, resolution ("native"
// or WxH), translator ("box64" or "fex", for 32-bit code), overlay (the performance overlay's level,
// stage 5.1: "off", "fps", "frametime", "full", or "" for the one in Settings), fpsLimit (the frame
// rate limit, stage 5.2: "off", "28", "30", "40", "45", "48", "60", or "" for the one in Settings),
// and the switches
// boost, pause, x87, fill (true = on; the default comes from defaults.env, so a switch at its default
// writes no line), plus
// advanced: the file's other NAME=value lines as text. save() writes them back: comments and blank lines
// stay where they were, a setting's line is replaced in place (or dropped at its default), and the
// advanced lines follow at the end. Written to a new file renamed over the old one.
//
// Also Proton's Wine base (installed or not, its download through get-runtime.sh), and the game's last
// run: its log's summary and last lines, with Steam's utdeck-compat.log for a Steam game (long numbers
// masked: Steam's logs can hold the account's).
class GameSettings : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged)
  Q_PROPERTY(QString downloadResult READ downloadResult NOTIFY downloadChanged)

public:
  explicit GameSettings(QObject* parent = nullptr);

  bool downloading() const { return m_download.state() != QProcess::NotRunning; }
  QString downloadResult() const { return m_download_result; }

  Q_INVOKABLE QVariantMap load(const QString& app_id) const;
  Q_INVOKABLE bool save(const QString& app_id, const QVariantMap& settings);
  // An empty utdeck-env: every setting back to its default (comments kept).
  Q_INVOKABLE bool reset(const QString& app_id);
  // The settings for all games (stage 5.1), in wine/user.env, which game-run.sh reads between a
  // game's own settings and defaults.env (defaults.env is replaced with each app update, user.env
  // stays): overlay ("off", "fps", "frametime" or "full"), overlayPosition ("top-left",
  // "top-center" or "top-right") and fpsLimit ("off", "28", "30", "40", "45", "48" or "60"). A value
  // at its default (off, top-left) writes no line.
  Q_INVOKABLE QVariantMap loadGlobal() const;
  Q_INVOKABLE bool saveGlobal(const QVariantMap& settings);
  Q_INVOKABLE bool protonInstalled() const;
  Q_INVOKABLE void downloadProton();
  // The last run's log: {summary, log (its last lines), compat (Steam's utdeck-compat.log, last lines),
  // file}; empty strings when the game has not run.
  Q_INVOKABLE QVariantMap lastRun(const QString& app_id) const;

signals:
  void downloadChanged();

private:
  QString envPath(const QString& app_id) const;

  QProcess m_download;
  QString m_download_result;
};
