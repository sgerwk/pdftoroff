PROGS=pdftoroff pdffit pdfrects-test

CFLAGS+=-g -Wall -Wextra
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}

all: ${PROGS}

install: all
	cp pdftoroff pdftoebook pdffit ${DESTDIR}/usr/bin
	cp pdftoroff.1 pdffit.1 ${DESTDIR}/usr/share/man/man1

pdftoroff: pdftext.o
pdftoroff pdffit pdfrects-test: pdfrects.o

clean:
	rm -f *.o ${PROGS}

