import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// import IfcViewer.Ui

ApplicationWindow {
    id: rootWindow

    width: 1024
    height: 768

    title: qsTr("IfcViewer")
    visible: true

    Text {
        id: txt

        anchors.centerIn: rootWindow
        text: "test"
    }
}
