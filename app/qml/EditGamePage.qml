import QtQuick
import Lomiri.Components

// Stage 4.5: one of the user's own games, from a long press on its card: its name, its program and
// its cover changed, or the game removed. Removing keeps the game's files and, unless asked, its
// Wine data (its saved games): adding the same folder again later finds them. An installed game
// lives in its Wine data, so deleting that deletes the game too (said before it is done).
Page {
    id: page

    property string appId
    property var game: ownGames.game(appId)
    property string chosenExe: game.exe || ""
    property string cover: game.cover || ""
    property bool confirming: false
    property string saveError: ""

    Component.onCompleted: ownGames.check(game.folder)

    header: PageHeader {
        id: pageHeader

        title: page.game.name || ""
    }

    Flickable {
        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        contentHeight: column.height + units.gu(4)

        Column {
            id: column

            x: units.gu(2)
            y: units.gu(2)
            width: parent.width - units.gu(4)
            spacing: units.gu(1.5)

            Label {
                text: i18n.tr("Name")
            }

            TextField {
                id: nameField

                width: parent.width
                text: page.game.name || ""
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Folder: %1").arg(page.game.folder || "")
            }

            Label {
                text: i18n.tr("The game's program")
            }

            ActivityIndicator {
                running: ownGames.checking
                visible: running
            }

            Repeater {
                model: ownGames.checking ? [] : ownGames.executables

                delegate: ListItem {
                    id: exeItem

                    required property var modelData

                    width: column.width
                    height: exeLayout.height
                    color: page.chosenExe === modelData.path ? theme.palette.highlighted.background : "transparent"
                    onClicked: page.chosenExe = modelData.path

                    ListItemLayout {
                        id: exeLayout

                        title.text: exeItem.modelData.path
                        subtitle.text: exeItem.modelData.machine + ", " + i18n.tr("%1 MB").arg(Math.max(1, Math.round(exeItem.modelData.kb / 1024)))
                                       + (exeItem.modelData.unlikely ? i18n.tr(" (likely not the game: a setup, uninstaller or helper)") : "")
                        Icon {
                            SlotsLayout.position: SlotsLayout.Trailing
                            width: units.gu(2.5)
                            height: width
                            name: "tick"
                            visible: page.chosenExe === exeItem.modelData.path
                        }
                    }
                }
            }

            Label {
                text: i18n.tr("Cover")
            }

            Flow {
                width: parent.width
                spacing: units.gu(1)

                Repeater {
                    model: [""].concat(ownGames.images(page.game.folder || ""))

                    delegate: LomiriShape {
                        required property string modelData

                        width: units.gu(8)
                        height: units.gu(12)
                        backgroundColor: theme.palette.normal.base
                        sourceFillMode: LomiriShape.PreserveAspectCrop
                        source: modelData !== "" ? coverImage : null

                        Image {
                            id: coverImage

                            visible: false
                            source: parent.modelData !== "" ? "file://" + parent.modelData : ""
                            sourceSize.width: units.gu(8)
                            asynchronous: true
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: parent.modelData === ""
                            text: i18n.tr("None")
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: page.cover === parent.modelData ? units.dp(3) : 0
                            border.color: theme.palette.normal.positive
                            radius: units.gu(1)
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.cover = parent.modelData
                        }
                    }
                }
            }

            Label {
                width: parent.width
                visible: page.saveError !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: page.saveError
            }

            Button {
                text: i18n.tr("Save")
                color: theme.palette.normal.positive
                enabled: nameField.text.trim() !== "" && page.chosenExe !== "" && !ownGames.checking
                onClicked: {
                    if (ownGames.edit(page.appId, nameField.text, page.chosenExe, page.cover)) {
                        library.refresh()
                        page.pageStack.pop()
                    } else {
                        page.saveError = i18n.tr("The game could not be saved.")
                    }
                }
            }

            // How the game runs (stage 4.6).
            Button {
                text: i18n.tr("Settings")
                onClicked: page.pageStack.push(Qt.resolvedUrl("GameSettingsPage.qml"),
                                               {appId: page.appId, title: page.game.name || ""})
            }

            // Removing, confirmed on the page.
            Button {
                visible: !page.confirming
                text: i18n.tr("Remove from the library...")
                color: theme.palette.normal.negative
                enabled: library.runningGame !== page.appId
                onClicked: page.confirming = true
            }

            Column {
                width: parent.width
                spacing: units.gu(1.5)
                visible: page.confirming

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: page.game.installed
                          ? i18n.tr("The game is taken out of the library. Its Wine data is kept (with its saved games, and the installed game itself), unless you delete it below.")
                          : i18n.tr("The game is taken out of the library. Its files in %1 stay where they are (delete them over USB or with the file manager), and so does its Wine data with its saved games, unless you delete it below.").arg(page.game.folder || "")
                }

                Row {
                    spacing: units.gu(1.5)

                    CheckBox {
                        id: deleteData
                    }

                    Label {
                        width: column.width - units.gu(5)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: page.game.installed
                              ? i18n.tr("Also delete its Wine data: its saved games and the installed game")
                              : i18n.tr("Also delete its Wine data, with any saved games in it")
                    }
                }

                Row {
                    spacing: units.gu(1.5)

                    Button {
                        text: deleteData.checked ? i18n.tr("Remove and delete") : i18n.tr("Remove")
                        color: theme.palette.normal.negative
                        onClicked: {
                            if (ownGames.remove(page.appId, deleteData.checked)) {
                                library.refresh()
                                page.pageStack.pop()
                            } else {
                                page.saveError = i18n.tr("The game could not be removed.")
                            }
                        }
                    }

                    Button {
                        text: i18n.tr("Cancel")
                        onClicked: {
                            deleteData.checked = false
                            page.confirming = false
                        }
                    }
                }
            }
        }
    }
}
