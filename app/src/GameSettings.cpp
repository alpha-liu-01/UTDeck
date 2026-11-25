#include "GameSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include "Library.h"

namespace
{
const QString kOwnPrefix = QStringLiteral("own:");
const QRegularExpression kName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
const QRegularExpression kOwnId(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
const QRegularExpression kDigits(QStringLiteral("^\\d+$"));

// The settings the page shows, by their utdeck-env names.
const QStringList kKnown = {QStringLiteral("UTDECK_RUNTIME"), QStringLiteral("GAME_ARGS"),
                            QStringLiteral("UTDECK_RESOLUTION"), QStringLiteral("HODLL"),
                            QStringLiteral("UTDECK_BOOST"), QStringLiteral("UTDECK_PAUSE"), QStringLiteral("UTDECK_KEEP_AWAKE"),
                            QStringLiteral("FEX_X87REDUCEDPRECISION"), QStringLiteral("UTDECK_FILL"),
                            QStringLiteral("UTDECK_OVERLAY"), QStringLiteral("UTDECK_FPS_LIMIT")};

// The performance overlay's levels (stage 5.1; game-run.sh turns them into MangoHud's settings) and
// its places on the screen.
const QStringList kOverlayLevels = {QStringLiteral("off"), QStringLiteral("fps"), QStringLiteral("frametime"),
                                    QStringLiteral("full")};
const QStringList kOverlayPositions = {QStringLiteral("top-left"), QStringLiteral("top-center"),
                                       QStringLiteral("top-right")};
// The frame rate limits (stage 5.2): each divides a refresh rate phones' panels offer.
const QStringList kFpsLimits = {QStringLiteral("off"), QStringLiteral("28"), QStringLiteral("30"), QStringLiteral("40"),
                                QStringLiteral("45"), QStringLiteral("48"), QStringLiteral("60")};

QString Wine()
{
  return QDir::homePath() + QStringLiteral("/utdeck-probe/wine");
}

// NAME=value lines of a file (the first of each name), comments and blank lines skipped.
QHash<QString, QString> ReadEnv(const QString& path)
{
  QHash<QString, QString> values;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return values;
  while (!file.atEnd())
  {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq > 0 && !line.startsWith(QLatin1Char('#')) && !values.contains(line.left(eq)))
      values.insert(line.left(eq), line.mid(eq + 1));
  }
  return values;
}

QString OneLine(const QString& value)
{
  QString v = value;
  return v.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' ')).trimmed();
}

// The last LINES lines of a text, without terminal colours, long numbers masked (Steam's logs can hold
// the account's number).
QString Tail(const QString& text, int lines)
{
  static const QRegularExpression colours(QStringLiteral("\\x1b\\[[0-9;]*m"));
  static const QRegularExpression long_number(QStringLiteral("\\d{9,}"));
  QStringList all = text.split(QLatin1Char('\n'));
  if (all.size() > lines)
    all = all.mid(all.size() - lines);
  QString out = all.join(QLatin1Char('\n'));
  out.remove(colours);
  out.replace(long_number, QStringLiteral("(number)"));
  return out;
}

QString ReadAll(const QString& path, qint64 max_bytes)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  if (file.size() > max_bytes)
    file.seek(file.size() - max_bytes);
  return QString::fromUtf8(file.readAll());
}
}  // namespace

GameSettings::GameSettings(QObject* parent) : QObject(parent)
{
  m_download.setProcessChannelMode(QProcess::MergedChannels);
  connect(&m_download, &QProcess::stateChanged, this, &GameSettings::downloadChanged);
  connect(&m_download, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    const QStringList out = QString::fromUtf8(m_download.readAll()).trimmed().split(QLatin1Char('\n'));
    m_download_result = status == QProcess::NormalExit && code == 0 && protonInstalled()
                            ? tr("Proton 11's Wine base is installed.")
                            : tr("The download failed: %1").arg(out.isEmpty() ? QString() : out.last().trimmed());
    emit downloadChanged();
  });
}

QString GameSettings::envPath(const QString& app_id) const
{
  if (app_id.startsWith(kOwnPrefix))
  {
    const QString id = app_id.mid(kOwnPrefix.size());
    return kOwnId.match(id).hasMatch() ? Wine() + QStringLiteral("/own/") + id + QStringLiteral("/utdeck-env") : QString();
  }
  if (!kDigits.match(app_id).hasMatch())
    return {};
  return QDir::homePath() + QStringLiteral("/.local/share/Steam/steamapps/compatdata/") + app_id + QStringLiteral("/utdeck-env");
}

QVariantMap GameSettings::load(const QString& app_id) const
{
  QVariantMap s;
  const QString path = envPath(app_id);
  if (path.isEmpty())
    return s;
  const QHash<QString, QString> env = ReadEnv(path);
  const QHash<QString, QString> defaults = ReadEnv(Wine() + QStringLiteral("/defaults.env"));
  const auto value = [&](const QString& name, const QString& fallback) {
    return env.value(name, defaults.value(name, fallback));
  };
  s[QStringLiteral("runtime")] = value(QStringLiteral("UTDECK_RUNTIME"), QStringLiteral("utdeck")) == QLatin1String("proton")
                                     ? QStringLiteral("proton") : QStringLiteral("utdeck");
  s[QStringLiteral("args")] = env.value(QStringLiteral("GAME_ARGS"));
  s[QStringLiteral("resolution")] = value(QStringLiteral("UTDECK_RESOLUTION"), QStringLiteral("native"));
  s[QStringLiteral("translator")] =
      value(QStringLiteral("HODLL"), QString()).contains(QLatin1String("fex"), Qt::CaseInsensitive) ? QStringLiteral("fex")
                                                                                                    : QStringLiteral("box64");
  s[QStringLiteral("boost")] = value(QStringLiteral("UTDECK_BOOST"), QStringLiteral("0")) != QLatin1String("0");
  s[QStringLiteral("pause")] = value(QStringLiteral("UTDECK_PAUSE"), QStringLiteral("0")) == QLatin1String("1");
  s[QStringLiteral("awake")] = value(QStringLiteral("UTDECK_KEEP_AWAKE"), QStringLiteral("0")) == QLatin1String("1");
  s[QStringLiteral("x87")] = value(QStringLiteral("FEX_X87REDUCEDPRECISION"), QStringLiteral("0")) == QLatin1String("1");
  s[QStringLiteral("fill")] = value(QStringLiteral("UTDECK_FILL"), QStringLiteral("0")) == QLatin1String("1");
  // The game's own overlay level, or "" for the one in Settings (user.env).
  const QString overlay = env.value(QStringLiteral("UTDECK_OVERLAY"));
  s[QStringLiteral("overlay")] = kOverlayLevels.contains(overlay) ? overlay : QString();
  // The game's own frame rate limit, or "" for the one in Settings (user.env).
  const QString limit = env.value(QStringLiteral("UTDECK_FPS_LIMIT"));
  s[QStringLiteral("fpsLimit")] = kFpsLimits.contains(limit) ? limit : QString();
  // The other NAME=value lines, as the advanced text.
  QStringList advanced;
  QFile file(path);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    while (!file.atEnd())
    {
      const QString line = QString::fromUtf8(file.readLine()).trimmed();
      const int eq = line.indexOf(QLatin1Char('='));
      if (eq > 0 && !line.startsWith(QLatin1Char('#')) && !kKnown.contains(line.left(eq)))
        advanced << line;
    }
  s[QStringLiteral("advanced")] = advanced.join(QLatin1Char('\n'));
  return s;
}

bool GameSettings::save(const QString& app_id, const QVariantMap& s)
{
  const QString path = envPath(app_id);
  if (path.isEmpty())
    return false;
  const QHash<QString, QString> defaults = ReadEnv(Wine() + QStringLiteral("/defaults.env"));
  // Each setting's line, or "" for none (its default applies).
  QHash<QString, QString> want;
  const auto unless_default = [&](const QString& name, const QString& v, const QString& fallback) {
    want[name] = v == defaults.value(name, fallback) ? QString() : v;
  };
  want[QStringLiteral("UTDECK_RUNTIME")] = s.value(QStringLiteral("runtime")).toString() == QLatin1String("proton")
                                               ? QStringLiteral("proton") : QString();
  want[QStringLiteral("GAME_ARGS")] = OneLine(s.value(QStringLiteral("args")).toString());
  const QString resolution = s.value(QStringLiteral("resolution")).toString();
  want[QStringLiteral("UTDECK_RESOLUTION")] =
      QRegularExpression(QStringLiteral("^\\d{3,4}x\\d{3,4}$")).match(resolution).hasMatch() ? resolution : QString();
  want[QStringLiteral("HODLL")] = s.value(QStringLiteral("translator")).toString() == QLatin1String("fex")
                                      ? QStringLiteral("libwow64fex.dll") : QString();
  unless_default(QStringLiteral("UTDECK_BOOST"), s.value(QStringLiteral("boost")).toBool() ? QStringLiteral("1024") : QStringLiteral("0"), QStringLiteral("0"));
  unless_default(QStringLiteral("UTDECK_PAUSE"), s.value(QStringLiteral("pause")).toBool() ? QStringLiteral("1") : QStringLiteral("0"), QStringLiteral("0"));
  unless_default(QStringLiteral("UTDECK_KEEP_AWAKE"), s.value(QStringLiteral("awake")).toBool() ? QStringLiteral("1") : QStringLiteral("0"), QStringLiteral("0"));
  unless_default(QStringLiteral("FEX_X87REDUCEDPRECISION"), s.value(QStringLiteral("x87")).toBool() ? QStringLiteral("1") : QStringLiteral("0"), QStringLiteral("0"));
  unless_default(QStringLiteral("UTDECK_FILL"), s.value(QStringLiteral("fill")).toBool() ? QStringLiteral("1") : QStringLiteral("0"), QStringLiteral("0"));
  const QString overlay = s.value(QStringLiteral("overlay")).toString();
  want[QStringLiteral("UTDECK_OVERLAY")] = kOverlayLevels.contains(overlay) ? overlay : QString();
  const QString limit = s.value(QStringLiteral("fpsLimit")).toString();
  want[QStringLiteral("UTDECK_FPS_LIMIT")] = kFpsLimits.contains(limit) ? limit : QString();

  // The old file: comments and blank lines stay; a setting's line is replaced where it was.
  QStringList out;
  QSet<QString> written;
  QFile old(path);
  if (old.open(QIODevice::ReadOnly | QIODevice::Text))
    while (!old.atEnd())
    {
      const QString line = QString::fromUtf8(old.readLine()).trimmed();
      const int eq = line.indexOf(QLatin1Char('='));
      const QString name = eq > 0 ? line.left(eq) : QString();
      if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
        out << line;
      else if (kKnown.contains(name) && !written.contains(name))
      {
        if (!want.value(name).isEmpty())
          out << name + QLatin1Char('=') + want.value(name);
        written << name;
      }
    }
  for (const QString& name : kKnown)
    if (!written.contains(name) && !want.value(name).isEmpty())
      out << name + QLatin1Char('=') + want.value(name);
  // The advanced lines, as typed: NAME=value only, the page's own settings left to the page.
  for (const QString& raw : s.value(QStringLiteral("advanced")).toString().split(QLatin1Char('\n')))
  {
    const QString line = raw.trimmed();
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq > 0 && kName.match(line.left(eq)).hasMatch() && !kKnown.contains(line.left(eq)))
      out << line;
  }
  while (!out.isEmpty() && out.last().isEmpty())
    out.removeLast();

  if (!QDir().mkpath(QFileInfo(path).path()))
    return false;
  QFile file(path + QStringLiteral(".new"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  file.write((out.join(QLatin1Char('\n')) + (out.isEmpty() ? QString() : QStringLiteral("\n"))).toUtf8());
  file.close();
  QFile::remove(path);
  return QFile::rename(path + QStringLiteral(".new"), path);
}

bool GameSettings::reset(const QString& app_id)
{
  if (envPath(app_id).isEmpty())
    return false;
  const QHash<QString, QString> d = ReadEnv(Wine() + QStringLiteral("/defaults.env"));
  QVariantMap s;
  s[QStringLiteral("runtime")] = QStringLiteral("utdeck");
  s[QStringLiteral("translator")] = QStringLiteral("box64");
  s[QStringLiteral("resolution")] = QStringLiteral("native");
  s[QStringLiteral("boost")] = d.value(QStringLiteral("UTDECK_BOOST"), QStringLiteral("0")) != QLatin1String("0");
  s[QStringLiteral("pause")] = d.value(QStringLiteral("UTDECK_PAUSE")) == QLatin1String("1");
  s[QStringLiteral("awake")] = d.value(QStringLiteral("UTDECK_KEEP_AWAKE")) == QLatin1String("1");
  s[QStringLiteral("x87")] = d.value(QStringLiteral("FEX_X87REDUCEDPRECISION")) == QLatin1String("1");
  s[QStringLiteral("fill")] = d.value(QStringLiteral("UTDECK_FILL")) == QLatin1String("1");
  return save(app_id, s);
}

QVariantMap GameSettings::loadGlobal() const
{
  const QHash<QString, QString> user = ReadEnv(Wine() + QStringLiteral("/user.env"));
  const QString overlay = user.value(QStringLiteral("UTDECK_OVERLAY"));
  const QString position = user.value(QStringLiteral("UTDECK_OVERLAY_POSITION"));
  const QString limit = user.value(QStringLiteral("UTDECK_FPS_LIMIT"));
  QVariantMap g;
  g[QStringLiteral("overlay")] = kOverlayLevels.contains(overlay) ? overlay : QStringLiteral("off");
  g[QStringLiteral("overlayPosition")] = kOverlayPositions.contains(position) ? position : QStringLiteral("top-left");
  g[QStringLiteral("fpsLimit")] = kFpsLimits.contains(limit) ? limit : QStringLiteral("off");
  return g;
}

bool GameSettings::saveGlobal(const QVariantMap& settings)
{
  const QString overlay = settings.value(QStringLiteral("overlay")).toString();
  const QString position = settings.value(QStringLiteral("overlayPosition")).toString();
  const QString limit = settings.value(QStringLiteral("fpsLimit")).toString();
  QStringList out;
  out << QStringLiteral("# UTDeck: the user's settings for all games (the app's Settings page); a game's own")
      << QStringLiteral("# settings (its utdeck-env) win over these, and these over defaults.env.");
  if (kOverlayLevels.contains(overlay) && overlay != QLatin1String("off"))
    out << QStringLiteral("UTDECK_OVERLAY=") + overlay;
  if (kOverlayPositions.contains(position) && position != QLatin1String("top-left"))
    out << QStringLiteral("UTDECK_OVERLAY_POSITION=") + position;
  if (kFpsLimits.contains(limit) && limit != QLatin1String("off"))
    out << QStringLiteral("UTDECK_FPS_LIMIT=") + limit;
  const QString path = Wine() + QStringLiteral("/user.env");
  QFile file(path + QStringLiteral(".new"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  file.write((out.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
  file.close();
  QFile::remove(path);
  return QFile::rename(path + QStringLiteral(".new"), path);
}

bool GameSettings::protonInstalled() const
{
  return QFileInfo(Wine() + QStringLiteral("/runtime-proton/bin/wine")).isExecutable();
}

void GameSettings::downloadProton()
{
  if (m_download.state() != QProcess::NotRunning)
    return;
  m_download_result.clear();
  m_download.start(QStringLiteral("sh"), {Wine() + QStringLiteral("/get-runtime.sh"), QStringLiteral("proton")});
  emit downloadChanged();
}

QVariantMap GameSettings::lastRun(const QString& app_id) const
{
  QVariantMap run;
  run[QStringLiteral("summary")] = QString();
  run[QStringLiteral("log")] = QString();
  run[QStringLiteral("compat")] = QString();
  run[QStringLiteral("file")] = QString();
  const QString label = app_id.startsWith(kOwnPrefix) ? QStringLiteral("own-") + app_id.mid(kOwnPrefix.size())
                                                      : QStringLiteral("steam-") + app_id;
  if (envPath(app_id).isEmpty())
    return run;
  // The newest run of the game itself (not one of Steam's install scripts for it).
  const QDir games(Wine() + QStringLiteral("/games"));
  QString log;
  for (const QString& name : games.entryList({label + QStringLiteral("-*.log")}, QDir::Files, QDir::Time))
  {
    const QString first = ReadAll(games.filePath(name), 1 << 20).section(QLatin1Char('\n'), 0, 0);
    if (!first.contains(QLatin1String("iscriptevaluator")))
    {
      log = games.filePath(name);
      break;
    }
  }
  if (log.isEmpty())
    return run;
  const QString text = ReadAll(log, 4 << 20);
  static const QRegularExpression exit_line(QStringLiteral("(?:^|\\n)exit (-?\\d+)\\s*$"));
  const QRegularExpressionMatch exit = exit_line.match(text);
  const QString problem = Library::problemIn(text);
  QStringList summary;
  summary << tr("Started %1").arg(QFileInfo(log).birthTime().isValid()
                                      ? QFileInfo(log).birthTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                      : QFileInfo(log).fileName());
  summary << (exit.hasMatch() ? tr("Ended with exit code %1.").arg(exit.captured(1)) : tr("Still running, or ended without a closing line."));
  if (!problem.isEmpty())
    summary << problem;
  run[QStringLiteral("summary")] = summary.join(QLatin1Char('\n'));
  run[QStringLiteral("log")] = Tail(text, 300);
  run[QStringLiteral("file")] = log;
  if (!app_id.startsWith(kOwnPrefix))
    run[QStringLiteral("compat")] =
        Tail(ReadAll(QFileInfo(envPath(app_id)).path() + QStringLiteral("/utdeck-compat.log"), 1 << 20), 80);
  return run;
}
