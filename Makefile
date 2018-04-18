PROGS=pdftoroff pdffit pdfrects hovacui fbhovacui xhovacui

CFLAGS+=-g -Wall -Wextra -Wformat -Wformat-security
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}
fbhovacui hovacui: LDFLAGS+=\
	${shell pkg-config --libs ncurses || echo '' -lncurses -ltinfo}
xhovacui hovacui: LDFLAGS+=${shell pkg-config --libs x11}

all: ${PROGS}

install: all
	mkdir -p ${DESTDIR}/usr/bin
	cp hovacui pdftoroff pdftoebook pdffit pdfrects ${DESTDIR}/usr/bin
	mkdir -p ${DESTDIR}/usr/share/man/man1
	cp hovacui.1 pdftoroff.1 pdffit.1 pdfrects.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects hovacui fbhovacui xhovacui: pdfrects.o
fbhovacui: cairofb.o vt.o hovacui.o fbhovacui.o
xhovacui: cairofb.o vt.o hovacui.o xhovacui.o
hovacui: cairofb.o vt.o hovacui.o hovacui-main.o

clean:
	rm -f *.o ${PROGS}

