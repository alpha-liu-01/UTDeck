import QtQuick
import Lomiri.Components

// The library: the installed Steam games as a grid of covers. A tap starts the game through hidden
// Steam (Library.play). Read again whenever the app comes to the front, so games installed or
// removed meanwhile show up.
Page {
    id: page

    property string starting: ""

    header: PageHeader {
        id: pageHeader

        title: "UTDeck"
        trailingActionBar.actions: [
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

    GridView {
        id: grid

        anchors {
            top: pageHeader.bottom
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
            onClicked: {
                if (library.play(model.appId)) {
                    page.starting = model.title
                    startingTimer.restart()
                }
            }
        }
    }

    Label {
        anchors.centerIn: grid
        width: grid.width - units.gu(4)
        visible: library.count === 0
        text: i18n.tr("No installed Steam games found in %1.").arg(library.steamFolder)
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
                  : page.starting !== "" ? i18n.tr("Starting %1 through Steam...").arg(page.starting)
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
