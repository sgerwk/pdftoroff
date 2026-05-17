#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

/*
 * escape filenames
 */
char *filenameescape(char *filename) {
	char *res;
	int i, j;

	res = malloc(strlen(filename) * 3 + 1);
	for (i = 0, j = 0; filename[i] != '\0'; i++)
		if ((unsigned char) filename[i] >= 32 && filename[i] != '%')
			res[j++] = filename[i];
		else {
			sprintf(res + j, "%%%02X", filename[i]);
			j += 3;
		}
	res[j] = '\0';

	return res;
}

/*
 * from file name to uri
 */
char *filenametouri(char *filename) {
	char *dir, *sep, *esc, *uri;

	if (filename[0] == '/') {
		dir = strdup("");
		sep = "";
	}
	else {
		dir = malloc(4096);
		if (dir == NULL) {
			printf("failed to allocate memory for directory\n");
			return NULL;
		}
		if (getcwd(dir, 4096) == NULL) {
			printf("error in obtaining the current directory\n");
			return NULL;
		}
		sep = "/";
	}

	esc = filenameescape(filename);

	uri = malloc(strlen("file:") + strlen(dir) +
		strlen(sep) + strlen(esc) + 1);
	if (uri == NULL) {
		printf("failed to allocate memory for file name\n");
		free(esc);
		return NULL;
	}
	strcpy(uri, "file:");
	strcat(uri, dir);
	strcat(uri, sep);
	strcat(uri, esc);

	free(esc);
	free(dir);
	return uri;
}

/*
 * add suffix and change extension
 */
char *suffixextension(char *in, char *suffix, char *ext) {
	char *base, *pos, *out;

	base = strdup(in);
	pos = strrchr(base, '.');
	if (pos != NULL)
		*pos = '\0';

	out = malloc(strlen(in) + strlen(suffix) + strlen(ext) + 1);
	strcpy(out, base);
	strcat(out, suffix);
	strcat(out, ext);

	free(base);
	return out;
}

/*
 * add suffix to a pdf filename
 */
char *pdfaddsuffix(char *infile, char *suffix) {
	char *ext;
	ext = (ext = strrchr(infile, '.')) ? ext : "";
	return suffixextension(infile, suffix, ext);
}

