PROGS=pdftoroff pdffit pdfrects

CFLAGS+=-g -Wall -Wextra
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}

all: ${PROGS}

install: all
	cp pdftoroff pdftoebook pdffit ${DESTDIR}/usr/bin
	cp pdftoroff.1 pdffit.1 pdfrects.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdfrects: pdfrects-main.o
pdftoroff pdffit pdfrects: pdfrects.o

clean:
	rm -f *.o ${PROGS}

