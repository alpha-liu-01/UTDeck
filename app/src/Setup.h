#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <atomic>

// The first-time setup (stage 4.2): on a phone with nothing but UTDeck's click, the app runs the
// click's setup/utdeck-setup.sh as a systemd user unit (utdeck-setup, so that Lomiri does not pause
// it with the app) and shows each step's state, read from $XDG_RUNTIME_DIR/utdeck/setup/STEP
// ("STATE DETAIL") while a run goes on, and from the script's "check" otherwise. The mandatory part
// is UTDeck's files and the Wine runtime; Steam (the hidden screen, the client, starting it, the
// sign-in) is optional: the user's choice and Steam's switch are kept with QSettings.
//
// The sign-in is a QR code: while the "signin" step runs, hidden Steam shows its "Sign in to Steam"
// window on its hidden screen (Xvfb, :50). Every 2 s the screen is captured (libX11) and the QR code
// read from it (ZXing-C++), off the UI thread; when the code changes it is drawn again natively and
// offered to QML as image://qr/<revision>. The user scans it with the Steam phone app and Steam
// signs itself in. What is kept: the drawn code and a hash of its text, to notice a new code; the
// text itself (a one-time sign-in link) is never stored, logged or written. Captures stop once the
// step is done: later windows on that screen can show the account name.
class Setup : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool needed READ needed NOTIFY neededChanged)
  Q_PROPERTY(bool running READ running NOTIFY stepsChanged)
  Q_PROPERTY(bool failed READ failed NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
  Q_PROPERTY(bool baseDone READ baseDone NOTIFY stepsChanged)
  Q_PROPERTY(bool steamInstalled READ steamInstalled NOTIFY stepsChanged)
  Q_PROPERTY(bool steamReady READ steamReady NOTIFY stepsChanged)
  Q_PROPERTY(bool steamRunning READ steamRunning NOTIFY stepsChanged)
  Q_PROPERTY(bool steamSignedIn READ steamSignedIn NOTIFY stepsChanged)
  Q_PROPERTY(bool signingIn READ signingIn NOTIFY stepsChanged)
  Q_PROPERTY(QString steamChoice READ steamChoice NOTIFY steamChanged)
  Q_PROPERTY(bool steamEnabled READ steamEnabled NOTIFY steamChanged)
  Q_PROPERTY(bool steamStarting READ steamStarting NOTIFY steamChanged)
  Q_PROPERTY(bool qrAvailable READ qrAvailable NOTIFY qrChanged)
  Q_PROPERTY(int qrRevision READ qrRevision NOTIFY qrChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(bool fallbackActive READ fallbackActive NOTIFY fallbackChanged)
  Q_PROPERTY(int screenRevision READ screenRevision NOTIFY screenChanged)

public:
  explicit Setup(QObject* parent = nullptr);
  ~Setup() override;

  // The setup page is needed: a run goes on, the mandatory part is not done, or Steam is neither
  // set up nor skipped (or chosen and not installed yet).
  bool needed() const { return m_needed; }
  bool running() const { return anyState(QStringLiteral("running")); }
  bool failed() const { return anyState(QStringLiteral("failed")); }
  QVariantList steps() const { return m_steps; }
  bool baseDone() const { return stepDone("files") && stepDone("runtime"); }
  bool steamInstalled() const { return stepDone("xvfb") && stepDone("steam"); }
  // Installed and UTDeck's runtime Steam's choice for Windows games (the "tool" step, after the
  // first sign-in).
  bool steamReady() const { return steamInstalled() && stepDone("tool"); }
  bool steamRunning() const { return stepDone("service"); }
  bool steamSignedIn() const { return stepDone("signin"); }
  bool signingIn() const { return stepState("signin") == QLatin1String("running"); }
  // "use", "skip", or empty (not asked yet). Steam installed counts as "use".
  QString steamChoice() const;
  // Steam's switch: on, hidden Steam runs while the app does; off, only while a Steam game runs.
  bool steamEnabled() const { return m_steam_enabled; }
  // The app started hidden Steam and it has not signed in yet (it takes about a minute on the
  // Nothing Phone (1): some 50 s of "Connecting" first).
  bool steamStarting() const { return m_steam_starting; }
  bool qrAvailable() const { return !qrImage().isNull(); }
  int qrRevision() const { return m_qr_revision; }
  QString lastError() const { return m_last_error; }

  // The current QR code, one pixel per module with a 4-module white border (for the image provider).
  QImage qrImage() const;

  // Fallback B (the maintainer's choice, 2026-11-21): hidden Steam's real sign-in window shown in the
  // app, for when the QR code cannot be scanned. Its screen is captured twice a second (only while
  // the sign-in step runs and the fallback is shown); taps and typing are passed to it through XTest.
  // What is typed is passed on character by character and kept nowhere.
  bool fallbackActive() const { return m_fallback; }
  int screenRevision() const { return m_screen_revision; }
  QImage screenImage() const;
  Q_INVOKABLE void setFallback(bool active);
  // A tap at (fx, fy), fractions of the screen's width and height.
  Q_INVOKABLE void tap(qreal fx, qreal fy);
  Q_INVOKABLE void type(const QString& text);
  // "BackSpace", "Return" or "Tab".
  Q_INVOKABLE void key(const QString& name);

  // Asks the script which steps are done ("check").
  Q_INVOKABLE void check();
  // Starts the setup unit: the mandatory part, and Steam's too when with_steam (which also records
  // the choice). A unit already running is left to go on.
  Q_INVOKABLE bool start(bool with_steam);
  // The user skipped Steam: it can be set up later from the settings.
  Q_INVOKABLE void skipSteam();
  // Steam's switch. Off stops hidden Steam now; on starts it (if installed).
  Q_INVOKABLE void setSteamEnabled(bool enabled);
  // A game ended: with Steam's switch off, hidden Steam (started on demand by the game's Play) is
  // stopped again, 45 s later (stage 4.6: stopped at once, it was shut down under the next game's
  // launch when the user started another game right away).
  Q_INVOKABLE void gameEnded();
  // A game is being started or runs: a pending stop of Steam (gameEnded) is called off.
  Q_INVOKABLE void gameStarting();

signals:
  void neededChanged();
  void stepsChanged();
  void steamChanged();
  void qrChanged();
  void lastErrorChanged();
  void finished();
  void fallbackChanged();
  void screenChanged();

private:
  QString stepState(const char* name) const;
  bool stepDone(const char* name) const { return stepState(name) == QLatin1String("done"); }
  bool anyState(const QString& state) const;
  void readSteps();
  void updateNeeded();
  void steamService(const QString& command);
  void watchSteam();
  void lookForSignInWindow();
  void updateCapture();
  void capture();
  void captured(bool found, size_t text_hash, const QImage& code);
  void captureScreen();
  void setLastError(const QString& error);

  QString m_script;
  QString m_service;
  QString m_state_dir;
  QFileSystemWatcher m_watcher;
  QTimer m_poll;
  QTimer m_capture_timer;
  QTimer m_steam_watch;
  QTimer m_steam_stop;
  QElapsedTimer m_steam_since;
  bool m_steam_starting = false;
  std::atomic<bool> m_looking{false};
  QHash<QString, QString> m_checked;  // step -> "done" or "todo", from the last check
  QVariantList m_steps;
  bool m_was_running = false;
  // After an update of the app only UTDeck's files are out of date: they are copied without the
  // setup page (the mandatory part only, so Steam is not started against its switch).
  bool m_quiet_update = false;
  bool m_needed = false;
  QString m_choice;
  bool m_steam_enabled = true;
  QString m_last_error;

  mutable QMutex m_qr_mutex;
  QImage m_qr;
  size_t m_qr_hash = 0;
  int m_qr_revision = 0;
  std::atomic<bool> m_capturing{false};

  bool m_fallback = false;
  QTimer m_screen_timer;
  QImage m_screen;  // under m_qr_mutex
  int m_screen_revision = 0;
  std::atomic<bool> m_screen_capturing{false};
};

// image://steamscreen/<revision>: hidden Steam's screen, for fallback B.
class ScreenImageProvider : public QQuickImageProvider
{
public:
  explicit ScreenImageProvider(Setup* setup)
      : QQuickImageProvider(QQuickImageProvider::Image), m_setup(setup)
  {
  }
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

private:
  Setup* m_setup;
};

// image://qr/<revision>: the current QR code, scaled up without smoothing to the size asked for.
class QrImageProvider : public QQuickImageProvider
{
public:
  explicit QrImageProvider(Setup* setup)
      : QQuickImageProvider(QQuickImageProvider::Image), m_setup(setup)
  {
  }
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;

private:
  Setup* m_setup;
};
