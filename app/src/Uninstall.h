#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantList>

// Stage 4.9: uninstalling UTDeck from the app (setup/utdeck-uninstall.sh, in the click).
// - plan(deleteGames, deleteSaves): what would be removed and kept, with sizes (the script's plan
//   mode, read in the background): items, one map per kind {action: remove|keep|refuse, kb, key,
//   where}.
// - start(deleteGames, deleteSaves, removeApp): the uninstall as a systemd user unit
//   (utdeck-uninstall), so that it goes on when the app closes; it waits for this process to end
//   before removing the app's own folders (and then the app itself, with removeApp).
// - steps: its steps' states while it runs, {name, state, detail}, read from
//   $XDG_RUNTIME_DIR/utdeck-uninstall/STEP every second.
class Uninstall : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool planning READ planning NOTIFY planChanged)
  Q_PROPERTY(QVariantList items READ items NOTIFY planChanged)
  Q_PROPERTY(bool started READ started NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
  // Every step but the last ones that wait for the app to close is done (or one failed).
  Q_PROPERTY(bool finished READ finished NOTIFY stepsChanged)
  Q_PROPERTY(bool failed READ failed NOTIFY stepsChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY stepsChanged)

public:
  explicit Uninstall(QObject* parent = nullptr);

  bool planning() const { return m_plan.state() != QProcess::NotRunning; }
  QVariantList items() const { return m_items; }
  bool started() const { return m_started; }
  QVariantList steps() const { return m_steps; }
  bool finished() const;
  bool failed() const;
  QString lastError() const { return m_error; }

  Q_INVOKABLE void plan(bool delete_games, bool delete_saves);
  Q_INVOKABLE bool start(bool delete_games, bool delete_saves, bool remove_app);

signals:
  void planChanged();
  void stepsChanged();

private:
  QStringList options(bool delete_games, bool delete_saves) const;
  void readSteps();

  QString m_script;
  QString m_state_dir;
  QProcess m_plan;
  QStringList m_next_plan;  // a plan asked for while one runs: run after it
  QVariantList m_items;
  bool m_started = false;
  QVariantList m_steps;
  QString m_error;
  QTimer m_poll;
};
