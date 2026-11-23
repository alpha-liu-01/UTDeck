#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>

#include <atomic>

// Stage 4.3, the launch flow: a game's launch as Steam records it, and hidden Steam's dialogs shown
// in the app.
//
// Steam writes one line per step of a launch into logs/console_log.txt:
//   [time] GameAction [AppID N, ActionID M] : LaunchApp changed task to TASK with ""
//   [time] GameAction [AppID N, ActionID M] : LaunchApp waiting for user response to TASK ""
//   [time] GameAction [AppID N, ActionID M] : LaunchApp continues with user response "TASK"
// Only those lines are read (the file's other lines can hold account data). Most waits Steam answers
// itself at once; one that lasts 2 s waits for a person: a dialog on hidden Steam's screen (Xvfb :50),
// such as the "Controller using Steam Input" notice (a window of its own) or a publisher's agreement
// (drawn inside Steam's main window, which UTDeck otherwise keeps closed: it is opened for the wait,
// steam://open/main, and then shown whole, with the account name, to the user only). The dialog's
// window (or else the main window, cut down to the box an agreement is drawn in, so the box is shown
// larger) is captured twice a second and offered to QML as
// image://steamdialog/<revision>; taps on it are passed to it (XTest). The picture is kept only while
// the dialog waits. When the launch goes on, Steam's main window (which a dialog brings back, drawing
// in software) is closed again, and once more a few seconds after the game's process starts (Steam
// brings the window up then too).
//
// An agreement answered in the main window does not reach the launch that waits for it: Steam saves
// the answer (a "<appid>_eula_<n>" entry in the account's localconfig.vdf) and the launch stays
// waiting for good (PvZ Replanted, the Nothing Phone). So during a ShowEula wait those entries alone
// are read from that file; when they change, the wait ends and agreementAccepted() asks for a new
// start, which Steam then runs past the agreement.
class SteamLaunch : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString appId READ appId NOTIFY changed)
  Q_PROPERTY(QString task READ task NOTIFY changed)
  Q_PROPERTY(bool waitingForUser READ waitingForUser NOTIFY changed)
  Q_PROPERTY(int dialogRevision READ dialogRevision NOTIFY dialogChanged)
  // The game the user just started (expect()): appId and task then only follow its launch.
  Q_PROPERTY(QString expectedAppId READ expectedAppId NOTIFY changed)
  // Steam's own record of the game's process ("Game process added/removed"), for this launch.
  Q_PROPERTY(bool gameProcessAdded READ gameProcessAdded NOTIFY changed)
  Q_PROPERTY(bool gameProcessRemoved READ gameProcessRemoved NOTIFY changed)

public:
  explicit SteamLaunch(QObject* parent = nullptr);
  ~SteamLaunch() override;

  QString appId() const { return m_app_id; }
  QString task() const { return m_task; }
  bool waitingForUser() const { return m_waiting_for_user; }
  int dialogRevision() const { return m_dialog_revision; }
  QString expectedAppId() const { return m_expected; }
  bool gameProcessAdded() const { return m_process_added; }
  bool gameProcessRemoved() const { return m_process_removed; }
  QImage dialogImage() const;

  // A tap at (fx, fy), fractions of the dialog's picture.
  Q_INVOKABLE void tap(qreal fx, qreal fy);
  // STEPS notches of the mouse wheel at (fx, fy) (negative: up): a drag on the page scrolls.
  Q_INVOKABLE void scroll(qreal fx, qreal fy, int steps);
  // The user started APP_ID (stage 4.3, the starting page): what Steam logged before is forgotten,
  // and from now on only this game's launch steps count.
  Q_INVOKABLE void expect(const QString& app_id);
  // The user leaves Steam's question unanswered (the dialog page's Cancel): the page closes, Steam's
  // main window too, and the task reads "Cancelled" until the next expect().
  Q_INVOKABLE void dismiss();
  // Stage 4.7: a dialog Steam shows outside a launch (its Install window, its Uninstall question,
  // after steam-game.sh): the page shows Steam's dialog window (never its main window) until that
  // window has gone for about 1.5 s, or 90 s without one.
  Q_INVOKABLE void showSteamDialog();

signals:
  void changed();
  void dialogChanged();
  // The agreement Steam waited for was accepted: the game is to be started again (see above).
  void agreementAccepted(const QString& app_id);

private:
  void readLog();
  void handleLine(const QString& line);
  void setWaitingForUser(bool waiting);
  void captureDialog();
  void openSteamWindow();
  QString agreements() const;
  void checkAgreement();
  void closeSteamWindowLater();

  QString m_log;
  qint64 m_offset = -1;
  QTimer m_poll;
  QTimer m_wait_timer;
  QTimer m_capture_timer;
  QString m_expected;
  QString m_app_id;
  QString m_action_id;
  QString m_task;
  bool m_process_added = false;
  bool m_process_removed = false;
  bool m_waiting = false;           // Steam said it waits
  bool m_waiting_for_user = false;  // ... for 2 s: a dialog for the user
  mutable QMutex m_mutex;
  QImage m_dialog;
  int m_dialog_x = 0;
  int m_dialog_y = 0;
  int m_dialog_revision = 0;
  std::atomic<bool> m_capturing{false};
  bool m_opened_main = false;   // asked Steam to open its main window during this wait
  bool m_manual = false;        // showSteamDialog(): not a launch's wait
  bool m_manual_seen = false;   // its dialog window was there
  int m_manual_misses = 0;      // captures since it was last there
  qint64 m_manual_since = 0;
  QString m_agreements;          // the app's agreement entries when the ShowEula wait began
  QString m_agreement_file;
  qint64 m_agreement_mtime = 0;
};

class SteamDialogImageProvider : public QQuickImageProvider
{
public:
  explicit SteamDialogImageProvider(SteamLaunch* launch)
      : QQuickImageProvider(QQuickImageProvider::Image), m_launch(launch)
  {
  }
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

private:
  SteamLaunch* m_launch;
};
