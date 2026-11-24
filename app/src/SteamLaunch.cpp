#include "SteamLaunch.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QMetaObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThreadPool>

#include "XScreen.h"

namespace
{
const char* const kSteamDisplay = ":50";
// How much of the log's end is read when the app opens: a launch already waiting is found.
const qint64 kTail = 64 * 1024;

// A box Steam draws inside its main window over a black backdrop (a publisher's agreement): its
// rectangle in IMAGE (the main window's picture), or an empty one. Steam darkens everything around
// the box to black (at most 7 per channel); the box and all inside it are lighter (22 or more). From
// the picture's centre, each edge is the last light pixel before a run of 8 dark ones. It counts
// only if the box is at least 300x200, leaves room at the sides, and the columns just left and right
// of it are dark over its whole height (the Store page, with no box, fails this).
QRect FindModalBox(const QImage& image)
{
  const int w = image.width(), h = image.height();
  if (w < 400 || h < 300)
    return {};
  const auto dark = [&image](int x, int y) {
    const QRgb p = image.pixel(x, y);
    return qMax(qRed(p), qMax(qGreen(p), qBlue(p))) <= 12;
  };
  const int cx = w / 2, cy = h / 2;
  if (dark(cx, cy))
    return {};
  const auto edge = [&](int dx, int dy) {
    int x = cx, y = cy, last_x = cx, last_y = cy, run = 0;
    for (;;)
    {
      x += dx;
      y += dy;
      if (x < 0 || y < 0 || x >= w || y >= h)
        break;
      if (!dark(x, y))
      {
        run = 0;
        last_x = x;
        last_y = y;
      }
      else if (++run >= 8)
        break;
    }
    return dx != 0 ? last_x : last_y;
  };
  const QRect box(QPoint(edge(-1, 0), edge(0, -1)), QPoint(edge(1, 0), edge(0, 1)));
  if (box.width() < 300 || box.height() < 200 || box.width() > w * 85 / 100)
    return {};
  const auto dark_column = [&](int x) {
    if (x < 0 || x >= w)
      return false;
    int count = 0, dark_count = 0;
    for (int y = box.top(); y <= box.bottom(); y += 4, ++count)
      dark_count += dark(x, y) ? 1 : 0;
    return count > 0 && dark_count * 10 >= count * 9;
  };
  if (!dark_column(box.left() - 6) || !dark_column(box.right() + 6))
    return {};
  return box;
}
}  // namespace

SteamLaunch::SteamLaunch(QObject* parent) : QObject(parent)
{
  m_log = QDir::homePath() + QStringLiteral("/.local/share/Steam/logs/console_log.txt");
  m_poll.setInterval(1000);
  connect(&m_poll, &QTimer::timeout, this, &SteamLaunch::readLog);
  m_wait_timer.setSingleShot(true);
  m_wait_timer.setInterval(2000);
  connect(&m_wait_timer, &QTimer::timeout, this, [this] { setWaitingForUser(m_waiting); });
  m_capture_timer.setInterval(500);
  connect(&m_capture_timer, &QTimer::timeout, this, &SteamLaunch::captureDialog);
  connect(&m_capture_timer, &QTimer::timeout, this, &SteamLaunch::checkAgreement);
  readLog();
  m_poll.start();
}

SteamLaunch::~SteamLaunch()
{
  m_capture_timer.stop();
  QThreadPool::globalInstance()->waitForDone(5000);
}

QImage SteamLaunch::dialogImage() const
{
  QMutexLocker lock(&m_mutex);
  return m_dialog;
}

void SteamLaunch::readLog()
{
  QFile file(m_log);
  if (!file.open(QIODevice::ReadOnly))
    return;
  const qint64 size = file.size();
  if (m_offset < 0 || size < m_offset)  // first read, or Steam started a new log
    m_offset = qMax<qint64>(0, size - kTail);
  if (size == m_offset)
    return;
  file.seek(m_offset);
  const QByteArray data = file.read(size - m_offset);
  // Up to the last whole line; the rest is read next time.
  const int end = data.lastIndexOf('\n');
  if (end < 0)
    return;
  m_offset += end + 1;
  static const QRegularExpression process_line(QStringLiteral("Game process (added|removed) ?: AppID (\\d+)"));
  for (const QByteArray& line : data.left(end).split('\n'))
  {
    if (line.contains("GameAction [AppID"))
    {
      handleLine(QString::fromUtf8(line));
    }
    else if (line.contains("Game process ") && !m_expected.isEmpty())
    {
      const QRegularExpressionMatch m = process_line.match(QString::fromUtf8(line));
      if (m.hasMatch() && m.captured(2) == m_expected)
      {
        const bool added = m.captured(1) == QLatin1String("added");
        (added ? m_process_added : m_process_removed) = true;
        if (added)
          closeSteamWindowLater();
        emit changed();
      }
    }
  }
}

void SteamLaunch::handleLine(const QString& line)
{
  static const QRegularExpression game_action(QStringLiteral(
      "GameAction \\[AppID (\\d+), ActionID (\\d+)\\] : LaunchApp (changed task to|waiting for user response to|continues with user response) \"?(\\w+)"));
  const QRegularExpressionMatch m = game_action.match(line);
  if (!m.hasMatch())
    return;
  if (!m_expected.isEmpty() && m.captured(1) != m_expected)
    return;  // another app's launch (Steam's own tools run as app 0)
  m_app_id = m.captured(1);
  m_action_id = m.captured(2);
  const QString what = m.captured(3);
  if (what.startsWith(QLatin1String("waiting")))
  {
    m_task = m.captured(4);
    m_waiting = true;
    m_wait_timer.start();
  }
  else
  {
    if (what.startsWith(QLatin1String("changed")))
      m_task = m.captured(4);
    m_waiting = false;
    m_wait_timer.stop();
    setWaitingForUser(false);
  }
  emit changed();
}

void SteamLaunch::expect(const QString& app_id)
{
  readLog();  // what Steam logged so far is behind us
  m_expected = app_id;
  m_app_id = app_id;
  m_task.clear();
  m_process_added = false;
  m_process_removed = false;
  m_waiting = false;
  m_wait_timer.stop();
  setWaitingForUser(false);
  emit changed();
}

void SteamLaunch::dismiss()
{
  if (!m_waiting_for_user)
    return;
  m_waiting = false;
  m_wait_timer.stop();
  if (!m_manual)
    m_task = QStringLiteral("Cancelled");
  m_manual = false;
  setWaitingForUser(false);
}

void SteamLaunch::showSteamDialog()
{
  if (m_waiting_for_user)
    return;
  m_manual = true;
  m_manual_seen = false;
  m_manual_misses = 0;
  m_manual_since = QDateTime::currentMSecsSinceEpoch();
  setWaitingForUser(true);
}

void SteamLaunch::closeSteamWindowLater()
{
  // Steam brings its main window up when it creates the game's process (PvZ Replanted: 50 s after
  // -applaunch, when steam-service.sh had stopped looking): closed twice, as the window takes a
  // moment to map.
  for (int ms : {3000, 10000})
    QTimer::singleShot(ms, this, [] {
      QThreadPool::globalInstance()->start([] { CloseLargeXWindows(kSteamDisplay, 1000, 500); });
    });
}

QString SteamLaunch::agreements() const
{
  // The newest localconfig.vdf (the signed-in account's); only this app's agreement entries are
  // taken from it, nothing else of the file is kept.
  QString newest;
  QDateTime newest_time;
  QDirIterator dirs(QDir::homePath() + QStringLiteral("/.local/share/Steam/userdata"), QDir::Dirs | QDir::NoDotAndDotDot);
  while (dirs.hasNext())
  {
    const QFileInfo file(dirs.next() + QStringLiteral("/config/localconfig.vdf"));
    if (file.exists() && (newest.isEmpty() || file.lastModified() > newest_time))
    {
      newest = file.filePath();
      newest_time = file.lastModified();
    }
  }
  QFile file(newest);
  if (newest.isEmpty() || !file.open(QIODevice::ReadOnly))
    return {};
  const QString text = QString::fromUtf8(file.readAll());
  const QRegularExpression entry(QStringLiteral("\"%1_eula_\\d+\"\\s+\"[^\"]*\"").arg(m_app_id));
  QStringList found;
  for (auto it = entry.globalMatch(text); it.hasNext();)
    found << it.next().captured(0).simplified();
  return found.join(QLatin1Char('\n'));
}

void SteamLaunch::checkAgreement()
{
  if (!m_waiting_for_user || m_task != QLatin1String("ShowEula"))
    return;
  const QString now = agreements();
  if (now == m_agreements || now.isEmpty())
    return;
  // Accepted: Steam saved the answer but the waiting launch does not go on; a new start does.
  const QString app = m_app_id;
  m_waiting = false;
  m_wait_timer.stop();
  m_task = QStringLiteral("AgreementAccepted");
  setWaitingForUser(false);
  emit agreementAccepted(app);
}

void SteamLaunch::setWaitingForUser(bool waiting)
{
  if (waiting == m_waiting_for_user)
    return;
  m_waiting_for_user = waiting;
  if (waiting)
  {
    m_opened_main = false;
    if (m_task == QLatin1String("ShowEula"))
      m_agreements = agreements();
    m_capture_timer.start();
    captureDialog();
  }
  else
  {
    m_capture_timer.stop();
    {
      QMutexLocker lock(&m_mutex);
      m_dialog = QImage();
    }
    ++m_dialog_revision;
    emit dialogChanged();
    // The dialog brought Steam's main window back (it draws in software): closed again, as
    // steam-service.sh does after a launch. Windows at least as large as hidden Steam's screen.
    CloseLargeXWindows(kSteamDisplay, 1000, 500);
  }
  emit changed();
}

void SteamLaunch::captureDialog()
{
  if (m_capturing.exchange(true))
    return;
  QThreadPool::globalInstance()->start([this] {
    QImage dialog;
    int x = 0, y = 0;
    XScreenImage screen;
    const std::vector<XWindowRect> windows = ListXWindows(kSteamDisplay);
    // The dialog: the frontmost window that is neither one of Steam's toasts (283x70, which slide in
    // over it: Ys IX, the Nothing Phone) nor Steam's main window (as large as the screen or more).
    const XWindowRect* found = nullptr;
    for (auto it = windows.rbegin(); it != windows.rend() && !found; ++it)
      if (it->width >= 300 && it->height >= 200 && !(it->width >= 1000 && it->height >= 500))
        found = &*it;
    // No dialog window of its own: Steam's main window, which then holds the dialog (an agreement).
    // Not for a dialog shown on request (showSteamDialog): that one is always a window of its own.
    for (auto it = windows.rbegin(); it != windows.rend() && !found && !m_manual; ++it)
      if (it->width >= 1000 && it->height >= 500)
        found = &*it;
    const bool none = !found;
    const bool main_window = found && found->width >= 1000 && found->height >= 500;
    if (found && CaptureXScreen(kSteamDisplay, &screen))
    {
      const XWindowRect& w = *found;
      const QImage whole(screen.pixels.data(), screen.width, screen.height, screen.stride, QImage::Format_RGB32);
      const QRect rect = QRect(w.x, w.y, w.width, w.height).intersected(whole.rect());
      if (!rect.isEmpty())
      {
        dialog = whole.copy(rect);
        x = rect.x();
        y = rect.y();
        // A box inside the main window (an agreement): only the box, so it is shown larger and
        // its buttons are easier to hit. Taps and scrolling are placed by the same offsets.
        const QRect box = main_window ? FindModalBox(dialog) : QRect();
        if (!box.isEmpty())
        {
          dialog = dialog.copy(box);
          x += box.x();
          y += box.y();
        }
      }
    }
    QMetaObject::invokeMethod(this, [this, dialog, x, y, none] {
      m_capturing = false;
      if (m_manual && m_waiting_for_user)
      {
        // A dialog on request: over when its window has gone (3 captures, about 1.5 s), or when
        // none came within 90 s.
        if (!dialog.isNull())
        {
          m_manual_seen = true;
          m_manual_misses = 0;
        }
        else if ((m_manual_seen && ++m_manual_misses >= 3) ||
                 (!m_manual_seen && QDateTime::currentMSecsSinceEpoch() - m_manual_since > 90000))
        {
          m_manual = false;
          setWaitingForUser(false);
          return;
        }
      }
      // Nothing on the hidden screen at all: Steam draws this question inside its main window,
      // which is closed; it is opened once for this wait.
      else if (m_waiting_for_user && none && !m_opened_main)
        openSteamWindow();
      if (!m_waiting_for_user || dialog.isNull())
        return;
      {
        QMutexLocker lock(&m_mutex);
        m_dialog = dialog;
        m_dialog_x = x;
        m_dialog_y = y;
      }
      ++m_dialog_revision;
      emit dialogChanged();
    }, Qt::QueuedConnection);
  });
}

void SteamLaunch::openSteamWindow()
{
  m_opened_main = true;
  QProcess::startDetached(
      QStringLiteral("sh"),
      {QStringLiteral("-c"),
       QStringLiteral("cd \"$HOME/.local/share/Steam/steamrtarm64\" && DISPLAY=:50 LD_LIBRARY_PATH=\"$PWD\" "
                      "./steam -silent steam://open/main > /dev/null 2>&1")});
}

void SteamLaunch::scroll(qreal fx, qreal fy, int steps)
{
  QImage dialog;
  int x = 0, y = 0;
  {
    QMutexLocker lock(&m_mutex);
    dialog = m_dialog;
    x = m_dialog_x;
    y = m_dialog_y;
  }
  if (!m_waiting_for_user || dialog.isNull() || steps == 0)
    return;
  XScroll(kSteamDisplay, x + qRound(qBound(0.0, fx, 1.0) * (dialog.width() - 1)),
          y + qRound(qBound(0.0, fy, 1.0) * (dialog.height() - 1)), steps);
  captureDialog();
}

void SteamLaunch::cancel()
{
  QImage dialog;
  int x = 0, y = 0;
  {
    QMutexLocker lock(&m_mutex);
    dialog = m_dialog;
    x = m_dialog_x;
    y = m_dialog_y;
  }
  if (m_waiting_for_user && !dialog.isNull())
    XPressKeyAt(kSteamDisplay, x + dialog.width() / 2, y + dialog.height() / 2, "Escape");
  dismiss();
}

void SteamLaunch::tap(qreal fx, qreal fy)
{
  QImage dialog;
  int x = 0, y = 0;
  {
    QMutexLocker lock(&m_mutex);
    dialog = m_dialog;
    x = m_dialog_x;
    y = m_dialog_y;
  }
  if (!m_waiting_for_user || dialog.isNull() || fx < 0 || fx > 1 || fy < 0 || fy > 1)
    return;
  XClick(kSteamDisplay, x + qRound(fx * (dialog.width() - 1)), y + qRound(fy * (dialog.height() - 1)));
  captureDialog();
}

QImage SteamDialogImageProvider::requestImage(const QString&, QSize* size, const QSize& requested_size)
{
  QImage dialog = m_launch->dialogImage();
  if (dialog.isNull())
  {
    dialog = QImage(16, 9, QImage::Format_RGB32);
    dialog.fill(Qt::black);
  }
  if (requested_size.width() > 0 && requested_size.height() > 0)
    dialog = dialog.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (size)
    *size = dialog.size();
  return dialog;
}
