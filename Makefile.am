bin_PROGRAMS = pengupop
noinst_PROGRAMS = bin2c

AM_CFLAGS = -g -Wall -O2 @ARCHFLAGS@

LINUX = @LINUX@
WIN32 = @WIN32@
DARWIN = @DARWIN@

BUILT_SOURCES = images.h font_16.h font_32.h destroy_group.h launch.h rebound.h stick.h
CLEANFILES = images.h font_16.h font_32.h destroy_group.h launch.h rebound.h stick.h

EXTRA_DIST = images.bin font_16.dat font_32.dat snd/destroy_group.wav snd/launch.wav snd/rebound.wav snd/stick.wav pengupop.png pengupop.desktop osx-mkbundle.sh osx-mkdiskimage.sh singleplayer_levels.c

bin2c_SOURCES = bin2c.c
pengupop_SOURCES = main.c error.c error.h packet.h image.c font.c sound.c sound.h singleplayer.c common.h gameid.c
nodist_pengupop_SOURCES = images.h

if LINUX
pengupop_LDADD = -lSDL -lz

endif
if WIN32
pengupop_LDADD = /usr/i586-mingw32msvc/lib/libSDL.a -lgdi32 -lwsock32 -ldxguid -lwinmm -lz
endif
if DARWIN
pengupop_LDFLAGS = -framework Carbon -framework CoreServices -framework AudioUnit -framework QuickTime -framework Cocoa -framework IOKit -framework OpenGL -framework SDL SDLMain.m
pengupop_LDADD = -lz -lstdc++

all-local: Pengupop.dmg

clean-local:
	@-rm -rf build Pengupop.app
	-rm -f Pengupop.dmg
endif

images.h: $(srcdir)/images.bin bin2c
	./bin2c $(srcdir)/images.bin images.h images 

font_32.h: $(srcdir)/font_32.dat bin2c
	./bin2c $(srcdir)/font_32.dat font_32.h font_32

font_16.h: $(srcdir)/font_16.dat bin2c
	./bin2c $(srcdir)/font_16.dat font_16.h font_16

destroy_group.h: $(srcdir)/snd/destroy_group.wav bin2c
	./bin2c $(srcdir)/snd/destroy_group.wav destroy_group.h destroy_group

launch.h: $(srcdir)/snd/launch.wav bin2c
	./bin2c $(srcdir)/snd/launch.wav launch.h launch

rebound.h: $(srcdir)/snd/rebound.wav bin2c
	./bin2c $(srcdir)/snd/rebound.wav rebound.h rebound

stick.h: $(srcdir)/snd/stick.wav bin2c
	./bin2c $(srcdir)/snd/stick.wav stick.h stick

Pengupop.dmg: pengupop
	strip pengupop
	./osx-mkbundle.sh Pengupop pengupop
	./osx-mkdiskimage.sh Pengupop Pengupop Pengupop.app
