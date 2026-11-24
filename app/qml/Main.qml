import QtQuick
import QtQuick.Window
import Lomiri.Components

// UTDeck's window: the library and, in later substages, the other pages, in Lomiri's native look.
// Games run in their own windows, started by hidden Steam (stage 3.S). Their windows carry this
// app's id (Wine patch 0016, UTDECK_APP_ID), so Lomiri shows them as part of this app.
//
// While a game's window is in front, this window hides (stage 4.1): Lomiri gives every window its
// own card in the app switcher, and it makes a window fullscreen only when no other window of the
// app shows (the Dolphin port's stage 3.2). It shows again as soon as the game's process ends
// (game-run.sh removes its file), before Lomiri would close an app left with no window of its own.
Window {
    id: window

    // Lomiri gives the app's next window the state its last window closed in, so after a fullscreen
    // game the library is told "fullscreen" too (the Dolphin port's stage 5). It asks for a normal
    // window instead, at most three times each time it comes back.
    property int normalRequests: 0

    function follow() {
        // Steam waits for the user (a dialog before a game starts, stage 4.3), or the running game's
        // log shows it cannot go on: the app shows itself, even over the game's window.
        if ((steamLaunch.waitingForUser || library.runningGameProblem !== "") && !visible) {
            normalRequests = 0
            show()
            raise()
            requestActivate()
            return
        }
        if (library.runningGame !== "" && library.gameInFront && library.runningGameProblem === "") {
            if (visible)
                hide()
        } else if (library.runningGame === "" && library.gameClosedFromOutside) {
            // The user swiped the game's card (UTDeck's) away: Lomiri is closing this app, so it
            // quits instead of showing its library again as a stray window (stage 4.3, stone).
            Qt.quit()
        } else if (library.runningGame === "" && !visible) {
            normalRequests = 0
            show()
            raise()
            requestActivate()
        }
    }

    // Stage 4.8: focus with a controller or a keyboard.
    function inPage(item, page) {
        for (let p = item; p; p = p.parent)
            if (p === page)
                return true
        return false
    }

    // A visible control inside item that takes the focus itself (a switch in a list row).
    function hasFocusable(item) {
        for (let i = 0; i < item.children.length; ++i) {
            const c = item.children[i]
            if (c.visible && (c.activeFocusOnTab || hasFocusable(c)))
                return true
        }
        return false
    }

    // Where the arrows stop: a visible, enabled control of the page, below its header; not the control
    // the focus is already inside (a text field, whose inner text input has the focus); not a list row
    // holding its own control (a switch row: the switch is the stop, one press of A toggles it).
    function stopsAt(item, page, from) {
        if (!item || item === page || !inPage(item, page) || !item.visible || !item.enabled)
            return false
        if (from && inPage(from, item))
            return false
        // The header's own actions (back, the page's icons): B and the buttons do their work.
        if (page.header && inPage(item, page.header))
            return false
        if (item.leadingActions !== undefined && hasFocusable(item))
            return false
        // A read-only text (a game's log): its cursor would take the arrows line by line; the right
        // stick scrolls it.
        if (item.readOnly === true)
            return false
        return true
    }

    // Lomiri opens the on-screen keyboard whenever a text field gets the focus (its InputHandler);
    // passing through a field with the arrows closes it again, and A on the field opens it.
    function focusTo(item, reason) {
        item.forceActiveFocus(reason)
        const now = window.activeFocusItem
        if (now && now.cursorPosition !== undefined)
            Qt.callLater(() => Qt.inputMethod.hide())
    }

    function focusPage() {
        const page = pageStack.currentPage
        if (!page)
            return
        if (typeof page.focusFirst === "function") {
            page.focusFirst()
            return
        }
        page.forceActiveFocus()
        // The page's first control, with the focus frame once a controller has been used.
        let first = page
        for (let i = 0; i < 200; ++i) {
            first = first.nextItemInFocusChain(true)
            if (!first || first === page)
                return
            if (stopsAt(first, page, null))
                break
        }
        if (stopsAt(first, page, null))
            focusTo(first, controllers.controllerUsed ? Qt.TabFocusReason : Qt.OtherFocusReason)
    }

    // The focus to the previous or next control of the page in Qt's tab order (Lomiri's controls take
    // part in it); false if there is none.
    function moveFocus(forward) {
        const page = pageStack.currentPage
        const from = window.activeFocusItem
        if (!page || !from)
            return false
        let next = from
        for (let i = 0; i < 300; ++i) {
            next = next.nextItemInFocusChain(forward)
            if (!next || next === from)
                return false
            if (stopsAt(next, page, from))
                break
        }
        if (!stopsAt(next, page, from))
            return false
        // Back past the start of the page (the chain wraps around): stay.
        const a = from.mapToItem(page, 0, 0), b = next.mapToItem(page, 0, 0)
        if (forward ? b.y < a.y - 1 : b.y > a.y + 1)
            return false
        focusTo(next, forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        ensureVisible(next)
        return true
    }

    // LB / RB: the focus about a screen up or down.
    function moveFocusPage(forward) {
        const page = pageStack.currentPage
        if (!page || !window.activeFocusItem)
            return
        const start = window.activeFocusItem.mapToItem(page, 0, 0).y
        const target = page.height * 0.7
        for (let i = 0; i < 60; ++i) {
            if (!moveFocus(forward))
                return
            const item = window.activeFocusItem
            if (Math.abs(item.mapToItem(page, 0, 0).y - start) >= target)
                return
        }
    }

    // The focused control scrolled into view inside the page's Flickable (its settings, lists).
    function ensureVisible(item) {
        let flick = item.parent
        while (flick && !(flick.contentY !== undefined && flick.contentHeight !== undefined && flick.flickableDirection !== undefined))
            flick = flick.parent
        if (!flick || !flick.contentItem)
            return
        const pos = item.mapToItem(flick.contentItem, 0, 0)
        const margin = units.gu(2)
        if (pos.y < flick.contentY + margin)
            flick.contentY = Math.max(0, pos.y - margin)
        else if (pos.y + item.height > flick.contentY + flick.height - margin)
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), pos.y + item.height - flick.height + margin)
    }

    width: units.gu(100)
    height: units.gu(56)
    visible: true
    title: "UTDeck"
    color: theme.palette.normal.background

    onVisibilityChanged: (visibility) => {
        if (visibility === Window.FullScreen && library.runningGame === "" && normalRequests < 3) {
            normalRequests++
            showNormal()
        }
    }

    Connections {
        target: library
        function onRunningGameChanged() { window.follow() }
        function onGameInFrontChanged() { window.follow() }
    }

    // Stage 4.8: the controllers' buttons become key presses while the app is in front and no game is
    // (the window hides while a game is in front; the app is not active with the screen off).
    Binding {
        target: controllers
        property: "navigation"
        value: window.active && Qt.application.state === Qt.ApplicationActive
    }

    MainView {
        id: mainView

        anchors.fill: parent
        // Stage 4.8: the keys no control took (a controller's buttons arrive as keys, Controllers):
        // - Escape (B): back one page;
        // - the arrows: the focus to the previous or next control of the page (Lomiri draws its focus
        //   frame), scrolled into view; a control that uses an arrow itself (a list, a text field's
        //   cursor) takes it first;
        // - Return (A): presses the focused control (a button, a switch, a list row); on a text field,
        //   opens the on-screen keyboard (typing is by touch or a keyboard until stage 6);
        // - Page Up / Down (LB / RB): the focus about a screen up or down.
        Keys.onPressed: (event) => {
            const item = window.activeFocusItem
            if (event.key === Qt.Key_Escape) {
                // The on-screen keyboard first, then the page.
                if (Qt.inputMethod.visible) {
                    Qt.inputMethod.hide()
                    event.accepted = true
                } else if (pageStack.depth > 1) {
                    pageStack.pop()
                    event.accepted = true
                }
            } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) {
                window.moveFocus(true)
                event.accepted = true
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) {
                window.moveFocus(false)
                event.accepted = true
            } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                window.moveFocusPage(event.key === Qt.Key_PageDown)
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && item) {
                // Lomiri's buttons, switches and list rows press themselves when Return is released
                // (UCAbstractButton and UCListItem's keyReleaseEvent): pressing them here too made a
                // switch toggle twice, so it never moved.
                if (item.sensingMargins !== undefined || item.leadingActions !== undefined)
                    return
                if (item.cursorPosition !== undefined && !item.readOnly) {
                    Qt.inputMethod.show()
                    event.accepted = true
                    return
                }
                if (typeof item.trigger === "function")
                    item.trigger()
                else if (typeof item.clicked === "function")
                    item.clicked()
                else
                    return
                event.accepted = true
            }
        }
        // The click package (or "utdeck" outside Lomiri), set in main.cpp from APP_ID.
        applicationName: Qt.application.name

        PageStack {
            id: pageStack

            // The focus goes to each page that comes to the front: the library no longer takes the
            // buttons while another page is shown over it.
            onCurrentPageChanged: Qt.callLater(window.focusPage)

            // A phone not set up yet starts with the setup (stage 4.2), the library once it is done.
            Component.onCompleted: push(Qt.resolvedUrl(setup.needed ? "SetupPage.qml" : "LibraryPage.qml"))
        }

        // Steam waits for an answer before starting a game: its dialog, on a page of its own.
        Connections {
            target: steamLaunch
            function onChanged() {
                const onTop = pageStack.currentPage && pageStack.currentPage.objectName === "dialogPage"
                if (steamLaunch.waitingForUser && !onTop) {
                    pageStack.push(Qt.resolvedUrl("DialogPage.qml"), {objectName: "dialogPage"})
                    window.follow()
                } else if (!steamLaunch.waitingForUser && onTop) {
                    pageStack.pop()
                }
            }
            // An agreement answered in Steam's window: the waiting launch never goes on, so the
            // game is started again (Steam then runs past the agreement).
            function onAgreementAccepted(appId) {
                agreementRestart.appId = appId
                agreementRestart.restart()
            }
        }

        // Two seconds later: Steam takes a moment to save the answer.
        Timer {
            id: agreementRestart

            property string appId

            interval: 2000
            onTriggered: {
                if (library.play(appId)) {
                    setup.gameStarting()
                    steamLaunch.expect(appId)
                }
            }
        }

        // Stage 4.7: an install or uninstall ended: the library read again; with Steam's switch off,
        // Steam (started for it) stops again once no download runs (Setup waits for them).
        Connections {
            target: steamGames
            function onInstalledChanged() {
                library.refresh()
                setup.gameEnded()
            }
        }

        // A game ended: with Steam's switch off, Steam (started for the game) stops again, 45 s later
        // unless another game starts meanwhile (stage 4.6).
        Connections {
            target: library
            function onRunningGameChanged() {
                if (library.runningGame === "")
                    setup.gameEnded()
                else
                    setup.gameStarting()
            }
        }

        Connections {
            target: setup
            function onFinished() {
                pageStack.clear()
                pageStack.push(Qt.resolvedUrl("LibraryPage.qml"))
                library.refresh()
            }
        }
    }
}
