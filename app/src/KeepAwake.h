#pragma once

#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>

// Stage 4.7: the screen kept on by the app itself, for what happens before a game runs: from the tap,
// through Steam's start, its "Steam is asking" windows, first-start installs and the game's launcher,
// a start can take minutes, and the screen's timeout turned it off meanwhile (game-run.sh's own
// keep-awake begins only with the game). hold(key) / release(key): the screen stays on while any key
// is held, through utdeck-awake.py hold (repowerd's keepDisplayOn), which ends when its input is
// closed or the app ends: repowerd then drops the request by itself.
class KeepAwake : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool screenOn READ screenOn NOTIFY screenOnChanged)

public:
  explicit KeepAwake(QObject* parent = nullptr);
  ~KeepAwake() override;

  bool screenOn() const { return !m_keys.isEmpty(); }
  Q_INVOKABLE void hold(const QString& key);
  Q_INVOKABLE void release(const QString& key);

signals:
  void screenOnChanged();

private:
  void update();

  QSet<QString> m_keys;
  QProcess m_helper;
};
