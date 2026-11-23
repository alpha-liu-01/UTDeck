#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <vector>

// Stage 4.4, the Controllers page: the connected controllers as games see them.
//
// Read through the phone's own SDL 2 (libSDL2-2.0.so.0, loaded at run time; a phone without it gets
// a message, not a failure): the library Wine's winebus reads controllers through, so the page shows
// each one under the names a game gets (A, B, X, Y, ... as on an Xbox pad), with the same layouts:
// SDL's own, or UTDeck's from padlayout.py where SDL's does not fit the kernel driver (stone's
// hid-nintendo). Also from the kernel: a USB controller it refused (a DualShock 4 clone that does not
// answer hid-sony's address request), with the reason in words.
//
// While the page is open (start() .. stop()): buttons, sticks and triggers live, a rumble test through
// SDL (which also says when the kernel driver has no rumble), and on request a check in Wine itself
// (game-run.sh with gptest.exe, as a game would start: what XInput, DirectInput and HID offer).
class Controllers : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool sdlAvailable READ sdlAvailable NOTIFY changed)
  // One map per controller: name, ids ("054c:0ce6"), bus ("Bluetooth", "USB"), driver (the kernel's),
  // games ("Xbox controller" or "Joystick"), layout ("SDL's" or "UTDeck's"), setting (true: games
  // get it through UTDeck's registry setting, PlayStation and Nintendo pads), problem (a reason it
  // does not reach games, or ""), live (false for a refused one).
  Q_PROPERTY(QVariantList list READ list NOTIFY changed)
  // One map per controller in list's order (only live ones): buttons (the names pressed), lx, ly,
  // rx, ry (-1..1, up negative), lt, rt (0..1).
  Q_PROPERTY(QVariantList state READ state NOTIFY stateChanged)
  Q_PROPERTY(bool wineChecking READ wineChecking NOTIFY wineChanged)
  // The Wine check's result, a few lines in words ("" before the first check).
  Q_PROPERTY(QString wineResult READ wineResult NOTIFY wineChanged)

public:
  explicit Controllers(QObject* parent = nullptr);
  ~Controllers() override;

  bool sdlAvailable() const { return m_sdl_ok; }
  QVariantList list() const { return m_list; }
  QVariantList state() const { return m_state; }
  bool wineChecking() const { return m_wine.state() != QProcess::NotRunning; }
  QString wineResult() const { return m_wine_result; }

  // The page opened and closed: SDL and the timers run only in between.
  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  // A short rumble on the controller at INDEX of list; the result in words.
  Q_INVOKABLE QString rumble(int index);
  // The check in Wine (about 10 s on stone); not while a game runs (the caller checks).
  Q_INVOKABLE void checkInWine();

signals:
  void changed();
  void stateChanged();
  void wineChanged();

private:
  struct Pad
  {
    void* controller = nullptr;  // SDL_GameController*
    int instance = -1;
    QVariantMap info;
  };

  bool loadSdl();
  void runPadLayout();
  void openAll(const QString& decisions);
  void closeAll();
  void poll();
  QVariantList refusedUsb() const;

  bool m_sdl_ok = false;
  bool m_running = false;
  QTimer m_poll;
  QProcess m_padlayout;
  QProcess m_wine;
  std::vector<Pad> m_pads;
  QVariantList m_list;
  QVariantList m_state;
  QString m_wine_result;
  int m_joysticks = -1;
};
