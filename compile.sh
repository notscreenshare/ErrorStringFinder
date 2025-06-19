#!/usr/bin/bash

glib-compile-resources --target=./resources/resources.c --generate-source ./resources/resources.xml

gcc -g ./main.c ./resources/resources.c \
    $(pkg-config --cflags gtk+-3.0) \
    $(pkg-config --libs gtk+-3.0) \
    -DFRIBIDI_LIB_STATIC -DXML_STATIC -DLZMA_API_STATIC \
    -o ./error-string-finder
