PROGS=pdftoroff

CFLAGS+=-g -Wall -Wextra
CFLAGS+=${shell pkg-config --cflags poppler-glib}
LDFLAGS+=${shell pkg-config --libs poppler-glib}

all: ${PROGS}

install: all
	cp pdftoroff pdftoebook ${DESTDIR}/usr/bin
	cp pdftoroff.1 ${DESTDIR}/usr/share/man/man1

clean:
	rm -f ${PROGS}

