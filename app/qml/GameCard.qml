import QtQuick
import Lomiri.Components

// One game in the library: Steam's portrait cover (library_600x900), or its title on a plain card
// when Steam has none, and the title below.
AbstractButton {
    id: card

    LomiriShape {
        id: cover

        anchors {
            top: parent.top
            topMargin: units.gu(0.5)
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width - units.gu(1.5)
        height: width * 1.5
        aspect: LomiriShape.DropShadow
        backgroundColor: theme.palette.normal.base
        sourceFillMode: LomiriShape.PreserveAspectCrop
        source: model.cover !== "" ? coverImage : null

        Image {
            id: coverImage

            visible: false
            source: model.cover
            asynchronous: true
            sourceSize.width: cover.width
        }

        Label {
            anchors {
                fill: parent
                margins: units.gu(1)
            }
            visible: model.cover === "" || coverImage.status === Image.Error
            text: model.title
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: theme.palette.normal.baseText
        }
    }

    Label {
        anchors {
            top: cover.bottom
            topMargin: units.gu(0.5)
            left: cover.left
            right: cover.right
        }
        text: model.title
        textSize: Label.Small
        maximumLineCount: 2
        wrapMode: Text.Wrap
        elide: Text.ElideRight
    }
}
