# pdftoroff

This package contains programs that find and use blocks of text in pdf files.
It includes a program for extracting text, for reformatting it to a different page
size and for viewing a pdf file in X11 and the Linux framebuffer.


## programs

The **pdftoroff** program extracts text from pdf files, trying to undo page,
column and paragraph formatting while retaining italic and bold faces. It
ouputs text in various text formats: groff, html, plain TeX, text, or
user-defined format. It is used by the included **pdftoebook** script to
reformat a pdf file to a smaller page, so that it becomes suited to be read on
a small tablet or e-ink ebook reader.

Also included is **pdffit**, which resizes the pages of a pdf file so that
their text fits into a given paper size (e.g., A4 or letter) with a given
margin. The related program **pdfrects** finds the bounding box or the text
area of the pages of a pdf file.

The **hovacui** pdf viewer for the Linux framebuffer and X11 automatically
zooms to the blocks of text. It is aimed at viewing files on small screens, and
is especially handy for multiple-columns documents. Details in the
[hovacui web page](http://sgerwk.altervista.org/hovacui/hovacui.html). Here is
a screenshots of hovacui with the goto page dialog:

![hovacui: screenshot of the goto to page field](/screenshots/fb-12.png?raw=true "hovacui: the gotopage dialog")

And here is the main menu:

![hovacui: screenshot of the main manu](/screenshots/fb-23.png?raw=true "hovacui: the main menu")


## installation

Generic instructions:

```
make
make install
```

### archlinux

This package is in AUR. Still, the PKGBUILD file is also accessible as an asset
from github.

- go to the [release page](../../releases) and download the latest ``PKGBUILD`` file to an empty directory
- in that directory, run `makepkg`
- install: `sudo pacman -U pdftoroff...tar.xz`

### opensuse

If the latest release in the [release page](../../releases) is for
example `1.0.0`:

- download sources: `curl -L -o $HOME/rpmbuild/SOURCES/pdftoroff-1.0.0.tar.gz https://github.com/sgerwk/pdftoroff/archive/v1.0.0.tar.gz` (replace `1.0.0` with the latest version number)
- download `pdftoroff.spec` from the [release page](../../releases)
- make the package: `rpmbuild -bb pdftoroff.spec`
- install: `sudo rpm -i $HOME/rpmbuild/RPMS/pdftoroff-version-etc.rpm`

### debian

- download the sources, unpack and compile them (do not install yet)
- make a directory `somewhere/pkg/DEBIAN`
- download there the `control` file from the [release page](../../releases)
- in the pdftoroff source directory run `make DESTDIR=somewhere/pkg install`
- create the package by `dpkg-deb -b somewhere/pkg .`
- install by `sudo dpkg -U pdftoroff...deb`

