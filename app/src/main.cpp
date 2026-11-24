// UTDeck's app (stage 4). The Qt 6 setup follows what the Dolphin port learned on this phone
// (dolphin-ut, docs/ut-port/stage3.0-foundations.md, stage3.1-library.md):
// - Lomiri gives apps QT_QPA_PLATFORM with "ubuntumirclient" first, a Qt 5 plugin: it is dropped
//   so that Qt 6 takes the next entry (Wayland);
// - Qt Quick needs an OpenGL ES default surface format on libhybris, or it logs "Cannot find
//   EGLConfig" before recovering;
// - Lomiri Components scale themselves (grid units), so QT_SCALE_FACTOR is not set.
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSurfaceFormat>
#include <QTimer>

#include <csignal>
#include <cstdlib>

#include <unistd.h>

#include "Controllers.h"
#include "GameSettings.h"
#include "KeepAwake.h"
#include "Library.h"
#include "OwnGames.h"
#include "SteamGames.h"
#include "Setup.h"
#include "SteamLaunch.h"
#include "Uninstall.h"

namespace
{
void DropUbuntuMirClientPlatform()
{
  const QByteArray platforms = qgetenv("QT_QPA_PLATFORM");
  if (platforms.isEmpty())
    return;
  QByteArrayList kept;
  for (const QByteArray& platform : platforms.split(';'))
  {
    if (!platform.isEmpty() && !platform.toLower().contains("ubuntumirclient"))
      kept.append(platform);
  }
  if (kept.isEmpty())
    qunsetenv("QT_QPA_PLATFORM");
  else
    qputenv("QT_QPA_PLATFORM", kept.join(';'));
}

void UseOpenGLES()
{
  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGLES);
  format.setVersion(3, 0);
  QSurfaceFormat::setDefaultFormat(format);
}

// The click's package name from lomiri-app-launch's APP_ID ("utdeck.alphaliu01_utdeck_0.1.0"), as
// Lomiri apps name their settings and data folders (MainView.applicationName).
QString PackageName()
{
  const QString app_id = qEnvironmentVariable("APP_ID");
  const int end = app_id.indexOf(QLatin1Char('_'));
  return end > 0 ? app_id.left(end) : QString();
}

// Lomiri closes apps with SIGTERM. A signal handler may only set a flag; a timer acts on it.
volatile std::sig_atomic_t s_quit_requested = 0;

void OnQuitSignal(int)
{
  s_quit_requested = 1;
}
}  // namespace

// The session's runtime folder for the app and everything it starts (stage 4.7): an app started from
// an adb shell by lomiri-app-launch got XDG_RUNTIME_DIR=~/.cache (the shell has none), so it watched the
// wrong folder for game-run.sh's state and never saw a game run.
void UseSessionRuntimeDir()
{
  const QByteArray runtime = "/run/user/" + QByteArray::number(getuid());
  if (qgetenv("XDG_RUNTIME_DIR") != runtime && QFileInfo(QString::fromLatin1(runtime)).isDir())
    qputenv("XDG_RUNTIME_DIR", runtime);
}

int main(int argc, char** argv)
{
  UseSessionRuntimeDir();
  DropUbuntuMirClientPlatform();
  UseOpenGLES();

  const QString package = PackageName();
  QCoreApplication::setApplicationName(package.isEmpty() ? QStringLiteral("utdeck") : package);
  QCoreApplication::setOrganizationName(package.isEmpty() ? QStringLiteral("utdeck") : package);
  QCoreApplication::setApplicationVersion(QStringLiteral(UTDECK_VERSION));

  QGuiApplication app(argc, argv);
  std::signal(SIGTERM, OnQuitSignal);
  std::signal(SIGINT, OnQuitSignal);
  QTimer quit_check;
  QObject::connect(&quit_check, &QTimer::timeout, &app, [] {
    if (s_quit_requested)
      QCoreApplication::exit(0);
  });
  quit_check.start(200);

  Library library;
  // The first-time setup and its sign-in by QR code (stage 4.2); before the engine, whose image
  // provider uses it.
  Setup setup;
  // Steam's launches and the dialogs it waits on (stage 4.3).
  SteamLaunch steam_launch;
  // The connected controllers as games see them (stage 4.4); read only while their page is open.
  Controllers controllers;
  // The user's own games: adding, editing, removing (stage 4.5).
  OwnGames own_games;
  // One game's settings (stage 4.6).
  GameSettings game_settings;
  // Steam's own tasks: the user's Steam library, installing, downloads, uninstalling (stage 4.7).
  SteamGames steam_games(&steam_launch);
  KeepAwake keep_awake;
  // Uninstalling UTDeck (stage 4.9).
  Uninstall uninstall;
  // Stage 4.8: the controller read through SDL is not seen by the phone as use (only touches are), so
  // the screen went off while the app was driven with a controller. Each use holds the screen on;
  // 15 s after the last one the hold ends, and repowerd then counts its own screen timeout again from
  // the start (checked on stone: off 61 s after the release with a 60 s timeout), as after a touch.
  QTimer controller_idle;
  controller_idle.setSingleShot(true);
  controller_idle.setInterval(15000);
  QObject::connect(&controllers, &Controllers::used, &app, [&] {
    keep_awake.hold(QStringLiteral("controller"));
    controller_idle.start();
  });
  QObject::connect(&controller_idle, &QTimer::timeout, &app,
                   [&] { keep_awake.release(QStringLiteral("controller")); });

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
  engine.rootContext()->setContextProperty(QStringLiteral("setup"), &setup);
  engine.rootContext()->setContextProperty(QStringLiteral("steamLaunch"), &steam_launch);
  engine.rootContext()->setContextProperty(QStringLiteral("controllers"), &controllers);
  engine.rootContext()->setContextProperty(QStringLiteral("ownGames"), &own_games);
  engine.rootContext()->setContextProperty(QStringLiteral("gameSettings"), &game_settings);
  engine.rootContext()->setContextProperty(QStringLiteral("steamGames"), &steam_games);
  engine.rootContext()->setContextProperty(QStringLiteral("keepAwake"), &keep_awake);
  engine.rootContext()->setContextProperty(QStringLiteral("uninstall"), &uninstall);
  engine.addImageProvider(QStringLiteral("steamdialog"), new SteamDialogImageProvider(&steam_launch));
  engine.addImageProvider(QStringLiteral("qr"), new QrImageProvider(&setup));
  engine.addImageProvider(QStringLiteral("steamscreen"), new ScreenImageProvider(&setup));
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("UTDeck", "Main");
  return app.exec();
}
