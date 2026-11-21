// UTDeck's app (stage 4). The Qt 6 setup follows what the Dolphin port learned on this phone
// (dolphin-ut, docs/ut-port/stage3.0-foundations.md, stage3.1-library.md):
// - Lomiri gives apps QT_QPA_PLATFORM with "ubuntumirclient" first, a Qt 5 plugin: it is dropped
//   so that Qt 6 takes the next entry (Wayland);
// - Qt Quick needs an OpenGL ES default surface format on libhybris, or it logs "Cannot find
//   EGLConfig" before recovering;
// - Lomiri Components scale themselves (grid units), so QT_SCALE_FACTOR is not set.
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSurfaceFormat>
#include <QTimer>

#include <csignal>
#include <cstdlib>

#include "Library.h"

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

int main(int argc, char** argv)
{
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

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("UTDeck", "Main");
  return app.exec();
}
