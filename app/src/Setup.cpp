#include "Setup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPair>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

#include <ZXing/BarcodeFormat.h>
#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>
#include <ZXing/ReadBarcode.h>

#include <functional>

#include <unistd.h>

#include "XScreen.h"

namespace
{
// The steps of utdeck-setup.sh, in its order: the mandatory part, then Steam's.
const char* const kSteps[] = {"files", "runtime", "xvfb", "steam", "service", "signin", "tool"};
// Hidden Steam's screen (steam-service.sh).
const char* const kSteamDisplay = ":50";
}  // namespace

Setup::Setup(QObject* parent) : QObject(parent)
{
  // The script in the click (setup/, next to the program); UTDECK_SETUP_SCRIPT overrides it.
  m_script = qEnvironmentVariable(
      "UTDECK_SETUP_SCRIPT",
      QCoreApplication::applicationDirPath() + QStringLiteral("/setup/utdeck-setup.sh"));
  // Hidden Steam's service script, where the setup's "files" step puts it.
  m_service = QDir::homePath() + QStringLiteral("/utdeck-probe/steam/steam-service.sh");
  QSettings settings;
  m_choice = settings.value(QStringLiteral("steam/choice")).toString();
  m_steam_enabled = settings.value(QStringLiteral("steam/enabled"), true).toBool();

  // The user's runtime folder itself, where game-run.sh and utdeck-setup.sh write (they run outside
  // the app, with the session's environment), not the app's XDG_RUNTIME_DIR: an app started from an adb
  // shell by lomiri-app-launch got XDG_RUNTIME_DIR=~/.cache, watched ~/.cache/utdeck and never saw a
  // game run (stage 4.7).
  QString runtime = QStringLiteral("/run/user/%1").arg(getuid());
  if (!QFileInfo(runtime).isDir())
    runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  m_state_dir = runtime + QStringLiteral("/utdeck/setup");
  QDir().mkpath(m_state_dir);
  m_watcher.addPath(m_state_dir);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Setup::readSteps);
  // Rewrites of a step's file (rename over it) show as directory changes; the timer catches the
  // rest while something runs.
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &Setup::readSteps);
  m_capture_timer.setInterval(2000);
  connect(&m_capture_timer, &QTimer::timeout, this, &Setup::capture);
  m_screen_timer.setInterval(500);
  connect(&m_screen_timer, &QTimer::timeout, this, &Setup::captureScreen);
  m_steam_watch.setInterval(5000);
  connect(&m_steam_watch, &QTimer::timeout, this, &Setup::watchSteam);
  // The stop of Steam after a game, with Steam's switch off (gameEnded): 45 s later, unless another
  // game started meanwhile (gameStarting).
  m_steam_stop.setSingleShot(true);
  m_steam_stop.setInterval(45000);
  connect(&m_steam_stop, &QTimer::timeout, this, [this] {
    // Not while Steam downloads a game (stage 4.7: steamapps/downloading/<appid>): asked again later.
    // A folder Steam left behind counts only while the game's state is more than "fully installed"
    // (StateFlags 4; an empty one stays after a file check that found nothing).
    const QString apps = QDir::homePath() + QStringLiteral("/.local/share/Steam/steamapps");
    bool downloads = false;
    for (const QString& app_id : QDir(apps + QStringLiteral("/downloading")).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
      QFile acf(apps + QStringLiteral("/appmanifest_") + app_id + QStringLiteral(".acf"));
      const QByteArray text = acf.open(QIODevice::ReadOnly) ? acf.readAll() : QByteArray();
      static const QRegularExpression flags(QStringLiteral("\"StateFlags\"\\s+\"(\\d+)\""));
      const QRegularExpressionMatch m = flags.match(QString::fromUtf8(text));
      if (!m.hasMatch() || m.captured(1) != QLatin1String("4"))
        downloads = true;
    }
    if (downloads)
    {
      m_steam_stop.start();
      return;
    }
    check();
    if (!m_steam_enabled && steamRunning())
      steamService(QStringLiteral("stop"));
  });
  check();

  // Updated app, set-up phone: the new files are copied quietly.
  if (!stepDone("files") && stepDone("runtime") && !running())
  {
    m_quiet_update = true;
    start(false);
  }

  // Steam's switch on: hidden Steam runs while the app does.
  if (m_steam_enabled && steamChoice() == QLatin1String("use") && steamInstalled() && !steamRunning() &&
      !running())
    steamService(QStringLiteral("start"));
}

Setup::~Setup()
{
  // A capture still being read uses this object when it ends.
  m_capture_timer.stop();
  m_screen_timer.stop();
  QThreadPool::globalInstance()->waitForDone(5000);
}

QString Setup::stepState(const char* name) const
{
  for (const QVariant& step : m_steps)
  {
    const QVariantMap map = step.toMap();
    if (map.value(QStringLiteral("name")) == QLatin1String(name))
      return map.value(QStringLiteral("state")).toString();
  }
  return QStringLiteral("todo");
}

bool Setup::anyState(const QString& state) const
{
  for (const QVariant& step : m_steps)
    if (step.toMap().value(QStringLiteral("state")) == state)
      return true;
  return false;
}

QString Setup::steamChoice() const
{
  if (m_choice.isEmpty() && steamInstalled())
    return QStringLiteral("use");
  return m_choice;
}

QImage Setup::qrImage() const
{
  QMutexLocker lock(&m_qr_mutex);
  return m_qr;
}

void Setup::check()
{
  if (!QFileInfo(m_script).isExecutable())
  {
    setLastError(tr("UTDeck's setup script is missing (%1).").arg(m_script));
    return;
  }
  QProcess process;
  process.start(m_script, {QStringLiteral("check")});
  if (!process.waitForFinished(10000))
  {
    process.kill();
    setLastError(tr("Checking the setup did not finish."));
    return;
  }
  // "STEP done" or "STEP todo", one per line.
  m_checked.clear();
  for (const QByteArray& line : process.readAllStandardOutput().split('\n'))
  {
    const QList<QByteArray> parts = line.trimmed().split(' ');
    if (parts.size() >= 2)
      m_checked.insert(QString::fromLatin1(parts[0]), QString::fromLatin1(parts[1]));
  }
  readSteps();
}

bool Setup::start(bool with_steam)
{
  if (with_steam)
  {
    m_choice = QStringLiteral("use");
    QSettings().setValue(QStringLiteral("steam/choice"), m_choice);
    emit steamChanged();
  }
  // The unit runs outside the app; a unit of that name still running means the setup goes on, and
  // systemd-run then fails, which is right.
  QStringList arguments = {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
                           QStringLiteral("--unit=utdeck-setup"), m_script, QStringLiteral("run")};
  if (with_steam)
    arguments.append(QStringLiteral("steam"));
  if (!QProcess::startDetached(QStringLiteral("systemd-run"), arguments))
  {
    setLastError(tr("Could not start the setup (systemd-run failed to start)."));
    return false;
  }
  setLastError({});
  m_poll.start();
  updateNeeded();
  return true;
}

void Setup::skipSteam()
{
  m_choice = QStringLiteral("skip");
  QSettings().setValue(QStringLiteral("steam/choice"), m_choice);
  emit steamChanged();
  updateNeeded();
}

void Setup::setSteamEnabled(bool enabled)
{
  if (enabled == m_steam_enabled)
    return;
  m_steam_enabled = enabled;
  QSettings().setValue(QStringLiteral("steam/enabled"), enabled);
  emit steamChanged();
  if (enabled && steamInstalled() && !steamRunning())
    steamService(QStringLiteral("start"));
  else if (!enabled && steamRunning())
    steamService(QStringLiteral("stop"));
}

void Setup::gameEnded()
{
  m_steam_stop.start();
}

void Setup::gameStarting()
{
  m_steam_stop.stop();
}

void Setup::steamService(const QString& command)
{
  // "stop" waits for Steam to shut down (up to 30 s); neither blocks the app.
  if (!QProcess::startDetached(QStringLiteral("sh"), {m_service, command}))
    setLastError(tr("Could not run Steam's service script (%1).").arg(m_service));
  if (command == QLatin1String("start"))
  {
    // Started, Steam signs itself in about a minute later: checked every 5 s until it has, for up
    // to 3 minutes (stage 4.2, S3: one check at 30 s came too early and left "Sign in" showing).
    m_steam_starting = true;
    m_steam_since.start();
    m_steam_watch.start();
    emit steamChanged();
  }
  else
  {
    // Steam stops within 30 s, after a cloud sync still running (up to 5 minutes, stage 4.7).
    for (const int seconds : {5, 15, 30, 60, 120, 330})
      QTimer::singleShot(seconds * 1000, this, &Setup::check);
  }
}

void Setup::watchSteam()
{
  check();
  const qint64 elapsed = m_steam_since.elapsed();
  // Signed in, given up after 3 minutes, or Steam not running 20 s after the start.
  if (steamSignedIn() || elapsed > 180000 || (elapsed > 20000 && !steamRunning()))
  {
    m_steam_watch.stop();
    m_steam_starting = false;
    emit steamChanged();
    return;
  }
  // Steam with no saved sign-in opens its sign-in window about 10 s after starting, instead of
  // signing in: its QR code on the hidden screen means "waiting for a sign-in" (nothing else is read
  // from the screen; the window then shows no account data).
  if (elapsed > 15000)
    lookForSignInWindow();
}

void Setup::lookForSignInWindow()
{
  if (m_looking.exchange(true))
    return;
  QThreadPool::globalInstance()->start([this] {
    XScreenImage screen;
    bool found = false;
    if (CaptureXScreen(kSteamDisplay, &screen))
    {
      ZXing::ImageView view(screen.pixels.data(), screen.width, screen.height, ZXing::ImageFormat::BGRX,
                            screen.stride);
      ZXing::ReaderOptions options;
      options.setFormats(ZXing::BarcodeFormat::QRCode);
      found = !ZXing::ReadBarcodes(view, options).empty();
    }
    QMetaObject::invokeMethod(this, [this, found] {
      m_looking = false;
      if (found && m_steam_starting && !steamSignedIn())
      {
        m_steam_watch.stop();
        m_steam_starting = false;
        emit steamChanged();
      }
    }, Qt::QueuedConnection);
  });
}

void Setup::readSteps()
{
  // The run's files: what goes on, what went wrong, and (while a run goes on) what it finished.
  // Otherwise the last check says done or todo: a "done" left from an earlier run (Steam started
  // then, stopped since) does not count.
  QHash<QString, QPair<QString, QString>> files;
  bool run_going = false;
  for (const char* name : kSteps)
  {
    QFile file(m_state_dir + QLatin1Char('/') + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const int space = line.indexOf(QLatin1Char(' '));
    const QString state = space < 0 ? line : line.left(space);
    files.insert(QLatin1String(name), {state, space < 0 ? QString() : line.mid(space + 1)});
    run_going = run_going || state == QLatin1String("running");
  }
  QVariantList steps;
  for (const char* name : kSteps)
  {
    QString state = m_checked.value(QLatin1String(name), QStringLiteral("todo"));
    QString detail;
    const auto file = files.constFind(QLatin1String(name));
    if (file != files.constEnd())
    {
      const QString& file_state = file->first;
      if (file_state == QLatin1String("running") || file_state == QLatin1String("failed") ||
          (run_going && file_state == QLatin1String("done")))
      {
        state = file_state;
        if (file_state != QLatin1String("done"))
          detail = file->second;
      }
    }
    steps.append(QVariantMap{{QStringLiteral("name"), QLatin1String(name)},
                             {QStringLiteral("state"), state},
                             {QStringLiteral("detail"), detail}});
  }
  if (steps != m_steps)
  {
    m_steps = steps;
    emit stepsChanged();
  }
  const bool now_running = running();
  if (now_running)
    m_poll.start();
  else
    m_poll.stop();
  if (m_was_running && !now_running)
  {
    // A run ended: the check tells what is installed and running now.
    m_was_running = false;
    m_quiet_update = false;
    QTimer::singleShot(0, this, &Setup::check);
  }
  m_was_running = m_was_running || now_running;
  updateCapture();
  updateNeeded();
}

void Setup::updateNeeded()
{
  const QString choice = steamChoice();
  const bool needed = (running() && !m_quiet_update) ||
                      (!baseDone() && !m_quiet_update) || choice.isEmpty() ||
                      (choice == QLatin1String("use") && !steamReady());
  if (needed == m_needed)
    return;
  const bool was_needed = m_needed;
  m_needed = needed;
  emit neededChanged();
  if (was_needed && !needed)
    emit finished();
}

void Setup::updateCapture()
{
  if (signingIn())
  {
    if (!m_capture_timer.isActive())
    {
      m_capture_timer.start();
      capture();
    }
    return;
  }
  m_capture_timer.stop();
  if (m_fallback)
    setFallback(false);
  // Signed in, or not signing in: no code is kept.
  bool had = false;
  {
    QMutexLocker lock(&m_qr_mutex);
    had = !m_qr.isNull();
    m_qr = QImage();
    m_qr_hash = 0;
  }
  if (had)
  {
    ++m_qr_revision;
    emit qrChanged();
  }
}

void Setup::capture()
{
  if (m_capturing.exchange(true))
    return;  // the previous capture is still being read
  QThreadPool::globalInstance()->start([this] {
    XScreenImage screen;
    bool found = false;
    size_t hash = 0;
    QImage code;
    if (CaptureXScreen(kSteamDisplay, &screen))
    {
      ZXing::ImageView view(screen.pixels.data(), screen.width, screen.height, ZXing::ImageFormat::BGRX,
                            screen.stride);
      ZXing::ReaderOptions options;
      options.setFormats(ZXing::BarcodeFormat::QRCode);
      const ZXing::Results results = ZXing::ReadBarcodes(view, options);
      if (!results.empty() && results.front().isValid())
      {
        const std::string text = results.front().text();
        found = true;
        hash = std::hash<std::string>{}(text);
        // Drawn again here, natively: one pixel per module, a 4-module white border.
        const ZXing::BitMatrix matrix =
            ZXing::MultiFormatWriter(ZXing::BarcodeFormat::QRCode).setMargin(0).encode(text, 0, 0);
        const int border = 4;
        code = QImage(matrix.width() + 2 * border, matrix.height() + 2 * border, QImage::Format_Grayscale8);
        code.fill(255);
        for (int y = 0; y < matrix.height(); ++y)
          for (int x = 0; x < matrix.width(); ++x)
            if (matrix.get(x, y))
              code.setPixel(x + border, y + border, qRgb(0, 0, 0));
      }
    }
    QMetaObject::invokeMethod(this, [this, found, hash, code] { captured(found, hash, code); },
                              Qt::QueuedConnection);
  });
}

void Setup::captured(bool found, size_t text_hash, const QImage& code)
{
  m_capturing = false;
  if (!signingIn() || !found)
    return;  // signed in meanwhile, or the window is not showing its code right now
  {
    QMutexLocker lock(&m_qr_mutex);
    if (text_hash == m_qr_hash)
      return;
    m_qr_hash = text_hash;
    m_qr = code;
  }
  ++m_qr_revision;
  emit qrChanged();
}

void Setup::setLastError(const QString& error)
{
  if (error == m_last_error)
    return;
  m_last_error = error;
  emit lastErrorChanged();
}

QImage QrImageProvider::requestImage(const QString&, QSize* size, const QSize& requested_size)
{
  QImage code = m_setup->qrImage();
  if (code.isNull())
  {
    code = QImage(1, 1, QImage::Format_Grayscale8);
    code.fill(255);
  }
  if (requested_size.width() > 0 && requested_size.height() > 0)
  {
    // Whole pixels per module, so every module stays sharp.
    const int scale = qMax(1, qMin(requested_size.width() / code.width(), requested_size.height() / code.height()));
    code = code.scaled(code.width() * scale, code.height() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
  }
  if (size)
    *size = code.size();
  return code;
}

QImage Setup::screenImage() const
{
  QMutexLocker lock(&m_qr_mutex);
  return m_screen;
}

void Setup::setFallback(bool active)
{
  active = active && signingIn();
  if (active == m_fallback)
    return;
  m_fallback = active;
  if (active)
  {
    m_screen_timer.start();
    captureScreen();
  }
  else
  {
    // Nothing of that screen is kept once it is not shown.
    m_screen_timer.stop();
    QMutexLocker lock(&m_qr_mutex);
    m_screen = QImage();
  }
  ++m_screen_revision;
  emit fallbackChanged();
  emit screenChanged();
}

void Setup::captureScreen()
{
  if (m_screen_capturing.exchange(true))
    return;
  QThreadPool::globalInstance()->start([this] {
    XScreenImage screen;
    QImage image;
    if (CaptureXScreen(kSteamDisplay, &screen))
    {
      // B G R X bytes: Qt's RGB32 on this little-endian phone.
      image = QImage(screen.pixels.data(), screen.width, screen.height, screen.stride, QImage::Format_RGB32).copy();
    }
    QMetaObject::invokeMethod(this, [this, image] {
      m_screen_capturing = false;
      if (!m_fallback || image.isNull())
        return;
      {
        QMutexLocker lock(&m_qr_mutex);
        m_screen = image;
      }
      ++m_screen_revision;
      emit screenChanged();
    }, Qt::QueuedConnection);
  });
}

void Setup::tap(qreal fx, qreal fy)
{
  const QImage screen = screenImage();
  if (!m_fallback || screen.isNull() || fx < 0 || fx > 1 || fy < 0 || fy > 1)
    return;
  XClick(kSteamDisplay, qRound(fx * (screen.width() - 1)), qRound(fy * (screen.height() - 1)));
  captureScreen();
}

void Setup::type(const QString& text)
{
  if (!m_fallback)
    return;
  XTypeText(kSteamDisplay, text.toStdU32String());
  captureScreen();
}

void Setup::key(const QString& name)
{
  if (!m_fallback || (name != QLatin1String("BackSpace") && name != QLatin1String("Return") &&
                      name != QLatin1String("Tab")))
    return;
  XPressKey(kSteamDisplay, name.toLatin1().constData());
  captureScreen();
}

QImage ScreenImageProvider::requestImage(const QString&, QSize* size, const QSize& requested_size)
{
  QImage screen = m_setup->screenImage();
  if (screen.isNull())
  {
    screen = QImage(16, 9, QImage::Format_RGB32);
    screen.fill(Qt::black);
  }
  if (requested_size.width() > 0 && requested_size.height() > 0)
    screen = screen.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (size)
    *size = screen.size();
  return screen;
}
