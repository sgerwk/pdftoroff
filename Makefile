PROGS=pdftoroff pdffit pdfrects pdfannot hovacui fbhovacui xhovacui cairoui

CFLAGS+=-g -Wall -Wextra -Wformat -Wformat-security
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDLIBS+=${shell pkg-config --libs poppler-glib}
fbhovacui hovacui cairoui: LDLIBS+=\
	${shell pkg-config --libs ncurses || echo '' -lncurses -ltinfo}
xhovacui hovacui cairoui: LDLIBS+=${shell pkg-config --libs x11}

all: ${PROGS}

install: all
	mkdir -p ${DESTDIR}/usr/bin
	cp hovacui pdftoroff pdftoebook ${DESTDIR}/usr/bin
	cp pdffit pdfrects pdfannot ${DESTDIR}/usr/bin
	mkdir -p ${DESTDIR}/usr/share/man/man1
	cp hovacui.1 pdftoroff.1 ${DESTDIR}/usr/share/man/man1
	cp pdffit.1 pdfrects.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects hovacui fbhovacui xhovacui: pdfrects.o
fbhovacui: cairofb.o vt.o cairoio-fb.o cairoui.o hovacui.o fbhovacui.o
xhovacui: cairofb.o vt.o cairoio-x11.o cairoui.o hovacui.o xhovacui.o
hovacui: cairofb.o vt.o cairoio-fb.o cairoio-x11.o cairoui.o \
hovacui.o hovacui-main.o
cairoui: cairofb.o vt.o cairoio-fb.o cairoio-x11.o cairoui.o cairoui-main.o

clean:
	rm -f *.o ${PROGS} cairoui-out.txt hovacui-out.txt

