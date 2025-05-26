spudir = $(pluginsdir)/spu

noinst_HEADERS += spu/common.h

# sub filters
libsubsdelay_plugin_la_SOURCES = spu/subsdelay.c
spu_LTLIBRARIES = libsubsdelay_plugin.la

# sub sources
libaudiobargraph_v_plugin_la_SOURCES = spu/audiobargraph_v.c
libaudiobargraph_v_plugin_la_LIBADD = $(LIBM)
liblogo_plugin_la_SOURCES = spu/logo.c
libmarq_plugin_la_SOURCES = spu/marq.c
libmosaic_plugin_la_SOURCES = spu/mosaic.c spu/mosaic.h
libmosaic_plugin_la_LIBADD = $(LIBM)
librss_plugin_la_SOURCES = spu/rss.c

spu_LTLIBRARIES += \
	libaudiobargraph_v_plugin.la \
	liblogo_plugin.la \
	libmarq_plugin.la \
	libmosaic_plugin.la \
	librss_plugin.la

libdynamicoverlay_plugin_la_SOURCES = \
	spu/dynamicoverlay/dynamicoverlay_buffer.c \
	spu/dynamicoverlay/dynamicoverlay_queue.c \
	spu/dynamicoverlay/dynamicoverlay_commands.c \
	spu/dynamicoverlay/dynamicoverlay.c spu/dynamicoverlay/dynamicoverlay.h
spu_LTLIBRARIES += libdynamicoverlay_plugin.la

# Disabled: liblivetranslate_plugin_la_SOURCES = spu/livetranslate.c
# Disabled: spu_LTLIBRARIES += liblivetranslate_plugin.la



# Whisper plugin - using the shared library from whisper.cpp build directory
liblivetranslate_whisper_plugin_la_SOURCES = spu/livetranslate_whisper.c
liblivetranslate_whisper_plugin_la_CFLAGS = $(AM_CFLAGS) -I/home/sharathg/whisper.cpp/include -I/home/sharathg/whisper.cpp/ggml/include
liblivetranslate_whisper_plugin_la_LIBADD = -L/home/sharathg/whisper.cpp/build/src -lwhisper
liblivetranslate_whisper_plugin_la_LDFLAGS = $(AM_LDFLAGS) -pthread -Wl,-rpath,/home/sharathg/whisper.cpp/build/src
spu_LTLIBRARIES += liblivetranslate_whisper_plugin.la

