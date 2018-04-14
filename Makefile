PROGS=pdftoroff pdffit pdfrects hovacui

CFLAGS+=-g -Wall -Wextra
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}
hovacui: LDFLAGS+=${shell pkg-config --libs ncurses}

all: ${PROGS}

install: all
	cp hovacui pdftoroff pdftoebook pdffit ${DESTDIR}/usr/bin
	cp hovacui.1 pdftoroff.1 pdffit.1 pdfrects.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects hovacui: pdfrects.o
hovacui: cairofb.o vt.o pdfrects.o hovacui.o fbhovacui.o

clean:
	rm -f *.o ${PROGS}

