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
// such as the "Controller using Steam Input" notice or a publisher's agreement. Then that dialog's
// window alone (the window in front, not Steam's main window, which shows the account name) is
// captured twice a second and offered to QML as image://steamdialog/<revision>; taps on it are
// passed to it (XTest). The picture is kept only while the dialog waits. When the launch goes on,
// Steam's main window (which a dialog brings back, drawing in software) is closed again.
class SteamLaunch : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString appId READ appId NOTIFY changed)
  Q_PROPERTY(QString task READ task NOTIFY changed)
  Q_PROPERTY(bool waitingForUser READ waitingForUser NOTIFY changed)
  Q_PROPERTY(int dialogRevision READ dialogRevision NOTIFY dialogChanged)

public:
  explicit SteamLaunch(QObject* parent = nullptr);
  ~SteamLaunch() override;

  QString appId() const { return m_app_id; }
  QString task() const { return m_task; }
  bool waitingForUser() const { return m_waiting_for_user; }
  int dialogRevision() const { return m_dialog_revision; }
  QImage dialogImage() const;

  // A tap at (fx, fy), fractions of the dialog's picture.
  Q_INVOKABLE void tap(qreal fx, qreal fy);

signals:
  void changed();
  void dialogChanged();

private:
  void readLog();
  void handleLine(const QString& line);
  void setWaitingForUser(bool waiting);
  void captureDialog();

  QString m_log;
  qint64 m_offset = -1;
  QTimer m_poll;
  QTimer m_wait_timer;
  QTimer m_capture_timer;
  QString m_app_id;
  QString m_action_id;
  QString m_task;
  bool m_waiting = false;           // Steam said it waits
  bool m_waiting_for_user = false;  // ... for 2 s: a dialog for the user
  mutable QMutex m_mutex;
  QImage m_dialog;
  int m_dialog_x = 0;
  int m_dialog_y = 0;
  int m_dialog_revision = 0;
  std::atomic<bool> m_capturing{false};
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
