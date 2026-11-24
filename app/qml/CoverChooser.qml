import QtQuick
import Lomiri.Components

// An own game's cover (stages 4.5 and 4.9): no cover, the pictures found in the game's folder and in
// Pictures, and "Choose a picture...", which asks another app for one through Content Hub (Gallery,
// File Manager: CoverPickerPage.qml). A picture from another app is copied into the game's own folder
// when the game is saved (OwnGames). Shared by the Add game and Edit game pages.
Flow {
    id: chooser

    // The game's folder, the cover chosen now, and the page stack the picker page goes on.
    property string folder
    property string cover
    property var stack
    // A choice made here (the page owns the cover).
    signal chosen(string path)

    // Found once per folder; a choice does not rebuild the list (a controller's focus stays put).
    readonly property var found: visible ? ownGames.images(chooser.folder) : []
    // The current cover and pictures from other apps, when they are not among those found.
    property var extra: []

    function addExtra(path) {
        if (path !== "" && chooser.found.indexOf(path) < 0 && chooser.extra.indexOf(path) < 0)
            chooser.extra = [path].concat(chooser.extra)
    }

    // From CoverPickerPage.qml.
    function picked(path) {
        chooser.addExtra(path)
        chooser.chosen(path)
    }

    onFoundChanged: chooser.addExtra(chooser.cover)
    Component.onCompleted: chooser.addExtra(chooser.cover)

    width: parent.width
    spacing: units.gu(1)

    Repeater {
        model: chooser.visible ? [""].concat(chooser.extra, chooser.found, ["pick:"]) : []

        delegate: LomiriShape {
            id: tile

            required property string modelData
            readonly property bool picker: modelData === "pick:"

            width: units.gu(8)
            height: units.gu(12)
            backgroundColor: theme.palette.normal.base
            sourceFillMode: LomiriShape.PreserveAspectCrop
            source: modelData !== "" && !picker ? coverImage : null

            Image {
                id: coverImage

                visible: false
                source: tile.modelData !== "" && !tile.picker ? "file://" + tile.modelData : ""
                sourceSize.width: units.gu(8)
                asynchronous: true
            }

            Label {
                anchors.centerIn: parent
                visible: tile.modelData === ""
                text: i18n.tr("None")
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - units.gu(1)
                spacing: units.gu(0.5)
                visible: tile.picker

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: units.gu(3)
                    height: width
                    name: "insert-image"
                }

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textSize: Label.Small
                    text: i18n.tr("Choose a picture...")
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: !tile.picker && chooser.cover === tile.modelData ? units.dp(3) : 0
                border.color: theme.palette.normal.positive
                radius: units.gu(1)
            }

            // A tap, or A on a controller (stage 4.8: reachable with the arrows, framed while it has
            // the focus).
            AbstractButton {
                id: coverButton

                anchors.fill: parent
                activeFocusOnTab: true
                onClicked: {
                    if (tile.picker)
                        chooser.stack.push(Qt.resolvedUrl("CoverPickerPage.qml"), {chooser: chooser})
                    else
                        chooser.chosen(tile.modelData)
                }
            }

            Rectangle {
                anchors {
                    fill: parent
                    margins: -units.dp(4)
                }
                visible: coverButton.activeFocus && controllers.controllerUsed
                color: "transparent"
                border.width: units.dp(2)
                border.color: theme.palette.normal.focus
                radius: units.gu(1.2)
            }
        }
    }
}
