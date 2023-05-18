PROGS=pdftoroff pdffit pdfrects pdfrecur pdfannot \
hovacui fbhovacui drmhovacui xhovacui cairoui

CFLAGS+=-g -Wall -Wextra -Wformat -Wformat-security
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDLIBS+=${shell pkg-config --libs poppler-glib}
fbhovacui drmhovacui hovacui cairoui: LDLIBS+=\
	${shell pkg-config --libs ncurses || echo '' -lncurses -ltinfo}
drmhovacui.o cairoio-drm.o cairodrm.o: CFLAGS+=\
	${shell pkg-config --cflags libdrm}
drmhovacui hovacui cairodrm: LDLIBS+=${shell pkg-config --libs libdrm}
xhovacui hovacui cairoui: LDLIBS+=${shell pkg-config --libs x11}

all: ${PROGS}

install: all
	mkdir -p ${DESTDIR}/usr/bin
	cp hovacui.conf ${DESTDIR}/etc
	cp hovacui pdfhscript pdfinteractive ${DESTDIR}/usr/bin
	cp pdftoroff pdftoebook ${DESTDIR}/usr/bin
	cp pdffit pdfrects pdfrecur pdfannot ${DESTDIR}/usr/bin
	mkdir -p ${DESTDIR}/usr/share/man/man1
	cp hovacui.1 pdftoroff.1 ${DESTDIR}/usr/share/man/man1
	cp pdffit.1 pdfrects.1 pdfrecur.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects pdfrecur: pdfrects.o
hovacui fbhovacui drmhovacui xhovacui: pdfrects.o
fbhovacui: cairofb.o vt.o cairoio-fb.o cairoui.o hovacui.o fbhovacui.o
drmhovacui: cairodrm.o vt.o cairoio-drm.o cairoui.o hovacui.o drmhovacui.o
xhovacui: cairoio-x11.o cairoui.o hovacui.o xhovacui.o
hovacui: cairofb.o cairodrm.o vt.o cairoio-x11.o cairoio-fb.o cairoio-drm.o \
cairoui.o hovacui.o hovacui-main.o
cairoui: cairofb.o vt.o cairoio-fb.o cairoio-x11.o cairoui.o cairoui-main.o
cairodrm: cairodrm-main.o

clean:
	rm -f *.o ${PROGS} cairodrm cairoui-out.txt hovacui-out.txt

