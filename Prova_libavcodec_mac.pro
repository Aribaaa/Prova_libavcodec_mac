SOURCES += main.cpp
LIBS += -L/opt/local/lib/ \
    -lavutil \
    -lavcodec \
    -lavformat \
    -lSDL \
    -lswscale \
    -lm \
    -lz
INCLUDEPATH += /opt/local/include/
QMAKE_CC = /usr/bin/gcc-4.2
CONFIG = +ppc -app_bundle
DEFINES += __DARWIN__
