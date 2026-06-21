QT += core gui qml quick quickcontrols2 multimedia dbus

CONFIG += c++17 release
TARGET = omacut
TEMPLATE = app

HEADERS += \
    filepicker.h \
    portalfilepicker.h \
    ffmpeg.h \
    thumbworker.h \
    thumbprovider.h \
    backend.h

SOURCES += \
    main.cpp \
    portalfilepicker.cpp \
    ffmpeg.cpp \
    thumbworker.cpp \
    thumbprovider.cpp \
    backend.cpp

RESOURCES += resources.qrc
