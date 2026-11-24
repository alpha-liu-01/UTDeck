import QtQuick
import Lomiri.Components

// The first-time setup (stage 4.2): each step of utdeck-setup.sh with its progress, and, while the
// sign-in step runs, the QR code of hidden Steam's sign-in window, drawn natively. The user scans it
// with the Steam phone app; UTDeck never sees a password. The mandatory part (UTDeck's files, the
// Wine runtime) comes first; Steam is offered after it and can be skipped.
Page {
    id: page

    readonly property bool showSteamSteps: setup.steamChoice === "use"

    function isSteamStep(name) {
        return name === "xvfb" || name === "steam" || name === "service" || name === "signin" || name === "tool"
    }

    function stepTitle(name) {
        switch (name) {
        case "files": return i18n.tr("UTDeck's files")
        case "xvfb": return i18n.tr("A hidden screen for Steam")
        case "runtime": return i18n.tr("The Wine runtime, for Windows games")
        case "steam": return i18n.tr("Steam")
        case "service": return i18n.tr("Starting Steam in the background")
        case "signin": return i18n.tr("Signing in to Steam")
        case "tool": return i18n.tr("UTDeck's runtime for Steam's Windows games")
        }
        return name
    }

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Set up UTDeck")
    }

    Flickable {
        id: flickable

        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        contentHeight: content.height + units.gu(4)

        Row {
            id: content

            x: units.gu(2)
            y: units.gu(2)
            width: flickable.width - units.gu(4)
            spacing: units.gu(3)

            Column {
                id: stepsColumn

                width: setup.signingIn ? (content.width - content.spacing) * 0.55 : content.width
                spacing: units.gu(1)

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: i18n.tr("UTDeck sets up what Windows games need: its own helpers and a Wine runtime, about 1.3 GB. Steam is optional. Nothing outside your home folder is changed.")
                }

                Repeater {
                    model: setup.steps

                    delegate: Item {
                        width: stepsColumn.width
                        height: visible ? units.gu(6) : 0
                        visible: !page.isSteamStep(modelData.name) || page.showSteamSteps

                        ActivityIndicator {
                            id: busy

                            anchors.verticalCenter: parent.verticalCenter
                            width: units.gu(3)
                            height: width
                            running: modelData.state === "running"
                            visible: running
                        }

                        Icon {
                            anchors.centerIn: busy
                            width: units.gu(2.5)
                            height: width
                            visible: !busy.running
                            name: modelData.state === "done" ? "tick"
                                  : modelData.state === "failed" ? "dialog-warning-symbolic"
                                  : "media-record"
                            color: modelData.state === "done" ? theme.palette.normal.positive
                                   : modelData.state === "failed" ? theme.palette.normal.negative
                                   : theme.palette.normal.base
                        }

                        Column {
                            anchors {
                                left: busy.right
                                leftMargin: units.gu(2)
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                            }

                            Label {
                                width: parent.width
                                text: page.stepTitle(modelData.name)
                                elide: Text.ElideRight
                            }

                            Label {
                                width: parent.width
                                visible: text !== ""
                                text: modelData.detail
                                textSize: Label.Small
                                elide: Text.ElideRight
                                color: modelData.state === "failed" ? theme.palette.normal.negative
                                                                    : theme.palette.normal.backgroundSecondaryText
                            }
                        }
                    }
                }

                Label {
                    width: parent.width
                    visible: setup.lastError !== ""
                    text: setup.lastError
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.negative
                }

                Button {
                    visible: !setup.running && (!setup.baseDone || setup.failed
                                                || (page.showSteamSteps && !setup.steamReady))
                    text: setup.failed ? i18n.tr("Try again") : i18n.tr("Set up")
                    color: theme.palette.normal.positive
                    onClicked: setup.start(page.showSteamSteps)
                }

                // Steam, offered once the mandatory part is done.
                Column {
                    width: parent.width
                    spacing: units.gu(1)
                    visible: setup.baseDone && setup.steamChoice === "" && !setup.running

                    Label {
                        width: parent.width
                        text: i18n.tr("Steam (optional)")
                        textSize: Label.Large
                    }

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: i18n.tr("To play your Steam games, UTDeck runs Steam hidden in the background: about 3.7 GB to download, and about 1.3 GB of memory while it runs (it can be switched off in the settings). You sign in by scanning a QR code with the Steam app. You can also set it up later.")
                    }

                    Row {
                        spacing: units.gu(2)

                        Button {
                            text: i18n.tr("Set up Steam")
                            color: theme.palette.normal.positive
                            onClicked: setup.start(true)
                        }

                        Button {
                            text: i18n.tr("Skip")
                            onClicked: setup.skipSteam()
                        }
                    }
                }
            }

            Column {
                id: signInColumn

                visible: setup.signingIn
                width: content.width - stepsColumn.width - content.spacing
                spacing: units.gu(1.5)

                Label {
                    width: parent.width
                    text: i18n.tr("Sign in with the Steam app")
                    textSize: Label.Large
                    wrapMode: Text.Wrap
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: i18n.tr("On another phone, open the Steam app, tap the shield (Steam Guard), then the QR code button, and scan this code. Approve the sign-in there.")
                }

                Rectangle {
                    width: Math.min(parent.width, units.gu(28))
                    height: width
                    color: "white"

                    Image {
                        anchors.fill: parent
                        source: setup.qrAvailable ? "image://qr/" + setup.qrRevision : ""
                        sourceSize: Qt.size(width, height)
                        fillMode: Image.PreserveAspectFit
                        smooth: false
                        cache: false
                    }

                    ActivityIndicator {
                        anchors.centerIn: parent
                        running: !setup.qrAvailable
                    }
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    textSize: Label.Small
                    color: theme.palette.normal.backgroundSecondaryText
                    text: i18n.tr("Steam renews the code every half minute; the one shown is always the current one. UTDeck never sees your password.")
                }

                AbstractButton {
                    width: parent.width
                    height: fallbackLink.height
                    activeFocusOnTab: true
                    onClicked: setup.setFallback(true)

                    Label {
                        id: fallbackLink

                        width: parent.width
                        wrapMode: Text.Wrap
                        color: theme.palette.normal.activity
                        // Underlined while a controller or keyboard has the focus on it (stage 4.8).
                        font.underline: parent.activeFocus
                        text: i18n.tr("Can't scan the code? Use Steam's own sign-in window")
                    }
                }
            }
        }
    }

    // Fallback B: hidden Steam's real sign-in window, shown here. A tap on the picture is a tap on
    // Steam's window; what is typed in the field below goes to it character by character, and the
    // field is emptied at once (the keyboard is told the text is sensitive: no suggestions, nothing
    // learnt). UTDeck keeps none of it.
    Rectangle {
        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: setup.fallbackActive
        color: theme.palette.normal.background

        Image {
            id: steamScreen

            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                bottom: typing.top
                margins: units.gu(1)
            }
            source: setup.fallbackActive ? "image://steamscreen/" + setup.screenRevision : ""
            sourceSize: Qt.size(width, height)
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: false

            MouseArea {
                anchors.fill: parent
                onClicked: (mouse) => {
                    const x0 = (steamScreen.width - steamScreen.paintedWidth) / 2
                    const y0 = (steamScreen.height - steamScreen.paintedHeight) / 2
                    setup.tap((mouse.x - x0) / steamScreen.paintedWidth,
                              (mouse.y - y0) / steamScreen.paintedHeight)
                }
            }
        }

        Row {
            id: typing

            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: units.gu(1)
            }
            spacing: units.gu(1)

            TextField {
                id: field

                width: typing.width - backButton.width - deleteButton.width - tabButton.width
                       - enterButton.width - 4 * typing.spacing
                placeholderText: i18n.tr("Tap a field in Steam's window, then type here")
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onTextChanged: {
                    if (text !== "") {
                        setup.type(text)
                        text = ""
                    }
                }
                onAccepted: setup.key("Return")
            }

            Button {
                id: deleteButton

                text: i18n.tr("Delete")
                onClicked: setup.key("BackSpace")
            }

            Button {
                id: tabButton

                text: i18n.tr("Tab")
                onClicked: setup.key("Tab")
            }

            Button {
                id: enterButton

                text: i18n.tr("Enter")
                color: theme.palette.normal.positive
                onClicked: setup.key("Return")
            }

            Button {
                id: backButton

                text: i18n.tr("Back to the QR code")
                onClicked: setup.setFallback(false)
            }
        }
    }
}
