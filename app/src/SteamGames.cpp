#include "SteamGames.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUrl>

#include <algorithm>

#include "SteamLaunch.h"

namespace
{
const QRegularExpression kDigits(QStringLiteral("^\\d+$"));

QString Steam()
{
  return QDir::homePath() + QStringLiteral("/.local/share/Steam");
}

QString Apps()
{
  return Steam() + QStringLiteral("/steamapps");
}

// One field of a game's .acf (the first of its name): read here, never printed (the file also holds
// the account's number, LastOwner).
QString AcfField(const QString& app_id, const QString& name)
{
  QFile file(Apps() + QStringLiteral("/appmanifest_") + app_id + QStringLiteral(".acf"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};
  const QRegularExpression line(QStringLiteral("^\\s*\"") + QRegularExpression::escape(name) + QStringLiteral("\"\\s+\"([^\"]*)\""));
  while (!file.atEnd())
  {
    const QRegularExpressionMatch m = line.match(QString::fromUtf8(file.readLine()));
    if (m.hasMatch())
      return m.captured(1);
  }
  return {};
}

bool Installed(const QString& app_id)
{
  return AcfField(app_id, QStringLiteral("StateFlags")) == QLatin1String("4");
}

// Steam's portrait cover of a game, as the library's (Library.cpp): in librarycache/<appid>/ or one
// folder down.
QString Cover(const QString& app_id)
{
  const QString dir = Steam() + QStringLiteral("/appcache/librarycache/") + app_id;
  for (const QString& name : {QStringLiteral("library_600x900.jpg"), QStringLiteral("library_capsule.jpg"),
                              QStringLiteral("header.jpg")})
  {
    QDirIterator it(dir, {name}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
      return QUrl::fromLocalFile(it.next()).toString();
  }
  return {};
}

qint64 DirBytes(const QString& dir)
{
  qint64 total = 0;
  QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext())
  {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}
}  // namespace

SteamGames::SteamGames(SteamLaunch* launch, QObject* parent) : QObject(parent), m_launch(launch)
{
  connect(&m_list, &QProcess::finished, this, [this] {
    // appinfo.py --library: appid, type, systems, Windows size in MB, name (tab-separated).
    QVariantList games;
    for (const QString& line : QString::fromUtf8(m_list.readAllStandardOutput()).split(QLatin1Char('\n')))
    {
      const QStringList f = line.split(QLatin1Char('\t'));
      if (f.size() < 5 || !kDigits.match(f[0]).hasMatch() || (f[1] != QLatin1String("game") && f[1] != QLatin1String("demo")))
        continue;
      QVariantMap game;
      game[QStringLiteral("appId")] = f[0];
      game[QStringLiteral("name")] = f[4].trimmed();
      game[QStringLiteral("systems")] = f[2];
      game[QStringLiteral("sizeMb")] = f[3].toInt();
      game[QStringLiteral("cover")] = Cover(f[0]);
      game[QStringLiteral("installed")] = Installed(f[0]);
      game[QStringLiteral("windows")] = f[2].isEmpty() || f[2].contains(QLatin1String("windows"));
      game[QStringLiteral("cloud")] = f.size() > 5 ? f[5].trimmed() : QString();
      games << game;
    }
    std::sort(games.begin(), games.end(), [](const QVariant& a, const QVariant& b) {
      return QString::localeAwareCompare(a.toMap().value(QStringLiteral("name")).toString(),
                                         b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_games = games;
    emit gamesChanged();
  });
  connect(&m_script, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    if (status != QProcess::NormalExit || code != 0)
    {
      const QStringList out = QString::fromUtf8(m_script.readAll()).trimmed().split(QLatin1Char('\n'));
      m_error = tr("Steam could not be asked (%1).").arg(out.isEmpty() ? QString() : out.last());
      m_task.clear();
      emit taskChanged();
      return;
    }
    // Steam's Install or Uninstall window comes on the hidden screen now: shown in the app. (A file
    // check asks nothing; the script closed the main window Steam brought up.)
    if (m_task.value(QStringLiteral("verb")).toString() != QLatin1String("validate"))
      m_launch->showSteamDialog();
  });
  m_script.setProcessChannelMode(QProcess::MergedChannels);
  m_poll.setInterval(2000);
  connect(&m_poll, &QTimer::timeout, this, &SteamGames::poll);
  m_poll.start();
  poll();
}

void SteamGames::refresh()
{
  if (m_list.state() != QProcess::NotRunning)
    return;
  m_list.start(QStringLiteral("python3"),
               {QStringLiteral("-I"), QDir::homePath() + QStringLiteral("/utdeck-probe/appinfo.py"), QStringLiteral("--library")});
  emit gamesChanged();
}

int SteamGames::freeMb() const
{
  return int(QStorageInfo(Apps()).bytesAvailable() / (1024 * 1024));
}

int SteamGames::installedMb(const QString& app_id) const
{
  return int(AcfField(app_id, QStringLiteral("SizeOnDisk")).toLongLong() / (1024 * 1024));
}

bool SteamGames::install(const QString& app_id, const QString& name)
{
  return start(QStringLiteral("install"), app_id, name);
}

bool SteamGames::uninstall(const QString& app_id, const QString& name, bool delete_data)
{
  if (!start(QStringLiteral("uninstall"), app_id, name))
    return false;
  // The install folder's name is read now: the .acf, where it is, goes with the uninstall.
  if (delete_data)
    m_delete_after.insert(app_id, AcfField(app_id, QStringLiteral("installdir")));
  else
    m_delete_after.remove(app_id);
  return true;
}

bool SteamGames::verify(const QString& app_id, const QString& name)
{
  return start(QStringLiteral("validate"), app_id, name);
}

int SteamGames::validationResult(const QString& app_id)
{
  // Steam's own verdict, in its console log since the task started: "GameAction [AppID X, ActionID N] :
  // VerifyApp changed task to Completed with "76"" (all 76 files fine), "... Failed with "1"" (1 file
  // missing or damaged: downloaded again next), "... Completed with "0"" (nothing checked: Steam does
  // not check a game that is running or starting; stone, Barro 2020, 2026-11-23).
  const auto since = [](const QString& path, qint64 offset) {
    QStringList lines;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return lines;
    if (f.size() >= offset)   // else rotated meanwhile (1 MB): the new file from its start
      f.seek(offset);
    while (!f.atEnd())
      lines << QString::fromUtf8(f.readLine());
    return lines;
  };
  const QRegularExpression verdict(QStringLiteral("GameAction \\[AppID ") + app_id +
                                   QStringLiteral(", ActionID \\d+\\] : VerifyApp changed task to (Completed|Failed) with \"(\\d+)\""));
  for (const QString& line : since(Steam() + QStringLiteral("/logs/console_log.txt"), m_console_offset))
  {
    const QRegularExpressionMatch m = verdict.match(line);
    if (!m.hasMatch())
      continue;
    const int n = m.captured(2).toInt();
    if (m.captured(1) == QLatin1String("Completed"))
      return n > 0 ? 0 : -3;
    // Failed: what Steam fetches again is done when its download scheduler finishes with the game.
    for (const QString& c : since(Steam() + QStringLiteral("/logs/content_log.txt"), m_log_offset))
      if (c.contains(QStringLiteral("AppID ") + app_id + QStringLiteral(" scheduler finished")) &&
          !c.contains(QLatin1String("Suspended")))
        return n;
    return -2;
  }
  return -2;
}

bool SteamGames::start(const QString& verb, const QString& app_id, const QString& name)
{
  m_error.clear();
  if (!kDigits.match(app_id).hasMatch() || !m_task.isEmpty() || m_script.state() != QProcess::NotRunning)
    return false;
  m_task = {{QStringLiteral("appId"), app_id}, {QStringLiteral("verb"), verb}, {QStringLiteral("name"), name}};
  m_task_ticks = 0;
  m_log_offset = QFileInfo(Steam() + QStringLiteral("/logs/content_log.txt")).size();
  m_console_offset = QFileInfo(Steam() + QStringLiteral("/logs/console_log.txt")).size();
  m_script.start(QStringLiteral("sh"),
                 {QDir::homePath() + QStringLiteral("/utdeck-probe/steam/steam-game.sh"), verb, app_id});
  emit taskChanged();
  return true;
}

void SteamGames::poll()
{
  // The downloads: Steam's steamapps/downloading/<appid> folders, against the game's total to stage.
  QVariantList downloads;
  QSet<QString> now;
  const qint64 msecs = QDateTime::currentMSecsSinceEpoch();
  const QDir downloading(Apps() + QStringLiteral("/downloading"));
  for (const QString& app_id : downloading.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    if (!kDigits.match(app_id).hasMatch())
      continue;
    // A folder Steam left behind (an empty one after a file check that found nothing, stage 4.7) is
    // not a download: only while the game's state is more than "fully installed" (4).
    if (AcfField(app_id, QStringLiteral("StateFlags")) == QLatin1String("4"))
      continue;
    now << app_id;
    const qint64 total = qMax(AcfField(app_id, QStringLiteral("BytesToStage")).toLongLong(),
                              AcfField(app_id, QStringLiteral("BytesToDownload")).toLongLong());
    const qint64 done = DirBytes(downloading.filePath(app_id));
    // The speed: averaged over the polls (a new sample weighs 30 %), from the second poll on.
    double& speed = m_speed[app_id];
    if (m_last_sample.contains(app_id))
    {
      const qint64 bytes = m_last_sample.value(app_id).first;
      const qint64 at = m_last_sample.value(app_id).second;
      if (msecs > at && done >= bytes)
      {
        const double sample = double(done - bytes) * 1000.0 / double(msecs - at);
        speed = speed > 0 ? 0.7 * speed + 0.3 * sample : sample;
      }
    }
    else
      speed = -1;
    m_last_sample.insert(app_id, {done, msecs});
    QVariantMap d;
    d[QStringLiteral("appId")] = app_id;
    d[QStringLiteral("name")] = nameOf(app_id);
    m_names.insert(app_id, d.value(QStringLiteral("name")).toString());
    // Nothing in the folder yet: unknown, not 0 % (Barro 2020 kept it empty until its last second).
    d[QStringLiteral("percent")] = total > 0 && done > 0 ? int(qMin<qint64>(99, done * 100 / total)) : -1;
    d[QStringLiteral("doneMb")] = int(done / (1024 * 1024));
    d[QStringLiteral("totalMb")] = int(total / (1024 * 1024));
    d[QStringLiteral("speedKb")] = speed >= 0 ? int(speed / 1024) : -1;
    d[QStringLiteral("secondsLeft")] = total > done && speed > 1024 ? int(double(total - done) / speed) : -1;
    d[QStringLiteral("update")] = AcfField(app_id, QStringLiteral("SizeOnDisk")).toLongLong() > 0;
    downloads << d;
  }
  // Downloads that ended: installed (the shortcut Steam made goes), the library read again.
  bool ended = false;
  for (const QString& app_id : m_was_downloading)
  {
    if (now.contains(app_id))
      continue;
    m_last_sample.remove(app_id);
    m_speed.remove(app_id);
    // (Not a file check's repair download: the check reports itself.)
    const bool repair = m_task.value(QStringLiteral("verb")).toString() == QLatin1String("validate") &&
                        m_task.value(QStringLiteral("appId")).toString() == app_id;
    if (Installed(app_id) && !repair)
    {
      removeSteamShortcut(app_id);
      addFinished(app_id, nameOf(app_id), QStringLiteral("installed"));
      ended = true;
    }
  }
  m_was_downloading = now;
  if (downloads != m_downloads)
  {
    m_downloads = downloads;
    emit downloadsChanged();
  }

  // The task started from the app.
  if (!m_task.isEmpty() && m_script.state() == QProcess::NotRunning)
  {
    const QString app_id = m_task.value(QStringLiteral("appId")).toString();
    const QString verb = m_task.value(QStringLiteral("verb")).toString();
    const bool asking = m_launch->waitingForUser();
    ++m_task_ticks;
    bool done = false;
    if (verb == QLatin1String("validate"))
    {
      // Over when Steam's logs show the check's end (after the download of what it fetched again,
      // if any), or that it did not check (-3); or after 30 minutes.
      const int result = validationResult(app_id);
      if (result != -2 || m_task_ticks > 900)
      {
        addFinished(app_id, m_task.value(QStringLiteral("name")).toString(), QStringLiteral("checked"), result != -2 ? result : -1);
        done = true;
      }
    }
    else if (verb == QLatin1String("install"))
    {
      // Over once the download runs (it shows with the downloads) or the game is installed; or
      // when Steam's window was closed without installing (nothing after 20 s).
      if (now.contains(app_id) || Installed(app_id))
        done = true;
      else if (!asking && m_task_ticks > 10)
        done = true;
      // Installed with no download seen (a small game, between two polls).
      if (done && Installed(app_id) && !now.contains(app_id) && !m_was_downloading.contains(app_id))
      {
        removeSteamShortcut(app_id);
        addFinished(app_id, m_task.value(QStringLiteral("name")).toString(), QStringLiteral("installed"));
        ended = true;
      }
    }
    else
    {
      const bool acf = QFile::exists(Apps() + QStringLiteral("/appmanifest_") + app_id + QStringLiteral(".acf"));
      const bool uninstalling = AcfField(app_id, QStringLiteral("StateFlags")) != QLatin1String("4");
      if (!acf)
      {
        // Uninstalled. The Windows folder too, if the user asked (only compatdata/<digits> itself), and
        // what the game wrote into its install folder (common/<installdir>, which Steam leaves when the
        // game made files there: Aperture Desk Job's settings and log). Each only inside its folder.
        QString what = QStringLiteral("uninstalled");
        if (m_delete_after.contains(app_id))
        {
          const auto inside = [](const QString& parent, const QString& name) {
            const QString base = QFileInfo(parent).canonicalFilePath();
            const QString path = QFileInfo(parent + QLatin1Char('/') + name).canonicalFilePath();
            return !base.isEmpty() && !name.isEmpty() && !name.contains(QLatin1Char('/')) && name != QLatin1String("..") &&
                           path.startsWith(base + QLatin1Char('/'))
                       ? path
                       : QString();
          };
          const QString data = inside(Apps() + QStringLiteral("/compatdata"), app_id);
          if (!data.isEmpty())
            QDir(data).removeRecursively();
          // Not when another installed game uses the same folder.
          const QString dir = m_delete_after.value(app_id);
          bool shared = false;
          for (const QString& acf : QDir(Apps()).entryList({QStringLiteral("appmanifest_*.acf")}, QDir::Files))
          {
            const QString other = acf.mid(12, acf.size() - 16);
            if (other != app_id && AcfField(other, QStringLiteral("installdir")) == dir)
              shared = true;
          }
          const QString left = shared ? QString() : inside(Apps() + QStringLiteral("/common"), dir);
          if (!left.isEmpty())
            QDir(left).removeRecursively();
          m_delete_after.remove(app_id);
          what = QStringLiteral("uninstalledAll");
        }
        addFinished(app_id, m_task.value(QStringLiteral("name")).toString(), what);
        done = true;
        ended = true;
      }
      else if (!asking && !uninstalling && m_task_ticks > 10)
      {
        done = true;  // Steam's question was cancelled
        m_delete_after.remove(app_id);
      }
    }
    if (done)
    {
      m_task.clear();
      emit taskChanged();
    }
  }
  if (ended)
    emit installedChanged();
}

QVariantMap SteamGames::cloudState(const QString& app_id) const
{
  QVariantMap state;
  if (!kDigits.match(app_id).hasMatch())
    return state;
  // The cloud list: the account folder with one for this game (the newest, if several accounts).
  QString list;
  QDateTime newest;
  const QDir userdata(Steam() + QStringLiteral("/userdata"));
  for (const QString& account : userdata.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    const QFileInfo f(userdata.filePath(account) + QLatin1Char('/') + app_id + QStringLiteral("/remotecache.vdf"));
    if (f.isFile() && (list.isEmpty() || f.lastModified() > newest))
    {
      list = f.filePath();
      newest = f.lastModified();
    }
  }
  int files = 0, in_sync = 0;
  qint64 bytes = 0;
  QFile file(list);
  if (!list.isEmpty() && file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    // Each file's block: its "size" and "syncstate" (1: in sync), two tabs in.
    static const QRegularExpression field(QStringLiteral("^\\t\\t\"(size|syncstate)\"\\t\\t\"(\\d+)\""));
    while (!file.atEnd())
    {
      const QRegularExpressionMatch m = field.match(QString::fromUtf8(file.readLine()));
      if (!m.hasMatch())
        continue;
      if (m.captured(1) == QLatin1String("size"))
      {
        ++files;
        bytes += m.captured(2).toLongLong();
      }
      else if (m.captured(2) == QLatin1String("1"))
        ++in_sync;
    }
  }
  state[QStringLiteral("known")] = !list.isEmpty();
  state[QStringLiteral("files")] = files;
  state[QStringLiteral("sizeKb")] = int(bytes / 1024);
  state[QStringLiteral("inSync")] = in_sync;
  state[QStringLiteral("lastSync")] = list.isEmpty() ? QString() : newest.toString(QStringLiteral("yyyy-MM-dd HH:mm"));

  // Steam's cloud log (the current file after the previous one: it rotates at 1 MB): the last result,
  // and a problem in the last sync.
  const QString tag = QStringLiteral("[AppID ") + app_id + QStringLiteral("] ");
  static const QRegularExpression result(QStringLiteral("(Download|Upload) complete, result (\\S+)"));
  QString last_result, problem;
  for (const QString& name : {QStringLiteral("cloud_log.previous.txt"), QStringLiteral("cloud_log.txt")})
  {
    QFile log(Steam() + QStringLiteral("/logs/") + name);
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    while (!log.atEnd())
    {
      const QString line = QString::fromUtf8(log.readLine());
      if (!line.contains(tag))
        continue;
      const QRegularExpressionMatch m = result.match(line);
      if (m.hasMatch())
      {
        last_result = m.captured(2).remove(QLatin1Char(','));
        problem = last_result == QLatin1String("OK") ? QString() : QStringLiteral("failed");
      }
      // Each sync starts afresh. "Successfully synced" does not clear a problem: Steam logs it also
      // after a sync that skipped every file it could not place (Celceta before step 8).
      else if (line.contains(QLatin1String("Starting sync")))
        problem.clear();
      else if (line.contains(QLatin1String("Sync conflict for file")))
        problem = QStringLiteral("conflict");
      else if ((line.contains(QLatin1String("Unable to resolve path")) || line.contains(QLatin1String("failed to resolve path"))) &&
               problem.isEmpty())
        problem = QStringLiteral("path");
    }
  }
  state[QStringLiteral("result")] = last_result;
  state[QStringLiteral("problem")] = problem;

  // The phone copies set aside (utdeck-profile.py: cloud-wins, merges; by hand in stage 4.7).
  QStringList backups;
  static const QRegularExpression dated(QStringLiteral("^utdeck-saves-(\\d{4})(\\d{2})(\\d{2})$"));
  for (const QString& d : QDir(Apps() + QStringLiteral("/compatdata/") + app_id).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    const QRegularExpressionMatch m = dated.match(d);
    if (m.hasMatch())
      backups << m.captured(1) + QLatin1Char('-') + m.captured(2) + QLatin1Char('-') + m.captured(3);
  }
  state[QStringLiteral("backups")] = backups;
  return state;
}

QString SteamGames::nameOf(const QString& app_id) const
{
  for (const QVariant& g : m_games)
    if (g.toMap().value(QStringLiteral("appId")).toString() == app_id)
      return g.toMap().value(QStringLiteral("name")).toString();
  const QString name = AcfField(app_id, QStringLiteral("name"));
  return !name.isEmpty() ? name : m_names.value(app_id, app_id);
}

void SteamGames::addFinished(const QString& app_id, const QString& name, const QString& what, int detail)
{
  m_finished.prepend(QVariantMap{{QStringLiteral("appId"), app_id},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("what"), what},
                                 {QStringLiteral("detail"), detail},
                                 {QStringLiteral("time"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))}});
  while (m_finished.size() > 20)
    m_finished.removeLast();
  emit finishedChanged();
}

void SteamGames::removeSteamShortcut(const QString& app_id)
{
  // Steam's "Create an application shortcut" (ticked by default in its Install window): a .desktop
  // file that runs steam steam://rungameid/<appid>, which would start Steam's desktop client next to
  // UTDeck's own entry (utdeck-game-<appid>.desktop, steam-library.py), which stays.
  const QDir apps(QDir::homePath() + QStringLiteral("/.local/share/applications"));
  for (const QString& name : apps.entryList({QStringLiteral("*.desktop")}, QDir::Files))
  {
    if (name.startsWith(QLatin1String("utdeck-game-")))
      continue;
    QFile file(apps.filePath(name));
    if (file.open(QIODevice::ReadOnly) &&
        file.readAll().contains(QByteArray("steam://rungameid/") + app_id.toUtf8() + '\n'))
    {
      file.close();
      QFile::remove(apps.filePath(name));
    }
  }
}
