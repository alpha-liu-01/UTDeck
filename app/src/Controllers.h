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
  // Stage 4.8: the app used with a controller. While navigation is on (Main.qml: the app in front, the
  // screen on, no game in front), the controllers' buttons become key presses for the app's focused
  // window: the D-pad and left stick arrow keys (repeating while held), confirm Return, back Escape,
  // X Menu (a game's page), Y Search, LB/RB Page Up/Down, Start F2 (Settings), Back F3 (Downloads);
  // the right stick scrolls (wheel events). Confirm is the bottom button, except on Nintendo-style pads
  // (vendor 057e), where the buttons follow their labels: A on the right confirms, B at the bottom goes
  // back (the maintainer, 2026-11-23); X and Y follow the labels there too.
  // navigationPaused (the Controllers page, where buttons are being tried): only back held for a
  // second does something (Escape).
  Q_PROPERTY(bool navigation READ navigation WRITE setNavigation NOTIFY navigationChanged)
  Q_PROPERTY(bool navigationPaused MEMBER m_nav_paused NOTIFY navigationChanged)
  // A controller's button has moved the app since it started (for the button hints).
  Q_PROPERTY(bool controllerUsed READ controllerUsed NOTIFY controllerUsedChanged)
  // The labels on the controller used last, for the button hints: "xbox", "playstation" (vendor 054c)
  // or "nintendo" (057e).
  Q_PROPERTY(QString buttonStyle READ buttonStyle NOTIFY controllerUsedChanged)
  // A pointer page ("Steam is asking", stage 4.8 step 5): the left stick moves a pointer smoothly
  // (cursorMoved, each poll, -1..1 per axis) instead of sending arrow keys; the D-pad still sends them.
  Q_PROPERTY(bool cursorMode MEMBER m_cursor_mode NOTIFY navigationChanged)

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
  bool navigation() const { return m_navigation; }
  void setNavigation(bool on);
  bool controllerUsed() const { return m_controller_used; }
  QString buttonStyle() const { return m_style; }
  // A short rumble on the controller at INDEX of list; the result in words.
  Q_INVOKABLE QString rumble(int index);
  // The check in Wine (about 10 s on stone); not while a game runs (the caller checks).
  Q_INVOKABLE void checkInWine();

signals:
  void changed();
  void stateChanged();
  void navigationChanged();
  void controllerUsedChanged();
  // A button pressed or a stick moved while SDL is open (at most once a second).
  void used();
  // The left stick in cursorMode: how far it is pushed (-1..1, past a small dead zone; up negative).
  void cursorMoved(qreal x, qreal y);
  void wineChanged();

private:
  struct Pad
  {
    void* controller = nullptr;  // SDL_GameController*
    int instance = -1;
    QVariantMap info;
    bool nintendo = false;       // buttons follow their labels for confirm and back (vendor 057e)
    QString style;               // "xbox", "playstation" or "nintendo": the labels on its buttons
    uint32_t held = 0;           // the buttons held at the last poll (bit = SDL's button number)
  };

  bool loadSdl();
  void runPadLayout();
  void openAll(const QString& decisions);
  void closeAll();
  void poll();
  // The SDL side opened or closed as the page or the navigation need it.
  void updateOpen();
  void openSdl();
  void closeSdl();
  // Navigation: one controller's buttons and sticks to key presses (stage 4.8).
  void navigate(Pad& pad);
  void sendKey(int key, bool autorepeat = false);
  QVariantList refusedUsb() const;

  bool m_sdl_ok = false;
  bool m_running = false;
  bool m_page_open = false;
  bool m_navigation = false;
  bool m_nav_paused = false;
  bool m_controller_used = false;
  bool m_cursor_mode = false;
  QString m_style = QStringLiteral("xbox");
  int m_nav_dir = 0;            // the arrow key held (0: none)
  qint64 m_nav_next = 0;        // when it repeats next (ms since epoch)
  qint64 m_used_at = 0;         // when used() was last emitted
  qint64 m_back_since = 0;      // back held since (paused navigation), 0: not held
  QTimer m_poll;
  QProcess m_padlayout;
  QProcess m_wine;
  std::vector<Pad> m_pads;
  QVariantList m_list;
  QVariantList m_state;
  QString m_wine_result;
  int m_joysticks = -1;
};
