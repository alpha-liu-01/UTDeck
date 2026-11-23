#include "OwnGames.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const QString kOwnPrefix = QStringLiteral("own:");
const QRegularExpression kOwnId(QStringLiteral("^[a-z0-9][a-z0-9._-]{0,63}$"));
// Executables that are rarely the game: uninstallers, setups, crash reporters, redistributables.
const QRegularExpression kUnlikely(
    QStringLiteral("unins|uninst|setup|install|crash|report|redist|vc_?redist|dxsetup|dotnet|directx|"
                   "helper|updat|cleanup|unitycrashhandler|notification|launcherpatcher"),
    QRegularExpression::CaseInsensitiveOption);

QString Home(const QString& path)
{
  return QDir::homePath() + QLatin1Char('/') + path;
}

QString OwnFolder()
{
  return Home(QStringLiteral("utdeck-probe/wine/own"));
}

QString IdOf(const QString& app_id)
{
  const QString id = app_id.startsWith(kOwnPrefix) ? app_id.mid(kOwnPrefix.size()) : QString();
  return kOwnId.match(id).hasMatch() ? id : QString();
}

QHash<QString, QString> ReadConf(const QString& path)
{
  QHash<QString, QString> fields;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return fields;
  while (!file.atEnd())
  {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq > 0 && !line.startsWith(QLatin1Char('#')) && !fields.contains(line.left(eq)))
      fields.insert(line.left(eq), line.mid(eq + 1));
  }
  return fields;
}

// A line value may not hold a line break (game.conf is one NAME=value per line).
QString OneLine(const QString& value)
{
  QString v = value;
  v.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
  return v.trimmed();
}

// The likely game first: not "unlikely" by name, then the largest.
void SortLikely(QVariantList& list)
{
  std::stable_sort(list.begin(), list.end(), [](const QVariant& a, const QVariant& b) {
    const QVariantMap x = a.toMap(), y = b.toMap();
    if (x.value(QStringLiteral("unlikely")).toBool() != y.value(QStringLiteral("unlikely")).toBool())
      return !x.value(QStringLiteral("unlikely")).toBool();
    return x.value(QStringLiteral("kb")).toLongLong() > y.value(QStringLiteral("kb")).toLongLong();
  });
}
}  // namespace

OwnGames::OwnGames(QObject* parent) : QObject(parent)
{
  connect(&m_check, &QProcess::stateChanged, this, &OwnGames::checkedChanged);
  connect(&m_check, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    m_executables.clear();
    m_error.clear();
    if (status != QProcess::NormalExit || code != 0)
    {
      m_error = tr("The check of the game's files failed (gamecheck.py: %1).")
                    .arg(QString::fromUtf8(m_check.readAllStandardError()).trimmed().right(200));
      emit checkedChanged();
      return;
    }
    // gamecheck.py --programs: "  x86-64   64-bit    56088 KB  game.exe" under "Windows files:".
    static const QRegularExpression file_line(QStringLiteral("^  (\\S+)\\s+(\\d+)-bit\\s+(\\d+) KB  (.+)$"));
    QString section;
    for (const QString& line : QString::fromUtf8(m_check.readAllStandardOutput()).split(QLatin1Char('\n')))
    {
      if (line.startsWith(QLatin1String("Windows files:")))
        section = QStringLiteral("files");
      else if (!line.startsWith(QLatin1String("  ")))
        section.clear();
      else if (section == QLatin1String("files"))
      {
        const QRegularExpressionMatch m = file_line.match(line);
        if (!m.hasMatch() || !m.captured(4).endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
          continue;
        QVariantMap exe;
        exe[QStringLiteral("path")] = m.captured(4);
        exe[QStringLiteral("machine")] = m.captured(1);
        exe[QStringLiteral("bits")] = m.captured(2).toInt();
        exe[QStringLiteral("kb")] = m.captured(3).toLongLong();
        exe[QStringLiteral("unlikely")] = kUnlikely.match(QFileInfo(m.captured(4)).fileName()).hasMatch();
        m_executables << exe;
      }
    }
    SortLikely(m_executables);
    if (m_executables.isEmpty())
      m_error = tr("No Windows program (.exe) in this folder.");
    emit checkedChanged();
  });
}

QString OwnGames::gamesFolder() const
{
  return Home(QStringLiteral("Documents/UTDeck/Games"));
}

QVariantList OwnGames::folders() const
{
  // Folders already added (their game.conf's DIR, or the folder above an exe in it).
  QSet<QString> added;
  const QDir own(OwnFolder());
  for (const QString& id : own.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    const QHash<QString, QString> conf = ReadConf(own.filePath(id) + QStringLiteral("/game.conf"));
    if (!conf.value(QStringLiteral("DIR")).isEmpty())
      added << QDir::cleanPath(conf.value(QStringLiteral("DIR")));
  }
  QVariantList list;
  const QDir games(gamesFolder());
  for (const QFileInfo& dir : games.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
  {
    QVariantMap entry;
    entry[QStringLiteral("name")] = dir.fileName();
    entry[QStringLiteral("path")] = dir.filePath();
    bool is_added = false;
    for (const QString& path : added)
      if (path == dir.filePath() || path.startsWith(dir.filePath() + QLatin1Char('/')))
        is_added = true;
    entry[QStringLiteral("added")] = is_added;
    list << entry;
  }
  return list;
}

QVariantList OwnGames::installers() const
{
  QVariantList list;
  for (const QString& where : {QStringLiteral("Downloads"), QStringLiteral("Documents"), QStringLiteral("Documents/UTDeck")})
  {
    const QDir dir(Home(where));
    for (const QFileInfo& file : dir.entryInfoList({QStringLiteral("*.exe"), QStringLiteral("*.EXE")}, QDir::Files, QDir::Name))
    {
      QVariantMap entry;
      entry[QStringLiteral("name")] = file.fileName();
      entry[QStringLiteral("path")] = file.filePath();
      entry[QStringLiteral("mb")] = int(file.size() / (1024 * 1024));
      list << entry;
    }
  }
  return list;
}

void OwnGames::check(const QString& folder)
{
  if (m_check.state() != QProcess::NotRunning)
    return;
  m_executables.clear();
  m_error.clear();
  // -I: Python's isolated mode, so nothing from the game's folder is imported.
  m_check.start(QStringLiteral("python3"), {QStringLiteral("-I"), Home(QStringLiteral("utdeck-probe/wine/gamecheck.py")),
                                            QStringLiteral("--programs"), folder});
  emit checkedChanged();
}

QStringList OwnGames::images(const QString& folder) const
{
  QStringList found;
  const QStringList kinds = {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
                             QStringLiteral("*.JPG"), QStringLiteral("*.PNG")};
  if (!folder.isEmpty())
  {
    QDirIterator it(folder, kinds, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext() && found.size() < 40)
    {
      const QString path = it.next();
      if (path.mid(folder.size()).count(QLatin1Char('/')) <= 2)  // the folder and two levels down
        found << path;
    }
  }
  QDir pictures(Home(QStringLiteral("Pictures")));
  for (const QFileInfo& file : pictures.entryInfoList(kinds, QDir::Files, QDir::Time))
    if (found.size() < 60)
      found << file.filePath();
  return found;
}

QString OwnGames::newId(const QString& name, const QString& folder) const
{
  // Lower case letters, digits and "-" from the name; "game" if nothing is left.
  QString slug;
  for (const QChar c : name.toLower())
  {
    if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
      slug += c;
    else if (!slug.isEmpty() && !slug.endsWith(QLatin1Char('-')))
      slug += QLatin1Char('-');
  }
  while (slug.endsWith(QLatin1Char('-')))
    slug.chop(1);
  slug = slug.left(40);
  if (slug.isEmpty())
    slug = QStringLiteral("game");
  // A game removed earlier from the same folder keeps its prefix: it is used again.
  for (int n = 1; n < 100; ++n)
  {
    const QString id = n == 1 ? slug : QStringLiteral("%1-%2").arg(slug).arg(n);
    const QString dir = OwnFolder() + QLatin1Char('/') + id;
    if (!QFileInfo::exists(dir))
      return id;
    const QHash<QString, QString> removed = ReadConf(dir + QStringLiteral("/game.conf.removed"));
    if (!folder.isEmpty() && !QFileInfo::exists(dir + QStringLiteral("/game.conf")) &&
        QDir::cleanPath(removed.value(QStringLiteral("DIR"))) == QDir::cleanPath(folder))
      return id;
  }
  return {};
}

bool OwnGames::writeConf(const QString& id, const QVariantMap& fields)
{
  const QString dir = OwnFolder() + QLatin1Char('/') + id;
  if (!QDir().mkpath(dir))
    return false;
  QFile file(dir + QStringLiteral("/game.conf.new"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  for (const QString& key : {QStringLiteral("NAME"), QStringLiteral("DIR"), QStringLiteral("EXE"),
                             QStringLiteral("COVER"), QStringLiteral("INSTALLER")})
    if (!fields.value(key).toString().isEmpty())
      file.write((key + QLatin1Char('=') + OneLine(fields.value(key).toString()) + QLatin1Char('\n')).toUtf8());
  file.close();
  QFile::remove(dir + QStringLiteral("/game.conf"));
  return QFile::rename(dir + QStringLiteral("/game.conf.new"), dir + QStringLiteral("/game.conf"));
}

QString OwnGames::add(const QString& name, const QString& folder, const QString& exe, const QString& cover)
{
  m_error.clear();
  if (name.trimmed().isEmpty() || !QFileInfo(folder + QLatin1Char('/') + exe).isFile())
  {
    m_error = tr("The game's program was not found.");
    emit checkedChanged();
    return {};
  }
  const QString id = newId(name, folder);
  if (id.isEmpty() || !writeConf(id, {{QStringLiteral("NAME"), name.trimmed()},
                                      {QStringLiteral("DIR"), QDir::cleanPath(folder)},
                                      {QStringLiteral("EXE"), exe},
                                      {QStringLiteral("COVER"), cover}}))
  {
    m_error = tr("The game could not be saved.");
    emit checkedChanged();
    return {};
  }
  QFile::remove(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf.removed"));
  return kOwnPrefix + id;
}

QVariantMap OwnGames::game(const QString& app_id) const
{
  const QString id = IdOf(app_id);
  QVariantMap map;
  if (id.isEmpty())
    return map;
  const QHash<QString, QString> conf = ReadConf(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf"));
  map[QStringLiteral("name")] = conf.value(QStringLiteral("NAME"));
  map[QStringLiteral("folder")] = conf.value(QStringLiteral("DIR"));
  map[QStringLiteral("exe")] = conf.value(QStringLiteral("EXE"));
  map[QStringLiteral("cover")] = conf.value(QStringLiteral("COVER"));
  map[QStringLiteral("installed")] = conf.value(QStringLiteral("DIR")).startsWith(OwnFolder());
  return map;
}

bool OwnGames::edit(const QString& app_id, const QString& name, const QString& exe, const QString& cover)
{
  const QString id = IdOf(app_id);
  if (id.isEmpty())
    return false;
  const QHash<QString, QString> conf = ReadConf(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf"));
  const QString folder = conf.value(QStringLiteral("DIR"));
  if (name.trimmed().isEmpty() || !QFileInfo(folder + QLatin1Char('/') + exe).isFile())
    return false;
  return writeConf(id, {{QStringLiteral("NAME"), name.trimmed()},
                        {QStringLiteral("DIR"), folder},
                        {QStringLiteral("EXE"), exe},
                        {QStringLiteral("COVER"), cover},
                        {QStringLiteral("INSTALLER"), conf.value(QStringLiteral("INSTALLER"))}});
}

bool OwnGames::remove(const QString& app_id, bool delete_prefix)
{
  const QString id = IdOf(app_id);
  if (id.isEmpty())
    return false;
  const QString dir = OwnFolder() + QLatin1Char('/') + id;
  if (delete_prefix)
  {
    // Only this game's folder under wine/own (the game's own files elsewhere stay).
    const QString real = QFileInfo(dir).canonicalFilePath();
    if (real.isEmpty() || !real.startsWith(QFileInfo(OwnFolder()).canonicalFilePath() + QLatin1Char('/')))
      return false;
    return QDir(real).removeRecursively();
  }
  QFile::remove(dir + QStringLiteral("/game.conf.removed"));
  return QFile::rename(dir + QStringLiteral("/game.conf"), dir + QStringLiteral("/game.conf.removed"));
}

QString OwnGames::startInstall(const QString& name, const QString& installer)
{
  m_error.clear();
  const QString script = Home(QStringLiteral("utdeck-probe/wine/own-run.sh"));
  if (!QFileInfo(installer).isFile() || !QFileInfo(script).isExecutable())
  {
    m_error = tr("The installer or UTDeck's start script was not found.");
    emit checkedChanged();
    return {};
  }
  // An unfinished game with this very installer is used again (its prefix and what the installer put
  // there), so a second tap or a second try never makes a second game (stage 4.5: two 3DMark 11
  // installs side by side, 1.4 GB for nothing on a phone with little space).
  QString id;
  for (const QVariant& entry : pending())
  {
    const QString other = IdOf(entry.toMap().value(QStringLiteral("appId")).toString());
    if (ReadConf(OwnFolder() + QLatin1Char('/') + other + QStringLiteral("/game.conf")).value(QStringLiteral("INSTALLER")) == installer)
      id = other;
  }
  if (id.isEmpty())
    id = newId(name, QString());
  if (id.isEmpty() || !writeConf(id, {{QStringLiteral("NAME"), name.trimmed()},
                                      {QStringLiteral("INSTALLER"), installer}}))
  {
    m_error = tr("The game could not be saved.");
    emit checkedChanged();
    return {};
  }
  // As a game starts (Library::play): a unit outside the app; the installer's window comes over it.
  if (!QProcess::startDetached(QStringLiteral("systemd-run"),
                               {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
                                QStringLiteral("--unit=utdeck-play-own-") + id, script, id,
                                QStringLiteral("--install"), installer}))
  {
    m_error = tr("Could not start the installer (systemd-run failed to start).");
    emit checkedChanged();
    return {};
  }
  return kOwnPrefix + id;
}

QVariantList OwnGames::installedPrograms(const QString& app_id) const
{
  const QString id = IdOf(app_id);
  QVariantList list;
  if (id.isEmpty())
    return list;
  const QString drive = OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/pfx/drive_c");
  // Not Windows' and Wine's own, nor the user folders (an installer's own copy may sit in Temp).
  static const QRegularExpression skip(
      QStringLiteral("^/(windows|users|ProgramData)/|^/Program Files[^/]*/(Common Files|Internet Explorer|"
                     "Windows [^/]+|Wine Mono|Microsoft[^/]*)/"),
      QRegularExpression::CaseInsensitiveOption);
  QDirIterator it(drive, {QStringLiteral("*.exe"), QStringLiteral("*.EXE")}, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext())
  {
    const QString path = it.next();
    const QString rel = path.mid(drive.size());
    if (skip.match(rel).hasMatch())
      continue;
    QVariantMap entry;
    entry[QStringLiteral("path")] = path;
    entry[QStringLiteral("name")] = QStringLiteral("C:") + QString(rel).replace(QLatin1Char('/'), QLatin1Char('\\'));
    entry[QStringLiteral("kb")] = QFileInfo(path).size() / 1024;
    entry[QStringLiteral("unlikely")] = kUnlikely.match(QFileInfo(path).fileName()).hasMatch();
    list << entry;
  }
  SortLikely(list);
  return list;
}

bool OwnGames::finishInstall(const QString& app_id, const QString& name, const QString& program, const QString& cover)
{
  const QString id = IdOf(app_id);
  const QString drive = OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/pfx/drive_c/");
  if (id.isEmpty() || !program.startsWith(drive) || !QFileInfo(program).isFile() || name.trimmed().isEmpty())
    return false;
  const QHash<QString, QString> conf = ReadConf(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf"));
  return writeConf(id, {{QStringLiteral("NAME"), name.trimmed()},
                        {QStringLiteral("DIR"), QFileInfo(program).path()},
                        {QStringLiteral("EXE"), QFileInfo(program).fileName()},
                        {QStringLiteral("COVER"), cover},
                        {QStringLiteral("INSTALLER"), conf.value(QStringLiteral("INSTALLER"))}});
}

bool OwnGames::pendingInstall(const QString& app_id) const
{
  const QString id = IdOf(app_id);
  if (id.isEmpty())
    return false;
  const QHash<QString, QString> conf = ReadConf(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf"));
  return conf.value(QStringLiteral("EXE")).isEmpty() && !conf.value(QStringLiteral("INSTALLER")).isEmpty();
}

QVariantList OwnGames::pending() const
{
  QVariantList list;
  const QDir own(OwnFolder());
  for (const QString& id : own.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
  {
    if (!kOwnId.match(id).hasMatch() || !pendingInstall(kOwnPrefix + id))
      continue;
    const QHash<QString, QString> conf = ReadConf(own.filePath(id) + QStringLiteral("/game.conf"));
    QVariantMap entry;
    entry[QStringLiteral("appId")] = kOwnPrefix + id;
    entry[QStringLiteral("name")] = conf.value(QStringLiteral("NAME"), id);
    list << entry;
  }
  return list;
}

bool OwnGames::rerunInstall(const QString& app_id)
{
  const QString id = IdOf(app_id);
  const QString script = Home(QStringLiteral("utdeck-probe/wine/own-run.sh"));
  const QHash<QString, QString> conf = ReadConf(OwnFolder() + QLatin1Char('/') + id + QStringLiteral("/game.conf"));
  const QString installer = conf.value(QStringLiteral("INSTALLER"));
  if (id.isEmpty() || !QFileInfo(installer).isFile() || !QFileInfo(script).isExecutable())
    return false;
  return QProcess::startDetached(QStringLiteral("systemd-run"),
                                 {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--quiet"),
                                  QStringLiteral("--unit=utdeck-play-own-") + id, script, id,
                                  QStringLiteral("--install"), installer});
}
