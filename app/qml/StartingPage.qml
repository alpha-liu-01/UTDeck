import QtQuick
import Lomiri.Components

// Stage 4.3, the starting page: from the tap on a game until its window is in front, what is
// happening, in words: Steam starting and signing in, then Steam's own steps for this launch
// (SteamLaunch, from Steam's log), and how long it has taken. A question from Steam opens the dialog
// page on top of this one; the game's window in front hides the app (Main.qml); when the game ends,
// this page goes and the library is back. "Back to the library" leaves the page; the game goes on
// starting. When the launch fails, the page says so in words (item 4): the game stopped right after
// starting (with what its log shows), Steam's game process ended without UTDeck's runtime running it,
// Steam did not start the game, or Steam did not sign in; with "Try again".
Page {
    id: page

    property string appId
    property string title
    property url cover
    property double startedAt: Date.now()
    property int elapsed: 0
    property bool gameSeen: false
    property bool frontSeen: false
    property string failTitle: ""
    property string failDetail: ""
    property bool failSignIn: false
    property bool failEndable: false
    property double completedAt: 0
    // One of the user's own games (stage 4.5): no Steam steps; its first start prepares its Wine folder.
    readonly property bool own: library.isOwn(appId)
    property bool firstStart: false

    // Stage 4.7: the screen stays on while the start goes on (the game's "Keep the screen on" setting):
    // from the tap until the game runs, Steam's start and its dialogs included, a start can take
    // minutes, and the screen's timeout turned it off meanwhile. Released once the game's window has
    // been in front (game-run.sh's own keep-awake follows the game from then on, and lets the timeout
    // work while another app is in front), when this page goes, or when it shows an error.
    readonly property bool keepScreen: page.failTitle === "" && !page.frontSeen && !!gameSettings.load(page.appId).awake
    onKeepScreenChanged: page.keepScreen ? keepAwake.hold("starting") : keepAwake.release("starting")

    Component.onCompleted: {
        page.firstStart = page.own && !library.isPrepared(page.appId)
        if (page.keepScreen)
            keepAwake.hold("starting")
    }
    Component.onDestruction: keepAwake.release("starting")

    function fail(title, detail, signIn, endable) {
        page.failTitle = title
        page.failDetail = detail || ""
        page.failSignIn = !!signIn
        page.failEndable = !!endable
    }

    // Stage 4.8: a failed start puts the focus on what can be done about it (A presses it; B goes back
    // to the library, as "Back to the library").
    onFailTitleChanged: {
        if (page.failTitle === "" || !controllers.controllerUsed)
            return
        Qt.callLater(() => {
            for (const b of [retryButton, endButton, settingsButton])
                if (b.visible) {
                    b.forceActiveFocus(Qt.TabFocusReason)
                    return
                }
        })
    }

    function retry() {
        page.failTitle = ""
        page.failDetail = ""
        page.failSignIn = false
        page.failEndable = false
        page.gameSeen = false
        page.frontSeen = false
        page.completedAt = 0
        page.startedAt = Date.now()
        if (library.play(page.appId)) {
            setup.gameStarting()
            steamLaunch.expect(page.appId)
        }
    }

    // The checks for a launch that went wrong, once a second (not while Steam waits on a dialog).
    function checkFailure() {
        if (page.failTitle !== "" || steamLaunch.waitingForUser)
            return
        // The game runs, but its log already shows it cannot go on (it may spin instead of quitting).
        if (library.runningGame === page.appId && library.runningGameProblem !== "") {
            page.fail(i18n.tr("The game could not start."), library.runningGameProblem, false, true)
            return
        }
        if (page.gameSeen)
            return
        // UTDeck's tool still prepares Wine for the game: not a failed launch, and the 45 s wait for
        // the game after Steam's "Completed" starts over when it is done.
        if (page.preparing !== "") {
            page.completedAt = 0
            return
        }
        const waited = (Date.now() - page.startedAt) / 1000
        if (page.own) {
            // No Steam: own-run.sh prepares the prefix (about two minutes the first time), then starts it.
            if (waited > 240)
                page.fail(i18n.tr("The game did not start."),
                          i18n.tr("Its start script ran for four minutes without the game appearing. What it did is in utdeck-probe/wine/own/%1/own-run.log.").arg(page.appId.substring(4)))
            return
        }
        const ours = steamLaunch.expectedAppId === page.appId
        if (ours && steamLaunch.task === "Cancelled") {
            page.fail(i18n.tr("The game was not started."),
                      i18n.tr("Steam's question was left unanswered; Steam starts the game only after it."))
        } else if (ours && steamLaunch.gameProcessRemoved) {
            page.fail(i18n.tr("The game ended before it started."),
                      i18n.tr("Steam started it and it ended at once, without UTDeck's runtime running it. Steam may be set to another compatibility tool for this game."))
        } else if (ours && steamLaunch.task === "Completed") {
            if (page.completedAt === 0)
                page.completedAt = Date.now()
            else if (Date.now() - page.completedAt > 45000)
                page.fail(i18n.tr("Steam did not start the game."),
                          i18n.tr("Steam reported the game as started, but no game appeared within 45 seconds."))
        } else if (!setup.steamRunning && waited > 150) {
            page.fail(i18n.tr("Steam did not start."), i18n.tr("Hidden Steam is not running after two and a half minutes."))
        } else if (setup.steamRunning && !setup.steamSignedIn && waited > 180) {
            page.fail(i18n.tr("Steam did not sign in."),
                      i18n.tr("Steam is running but not signed in after three minutes. Sign in again from the settings."), true)
        }
    }

    // Stage 4.7: this game's download, if Steam updates it before the start (the Downloads page's data).
    readonly property var update: {
        for (const d of steamGames.downloads)
            if (d.appId === page.appId)
                return d
        return null
    }

    // Stage 4.8: what UTDeck's tool prepares before the game can start (its first start: minutes).
    property string preparing: ""

    function stepText() {
        if (page.preparing !== "" && library.runningGame !== page.appId)
            return i18n.tr("Preparing Wine for this game: %1...").arg(page.preparing)
        if (library.runningGame === page.appId)
            return i18n.tr("Starting the game...")
        if (page.own)
            return page.firstStart ? i18n.tr("Preparing Wine for this game...") : i18n.tr("Starting the game...")
        if (steamLaunch.expectedAppId === page.appId && steamLaunch.task !== "") {
            switch (steamLaunch.task) {
            case "ProcessingInstallScript":
                return i18n.tr("Installing what the game needs...")
            case "SynchronizingCloud":
            case "SynchronizingStats":
                return i18n.tr("Synchronizing your saves with Steam Cloud...")
            case "ShowInterstitials":
                return i18n.tr("Steam is checking for notices...")
            case "ShowEula":
                return i18n.tr("Waiting for your answer to the agreement...")
            // Stage 4.7: Steam brings the game up to date before it starts (about 10 s when there is
            // nothing new), or checks its files.
            case "DownloadingDepots":
                if (!page.update)
                    return i18n.tr("Steam is checking for an update...")
                return page.update.percent >= 0 ? i18n.tr("Updating the game before it starts: %1 %").arg(page.update.percent)
                                                : i18n.tr("Updating the game before it starts...")
            case "VerifyingFiles":
                return i18n.tr("Steam is checking the game's files...")
            case "AgreementAccepted":
                return i18n.tr("Agreement accepted, starting the game again...")
            case "CreatingProcess":
            case "WaitingGameWindow":
            case "Completed":
                return i18n.tr("Starting the game...")
            }
            return i18n.tr("Steam is preparing the game...")
        }
        if (!setup.steamRunning)
            return i18n.tr("Starting Steam...")
        if (!setup.steamSignedIn)
            return i18n.tr("Steam is signing in...")
        return i18n.tr("Asking Steam to start the game...")
    }

    function hintText() {
        if (page.preparing !== "" && library.runningGame !== page.appId)
            return i18n.tr("A game's first start prepares its own Windows folder (DirectX for Vulkan, .NET): a few minutes. Later starts skip this.")
        if (page.own)
            return page.firstStart && library.runningGame !== page.appId
                   ? i18n.tr("The first start of a game prepares its own Windows folder (DirectX for Vulkan, .NET): about two minutes.")
                   : ""
        if (steamLaunch.expectedAppId === page.appId && steamLaunch.task === "DownloadingDepots" && page.update)
            return i18n.tr("Steam updates a game before starting it. With Steam's switch off, Steam runs only around a game, so updates come at its start.")
        if (steamLaunch.expectedAppId === page.appId && steamLaunch.task === "ProcessingInstallScript")
            return i18n.tr("The first start of a game can take several minutes: Windows parts such as Visual C++ and DirectX are installed for it.")
        if (!setup.steamRunning || !setup.steamSignedIn)
            return i18n.tr("Steam starts in the background and signs itself in; this takes about a minute.")
        return ""
    }

    header: PageHeader {
        id: pageHeader

        title: page.title
    }

    // Steam's state (running, signed in) is read again while the page is open.
    Timer {
        interval: 5000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: setup.check()
    }

    Timer {
        interval: 1000
        repeat: true
        running: page.failTitle === ""
        onTriggered: {
            page.elapsed = Math.round((Date.now() - page.startedAt) / 1000)
            page.preparing = page.own ? "" : library.preparing(page.appId)
            page.checkFailure()
        }
    }

    // The game came and went: back to the library, unless it stopped right after starting.
    Connections {
        target: library
        function onRunningGameChanged() {
            if (library.runningGame === page.appId) {
                page.gameSeen = true
            } else if (library.runningGame === "" && page.failTitle !== "") {
                page.failEndable = false   // ended (by "End the game" or by itself): the error stays
            } else if (library.runningGame === "" && page.gameSeen) {
                if (library.gameClosedFromOutside)
                    return   // Main.qml quits the app
                const quick = library.lastGameSeconds < 20 && library.lastGameExit !== 0
                if (!page.frontSeen || quick) {
                    let detail = library.lastGameProblem
                    if (library.lastGameExit > 0)
                        detail = (detail ? detail + " " : "") + i18n.tr("(Exit code %1 after %2 s.)").arg(library.lastGameExit).arg(library.lastGameSeconds)
                    page.gameSeen = false
                    page.fail(i18n.tr("The game stopped right after starting."), detail)
                } else {
                    page.pageStack.pop()
                }
            }
        }
        function onGameInFrontChanged() {
            if (library.gameInFront && library.runningGame === page.appId)
                page.frontSeen = true
        }
    }

    Row {
        anchors {
            top: pageHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            margins: units.gu(3)
        }
        spacing: units.gu(4)

        LomiriShape {
            id: coverShape

            height: parent.height
            width: height / 1.5
            aspect: LomiriShape.DropShadow
            sourceFillMode: LomiriShape.PreserveAspectCrop
            source: Image {
                source: page.cover
                asynchronous: true
            }
        }

        Column {
            width: parent.width - coverShape.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: units.gu(2)

            Row {
                spacing: units.gu(2)
                width: parent.width

                ActivityIndicator {
                    running: page.failTitle === ""
                    visible: running
                    anchors.verticalCenter: parent.verticalCenter
                }

                Icon {
                    visible: page.failTitle !== ""
                    name: "dialog-warning-symbolic"
                    color: theme.palette.normal.negative
                    width: units.gu(3)
                    height: width
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - units.gu(5)
                    text: page.failTitle !== "" ? page.failTitle : page.stepText()
                    textSize: Label.Large
                    wrapMode: Text.Wrap
                }
            }

            Label {
                width: parent.width
                visible: text !== ""
                text: page.failTitle !== "" ? page.failDetail : page.hintText()
                wrapMode: Text.Wrap
                color: theme.palette.normal.backgroundSecondaryText
            }

            // Stage 4.7: an update's progress.
            ProgressBar {
                width: parent.width
                visible: page.failTitle === "" && page.update !== null
                indeterminate: !page.update || page.update.percent < 0
                minimumValue: 0
                maximumValue: 100
                value: page.update ? Math.max(0, page.update.percent) : 0
            }

            Label {
                visible: page.failTitle === ""
                text: i18n.tr("%1:%2").arg(Math.floor(page.elapsed / 60)).arg(("0" + page.elapsed % 60).slice(-2))
                color: theme.palette.normal.backgroundSecondaryText
            }

            Row {
                spacing: units.gu(2)

                Button {
                    id: retryButton

                    visible: page.failTitle !== "" && !page.failSignIn && !page.failEndable
                    text: i18n.tr("Try again")
                    color: theme.palette.normal.positive
                    onClicked: page.retry()
                }

                Button {
                    id: endButton

                    visible: page.failEndable
                    text: i18n.tr("End the game")
                    color: theme.palette.normal.negative
                    onClicked: library.endGame()
                }

                Button {
                    id: settingsButton

                    visible: page.failSignIn
                    text: i18n.tr("Settings")
                    color: theme.palette.normal.positive
                    onClicked: page.pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }

                Button {
                    text: i18n.tr("Back to the library")
                    onClicked: page.pageStack.pop()
                }
            }
        }
    }
}
