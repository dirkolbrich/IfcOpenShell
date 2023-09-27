qt_add_qml_module(ifcviewerUi
    URI IfcViewer.Ui
    VERSION 1.0
    RESOURCE_PREFIX /ifcviewer/ui
    QML_FILES
        Main.qml
)

target_include_directories(ifcviewerUi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
