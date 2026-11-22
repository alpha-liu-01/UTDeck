#include "SteamLaunch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
  for (const QByteArray& line : data.left(end).split('\n'))
    if (line.contains("GameAction [AppID"))
      handleLine(QString::fromUtf8(line));
}

void SteamLaunch::handleLine(const QString& line)
{
  static const QRegularExpression game_action(QStringLiteral(
      "GameAction \\[AppID (\\d+), ActionID (\\d+)\\] : LaunchApp (changed task to|waiting for user response to|continues with user response) \"?(\\w+)"));
  const QRegularExpressionMatch m = game_action.match(line);
  if (!m.hasMatch())
    return;
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

void SteamLaunch::setWaitingForUser(bool waiting)
{
  if (waiting == m_waiting_for_user)
    return;
  m_waiting_for_user = waiting;
  if (waiting)
  {
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
      }
    }
    QMetaObject::invokeMethod(this, [this, dialog, x, y] {
      m_capturing = false;
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
