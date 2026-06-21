QT += core gui quick testlib dbus
CONFIG += c++17 testcase
TARGET = backend_tests
TEMPLATE = app

INCLUDEPATH += ..

HEADERS += \
    ../backend.h \
    ../ffmpeg.h \
    ../filepicker.h \
    ../portalfilepicker.h \
    ../thumbprovider.h \
    ../thumbworker.h

SOURCES += \
    backend_tests.cpp \
    ../backend.cpp \
    ../ffmpeg.cpp \
    ../portalfilepicker.cpp \
    ../thumbprovider.cpp \
    ../thumbworker.cpp
