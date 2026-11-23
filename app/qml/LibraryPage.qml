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

    GridView {
        id: grid

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
            // A long press: one of the user's own games opens its page (stage 4.5: edit, remove, its
            // settings), a Steam game its settings (stage 4.6).
            onPressAndHold: {
                if (library.isOwn(model.appId))
                    page.pageStack.push(Qt.resolvedUrl("EditGamePage.qml"), {appId: model.appId})
                else
                    page.pageStack.push(Qt.resolvedUrl("GameSettingsPage.qml"), {appId: model.appId, title: model.title})
            }
            onClicked: {
                if (library.play(model.appId)) {
                    setup.gameStarting()
                    page.starting = model.title
                    page.startingOwn = library.isOwn(model.appId)
                    startingTimer.restart()
                    // The starting page (stage 4.3) follows this launch until the game's window is in front.
                    steamLaunch.expect(model.appId)
                    page.pageStack.push(Qt.resolvedUrl("StartingPage.qml"),
                                        {appId: model.appId, title: model.title, cover: model.cover})
                }
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
            bottom: parent.bottom
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

    Timer {
        id: startingTimer

        interval: 30000
        onTriggered: page.starting = ""
    }
}
