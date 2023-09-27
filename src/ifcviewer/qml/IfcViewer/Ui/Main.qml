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

    SplitView {
        anchors.fill: parent

        Rectangle {
            id: viewLeft

            implicitWidth: rootWindow.width / 4
            SplitView.maximumWidth: 400
            Label {
                text: "View 1"
                anchors.centerIn: parent
            }
        }
        Rectangle {
            id: viewCenter

            SplitView.minimumWidth: 50
            SplitView.fillWidth: true
            Label {
                text: "View 2"
                anchors.centerIn: viewCenter
            }
        }
        Rectangle {
            id: viewRight

            implicitWidth: rootWindow.width / 4
            Label {
                text: "View 3"
                anchors.centerIn: viewRight
            }
        }
    }
}
