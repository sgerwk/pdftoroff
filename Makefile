PROGS=pdftoroff pdffit pdfrects hovacui xhovacui

CFLAGS+=-g -Wall -Wextra
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}
hovacui: LDFLAGS+=${shell pkg-config --libs ncurses}
xhovacui: LDFLAGS+=${shell pkg-config --libs x11}

all: ${PROGS}

install: all
	cp hovacui pdftoroff pdftoebook pdffit ${DESTDIR}/usr/bin
	cp hovacui.1 pdftoroff.1 pdffit.1 pdfrects.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects hovacui xhovacui: pdfrects.o
hovacui: cairofb.o vt.o hovacui.o fbhovacui.o
xhovacui: cairofb.o vt.o hovacui.o xhovacui.o

clean:
	rm -f *.o ${PROGS}

