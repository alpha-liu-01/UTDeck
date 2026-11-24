import QtQuick
import Lomiri.Components

// UTDeck's settings (stage 4.2): Steam, which is optional. Not set up: set it up from here. Set up:
// its switch (on, hidden Steam runs while the app does; off, only while a Steam game runs, saving
// about 1.3 GB of memory) and its state. Stage 4.4: the Controllers page.
Page {
    id: page

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Settings")
    }

    Component.onCompleted: setup.check()

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

            ListItem {
                width: parent.width
                height: controllersLayout.height
                divider.visible: false
                onClicked: page.pageStack.push(Qt.resolvedUrl("ControllersPage.qml"))

                ListItemLayout {
                    id: controllersLayout

                    title.text: i18n.tr("Controllers")
                    subtitle.text: i18n.tr("What games get from each connected controller")

                    ProgressionSlot {}
                }
            }

            Label {
                text: i18n.tr("Steam")
                textSize: Label.Large
            }

            // Not set up (skipped, or never asked).
            Column {
                width: parent.width
                spacing: units.gu(1.5)
                visible: !setup.steamInstalled

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: i18n.tr("Steam is not set up. Set it up to play your Steam games: about 3.7 GB to download; you sign in by scanning a QR code with the Steam app.")
                }

                Button {
                    text: i18n.tr("Set up Steam")
                    color: theme.palette.normal.positive
                    onClicked: {
                        if (setup.start(true))
                            page.pageStack.push(Qt.resolvedUrl("SetupPage.qml"))
                    }
                }
            }

            // Set up.
            ListItem {
                width: parent.width
                height: layout.height
                visible: setup.steamInstalled
                divider.visible: false

                ListItemLayout {
                    id: layout

                    title.text: i18n.tr("Run Steam in the background")
                    subtitle.text: i18n.tr("Off saves about 1.3 GB of memory. A Steam game still starts Steam while it runs.")
                    subtitle.maximumLineCount: 3
                    subtitle.wrapMode: Text.Wrap

                    Switch {
                        SlotsLayout.position: SlotsLayout.Trailing
                        checked: setup.steamEnabled
                        onClicked: setup.setSteamEnabled(checked)
                    }
                }
            }

            Label {
                width: parent.width
                visible: setup.steamInstalled
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: setup.steamStarting && !setup.steamSignedIn ? i18n.tr("Steam is starting and signing in (about a minute).")
                      : !setup.steamRunning ? i18n.tr("Steam is not running.")
                      : setup.steamSignedIn ? i18n.tr("Steam is running and signed in.")
                      : i18n.tr("Steam is running but not signed in.")
            }

            Button {
                visible: setup.steamInstalled && setup.steamRunning && !setup.steamSignedIn && !setup.steamStarting
                text: i18n.tr("Sign in")
                onClicked: {
                    if (setup.start(true))
                        page.pageStack.push(Qt.resolvedUrl("SetupPage.qml"))
                }
            }

            Label {
                width: parent.width
                visible: setup.lastError !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: setup.lastError
            }

            // Stage 4.9.
            ListItem {
                width: parent.width
                height: uninstallLayout.height
                divider.visible: false
                onClicked: page.pageStack.push(Qt.resolvedUrl("UninstallPage.qml"))

                ListItemLayout {
                    id: uninstallLayout

                    title.text: i18n.tr("Uninstall UTDeck")
                    subtitle.text: i18n.tr("Remove UTDeck from this phone; your games and saved games can stay")
                    subtitle.maximumLineCount: 2
                    subtitle.wrapMode: Text.Wrap

                    ProgressionSlot {}
                }
            }
        }
    }
}
