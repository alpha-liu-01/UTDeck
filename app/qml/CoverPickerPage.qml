import QtQuick
import Lomiri.Components
import Lomiri.Content

// A cover picture from another app (after stage 4.9): Content Hub's own picker lists the apps that
// can hand over a picture (Gallery, File Manager); the chosen app opens, the user picks one picture
// there, and Content Hub copies it into this app's cache (HubIncoming). Its path goes back to the
// cover chooser (CoverChooser.qml), and OwnGames copies it into the game's folder when the game is
// saved. Picking happens in the other app, by touch: the controller bridge works only while UTDeck
// is in front.
Page {
    id: page

    // The CoverChooser that opened this page.
    property var chooser
    property var transfer: null

    function done() {
        if (page.pageStack && page.pageStack.currentPage === page)
            page.pageStack.pop()
    }

    ContentPeerPicker {
        id: picker

        anchors.fill: parent
        headerText: i18n.tr("Choose a cover from")
        contentType: ContentType.Pictures
        handler: ContentHandler.Source

        onPeerSelected: {
            picker.peer.selectionType = ContentTransfer.Single
            page.transfer = picker.peer.request()
        }
        onCancelPressed: page.done()
    }

    // "Transfer in progress" while the other app is open.
    ContentTransferHint {
        anchors.fill: parent
        activeTransfer: page.transfer
    }

    Connections {
        target: page.transfer
        function onStateChanged() {
            if (page.transfer.state === ContentTransfer.Charged) {
                const items = page.transfer.items
                if (items.length > 0) {
                    const url = items[0].url.toString()
                    if (url.startsWith("file://"))
                        page.chooser.picked(decodeURIComponent(url.substring("file://".length)))
                }
                page.done()
            } else if (page.transfer.state === ContentTransfer.Aborted) {
                page.done()
            }
        }
    }
}
