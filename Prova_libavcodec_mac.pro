SOURCES += main.cpp
LIBS += -L/opt/local/lib/ \
    -lavcodec \
    -lavformat \
    -lavutil \
    -lSDL -D_GNU_SOURCE=1 -D_THREAD_SAFE \
    -lm \
    -lz
INCLUDEPATH += /opt/local/include/
QMAKE_CC = /usr/bin/gcc-4.2
CONFIG = +ppc -app_bundle
