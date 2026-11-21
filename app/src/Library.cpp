#include "Library.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <algorithm>

#include <unistd.h>

namespace
{
// Installed by Steam but not games: its runtimes, Proton, the shared redistributables, FEX, SteamVR
// (as tools/ut-probe/steam-library.py).
const QSet<QString> kToolIds = {"228980",  "1070560", "1391110", "1628350",
                                "4183110", "3127680", "1493710"};
const QRegularExpression kToolNames(
    QStringLiteral("^(Steam Linux Runtime|Proton|Steamworks Common Redistributables|FEX$|SteamVR)"));

// The first value of each key in an .acf file: the app's own, not a depot's further down.
QHash<QString, QString> ManifestFields(const QString& path)
{
  QHash<QString, QString> fields;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return fields;
  static const QRegularExpression line(QStringLiteral("^\\s*\"(\\w+)\"\\s+\"([^\"]*)\""));
  while (!file.atEnd())
  {
    const QString text = QString::fromUtf8(file.readLine());
    const QRegularExpressionMatch m = line.match(text);
    if (m.hasMatch() && !fields.contains(m.captured(1)))
      fields.insert(m.captured(1), m.captured(2));
  }
  return fields;
}

// Steam's art for a game, best first: the portrait cover, the wide header, the logo. Newer Steam
// clients keep each picture in a folder named after its hash (librarycache/<id>/<hash>/...), older
// ones straight in librarycache/<id>/; the portrait cover is library_600x900.jpg or, in the hashed
// folders, library_capsule.jpg (both 300x450 on stone). A logo alone is no cover (stage 4.1: Stray's
// logo.png was shown while its capsule sat in a hashed folder). Of several copies, the newest wins.
QString CoverFor(const QString& steam, const QString& app_id)
{
  const QDir cache(steam + QStringLiteral("/appcache/librarycache/") + app_id);
  if (!cache.exists())
    return {};
  QStringList folders = {cache.path()};
  for (const QFileInfo& sub : cache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time))
    folders.append(sub.filePath());
  for (const QStringList& names :
       {QStringList{"library_600x900.jpg", "library_capsule.jpg"},
        QStringList{"header.jpg", "library_header.jpg"}, QStringList{"logo.png"}})
  {
    QFileInfo best;
    for (const QString& folder : folders)
      for (const QString& name : names)
      {
        const QFileInfo file(folder + QLatin1Char('/') + name);
        if (file.isFile() && (!best.exists() || file.lastModified() > best.lastModified()))
          best = file;
      }
    if (best.exists())
      return QUrl::fromLocalFile(best.filePath()).toString();
  }
  return {};
}
}  // namespace

Library::Library(QObject* parent) : QAbstractListModel(parent)
{
  m_steam = QDir::homePath() + QStringLiteral("/.local/share/Steam");
  // Stage 3's service script, where tools/ut-probe/steam-push.sh puts it; UTDECK_STEAM_SERVICE
  // overrides it until the app carries its own copy.
  m_service = qEnvironmentVariable("UTDECK_STEAM_SERVICE",
                                   QDir::homePath() + QStringLiteral("/utdeck-probe/steam/steam-service.sh"));
  refresh();

  // The running game, from game-run.sh's file (stage 4.1).
  QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty())
    runtime = QStringLiteral("/run/user/%1").arg(getuid());
  m_state_dir = runtime + QStringLiteral("/utdeck");
  QDir().mkpath(m_state_dir);
  m_state_watcher.addPath(m_state_dir);
  connect(&m_state_watcher, &QFileSystemWatcher::directoryChanged, this, &Library::readGameState);
  m_focus_poll.setInterval(250);
  connect(&m_focus_poll, &QTimer::timeout, this, &Library::readGameState);
  readGameState();
}

int Library::rowCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : m_games.size();
}

QVariant Library::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.row() >= m_games.size())
    return {};
  const Game& game = m_games[index.row()];
  switch (role)
  {
  case AppIdRole:
    return game.app_id;
  case Qt::DisplayRole:
  case TitleRole:
    return game.title;
  case CoverRole:
    return game.cover;
  case SizeRole:
    return game.size;
  default:
    return {};
  }
}

QHash<int, QByteArray> Library::roleNames() const
{
  return {{AppIdRole, "appId"}, {TitleRole, "title"}, {CoverRole, "cover"}, {SizeRole, "size"}};
}

void Library::refresh()
{
  QVector<Game> games;
  const QDir apps(m_steam + QStringLiteral("/steamapps"));
  static const QRegularExpression manifest(QStringLiteral("^appmanifest_(\\d+)\\.acf$"));
  for (const QString& name : apps.entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files))
  {
    const QRegularExpressionMatch m = manifest.match(name);
    if (!m.hasMatch())
      continue;
    const QHash<QString, QString> fields = ManifestFields(apps.filePath(name));
    Game game;
    game.app_id = m.captured(1);
    game.title = fields.value(QStringLiteral("name")).simplified();
    if (kToolIds.contains(game.app_id) || kToolNames.match(game.title).hasMatch())
      continue;
    if (!(fields.value(QStringLiteral("StateFlags")).toLongLong() & 4))  // 4: fully installed
      continue;
    game.size = fields.value(QStringLiteral("SizeOnDisk")).toLongLong();
    game.cover = CoverFor(m_steam, game.app_id);
    games.append(game);
  }
  std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) {
    return QString::localeAwareCompare(a.title, b.title) < 0;
  });

  const bool count_changed = games.size() != m_games.size();
  beginResetModel();
  m_games = games;
  endResetModel();
  if (count_changed)
    emit countChanged();
}

bool Library::play(const QString& app_id)
{
  static const QRegularExpression digits(QStringLiteral("^\\d+$"));
  if (!digits.match(app_id).hasMatch())
    return false;
  if (!QFileInfo(m_service).isExecutable())
  {
    setLastError(tr("UTDeck's Steam service is not installed (%1).").arg(m_service));
    return false;
  }
  // The same command as the app drawer entries (steam-library.py). A unit of that name that is
  // still running means the game is already starting: systemd-run then fails, which is right.
  const QStringList arguments = {QStringLiteral("--user"),
                                 QStringLiteral("--collect"),
                                 QStringLiteral("--quiet"),
                                 QStringLiteral("--unit=utdeck-play-") + app_id,
                                 m_service,
                                 QStringLiteral("play"),
                                 app_id};
  if (!QProcess::startDetached(QStringLiteral("systemd-run"), arguments))
  {
    setLastError(tr("Could not start the game (systemd-run failed to start)."));
    return false;
  }
  setLastError({});
  return true;
}

void Library::setLastError(const QString& error)
{
  if (error == m_last_error)
    return;
  m_last_error = error;
  emit lastErrorChanged();
}

void Library::readGameState()
{
  QString game;
  bool in_front = false;
  QFile file(m_state_dir + QStringLiteral("/game"));
  if (file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    const QStringList parts =
        QString::fromUtf8(file.readLine()).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Still running: the pid is game-run.sh's (a file left by a killed run names a dead or reused pid).
    QFile cmdline(parts.size() >= 2 ? QStringLiteral("/proc/%1/cmdline").arg(parts[1]) : QString());
    if (parts.size() >= 2 && cmdline.open(QIODevice::ReadOnly) && cmdline.readAll().contains("game-run.sh"))
    {
      game = parts[0];
      if (game.startsWith(QStringLiteral("steam-")))
        game = game.mid(6);
      QFile focus(parts.size() >= 3 ? parts[2] : QString());
      in_front = focus.open(QIODevice::ReadOnly) && focus.read(1) == "1";
    }
  }

  if (game.isEmpty())
    m_focus_poll.stop();
  else if (!m_focus_poll.isActive())
    m_focus_poll.start();
  if (game != m_running_game)
  {
    m_running_game = game;
    emit runningGameChanged();
  }
  if (in_front != m_game_in_front)
  {
    m_game_in_front = in_front;
    emit gameInFrontChanged();
  }
}
