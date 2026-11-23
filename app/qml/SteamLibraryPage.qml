import QtQuick
import Lomiri.Components

// Stage 4.7: the games of the user's Steam library (SteamGames), to install one without Steam's
// interface. Every game is listed; one with no Windows build is marked as not playable yet. A tap shows
// the download's size against the free space and "Install": Steam's own Install window then comes on
// the "Steam is asking" page, and the download shows on the Downloads page.
Page {
    id: page

    property string chosen: ""

    Component.onCompleted: steamGames.refresh()

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Your Steam library")
    }

    Column {
        id: top

        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            margins: units.gu(2)
        }
        spacing: units.gu(1)

        TextField {
            id: search

            width: parent.width
            placeholderText: i18n.tr("Search")
            inputMethodHints: Qt.ImhNoPredictiveText
        }

        Label {
            width: parent.width
            visible: steamGames.lastError !== ""
            wrapMode: Text.Wrap
            color: theme.palette.normal.negative
            text: steamGames.lastError
        }

        ActivityIndicator {
            running: steamGames.loading
            visible: running
        }
    }

    ListView {
        anchors {
            top: top.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            topMargin: units.gu(1)
        }
        clip: true
        model: steamGames.games.filter(function (g) {
            return search.text === "" || g.name.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
        })

        delegate: ListItem {
            id: item

            required property var modelData
            readonly property bool open: page.chosen === modelData.appId

            width: ListView.view.width
            height: row.height + (open ? details.height : 0) + units.gu(2)
            onClicked: page.chosen = open ? "" : modelData.appId

            Row {
                id: row

                x: units.gu(2)
                y: units.gu(1)
                spacing: units.gu(1.5)

                LomiriShape {
                    width: units.gu(5)
                    height: units.gu(7.5)
                    backgroundColor: theme.palette.normal.base
                    sourceFillMode: LomiriShape.PreserveAspectCrop
                    source: item.modelData.cover !== "" ? coverImage : null

                    Image {
                        id: coverImage

                        visible: false
                        source: item.modelData.cover
                        sourceSize.width: units.gu(5)
                        asynchronous: true
                    }
                }

                Column {
                    width: item.width - units.gu(10.5)
                    anchors.verticalCenter: parent.verticalCenter

                    Label {
                        width: parent.width
                        text: item.modelData.name
                        elide: Text.ElideRight
                    }

                    Label {
                        width: parent.width
                        textSize: Label.Small
                        color: item.modelData.installed ? theme.palette.normal.positive
                               : !item.modelData.windows ? theme.palette.normal.negative
                               : theme.palette.normal.backgroundSecondaryText
                        text: item.modelData.installed ? i18n.tr("Installed")
                              : !item.modelData.windows ? i18n.tr("Linux only: not playable yet")
                              : item.modelData.sizeMb > 0 ? i18n.tr("%1 MB").arg(item.modelData.sizeMb) : ""
                    }
                }
            }

            Column {
                id: details

                anchors.top: row.bottom
                x: units.gu(2)
                width: item.width - units.gu(4)
                spacing: units.gu(1)
                visible: item.open && !item.modelData.installed

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: !item.modelData.windows
                          ? i18n.tr("This game has no Windows build, which UTDeck runs; Linux games come later.")
                          : i18n.tr("Its Windows build: about %1 MB. Free on the phone: %2 MB.").arg(item.modelData.sizeMb).arg(steamGames.freeMb())
                          + (item.modelData.systems.indexOf("linux") >= 0
                             ? " " + i18n.tr("It also has a Linux build: Steam is told to install the Windows one (it restarts for a moment).") : "")
                }

                Button {
                    text: i18n.tr("Install")
                    color: theme.palette.normal.positive
                    enabled: item.modelData.windows && steamGames.task.appId === undefined
                    // On to the Downloads page, which follows it (Steam's Install window comes over it).
                    onClicked: {
                        if (steamGames.install(item.modelData.appId, item.modelData.name))
                            page.pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
                    }
                }
            }
        }
    }
}
