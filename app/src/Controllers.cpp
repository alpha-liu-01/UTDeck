#include "Controllers.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <dlfcn.h>

#include <cmath>
#include <cstdint>

namespace
{
// The phone's SDL 2, loaded at run time (the click does not carry it; Wine's winebus uses this one).
struct SdlGuid
{
  uint8_t data[16];
};

struct Sdl
{
  void* lib = nullptr;
  int (*Init)(uint32_t) = nullptr;
  void (*QuitSubSystem)(uint32_t) = nullptr;
  int (*NumJoysticks)() = nullptr;
  const char* (*JoystickNameForIndex)(int) = nullptr;
  const char* (*JoystickPathForIndex)(int) = nullptr;
  SdlGuid (*JoystickGetDeviceGUID)(int) = nullptr;
  uint16_t (*JoystickGetDeviceVendor)(int) = nullptr;
  uint16_t (*JoystickGetDeviceProduct)(int) = nullptr;
  int (*IsGameController)(int) = nullptr;
  void* (*GameControllerOpen)(int) = nullptr;
  void (*GameControllerClose)(void*) = nullptr;
  uint8_t (*GameControllerGetButton)(void*, int) = nullptr;
  int16_t (*GameControllerGetAxis)(void*, int) = nullptr;
  int (*GameControllerRumble)(void*, uint16_t, uint16_t, uint32_t) = nullptr;
  int (*GameControllerAddMapping)(const char*) = nullptr;
  void (*GameControllerUpdate)() = nullptr;
  void (*PumpEvents)() = nullptr;
  void (*FlushEvents)(uint32_t, uint32_t) = nullptr;
  const char* (*GetError)() = nullptr;
};

Sdl g_sdl;
const uint32_t kSdlInit = 0x00000200u | 0x00002000u;  // SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER

template <typename T>
bool Load(T& function, const char* name)
{
  function = reinterpret_cast<T>(dlsym(g_sdl.lib, name));
  return function != nullptr;
}

// SDL 2's game controller buttons, in its own order (SDL_GameControllerButton), under the names a
// game shows for an Xbox pad; "Extra" is SDL's misc1 (Capture on a Switch pad).
const char* const kButtons[] = {"A",  "B",  "X",  "Y",  "Back", "Guide", "Start", "LS",
                                "RS", "LB", "RB", "Up", "Down", "Left",  "Right", "Extra"};

QString Wine()
{
  return QDir::homePath() + QStringLiteral("/utdeck-probe/wine");
}

QString ReadLine(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QString::fromUtf8(file.readLine()).trimmed();
}

// The kernel driver of the HID device behind an evdev node (/dev/input/eventN).
QString KernelDriver(const QString& event_path)
{
  const QString node = QFileInfo(event_path).fileName();
  const QString hid = QFileInfo(QStringLiteral("/sys/class/input/%1/device/device").arg(node)).canonicalFilePath();
  if (hid.isEmpty())
    return {};
  return QFileInfo(QFileInfo(hid + QStringLiteral("/driver")).canonicalFilePath()).fileName();
}
}  // namespace

Controllers::Controllers(QObject* parent) : QObject(parent)
{
  m_poll.setInterval(33);
  connect(&m_poll, &QTimer::timeout, this, &Controllers::poll);
  connect(&m_padlayout, &QProcess::finished, this, [this] {
    if (m_running)
      openAll(QString::fromUtf8(m_padlayout.readAllStandardError()));
  });
  connect(&m_wine, &QProcess::stateChanged, this, &Controllers::wineChanged);
  connect(&m_wine, &QProcess::finished, this, [this] {
    // The newest log of this check (game-run.sh: wine/games/controllers-<time>.log).
    QDir games(Wine() + QStringLiteral("/games"));
    const QStringList logs =
        games.entryList({QStringLiteral("controllers-*.log")}, QDir::Files, QDir::Time);
    QFile log(logs.isEmpty() ? QString() : games.filePath(logs.first()));
    if (!log.open(QIODevice::ReadOnly))
    {
      m_wine_result = tr("The check could not run: Wine's runtime or the test program is missing.");
      emit wineChanged();
      return;
    }
    QStringList lines;
    QString section;
    QStringList dinput, hid;
    static const QRegularExpression xinput_slot(QStringLiteral("(\\d+) \\(type"));
    for (const QString& line : QString::fromUtf8(log.readAll()).split(QLatin1Char('\n')))
    {
      if (line.startsWith(QLatin1String("XInput controllers:")))
      {
        int count = 0;
        for (auto it = xinput_slot.globalMatch(line); it.hasNext(); it.next())
          ++count;
        lines << (count ? tr("XInput (most games): %n controller(s)", "", count)
                        : tr("XInput (most games): none"));
        section.clear();
      }
      else if (line.startsWith(QLatin1String("DirectInput 8 game controllers:")))
        section = QStringLiteral("dinput");
      else if (line.startsWith(QLatin1String("HID game controllers:")))
        section = QStringLiteral("hid");
      else if (line.startsWith(QLatin1String("  ")) && section == QLatin1String("dinput"))
        dinput << line.trimmed();
      else if (line.startsWith(QLatin1String("  ")) && section == QLatin1String("hid"))
        hid << line.trimmed();
      else
        section.clear();
    }
    lines << (dinput.isEmpty() ? tr("DirectInput (older games): none")
                               : tr("DirectInput (older games): %1").arg(dinput.join(QStringLiteral("; "))));
    lines << (hid.isEmpty() ? tr("HID (games with their own controller support): none")
                            : tr("HID (games with their own controller support): %1").arg(hid.join(QStringLiteral("; "))));
    m_wine_result = lines.join(QLatin1Char('\n'));
    emit wineChanged();
  });
}

Controllers::~Controllers()
{
  m_page_open = false;
  m_navigation = false;
  closeSdl();
}

bool Controllers::loadSdl()
{
  if (g_sdl.lib)
    return true;
  void* lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
  if (!lib)
    return false;
  g_sdl.lib = lib;
  const bool ok = Load(g_sdl.Init, "SDL_Init") && Load(g_sdl.QuitSubSystem, "SDL_QuitSubSystem") &&
                  Load(g_sdl.NumJoysticks, "SDL_NumJoysticks") &&
                  Load(g_sdl.JoystickNameForIndex, "SDL_JoystickNameForIndex") &&
                  Load(g_sdl.JoystickPathForIndex, "SDL_JoystickPathForIndex") &&
                  Load(g_sdl.JoystickGetDeviceGUID, "SDL_JoystickGetDeviceGUID") &&
                  Load(g_sdl.JoystickGetDeviceVendor, "SDL_JoystickGetDeviceVendor") &&
                  Load(g_sdl.JoystickGetDeviceProduct, "SDL_JoystickGetDeviceProduct") &&
                  Load(g_sdl.IsGameController, "SDL_IsGameController") &&
                  Load(g_sdl.GameControllerOpen, "SDL_GameControllerOpen") &&
                  Load(g_sdl.GameControllerClose, "SDL_GameControllerClose") &&
                  Load(g_sdl.GameControllerGetButton, "SDL_GameControllerGetButton") &&
                  Load(g_sdl.GameControllerGetAxis, "SDL_GameControllerGetAxis") &&
                  Load(g_sdl.GameControllerRumble, "SDL_GameControllerRumble") &&
                  Load(g_sdl.GameControllerAddMapping, "SDL_GameControllerAddMapping") &&
                  Load(g_sdl.GameControllerUpdate, "SDL_GameControllerUpdate") &&
                  Load(g_sdl.PumpEvents, "SDL_PumpEvents") && Load(g_sdl.FlushEvents, "SDL_FlushEvents") &&
                  Load(g_sdl.GetError, "SDL_GetError");
  if (!ok)
  {
    dlclose(lib);
    g_sdl = Sdl();
  }
  return ok;
}

void Controllers::start()
{
  m_page_open = true;
  updateOpen();
}

void Controllers::stop()
{
  m_page_open = false;
  updateOpen();
}

void Controllers::setNavigation(bool on)
{
  if (on == m_navigation)
    return;
  m_navigation = on;
  m_nav_dir = 0;
  m_back_since = 0;
  emit navigationChanged();
  updateOpen();
}

void Controllers::updateOpen()
{
  const bool want = m_page_open || m_navigation;
  if (want && !m_running)
    openSdl();
  else if (!want && m_running)
    closeSdl();
}

void Controllers::openSdl()
{
  if (m_running)
    return;
  m_running = true;
  m_sdl_ok = loadSdl();
  if (!m_sdl_ok)
  {
    m_list = refusedUsb();
    emit changed();
    m_poll.start();  // the kernel's refused controllers are still followed
    return;
  }
  runPadLayout();
}

void Controllers::closeSdl()
{
  if (!m_running)
    return;
  m_running = false;
  m_poll.stop();
  m_padlayout.kill();
  closeAll();
  if (m_sdl_ok)
    g_sdl.QuitSubSystem(kSdlInit);
}

void Controllers::runPadLayout()
{
  // padlayout.py decides, as game-run.sh does for a game, which controllers get UTDeck's layout; its
  // layouts (stdout) are given to this app's SDL too, its decisions (stderr) name which.
  m_padlayout.kill();
  m_padlayout.waitForFinished(1000);
  m_padlayout.start(QStringLiteral("python3"), {Wine() + QStringLiteral("/padlayout.py")});
}

void Controllers::openAll(const QString& decisions)
{
  closeAll();
  if (g_sdl.Init(kSdlInit) != 0)
  {
    m_sdl_ok = false;
    m_list = refusedUsb();
    emit changed();
    return;
  }
  for (const QByteArray& line : m_padlayout.readAllStandardOutput().split('\n'))
    if (!line.trimmed().isEmpty())
      g_sdl.GameControllerAddMapping(line.trimmed().constData());

  m_list.clear();
  const int count = g_sdl.NumJoysticks();
  m_joysticks = count;
  for (int i = 0; i < count; ++i)
  {
    Pad pad;
    const QString name = QString::fromUtf8(g_sdl.JoystickNameForIndex(i));
    const QString path = QString::fromUtf8(g_sdl.JoystickPathForIndex(i));
    const SdlGuid guid = g_sdl.JoystickGetDeviceGUID(i);
    const QString guid_text = QByteArray(reinterpret_cast<const char*>(guid.data), 16).toHex();
    const uint16_t bus = guid.data[0] | (guid.data[1] << 8);
    const uint16_t vendor = g_sdl.JoystickGetDeviceVendor(i);
    const bool game_controller = g_sdl.IsGameController(i);
    QVariantMap& info = pad.info;
    info[QStringLiteral("name")] = name;
    info[QStringLiteral("ids")] = QStringLiteral("%1:%2")
                                      .arg(vendor, 4, 16, QLatin1Char('0'))
                                      .arg(g_sdl.JoystickGetDeviceProduct(i), 4, 16, QLatin1Char('0'));
    info[QStringLiteral("bus")] = bus == 0x05 ? QStringLiteral("Bluetooth") : bus == 0x03 ? QStringLiteral("USB") : QString();
    info[QStringLiteral("driver")] = KernelDriver(path);
    info[QStringLiteral("games")] = game_controller ? tr("Xbox controller") : tr("Joystick (no Xbox layout)");
    // padlayout.py's line for this controller says whose layout it gets.
    QString decision;
    for (const QString& line : decisions.split(QLatin1Char('\n')))
      if (line.contains(guid_text))
        decision = line;
    info[QStringLiteral("layout")] =
        decision.contains(QStringLiteral("by position")) ? QStringLiteral("UTDeck's") : QStringLiteral("SDL's");
    // Wine's registry setting (controllers.reg) is what lets these reach games at all.
    info[QStringLiteral("setting")] = vendor == 0x054c || vendor == 0x057e;
    info[QStringLiteral("problem")] = QString();
    info[QStringLiteral("live")] = game_controller;
    if (game_controller)
      pad.controller = g_sdl.GameControllerOpen(i);
    info[QStringLiteral("pad")] = static_cast<int>(m_pads.size());
    pad.nintendo = vendor == 0x057e;
    pad.style = pad.nintendo ? QStringLiteral("nintendo")
                : vendor == 0x054c ? QStringLiteral("playstation") : QStringLiteral("xbox");
    m_list << info;
    m_pads.push_back(pad);
  }
  m_list << refusedUsb();
  emit changed();
  m_poll.start();
}

void Controllers::closeAll()
{
  for (Pad& pad : m_pads)
    if (pad.controller)
      g_sdl.GameControllerClose(pad.controller);
  m_pads.clear();
  m_state.clear();
}

void Controllers::poll()
{
  static int ticks = 0;
  if (++ticks % 30 == 0)
  {
    // About once a second: a USB controller the kernel refused (or no longer refuses).
    QVariantList refused = refusedUsb();
    QVariantList current;
    for (const QVariant& entry : m_list)
      if (!entry.toMap().value(QStringLiteral("problem")).toString().isEmpty())
        current << entry;
    if (refused != current)
    {
      QVariantList list;
      for (const QVariant& entry : m_list)
        if (entry.toMap().value(QStringLiteral("problem")).toString().isEmpty())
          list << entry;
      m_list = list + refused;
      emit changed();
    }
  }
  if (!m_sdl_ok)
    return;
  g_sdl.PumpEvents();
  g_sdl.GameControllerUpdate();
  g_sdl.FlushEvents(0, 0xFFFF);
  if (g_sdl.NumJoysticks() != m_joysticks)
  {
    // A controller came or went: the layouts are decided again, then all reopened.
    m_poll.stop();
    closeAll();
    g_sdl.QuitSubSystem(kSdlInit);
    runPadLayout();
    return;
  }
  QVariantList state;
  for (Pad& pad : m_pads)
  {
    if (m_navigation && pad.controller)
      navigate(pad);
    QVariantMap s;
    if (pad.controller)
    {
      QStringList pressed;
      for (int b = 0; b < 16; ++b)
        if (g_sdl.GameControllerGetButton(pad.controller, b))
          pressed << QString::fromLatin1(kButtons[b]);
      const auto axis = [&pad](int a) { return g_sdl.GameControllerGetAxis(pad.controller, a) / 32767.0; };
      const auto round2 = [](double v) { return std::round(v * 50) / 50; };  // steps of 0.02
      s[QStringLiteral("buttons")] = pressed;
      s[QStringLiteral("lx")] = round2(axis(0));
      s[QStringLiteral("ly")] = round2(axis(1));
      s[QStringLiteral("rx")] = round2(axis(2));
      s[QStringLiteral("ry")] = round2(axis(3));
      s[QStringLiteral("lt")] = round2(qMax(0.0, axis(4)));
      s[QStringLiteral("rt")] = round2(qMax(0.0, axis(5)));
    }
    state << s;
  }
  if (state != m_state)
  {
    m_state = state;
    emit stateChanged();
  }
}

QString Controllers::rumble(int index)
{
  if (index < 0 || index >= m_list.size())
    return {};
  const int pad = m_list.at(index).toMap().value(QStringLiteral("pad"), -1).toInt();
  if (pad < 0 || pad >= static_cast<int>(m_pads.size()) || !m_pads[pad].controller)
    return tr("This controller cannot be tested here.");
  if (g_sdl.GameControllerRumble(m_pads[pad].controller, 0xC000, 0xC000, 700) == 0)
    return tr("Rumble sent: it should be shaking now.");
  return tr("No rumble on this controller on this phone (the kernel driver has none for it).");
}

void Controllers::checkInWine()
{
  if (m_wine.state() != QProcess::NotRunning)
    return;
  m_wine_result.clear();
  // As a game starts (game-run.sh: the registry setting and padlayout.py's layouts), but not counted
  // as a game: no game file (the library stays), no pause, no CPU boost.
  m_wine.start(QStringLiteral("sh"),
               {QStringLiteral("-c"),
                QStringLiteral("UTDECK_SETUP_RUN=1 UTDECK_PAUSE=0 UTDECK_BOOST=0 \"$HOME/utdeck-probe/wine/game-run.sh\" "
                               "controllers \"$HOME/utdeck-probe/wine/padprobe\" gptest.exe 0 > /dev/null 2>&1")});
  emit wineChanged();
}

QVariantList Controllers::refusedUsb() const
{
  // A USB device with a HID interface but no input device behind it: the kernel's HID driver
  // refused it (stage 4.4: a DualShock 4 clone that does not answer hid-sony's feature report 0x81).
  QVariantList refused;
  static const QRegularExpression hid_name(QStringLiteral("^[0-9A-F]{4}:[0-9A-F]{4}:[0-9A-F]{4}\\.[0-9A-F]{4}$"));
  QDirIterator devices(QStringLiteral("/sys/bus/usb/devices"), QDir::Dirs | QDir::NoDotAndDotDot);
  while (devices.hasNext())
  {
    const QString dev = QFileInfo(devices.next()).canonicalFilePath();
    const QString base = QFileInfo(dev).fileName();
    if (base.startsWith(QLatin1String("usb")) || base.contains(QLatin1Char(':')) ||
        !QFile::exists(dev + QStringLiteral("/idVendor")))
      continue;
    bool hid_interface = false, input = false;
    QDirIterator interfaces(dev, {base + QStringLiteral(":*")}, QDir::Dirs);
    while (interfaces.hasNext())
    {
      const QString iface = interfaces.next();
      if (ReadLine(iface + QStringLiteral("/bInterfaceClass")) != QLatin1String("03"))
        continue;
      hid_interface = true;
      QDirIterator children(iface, QDir::Dirs | QDir::NoDotAndDotDot);
      while (children.hasNext())
      {
        const QString child = children.next();
        if (hid_name.match(QFileInfo(child).fileName()).hasMatch() && QFile::exists(child + QStringLiteral("/input")))
          input = true;
      }
    }
    if (!hid_interface || input)
      continue;
    const QString vendor = ReadLine(dev + QStringLiteral("/idVendor"));
    const QString product = ReadLine(dev + QStringLiteral("/idProduct"));
    QVariantMap info;
    info[QStringLiteral("name")] = ReadLine(dev + QStringLiteral("/product"));
    info[QStringLiteral("ids")] = vendor + QLatin1Char(':') + product;
    info[QStringLiteral("bus")] = QStringLiteral("USB");
    info[QStringLiteral("driver")] = QString();
    info[QStringLiteral("games")] = QString();
    info[QStringLiteral("layout")] = QString();
    info[QStringLiteral("setting")] = false;
    info[QStringLiteral("live")] = false;
    info[QStringLiteral("pad")] = -1;
    info[QStringLiteral("problem")] =
        vendor == QLatin1String("054c") && (product == QLatin1String("05c4") || product == QLatin1String("09cc"))
            ? tr("The phone's kernel did not accept this controller over USB: it does not answer the "
                 "DualShock 4's address request, as some copies do not. Connect it over Bluetooth instead.")
            : tr("The phone's kernel did not accept this controller (no input device was made for it).");
    refused << info;
  }
  return refused;
}

void Controllers::sendKey(int key, bool autorepeat)
{
  QWindow* window = QGuiApplication::focusWindow();
  if (!window)
    return;
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(), autorepeat);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(), autorepeat);
  QGuiApplication::sendEvent(window, &press);
  QGuiApplication::sendEvent(window, &release);
  if (!m_controller_used)
  {
    m_controller_used = true;
    emit controllerUsedChanged();
  }
}

void Controllers::navigate(Pad& pad)
{
  // SDL's buttons by number (SDL_GameControllerButton): 0 A (bottom), 1 B (right), 2 X (left), 3 Y
  // (top), 4 Back, 5 Guide, 6 Start, 7 LS, 8 RS, 9 LB, 10 RB, 11-14 D-pad up, down, left, right.
  uint32_t now = 0;
  for (int b = 0; b < 15; ++b)
    if (g_sdl.GameControllerGetButton(pad.controller, b))
      now |= 1u << b;
  const uint32_t pressed = now & ~pad.held;
  pad.held = now;
  // Confirm and back, X and Y: by position, or by label on a Nintendo-style pad (A right, B bottom,
  // X top, Y left).
  const int confirm = pad.nintendo ? 1 : 0, back = pad.nintendo ? 0 : 1;
  const int settings = pad.nintendo ? 3 : 2, search = pad.nintendo ? 2 : 3;
  const qint64 ms = QDateTime::currentMSecsSinceEpoch();

  // Any button or stick counts as the user being there (the phone sees touches, not SDL's input):
  // used() at most once a second, which keeps the screen on (main.cpp).
  bool active = now != 0;
  for (int axis = 0; axis < 6 && !active; ++axis)
    active = std::abs(g_sdl.GameControllerGetAxis(pad.controller, axis)) > 8000;
  if (active && pad.style != m_style)
  {
    m_style = pad.style;
    emit controllerUsedChanged();
  }
  if (active && ms - m_used_at >= 1000)
  {
    m_used_at = ms;
    emit used();
  }

  if (m_nav_paused)
  {
    // The Controllers page: only back held for a second leaves it.
    if (now & (1u << back))
    {
      if (!m_back_since)
        m_back_since = ms;
      else if (ms - m_back_since >= 1000)
      {
        m_back_since = 0;
        pad.held |= 1u << back;
        sendKey(Qt::Key_Escape);
      }
    }
    else
      m_back_since = 0;
    return;
  }

  const struct
  {
    int button;
    int key;
  } map[] = {{confirm, Qt::Key_Return}, {back, Qt::Key_Escape}, {settings, Qt::Key_Menu},
             {search, Qt::Key_Search}, {9, Qt::Key_PageUp},  {10, Qt::Key_PageDown},
             {6, Qt::Key_F2},          {4, Qt::Key_F3}};
  for (const auto& m : map)
    if (pressed & (1u << m.button))
      sendKey(m.key);

  // Directions: the D-pad, or the left stick past half way; held: again after 400 ms, then every
  // 120 ms.
  double lx = g_sdl.GameControllerGetAxis(pad.controller, 0) / 32767.0;
  double ly = g_sdl.GameControllerGetAxis(pad.controller, 1) / 32767.0;
  if (m_cursor_mode)
  {
    // A pointer page: the left stick moves the pointer (past a dead zone of 15 %), not the focus.
    const double x = std::abs(lx) > 0.15 ? lx : 0, y = std::abs(ly) > 0.15 ? ly : 0;
    if (x != 0 || y != 0)
      emit cursorMoved(x, y);
    lx = ly = 0;
  }
  int dir = 0;
  if ((now & (1u << 11)) || ly < -0.5)
    dir = Qt::Key_Up;
  else if ((now & (1u << 12)) || ly > 0.5)
    dir = Qt::Key_Down;
  else if ((now & (1u << 13)) || lx < -0.5)
    dir = Qt::Key_Left;
  else if ((now & (1u << 14)) || lx > 0.5)
    dir = Qt::Key_Right;
  if (dir != m_nav_dir)
  {
    m_nav_dir = dir;
    if (dir)
    {
      sendKey(dir);
      m_nav_next = ms + 400;
    }
  }
  else if (dir && ms >= m_nav_next)
  {
    sendKey(dir, true);
    m_nav_next = ms + 120;
  }

  // The right stick scrolls what is under the middle of the window (a wheel event), faster the
  // further it is pushed.
  const double ry = g_sdl.GameControllerGetAxis(pad.controller, 3) / 32767.0;
  if (std::abs(ry) > 0.25)
    if (QWindow* window = QGuiApplication::focusWindow())
    {
      const QPointF centre(window->width() / 2.0, window->height() / 2.0);
      QWheelEvent wheel(centre, window->mapToGlobal(centre), QPoint(), QPoint(0, int(-ry * 40)), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
      QGuiApplication::sendEvent(window, &wheel);
    }
}
