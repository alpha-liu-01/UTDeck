import QtQuick
import QtQuick.Window
import Lomiri.Components

// UTDeck's window: the library and, in later substages, the other pages, in Lomiri's native look.
// Games run in their own windows, started by hidden Steam (stage 3.S). Their windows carry this
// app's id (Wine patch 0016, UTDECK_APP_ID), so Lomiri shows them as part of this app.
//
// While a game's window is in front, this window hides (stage 4.1): Lomiri gives every window its
// own card in the app switcher, and it makes a window fullscreen only when no other window of the
// app shows (the Dolphin port's stage 3.2). It shows again as soon as the game's process ends
// (game-run.sh removes its file), before Lomiri would close an app left with no window of its own.
Window {
    id: window

    // Lomiri gives the app's next window the state its last window closed in, so after a fullscreen
    // game the library is told "fullscreen" too (the Dolphin port's stage 5). It asks for a normal
    // window instead, at most three times each time it comes back.
    property int normalRequests: 0

    function follow() {
        if (library.runningGame !== "" && library.gameInFront) {
            if (visible)
                hide()
        } else if (library.runningGame === "" && !visible) {
            normalRequests = 0
            show()
            raise()
            requestActivate()
        }
    }

    width: units.gu(100)
    height: units.gu(56)
    visible: true
    title: "UTDeck"
    color: theme.palette.normal.background

    onVisibilityChanged: (visibility) => {
        if (visibility === Window.FullScreen && library.runningGame === "" && normalRequests < 3) {
            normalRequests++
            showNormal()
        }
    }

    Connections {
        target: library
        function onRunningGameChanged() { window.follow() }
        function onGameInFrontChanged() { window.follow() }
    }

    MainView {
        id: mainView

        anchors.fill: parent
        // The click package (or "utdeck" outside Lomiri), set in main.cpp from APP_ID.
        applicationName: Qt.application.name

        PageStack {
            id: pageStack

            Component.onCompleted: push(Qt.resolvedUrl("LibraryPage.qml"))
        }
    }
}
