import QtQuick
import Lomiri.Components

// Stage 4.5: adding one of the user's own games (OwnGames). Two ways in: a game folder copied into
// Documents/UTDeck/Games, or an installer (an .exe in Downloads or Documents) run in the new game's
// Wine prefix, its window over the app, after which the program it installed is chosen from the list
// gamecheck.py --programs makes (the likely game first).
Page {
    id: page

    // "choose", "folder" (a copied folder), "installer" (before running it), "installing" (it runs),
    // "installed" (choose the installed program).
    property string step: "choose"
    property string folder: ""
    property string installer: ""
    property string installId: ""
    property bool installSeen: false
    property var programs: []
    property string chosenExe: ""
    property string cover: ""
    property string saveError: ""
    // Set at the first tap on "Run the installer": a second tap starts nothing more.
    property bool installStarting: false

    function choose(kind, path, name) {
        page.saveError = ""
        page.cover = ""
        page.chosenExe = ""
        nameField.text = name
        if (kind === "folder") {
            page.folder = path
            page.step = "folder"
            ownGames.check(path)
        } else {
            page.installer = path
            page.step = "installer"
        }
    }

    // A tidier default name from an installer's file name: "setup_the_game_1.2.exe" -> "the game",
    // "3DMark_11_v103_installer.exe" -> "3DMark 11" (only version-like numbers go).
    function installerName(file) {
        return file.replace(/\.exe$/i, "").replace(/[_]+/g, " ")
                   .replace(/\b(setup|installer|install|v\d+([.\-]\d+)*|\d+(\.\d+)+)\b/gi, "")
                   .replace(/\s+/g, " ").trim()
    }

    function chosenFolder() {
        return page.step === "installed" ? page.chosenExe.substring(0, page.chosenExe.lastIndexOf("/")) : page.folder
    }

    function save() {
        page.saveError = ""
        let id = ""
        if (page.step === "installed")
            id = ownGames.finishInstall(page.installId, nameField.text, page.chosenExe, page.cover) ? page.installId : ""
        else
            id = ownGames.add(nameField.text, page.folder, page.chosenExe, page.cover)
        if (id === "") {
            page.saveError = ownGames.error !== "" ? ownGames.error : i18n.tr("The game could not be saved.")
            return
        }
        library.refresh()
        page.pageStack.pop()
    }

    // B (Escape): as the header's Back does (a step back first, the page left from the first step).
    Keys.onEscapePressed: (event) => {
        if (page.step === "choose" || page.step === "installing") {
            event.accepted = false   // Main.qml leaves the page
            return
        }
        page.step = "choose"
        page.forceActiveFocus()   // the arrows go on from the top of the page
    }

    header: PageHeader {
        id: pageHeader

        title: i18n.tr("Add a game")
        leadingActionBar.actions: [
            Action {
                iconName: page.step === "choose" ? "back" : "previous"
                text: i18n.tr("Back")
                onTriggered: {
                    if (page.step === "choose" || page.step === "installing")
                        page.pageStack.pop()
                    else
                        page.step = "choose"
                }
            }
        ]
    }

    // The installer's run, followed through the library's running game (own-<id>).
    Connections {
        target: library
        function onRunningGameChanged() {
            if (page.step !== "installing")
                return
            if (library.runningGame === page.installId)
                page.installSeen = true
            else if (library.runningGame === "" && page.installSeen) {
                page.programs = ownGames.installedPrograms(page.installId)
                page.step = "installed"
            }
        }
    }

    // A chosen installed program: its folder is checked before saving.
    onChosenExeChanged: {
        if (page.step === "installed" && page.chosenExe !== "")
            ownGames.check(page.chosenFolder())
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

            // --- Choosing: an unfinished install, a folder, or an installer ---
            Column {
                width: parent.width
                spacing: units.gu(1)
                visible: page.step === "choose"

                Repeater {
                    model: page.step === "choose" ? ownGames.pending() : []

                    delegate: ListItem {
                        required property var modelData

                        width: column.width
                        height: pendingLayout.height
                        onClicked: {
                            page.installId = modelData.appId
                            nameField.text = modelData.name
                            page.programs = ownGames.installedPrograms(modelData.appId)
                            page.chosenExe = ""
                            page.cover = ""
                            page.step = "installed"
                        }

                        ListItemLayout {
                            id: pendingLayout

                            title.text: i18n.tr("Finish adding %1").arg(modelData.name)
                            subtitle.text: i18n.tr("Its installer ran; choose the program it installed")
                            ProgressionSlot {}
                        }
                    }
                }

                // Stage 4.7: a game of the user's Steam library, installed through hidden Steam.
                ListItem {
                    width: column.width
                    height: steamLayout.height
                    visible: setup.steamInstalled
                    onClicked: page.pageStack.push(Qt.resolvedUrl("SteamLibraryPage.qml"))

                    ListItemLayout {
                        id: steamLayout

                        title.text: i18n.tr("From your Steam library")
                        subtitle.text: i18n.tr("Install one of your Steam games")
                        ProgressionSlot {}
                    }
                }

                Label {
                    text: i18n.tr("A game folder")
                    textSize: Label.Large
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.backgroundSecondaryText
                    text: i18n.tr("Copy the game's folder from a computer into Documents/UTDeck/Games on the phone (over USB), then choose it here.")
                }

                Repeater {
                    model: page.step === "choose" ? ownGames.folders() : []

                    delegate: ListItem {
                        required property var modelData

                        width: column.width
                        height: folderLayout.height
                        enabled: !modelData.added
                        onClicked: page.choose("folder", modelData.path, modelData.name)

                        ListItemLayout {
                            id: folderLayout

                            title.text: modelData.name
                            subtitle.text: modelData.added ? i18n.tr("Already in the library") : ""
                            ProgressionSlot {}
                        }
                    }
                }

                Label {
                    text: i18n.tr("An installer")
                    textSize: Label.Large
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: theme.palette.normal.backgroundSecondaryText
                    text: i18n.tr("A game's setup program (.exe) in Downloads or Documents. It runs in the game's own Windows folder, over the app; then you choose the program it installed.")
                }

                Repeater {
                    model: page.step === "choose" ? ownGames.installers() : []

                    delegate: ListItem {
                        required property var modelData

                        width: column.width
                        height: installerLayout.height
                        onClicked: page.choose("installer", modelData.path, page.installerName(modelData.name))

                        ListItemLayout {
                            id: installerLayout

                            title.text: modelData.name
                            subtitle.text: i18n.tr("%1 MB").arg(modelData.mb)
                            ProgressionSlot {}
                        }
                    }
                }
            }

            // --- The name, for every way in ---
            Label {
                visible: page.step !== "choose" && page.step !== "installing"
                text: i18n.tr("Name")
            }

            TextField {
                id: nameField

                width: parent.width
                visible: page.step !== "choose" && page.step !== "installing"
            }

            // --- An installer, before and while it runs ---
            Label {
                width: parent.width
                visible: page.step === "installer"
                wrapMode: Text.Wrap
                text: i18n.tr("%1 will run in this game's own Windows folder. Its first start prepares that folder (about two minutes), then the installer's window comes over the app: install as on a computer (the default folder is fine).").arg(page.installer.substring(page.installer.lastIndexOf("/") + 1))
            }

            Button {
                visible: page.step === "installer"
                text: i18n.tr("Run the installer")
                color: theme.palette.normal.positive
                enabled: nameField.text.trim() !== "" && !page.installStarting
                onClicked: {
                    if (page.installStarting)
                        return
                    page.installStarting = true
                    const id = ownGames.startInstall(nameField.text, page.installer)
                    if (id === "") {
                        page.installStarting = false
                        page.saveError = ownGames.error
                        return
                    }
                    page.installId = id
                    page.installSeen = false
                    page.installStarting = false
                    page.step = "installing"
                }
            }

            Row {
                visible: page.step === "installing"
                spacing: units.gu(2)
                width: parent.width

                ActivityIndicator {
                    running: page.step === "installing"
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width - units.gu(6)
                    wrapMode: Text.Wrap
                    text: page.installSeen ? i18n.tr("The installer is running. When it ends, you choose the program it installed.")
                                           : i18n.tr("Preparing this game's Windows folder (about two minutes the first time)...")
                }
            }

            // --- The program: a folder's, after its check, or an installed one ---
            Label {
                visible: page.step === "folder" || page.step === "installed"
                text: i18n.tr("The game's program")
            }

            Row {
                visible: (page.step === "folder" || page.step === "installed") && ownGames.checking
                spacing: units.gu(2)

                ActivityIndicator {
                    running: ownGames.checking
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18n.tr("Looking at the game's programs...")
                }
            }

            Label {
                width: parent.width
                visible: page.step === "installed" && page.programs.length === 0
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: i18n.tr("The installer put no program into this game's Windows folder (it may have been closed or stopped early).")
            }

            Button {
                visible: page.step === "installed" && page.programs.length === 0
                text: i18n.tr("Run the installer again")
                color: theme.palette.normal.positive
                enabled: !page.installStarting
                onClicked: {
                    if (page.installStarting)
                        return
                    page.installStarting = true
                    const started = ownGames.rerunInstall(page.installId)
                    if (!started)
                        page.installStarting = false
                    if (started) {
                        page.installSeen = false
                        page.step = "installing"
                    } else {
                        page.saveError = i18n.tr("The installer could not be started again (is it still in Downloads?).")
                    }
                }
            }

            Repeater {
                model: page.step === "folder" ? ownGames.executables : page.step === "installed" ? page.programs : []

                delegate: ListItem {
                    id: exeItem

                    required property var modelData
                    // A folder's program relative to the folder (gamecheck.py's path), an installed
                    // one as its full path.
                    readonly property string value: modelData.path

                    width: column.width
                    height: exeLayout.height
                    color: page.chosenExe === value ? theme.palette.highlighted.background : "transparent"
                    onClicked: page.chosenExe = value

                    ListItemLayout {
                        id: exeLayout

                        title.text: page.step === "installed" ? modelData.name : exeItem.value
                        subtitle.text: (modelData.machine ? modelData.machine + ", " : "")
                                       + i18n.tr("%1 MB").arg(Math.max(1, Math.round(modelData.kb / 1024)))
                                       + (modelData.unlikely ? i18n.tr(" (likely not the game: a setup, uninstaller or helper)") : "")
                        Icon {
                            SlotsLayout.position: SlotsLayout.Trailing
                            width: units.gu(2.5)
                            height: width
                            name: "tick"
                            visible: page.chosenExe === exeItem.value
                        }
                    }
                }
            }

            Label {
                width: parent.width
                visible: page.step === "folder" && !ownGames.checking && ownGames.error !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: ownGames.error
            }

            // --- The cover (optional) ---
            Label {
                visible: (page.step === "folder" || page.step === "installed") && page.chosenExe !== ""
                text: i18n.tr("Cover (optional)")
            }

            CoverChooser {
                visible: (page.step === "folder" || page.step === "installed") && page.chosenExe !== ""
                folder: visible ? page.chosenFolder() : ""
                cover: page.cover
                stack: page.pageStack
                onChosen: (path) => page.cover = path
            }

            Label {
                width: parent.width
                visible: page.saveError !== ""
                wrapMode: Text.Wrap
                color: theme.palette.normal.negative
                text: page.saveError
            }

            Button {
                visible: page.step === "folder" || page.step === "installed"
                text: i18n.tr("Add to the library")
                color: theme.palette.normal.positive
                enabled: page.chosenExe !== "" && nameField.text.trim() !== "" && !ownGames.checking
                onClicked: page.save()
            }
        }
    }
}
