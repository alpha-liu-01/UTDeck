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

    Component.onCompleted: page.firstStart = page.own && !library.isPrepared(page.appId)

    function fail(title, detail, signIn, endable) {
        page.failTitle = title
        page.failDetail = detail || ""
        page.failSignIn = !!signIn
        page.failEndable = !!endable
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
        if (library.play(page.appId))
            steamLaunch.expect(page.appId)
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

    function stepText() {
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
        if (page.own)
            return page.firstStart && library.runningGame !== page.appId
                   ? i18n.tr("The first start of a game prepares its own Windows folder (DirectX for Vulkan, .NET): about two minutes.")
                   : ""
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

            Label {
                visible: page.failTitle === ""
                text: i18n.tr("%1:%2").arg(Math.floor(page.elapsed / 60)).arg(("0" + page.elapsed % 60).slice(-2))
                color: theme.palette.normal.backgroundSecondaryText
            }

            Row {
                spacing: units.gu(2)

                Button {
                    visible: page.failTitle !== "" && !page.failSignIn && !page.failEndable
                    text: i18n.tr("Try again")
                    color: theme.palette.normal.positive
                    onClicked: page.retry()
                }

                Button {
                    visible: page.failEndable
                    text: i18n.tr("End the game")
                    color: theme.palette.normal.negative
                    onClicked: library.endGame()
                }

                Button {
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
