import QtQuick
import Lomiri.Components

// Stage 4.6: one game's settings (GameSettings, its utdeck-env file), for a Steam game (a long press
// on its card) or one of the user's own (from its page). Changes apply at the game's next start.
// Also the game's last run: its summary in words and its log.
Page {
    id: page

    property string appId
    property string title
    property var s: gameSettings.load(appId)
    property var run: ({})
    property bool showLog: false
    property string saved: ""
    property bool confirmUninstall: false
    // Stage 4.7: the game's Steam Cloud state (Steam's files, read when the page opens) and how the game
    // uses Steam Cloud ("auto", "api", "", or undefined while the library list loads).
    property var cloud: library.isOwn(appId) ? ({}) : steamGames.cloudState(appId)
    readonly property var cloudKind: {
        for (const g of steamGames.games)
            if (g.appId === page.appId)
                return g.cloud
        return undefined
    }

    function sizeText(kb) {
        return kb >= 1024 ? i18n.tr("%1 MB").arg((kb / 1024).toFixed(1)) : i18n.tr("%1 KB").arg(kb)
    }

    readonly property var resolutions: ["native", "1200x540", "1600x720", "1920x864", "2000x900"]
    readonly property var switchRows: [
        {id: "boost", title: i18n.tr("CPU boost"), subtitle: i18n.tr("Higher CPU clocks for the game: a few frames per second more, a little warmer.")},
        {id: "pause", title: i18n.tr("Pause when not in front"), subtitle: i18n.tr("Freezes the game while the phone is locked or another app is in front.")},
        {id: "awake", title: i18n.tr("Keep the screen on"), subtitle: i18n.tr("While the game is in front, the screen does not turn off by itself (when played with a controller).")},
        {id: "x87", title: i18n.tr("Fast x87 maths"), subtitle: i18n.tr("64-bit precision for old x86 maths: much faster; turn it off if the game draws wrongly.")},
        {id: "fill", title: i18n.tr("Fill the screen"), subtitle: i18n.tr("A game window smaller than the screen is stretched to fill it.")}
    ]

    function collect() {
        return {
            runtime: runtimeSelector.selectedIndex === 1 ? "proton" : "utdeck",
            args: argsField.text,
            resolution: page.resolutions[resolutionSelector.selectedIndex],
            translator: translatorSelector.selectedIndex === 1 ? "fex" : "box64",
            boost: switches.boost,
            pause: switches.pause,
            awake: switches.awake,
            x87: switches.x87,
            fill: switches.fill,
            advanced: advancedArea.text
        }
    }

    // Lomiri's OptionSelector resets its index when its model is set (the Dolphin port's stage 3.5):
    // the indexes are assigned once the page is complete.
    function apply(settings) {
        page.s = settings
        runtimeSelector.selectedIndex = settings.runtime === "proton" ? 1 : 0
        resolutionSelector.selectedIndex = Math.max(0, page.resolutions.indexOf(settings.resolution))
        translatorSelector.selectedIndex = settings.translator === "fex" ? 1 : 0
        switches.boost = !!settings.boost
        switches.pause = !!settings.pause
        switches.awake = !!settings.awake
        switches.x87 = !!settings.x87
        switches.fill = !!settings.fill
        argsField.text = settings.args || ""
        advancedArea.text = settings.advanced || ""
        // The switch rows made again: a tapped switch no longer follows its binding.
        switchRepeater.model = []
        switchRepeater.model = page.switchRows
    }

    Component.onCompleted: {
        apply(page.s)
        if (!library.isOwn(page.appId) && steamGames.games.length === 0)
            steamGames.refresh()
    }

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Settings: %1").arg(page.title)
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
                width: parent.width
                visible: library.runningGame === page.appId
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("The game is running: changes apply at its next start.")
            }

            // --- The Wine base ---
            OptionSelector {
                id: runtimeSelector

                text: i18n.tr("Wine base")
                model: [i18n.tr("Default (Wine 11.16)"), i18n.tr("Proton 11")]
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Proton 11's Wine helps some games the default does not. Each base keeps its own Windows folder for the game, so saved games stay with the base they were made in. The first start on the other base prepares that folder, and Steam may run the game's install step there again: a few minutes.")
            }

            Row {
                spacing: units.gu(1.5)
                visible: runtimeSelector.selectedIndex === 1 && !gameSettings.protonInstalled()

                Button {
                    text: i18n.tr("Download Proton 11")
                    enabled: !gameSettings.downloading
                    onClicked: gameSettings.downloadProton()
                }

                ActivityIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: gameSettings.downloading
                    visible: running
                }
            }

            Label {
                width: parent.width
                visible: runtimeSelector.selectedIndex === 1 && (!gameSettings.protonInstalled() || gameSettings.downloadResult !== "")
                wrapMode: Text.Wrap
                text: gameSettings.downloadResult !== "" ? gameSettings.downloadResult
                      : gameSettings.downloading ? i18n.tr("Downloading (about 1.1 GB once unpacked)...")
                      : i18n.tr("Proton 11 is not on this phone yet: download it before the game's next start (about 1.1 GB once unpacked).")
            }

            // --- Launch options ---
            Label {
                text: i18n.tr("Launch options")
            }

            TextField {
                id: argsField

                width: parent.width
                placeholderText: i18n.tr("for example -force-vulkan")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            // --- Resolution ---
            OptionSelector {
                id: resolutionSelector

                text: i18n.tr("Resolution")
                model: [i18n.tr("Native (the game's own choice)"), "1200x540", "1600x720", "1920x864", "2000x900"]
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Most games set their own resolution, in their options or their launcher, and this changes nothing for them. It is for a game that only takes the desktop's size: Wine then scales the smaller picture to the screen.")
            }

            // --- 32-bit code ---
            OptionSelector {
                id: translatorSelector

                text: i18n.tr("32-bit code")
                model: [i18n.tr("Box64 (default)"), i18n.tr("FEX")]
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Only for 32-bit games and programs: try FEX if one does not start or hangs (installers often need it).")
            }

            // --- Switches ---
            Repeater {
                id: switchRepeater

                model: page.switchRows

                delegate: ListItem {
                    required property var modelData

                    width: column.width
                    height: switchLayout.height
                    divider.visible: false

                    ListItemLayout {
                        id: switchLayout

                        title.text: modelData.title
                        subtitle.text: modelData.subtitle
                        subtitle.maximumLineCount: 3
                        subtitle.wrapMode: Text.Wrap

                        Switch {
                            SlotsLayout.position: SlotsLayout.Trailing
                            checked: switches[modelData.id]
                            onCheckedChanged: switches[modelData.id] = checked
                        }
                    }
                }
            }

            // --- Advanced ---
            Label {
                text: i18n.tr("Advanced")
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("More settings, one NAME=value per line (for example GAME_OVERRIDES=d3dx9_43=n or WINEDEBUG_GAME=+loaddll).")
            }

            TextArea {
                id: advancedArea

                width: parent.width
                height: units.gu(12)
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            Label {
                width: parent.width
                visible: page.saved !== ""
                wrapMode: Text.Wrap
                text: page.saved
            }

            Row {
                spacing: units.gu(1.5)

                Button {
                    text: i18n.tr("Save")
                    color: theme.palette.normal.positive
                    onClicked: {
                        if (gameSettings.save(page.appId, page.collect())) {
                            page.apply(gameSettings.load(page.appId))
                            page.saved = i18n.tr("Saved: it applies at the game's next start.")
                        } else {
                            page.saved = i18n.tr("The settings could not be saved.")
                        }
                    }
                }

                Button {
                    text: i18n.tr("Reset to the defaults")
                    onClicked: {
                        if (gameSettings.reset(page.appId)) {
                            page.apply(gameSettings.load(page.appId))
                            page.saved = i18n.tr("Back to the defaults (the Advanced lines too).")
                        }
                    }
                }
            }

            // --- Steam Cloud (stage 4.7) ---
            Label {
                visible: !library.isOwn(page.appId)
                text: i18n.tr("Steam Cloud")
                textSize: Label.Large
            }

            Label {
                width: parent.width
                visible: !library.isOwn(page.appId)
                wrapMode: Text.Wrap
                text: {
                    const c = page.cloud
                    if (c.known) {
                        let t = i18n.tr("%1 files in Steam Cloud (%2), last synced %3.").arg(c.files).arg(page.sizeText(c.sizeKb)).arg(c.lastSync)
                        if (c.inSync < c.files)
                            t += " " + i18n.tr("%1 of them wait for the next sync.").arg(c.files - c.inSync)
                        return t + " " + i18n.tr("Steam brings them before the game starts and saves the game's changes after it ends.")
                    }
                    if (page.cloudKind === undefined)
                        return i18n.tr("Reading Steam's data...")
                    if (page.cloudKind === "")
                        return i18n.tr("This game does not use Steam Cloud: its saves stay on the phone, in its Windows folder.")
                    return i18n.tr("Steam has not synced this game yet: it brings its cloud saves at the game's first start.")
                }
            }

            Label {
                width: parent.width
                visible: !library.isOwn(page.appId) && page.cloud.problem !== undefined && page.cloud.problem !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: page.cloud.problem === "conflict"
                      ? i18n.tr("The last sync found saves that differ on the phone and in the cloud: Steam asks which to keep at the game's next start, on the \"Steam is asking\" page.")
                      : page.cloud.problem === "path"
                        ? i18n.tr("At the last sync Steam could not find the game's save folder, so its saves did not sync.")
                        : i18n.tr("The last sync failed (%1): Steam tries again at the game's next start (no network?).").arg(page.cloud.result)
            }

            Label {
                width: parent.width
                visible: !library.isOwn(page.appId) && page.cloud.backups !== undefined && page.cloud.backups.length > 0
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("Copies of saves made on the phone were set aside on %1, when the cloud's came first (folder utdeck-saves-DATE in Steam/steamapps/compatdata/%2).").arg(page.cloud.backups ? page.cloud.backups.join(", ") : "").arg(page.appId)
            }

            // --- Checking the game's files (stage 4.7) ---
            Label {
                visible: !library.isOwn(page.appId)
                text: i18n.tr("The game's files")
                textSize: Label.Large
            }

            Label {
                width: parent.width
                visible: !library.isOwn(page.appId)
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("If the game crashes or will not start, Steam can compare its files with its own copy and download again what is missing or damaged. Saves and settings are not touched.")
            }

            Button {
                visible: !library.isOwn(page.appId)
                text: i18n.tr("Check the game's files")
                enabled: library.runningGame !== page.appId && steamGames.task.appId === undefined
                onClicked: {
                    if (steamGames.verify(page.appId, page.title))
                        page.pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
                }
            }

            // --- Uninstalling a Steam game (stage 4.7) ---
            Label {
                visible: !library.isOwn(page.appId)
                text: i18n.tr("Uninstall")
                textSize: Label.Large
            }

            Label {
                width: parent.width
                visible: !library.isOwn(page.appId)
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
                text: i18n.tr("%1 MB on the phone. It stays in your Steam library, to install again later.").arg(steamGames.installedMb(page.appId))
            }

            Button {
                visible: !library.isOwn(page.appId) && !page.confirmUninstall
                text: i18n.tr("Uninstall...")
                color: theme.palette.normal.negative
                enabled: library.runningGame !== page.appId && steamGames.task.appId === undefined
                onClicked: page.confirmUninstall = true
            }

            Column {
                width: parent.width
                spacing: units.gu(1.5)
                visible: page.confirmUninstall

                Row {
                    spacing: units.gu(1.5)

                    CheckBox {
                        id: deleteData
                    }

                    Label {
                        width: column.width - units.gu(5)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        text: i18n.tr("Also delete its Windows folder, with any saved games not in Steam Cloud, and the files the game left in its install folder (Steam keeps both otherwise)")
                    }
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: i18n.tr("Steam then asks once more, on the \"Steam is asking\" page: tap Uninstall there.")
                }

                Row {
                    spacing: units.gu(1.5)

                    Button {
                        text: i18n.tr("Uninstall")
                        color: theme.palette.normal.negative
                        onClicked: {
                            // On to the Downloads page, which follows the uninstall (Steam's question
                            // comes over it).
                            if (steamGames.uninstall(page.appId, page.title, deleteData.checked)) {
                                const stack = page.pageStack
                                stack.pop()
                                stack.push(Qt.resolvedUrl("DownloadsPage.qml"))
                            }
                        }
                    }

                    Button {
                        text: i18n.tr("Cancel")
                        onClicked: {
                            deleteData.checked = false
                            page.confirmUninstall = false
                        }
                    }
                }
            }

            // --- The last run ---
            Label {
                text: i18n.tr("The last run")
                textSize: Label.Large
            }

            Button {
                text: page.showLog ? i18n.tr("Hide the log") : i18n.tr("Show the last run")
                onClicked: {
                    page.run = gameSettings.lastRun(page.appId)
                    page.showLog = !page.showLog
                }
            }

            Label {
                width: parent.width
                visible: page.showLog
                wrapMode: Text.Wrap
                text: page.run.summary ? page.run.summary : i18n.tr("The game has not run yet.")
            }

            TextArea {
                width: parent.width
                height: units.gu(40)
                visible: page.showLog && !!page.run.log
                readOnly: true
                text: page.run.log || ""
                font.family: "Ubuntu Mono"
                font.pixelSize: units.gu(1.4)
            }

            Label {
                visible: page.showLog && !!page.run.compat
                text: i18n.tr("Steam's start of the game (utdeck-compat.log)")
            }

            TextArea {
                width: parent.width
                height: units.gu(25)
                visible: page.showLog && !!page.run.compat
                readOnly: true
                text: page.run.compat || ""
                font.family: "Ubuntu Mono"
                font.pixelSize: units.gu(1.4)
            }
        }
    }

    // The switches' states, kept apart from the delegates (a Repeater's items are not addressable by
    // id); set by apply().
    QtObject {
        id: switches

        property bool boost
        property bool pause
        property bool awake
        property bool x87
        property bool fill
    }
}
