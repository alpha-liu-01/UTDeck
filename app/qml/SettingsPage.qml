import QtQuick
import Lomiri.Components

// UTDeck's settings (stage 4.2): Steam, which is optional. Not set up: set it up from here. Set up:
// its switch (on, hidden Steam runs while the app does; off, only while a Steam game runs, saving
// about 1.3 GB of memory) and its state. Stage 4.4: the Controllers page. Stage 5.1: the performance
// overlay for all games (each game's settings page can choose its own) and where it sits, saved to
// wine/user.env at once. Stage 5.2: the frame rate limit for all games, saved the same way. Stage 5.3:
// the switch for Steam's memory during games, saved the same way.
Page {
    id: page

    readonly property var overlayLevels: ["off", "fps", "frametime", "full"]
    readonly property var overlayPositions: ["top-left", "top-center", "top-right"]
    readonly property var fpsLimits: ["off", "28", "30", "40", "45", "48", "60"]
    readonly property var scales: ["fit", "stretch", "center"]
    // Set once the selectors show the saved values: a change before that is the page filling them in.
    property bool overlayReady: false

    function saveOverlay() {
        if (!page.overlayReady)
            return
        gameSettings.saveGlobal({
            overlay: page.overlayLevels[overlaySelector.selectedIndex],
            overlayPosition: page.overlayPositions[positionSelector.selectedIndex],
            fpsLimit: page.fpsLimits[fpsLimitSelector.selectedIndex],
            scale: page.scales[scaleSelector.selectedIndex],
            steamReclaim: steamReclaimSwitch.checked
        })
    }

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Settings")
    }

    Component.onCompleted: {
        setup.check()
        // Lomiri's OptionSelector resets its index when its model is set: the indexes once complete.
        const g = gameSettings.loadGlobal()
        overlaySelector.selectedIndex = Math.max(0, page.overlayLevels.indexOf(g.overlay))
        positionSelector.selectedIndex = Math.max(0, page.overlayPositions.indexOf(g.overlayPosition))
        fpsLimitSelector.selectedIndex = Math.max(0, page.fpsLimits.indexOf(g.fpsLimit))
        steamReclaimSwitch.checked = g.steamReclaim
        scaleSelector.selectedIndex = Math.max(0, page.scales.indexOf(g.scale))
        page.overlayReady = true
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
                text: i18n.tr("Performance overlay")
                textSize: Label.Large
            }

            OptionSelector {
                id: overlaySelector

                text: i18n.tr("For all games")
                model: [i18n.tr("Off"), i18n.tr("Frame rate"), i18n.tr("Frame rate and frame time"), i18n.tr("Full")]
                onSelectedIndexChanged: page.saveOverlay()
            }

            OptionSelector {
                id: positionSelector

                text: i18n.tr("Where it sits")
                model: [i18n.tr("Top left"), i18n.tr("Top center"), i18n.tr("Top right")]
                onSelectedIndexChanged: page.saveOverlay()
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Numbers over the game's picture, from the game's next start. A game's own settings page can choose another level. Frame rate costs about 0.5 ms for each picture, Full about 1 ms; Off costs nothing.")
            }

            Label {
                text: i18n.tr("Frame rate limit")
                textSize: Label.Large
            }

            OptionSelector {
                id: fpsLimitSelector

                text: i18n.tr("For all games")
                model: [i18n.tr("Off"), i18n.tr("%1 fps").arg(28), i18n.tr("%1 fps").arg(30), i18n.tr("%1 fps").arg(40),
                        i18n.tr("%1 fps").arg(45), i18n.tr("%1 fps").arg(48), i18n.tr("%1 fps").arg(60)]
                onSelectedIndexChanged: page.saveOverlay()
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Games draw no more pictures a second than this, from their next start: steadier frame times, a cooler phone, a longer battery. 30 and 60 match the screen's 60 Hz exactly. A game's own settings page can choose another limit.")
            }

            Label {
                text: i18n.tr("Picture scaling")
                textSize: Label.Large
            }

            OptionSelector {
                id: scaleSelector

                text: i18n.tr("For all games")
                model: [i18n.tr("Fit"), i18n.tr("Stretch"), i18n.tr("Centered (1:1)")]
                onSelectedIndexChanged: page.saveOverlay()
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("For a game whose picture is smaller than the screen: Fit makes it as large as possible and keeps its shape, Stretch fills the whole screen, Centered shows it pixel for pixel. From the game's next start, on the default Wine base. A game's own settings page can choose another.")
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

            // Set up: Steam's memory during a game (stage 5.3).
            ListItem {
                width: parent.width
                height: reclaimLayout.height
                visible: setup.steamInstalled
                divider.visible: false

                ListItemLayout {
                    id: reclaimLayout

                    title.text: i18n.tr("Free Steam's memory during games")
                    subtitle.text: i18n.tr("Once a game is up, Steam's own memory is moved out of the way: about 350 MB more for the game. Steam takes back what it needs by itself (cloud saves still work).")
                    subtitle.maximumLineCount: 4
                    subtitle.wrapMode: Text.Wrap

                    Switch {
                        id: steamReclaimSwitch

                        SlotsLayout.position: SlotsLayout.Trailing
                        onCheckedChanged: page.saveOverlay()
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
