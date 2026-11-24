import QtQuick
import Lomiri.Components

// Stage 4.4: the connected controllers as games see them (Controllers, through the phone's SDL 2,
// the library Wine reads controllers through): each one's name and connection, how games get it
// (an Xbox controller, with SDL's layout or UTDeck's for this phone's driver), its buttons, sticks
// and triggers live under the names games use, a rumble test; a controller the kernel refused, with
// the reason; and the check in Wine itself, on request.
Page {
    id: page

    readonly property var buttonNames: ["A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Guide",
                                        "LS", "RS", "Up", "Down", "Left", "Right", "Extra"]

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Controllers")
    }

    // Buttons are tried here: the app's navigation pauses (stage 4.8); B held for a second goes back.
    Component.onCompleted: {
        controllers.start()
        controllers.navigationPaused = true
    }
    Component.onDestruction: {
        controllers.navigationPaused = false
        controllers.stop()
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
            spacing: units.gu(2)

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: i18n.tr("What games get from each connected controller, with its buttons under the names games use for an Xbox pad. Connect a controller before starting a game.")
            }

            Label {
                width: parent.width
                visible: controllers.controllerUsed
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("The buttons are only tried here: hold B (on a Switch-style controller, B at the bottom) for a second to go back.")
            }

            Button {
                text: i18n.tr("Bluetooth settings")
                onClicked: Qt.openUrlExternally("settings:///system/bluetooth")
            }

            Label {
                width: parent.width
                visible: !controllers.sdlAvailable
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: i18n.tr("This phone has no SDL 2 library (libSDL2-2.0.so.0), which Wine reads controllers through: games here see no controller.")
            }

            Label {
                width: parent.width
                visible: controllers.list.length === 0
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("No controller connected. Pair one in the Bluetooth settings, or connect one with a USB cable.")
            }

            Repeater {
                model: controllers.list

                delegate: Rectangle {
                    id: card

                    required property var modelData
                    required property int index
                    readonly property var live: index < controllers.state.length ? controllers.state[index] : null
                    property string rumbleResult: ""

                    width: column.width
                    height: cardColumn.height + units.gu(3)
                    radius: units.gu(1)
                    color: theme.palette.normal.foreground

                    Column {
                        id: cardColumn

                        x: units.gu(1.5)
                        y: units.gu(1.5)
                        width: parent.width - units.gu(3)
                        spacing: units.gu(1)

                        Label {
                            width: parent.width
                            text: card.modelData.name || i18n.tr("Unknown controller")
                            textSize: Label.Large
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            wrapMode: Text.Wrap
                            color: theme.palette.normal.backgroundSecondaryText
                            text: [card.modelData.bus, card.modelData.ids,
                                   card.modelData.driver ? i18n.tr("kernel driver: %1").arg(card.modelData.driver) : ""]
                                  .filter(function (part) { return part !== "" }).join("  ·  ")
                        }

                        Label {
                            width: parent.width
                            visible: card.modelData.problem !== ""
                            wrapMode: Text.Wrap
                            color: theme.palette.normal.negative
                            text: card.modelData.problem
                        }

                        Label {
                            width: parent.width
                            visible: card.modelData.problem === ""
                            wrapMode: Text.Wrap
                            text: i18n.tr("Games see: %1").arg(card.modelData.games)
                                  + (card.modelData.layout === "UTDeck's" ? i18n.tr(", with UTDeck's layout for this phone's driver") : "")
                                  + (card.modelData.setting ? i18n.tr(" (it reaches games through UTDeck's Wine setting)") : "")
                        }

                        // The buttons, lit while pressed.
                        Flow {
                            width: parent.width
                            visible: card.modelData.live
                            spacing: units.gu(0.5)

                            Repeater {
                                model: page.buttonNames

                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property bool pressed: card.live !== null && card.live.buttons !== undefined
                                                                    && card.live.buttons.indexOf(modelData) >= 0

                                    width: chip.width + units.gu(1.5)
                                    height: units.gu(3)
                                    radius: units.gu(0.5)
                                    color: pressed ? theme.palette.normal.positive : theme.palette.normal.base

                                    Label {
                                        id: chip

                                        anchors.centerIn: parent
                                        text: parent.modelData
                                        color: parent.pressed ? theme.palette.normal.positiveText : theme.palette.normal.baseText
                                    }
                                }
                            }
                        }

                        // The sticks (a dot in a square) and the triggers (bars).
                        Row {
                            visible: card.modelData.live
                            spacing: units.gu(2)

                            Repeater {
                                model: [["lx", "ly", i18n.tr("Left stick")], ["rx", "ry", i18n.tr("Right stick")]]

                                delegate: Column {
                                    required property var modelData
                                    spacing: units.gu(0.5)

                                    Rectangle {
                                        width: units.gu(9)
                                        height: width
                                        radius: units.gu(0.5)
                                        color: theme.palette.normal.base

                                        Rectangle {
                                            readonly property real sx: card.live && card.live[modelData[0]] !== undefined ? card.live[modelData[0]] : 0
                                            readonly property real sy: card.live && card.live[modelData[1]] !== undefined ? card.live[modelData[1]] : 0

                                            width: units.gu(1.5)
                                            height: width
                                            radius: width / 2
                                            color: theme.palette.normal.positive
                                            x: (parent.width - width) / 2 * (1 + sx)
                                            y: (parent.height - height) / 2 * (1 + sy)
                                        }
                                    }

                                    Label {
                                        text: modelData[2]
                                        textSize: Label.Small
                                    }
                                }
                            }

                            Repeater {
                                model: [["lt", "LT"], ["rt", "RT"]]

                                delegate: Column {
                                    required property var modelData
                                    spacing: units.gu(0.5)

                                    Rectangle {
                                        width: units.gu(2.5)
                                        height: units.gu(9)
                                        radius: units.gu(0.5)
                                        color: theme.palette.normal.base

                                        Rectangle {
                                            readonly property real value: card.live && card.live[modelData[0]] !== undefined ? card.live[modelData[0]] : 0

                                            anchors.bottom: parent.bottom
                                            width: parent.width
                                            height: parent.height * value
                                            radius: units.gu(0.5)
                                            color: theme.palette.normal.positive
                                        }
                                    }

                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: modelData[1]
                                        textSize: Label.Small
                                    }
                                }
                            }
                        }

                        Row {
                            visible: card.modelData.live
                            spacing: units.gu(1.5)

                            Button {
                                text: i18n.tr("Test rumble")
                                onClicked: card.rumbleResult = controllers.rumble(card.index)
                            }

                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                width: cardColumn.width - units.gu(18)
                                wrapMode: Text.Wrap
                                text: card.rumbleResult
                                color: theme.palette.normal.backgroundSecondaryText
                            }
                        }
                    }
                }
            }

            Label {
                text: i18n.tr("Check in Wine")
                textSize: Label.Large
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: library.runningGame !== "" ? i18n.tr("Not while a game is running.")
                      : i18n.tr("Starts Wine as a game would and lists what Windows offers games (about 10 seconds).")
            }

            Row {
                spacing: units.gu(1.5)

                Button {
                    text: i18n.tr("Check")
                    enabled: !controllers.wineChecking && library.runningGame === ""
                    onClicked: controllers.checkInWine()
                }

                ActivityIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: controllers.wineChecking
                    visible: running
                }
            }

            Label {
                width: parent.width
                visible: controllers.wineResult !== ""
                wrapMode: Text.Wrap
                text: controllers.wineResult
            }
        }
    }
}
