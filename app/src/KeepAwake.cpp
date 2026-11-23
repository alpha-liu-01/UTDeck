#include "KeepAwake.h"

#include <QDir>

KeepAwake::KeepAwake(QObject* parent) : QObject(parent)
{
  m_helper.setProcessChannelMode(QProcess::ForwardedErrorChannel);
  m_helper.setStandardOutputFile(QProcess::nullDevice());
}

KeepAwake::~KeepAwake()
{
  m_keys.clear();
  update();
  m_helper.waitForFinished(2000);
}

void KeepAwake::hold(const QString& key)
{
  if (key.isEmpty() || m_keys.contains(key))
    return;
  m_keys.insert(key);
  update();
  emit screenOnChanged();
}

void KeepAwake::release(const QString& key)
{
  if (!m_keys.remove(key))
    return;
  update();
  emit screenOnChanged();
}

void KeepAwake::update()
{
  const bool running = m_helper.state() != QProcess::NotRunning;
  if (!m_keys.isEmpty() && !running)
  {
    // The helper's input stays open: closing it is the release.
    m_helper.start(QStringLiteral("python3"),
                   {QStringLiteral("-I"), QDir::homePath() + QStringLiteral("/utdeck-probe/wine/utdeck-awake.py"),
                    QStringLiteral("hold")});
  }
  else if (m_keys.isEmpty() && running)
  {
    m_helper.closeWriteChannel();
    if (!m_helper.waitForFinished(1000))
      m_helper.kill();
  }
}
