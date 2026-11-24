#include "Uninstall.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <unistd.h>

namespace
{
// The script's steps, in its order ("app" only with removeApp).
const char* const kSteps[] = {"stop", "steam", "utdeck", "entries", "memory", "appdata", "app"};
}  // namespace

Uninstall::Uninstall(QObject* parent) : QObject(parent)
{
  m_script = qEnvironmentVariable(
      "UTDECK_UNINSTALL_SCRIPT",
      QCoreApplication::applicationDirPath() + QStringLiteral("/setup/utdeck-uninstall.sh"));
  // The session's runtime folder, where the unit writes (as Setup reads the setup's steps).
  m_state_dir = QStringLiteral("/run/user/%1/utdeck-uninstall").arg(getuid());
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &Uninstall::readSteps);

  m_plan.setProcessChannelMode(QProcess::SeparateChannels);
  connect(&m_plan, &QProcess::finished, this, [this] {
    QVariantList items;
    const QString out = QString::fromUtf8(m_plan.readAllStandardOutput());
    for (const QString& line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    {
      const QStringList f = line.split(QLatin1Char('\t'));
      if (f.size() < 4)
        continue;
      QVariantMap item;
      item[QStringLiteral("action")] = f[0];
      item[QStringLiteral("kb")] = f[1].toLongLong();
      item[QStringLiteral("key")] = f[2];
      item[QStringLiteral("where")] = f[3];
      items << item;
    }
    m_items = items;
    if (!m_next_plan.isEmpty())
    {
      const QStringList next = m_next_plan;
      m_next_plan.clear();
      m_plan.start(QStringLiteral("sh"), next);
    }
    emit planChanged();
  });
}

QStringList Uninstall::options(bool delete_games, bool delete_saves) const
{
  QStringList o;
  if (delete_games)
    o << QStringLiteral("--delete-games");
  if (delete_saves)
    o << QStringLiteral("--delete-saves");
  return o;
}

void Uninstall::plan(bool delete_games, bool delete_saves)
{
  const QStringList arguments = QStringList{m_script, QStringLiteral("plan")} + options(delete_games, delete_saves);
  if (planning())
  {
    m_next_plan = arguments;
    return;
  }
  m_plan.start(QStringLiteral("sh"), arguments);
  emit planChanged();
}

bool Uninstall::start(bool delete_games, bool delete_saves, bool remove_app)
{
  // Once at a time; again after a failed run (a game was running, say).
  if (m_started && !failed())
    return false;
  // The unit runs outside the app; its old state goes first, so a step from an earlier run is not
  // read as this one's.
  QDir(m_state_dir).removeRecursively();
  QStringList arguments = {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
                           QStringLiteral("--unit=utdeck-uninstall"), QStringLiteral("sh"), m_script,
                           QStringLiteral("run")};
  arguments << options(delete_games, delete_saves) << QStringLiteral("--wait-app")
            << QString::number(QCoreApplication::applicationPid());
  if (remove_app)
    arguments << QStringLiteral("--remove-app");
  if (!QProcess::startDetached(QStringLiteral("systemd-run"), arguments))
  {
    m_error = tr("Could not start the uninstall (systemd-run failed to start).");
    emit stepsChanged();
    return false;
  }
  m_error.clear();
  m_started = true;
  m_poll.start();
  readSteps();
  return true;
}

void Uninstall::readSteps()
{
  QVariantList steps;
  for (const char* name : kSteps)
  {
    QFile file(m_state_dir + QLatin1Char('/') + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
      continue;
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const int space = line.indexOf(QLatin1Char(' '));
    QVariantMap step;
    step[QStringLiteral("name")] = QLatin1String(name);
    step[QStringLiteral("state")] = space < 0 ? line : line.left(space);
    step[QStringLiteral("detail")] = space < 0 ? QString() : line.mid(space + 1);
    steps << step;
  }
  if (steps != m_steps)
  {
    m_steps = steps;
    emit stepsChanged();
  }
}

bool Uninstall::finished() const
{
  // Done up to the steps that wait for the app to close (appdata, app).
  bool any = false;
  for (const QVariant& v : m_steps)
  {
    const QVariantMap step = v.toMap();
    const QString name = step.value(QStringLiteral("name")).toString();
    if (name == QLatin1String("appdata") || name == QLatin1String("app"))
      continue;
    any = true;
    if (step.value(QStringLiteral("state")).toString() != QLatin1String("done"))
      return false;
  }
  return any;
}

bool Uninstall::failed() const
{
  for (const QVariant& v : m_steps)
    if (v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("failed"))
      return true;
  return false;
}
