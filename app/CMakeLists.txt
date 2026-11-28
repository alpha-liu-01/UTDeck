# UTDeck's app (stage 4, docs/ut-deck/stage4-roadmap.md): a Qt 6 QML frontend in Lomiri's native
# look. Built in the utdeck-app-cross container by tools/app-build/build.sh.
cmake_minimum_required(VERSION 3.25)
project(utdeck VERSION 0.3.59 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick)
# Stage 4.2, the sign-in: hidden Steam's screen captured with libX11, the QR code read and drawn
# with ZXing-C++.
find_package(X11 REQUIRED)
find_package(ZXing REQUIRED)

# QTP0001: the module goes under qrc:/qt/qml/, which is on the QML import path, so
# QQmlApplicationEngine::loadFromModule("UTDeck", "Main") finds it (the Dolphin port's lesson).
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(utdeck
  src/main.cpp
  src/Library.cpp
  src/Library.h
  src/Setup.cpp
  src/Setup.h
  src/SteamLaunch.cpp
  src/SteamLaunch.h
  src/Controllers.cpp
  src/Controllers.h
  src/OwnGames.cpp
  src/OwnGames.h
  src/GameSettings.cpp
  src/GameSettings.h
  src/SteamGames.cpp
  src/SteamGames.h
  src/KeepAwake.cpp
  src/KeepAwake.h
  src/Uninstall.cpp
  src/Uninstall.h
  src/XScreen.cpp
  src/XScreen.h
)

qt_add_qml_module(utdeck
  URI UTDeck
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/LibraryPage.qml
    qml/GameCard.qml
    qml/SetupPage.qml
    qml/SettingsPage.qml
    qml/DialogPage.qml
    qml/StartingPage.qml
    qml/ControllersPage.qml
    qml/AddGamePage.qml
    qml/EditGamePage.qml
    qml/CoverChooser.qml
    qml/CoverPickerPage.qml
    qml/GameSettingsPage.qml
    qml/SteamLibraryPage.qml
    qml/DownloadsPage.qml
    qml/UninstallPage.qml
)

target_compile_definitions(utdeck PRIVATE UTDECK_VERSION="${PROJECT_VERSION}")
target_link_libraries(utdeck PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick X11::X11 X11::Xtst ZXing::ZXing ${CMAKE_DL_LIBS})

install(TARGETS utdeck RUNTIME DESTINATION .)
