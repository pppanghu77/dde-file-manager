#-------------------------------------------------
#
# Project created by QtCreator 2015-06-24T09:14:17
#
#-------------------------------------------------
#system($$PWD/../vendor/prebuild)
#include($$PWD/../vendor/vendor.pri)

INCLUDEPATH += $$PWD \
               $$PWD/../../../../../src/dde-file-manager-plugins/pluginPreview/dde-video-preview-plugin/

HEADERS += \
    $$PWD/interfaces/tst_all_interfaces.h

SOURCES += \
    $$PWD/test-main.cpp \
    $$PWD/ut_videopreview.cpp \
    $$PWD/ut_videopreviewplugin.cpp