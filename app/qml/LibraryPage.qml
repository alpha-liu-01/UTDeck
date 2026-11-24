import QtQuick
import Lomiri.Components

// The library: the installed Steam games and the user's own (stage 4.5) as a grid of covers. A tap
// starts the game (Library.play: through hidden Steam, or the user's with own-run.sh). Read again
// whenever the app comes to the front, so games installed or removed meanwhile show up.
Page {
    id: page

    property string starting: ""
    property bool startingOwn: false

    header: PageHeader {
        id: pageHeader

        title: "UTDeck"
        trailingActionBar.numberOfSlots: 4
        trailingActionBar.actions: [
            Action {
                iconName: "add"
                text: i18n.tr("Add a game")
                onTriggered: page.pageStack.push(Qt.resolvedUrl("AddGamePage.qml"))
            },
            // Stage 4.7: Steam's downloads and tasks, on a page of their own; the icon shows when
            // something runs.
            Action {
                iconName: steamGames.downloads.length > 0 || steamGames.task.appId !== undefined
                          ? "transfer-progress-download" : "save-to"
                text: i18n.tr("Downloads")
                onTriggered: page.pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
            },
            Action {
                iconName: "settings"
                text: i18n.tr("Settings")
                onTriggered: page.pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            },
            Action {
                iconName: "reload"
                text: i18n.tr("Refresh")
                onTriggered: library.refresh()
            }
        ]
    }

    // The game came and went: no "Starting ..." left over.
    Connections {
        target: library
        function onRunningGameChanged() {
            if (library.runningGame === "")
                page.starting = ""
        }
    }

    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive)
                library.refresh()
        }
    }

    // Stage 4.7: Steam signed out (its login ran out), above the games. Steam's downloads and tasks
    // have their own page (the header's Downloads).
    Column {
        id: notices

        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            margins: units.gu(1)
        }
        spacing: units.gu(1)

        Row {
            width: parent.width
            spacing: units.gu(1.5)
            visible: setup.steamInstalled && setup.steamRunning && !setup.steamSignedIn && !setup.steamStarting

            Label {
                width: parent.width - signInButton.width - units.gu(1.5)
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: i18n.tr("Steam is signed out: its games cannot start until you sign in again.")
            }

            Button {
                id: signInButton

                text: i18n.tr("Sign in")
                color: theme.palette.normal.positive
                onClicked: {
                    if (setup.start(true))
                        page.pageStack.push(Qt.resolvedUrl("SetupPage.qml"))
                }
            }
        }
    }

    // Stage 4.8: the controller's (or a keyboard's) buttons, as keys: arrows move between the cards,
    // Return plays, Menu (X) opens the game's page, Search (Y) "Add a game", F2 (Start) the settings,
    // F3 (Back/View) the downloads. The frame shows once a key or button has been used.
    property bool keyUsed: false
    readonly property bool showFocus: controllers.controllerUsed || page.keyUsed

    function playGame(appId, title, cover) {
        if (library.play(appId)) {
            setup.gameStarting()
            page.starting = title
            page.startingOwn = library.isOwn(appId)
            startingTimer.restart()
            // The starting page (stage 4.3) follows this launch until the game's window is in front.
            steamLaunch.expect(appId)
            page.pageStack.push(Qt.resolvedUrl("StartingPage.qml"), {appId: appId, title: title, cover: cover})
        }
    }

    function openGamePage(appId, title) {
        // One of the user's own games opens its page (stage 4.5: edit, remove, its settings), a Steam
        // game its settings (stage 4.6).
        if (library.isOwn(appId))
            page.pageStack.push(Qt.resolvedUrl("EditGamePage.qml"), {appId: appId})
        else
            page.pageStack.push(Qt.resolvedUrl("GameSettingsPage.qml"), {appId: appId, title: title})
    }

    // Where the focus goes when this page comes to the front (Main.qml's focusPage).
    function focusFirst() {
        grid.forceActiveFocus()
    }

    // The selected card is kept when the library is read again (each time the app comes to the front,
    // so also after a game: its card stays selected). The model is reset, which moves the grid to the
    // first card.
    property string keptAppId: ""
    Connections {
        target: library
        function onModelAboutToBeReset() {
            page.keptAppId = grid.currentItem ? grid.currentItem.appId : ""
        }
        function onModelReset() {
            Qt.callLater(() => {
                const i = library.indexOf(page.keptAppId)
                if (i >= 0)
                    grid.currentIndex = i
            })
        }
    }

    // The buttons, by the labels on the controller used last.
    function hint() {
        switch (controllers.buttonStyle) {
        case "playstation":
            return i18n.tr("Cross: play    Square: game settings    Triangle: add a game    Options: settings    Create: downloads")
        case "nintendo":
            return i18n.tr("A: play    X: game settings    Y: add a game    +: settings    -: downloads")
        }
        return i18n.tr("A: play    X: game settings    Y: add a game    Menu: settings    View: downloads")
    }

    GridView {
        id: grid

        focus: true
        keyNavigationEnabled: true
        // Room for the frame of a card on the first and last row.
        topMargin: units.gu(0.5)
        bottomMargin: units.gu(0.5)
        Keys.onPressed: (event) => {
            // Only while the library is the page shown (another page has the focus otherwise).
            if (page.pageStack.currentPage !== page)
                return
            page.keyUsed = true
            const card = grid.currentItem
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && card) {
                page.playGame(card.appId, card.title, card.coverUrl)
            } else if (event.key === Qt.Key_Menu && card) {
                page.openGamePage(card.appId, card.title)
            } else if (event.key === Qt.Key_Search) {
                page.pageStack.push(Qt.resolvedUrl("AddGamePage.qml"))
            } else if (event.key === Qt.Key_F2) {
                page.pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            } else if (event.key === Qt.Key_F3) {
                page.pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
            } else {
                return
            }
            event.accepted = true
        }

        anchors {
            top: notices.bottom
            left: parent.left
            right: parent.right
            bottom: status.top
            margins: units.gu(1)
        }
        clip: true
        // As many columns of about 16 grid units as fit.
        cellWidth: width / Math.max(1, Math.floor(width / units.gu(16)))
        cellHeight: cellWidth * 1.5 + units.gu(5)
        model: library

        delegate: GameCard {
            width: grid.cellWidth
            height: grid.cellHeight
            selected: GridView.isCurrentItem && grid.activeFocus && page.showFocus
            // A long press: the game's page (X on a controller).
            onPressAndHold: page.openGamePage(model.appId, model.title)
            onClicked: {
                grid.currentIndex = index
                page.playGame(model.appId, model.title, model.cover)
            }
        }
    }

    Label {
        anchors.centerIn: grid
        width: grid.width - units.gu(4)
        visible: library.count === 0
        text: i18n.tr("No games yet. Steam's installed games show up here, and so do your own (copied into Documents/UTDeck/Games).")
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: theme.palette.normal.backgroundSecondaryText
    }

    // What the last tap did: "Starting ..." for a while, or why it failed.
    Rectangle {
        id: status

        anchors {
            left: parent.left
            right: parent.right
            bottom: hintBar.top
        }
        height: statusLabel.text !== "" ? units.gu(5) : 0
        color: theme.palette.normal.foreground

        Label {
            id: statusLabel

            anchors {
                verticalCenter: parent.verticalCenter
                left: parent.left
                right: parent.right
                margins: units.gu(2)
            }
            text: library.lastError !== "" ? library.lastError
                  : page.starting !== "" ? (page.startingOwn ? i18n.tr("Starting %1...").arg(page.starting)
                                                             : i18n.tr("Starting %1 through Steam...").arg(page.starting))
                  : ""
            color: library.lastError !== "" ? theme.palette.normal.negative
                                            : theme.palette.normal.foregroundText
            elide: Text.ElideRight
        }
    }

    // Stage 4.8: the controller's buttons, once one has been used.
    Item {
        id: hintBar

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: controllers.controllerUsed ? units.gu(4) : 0
        visible: controllers.controllerUsed

        Label {
            anchors {
                verticalCenter: parent.verticalCenter
                left: parent.left
                right: parent.right
                margins: units.gu(2)
            }
            text: page.hint()
            textSize: Label.Small
            elide: Text.ElideRight
            color: theme.palette.normal.backgroundSecondaryText
        }
    }

    Timer {
        id: startingTimer

        interval: 30000
        onTriggered: page.starting = ""
    }
}
