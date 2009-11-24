SOURCES += main.cpp
LIBS += -L/Users/shaolin/Desktop/ffmpeg-0.5/libavcodec \
    -lavcodec \
    -L/Users/shaolin/Desktop/ffmpeg-0.5/libavformat \
    -lavformat \
    -L/Users/shaolin/Desktop/ffmpeg-0.5/libavutil \
    -lavutil \
    -lm \
    -lz
INCLUDEPATH += /Users/shaolin/Desktop/ffmpeg-0.5/
QMAKE_CC = /usr/bin/gcc-4.2
CONFIG = ppc -app_bundle
