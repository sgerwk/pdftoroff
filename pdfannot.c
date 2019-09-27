/*
 * pdfannot.c
 *
 * print annotations and actions in a pdf file
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <poppler.h>

/*
 * pages to show
 */
int first;
int last;

/*
 * output format
 */
enum format {
	text,
	html
} outformat;

/*
 * newline
 */
#define NEWLINE (outformat == text ? "\n" : "<br />\n")

/*
 * print a string and free it
 */
void printfree(gchar *prefix, gchar *s, gchar *suffix) {
	char *p;
	if (s == NULL)
		return;
	for (p = strchr(s, '\r'); p != NULL; p = strchr(p + 1, '\r'))
		*p = '\n';
	printf("%s%s%s", prefix, s, suffix);
	g_free(s);
}

/*
 * print the header for a page
 */
void printheader(gchar *title, PopplerPage *page) {
	if (last == first + 1)
		return;
	if (outformat == text)
		printf("================== ");
	else
		printf("<h2>");
	printf("%s ON PAGE %d", title, poppler_page_get_index(page) + 1);
	if (outformat == html)
		printf("</h2>");
	printf("\n");
}

/*
 * print the name of an annotation
 */
void printannotationname(PopplerAnnot *annot) {
	gint type;

	type = poppler_annot_get_annot_type(annot);

	// one day...
	// printfree(g_enum_to_string(PopplerAnnotType, type));

	switch (type) {
		case POPPLER_ANNOT_TEXT:
			printf("text:");
			break;
		case POPPLER_ANNOT_FREE_TEXT:
			printf("free text:");
			break;
		case POPPLER_ANNOT_LINE:
			printf("line:");
			break;
		case POPPLER_ANNOT_SQUARE:
			printf("square:");
			break;
		case POPPLER_ANNOT_CIRCLE:
			printf("circle:");
			break;
		case POPPLER_ANNOT_UNDERLINE:
			printf("underline:");
			break;
		case POPPLER_ANNOT_HIGHLIGHT:
			printf("highlight:");
			break;
		case POPPLER_ANNOT_SQUIGGLY:
			printf("squiggly:");
			break;
		case POPPLER_ANNOT_STRIKE_OUT:
			printf("strike out:");
			break;
		case POPPLER_ANNOT_FILE_ATTACHMENT:
			printf("file attachment:");
			break;
		case POPPLER_ANNOT_STAMP:
			printf("stamp:%s", NEWLINE);
			break;
		case POPPLER_ANNOT_CARET:
			printf("caret:%s", NEWLINE);
			break;
		case POPPLER_ANNOT_WIDGET:
			printf("widget (unsupported)%s", NEWLINE);
			break;
		default:
			printf("annotation (%d):", type);
			break;
	}
}

/*
 * print a markup annotation
 */
int printannotationmarkup(PopplerAnnotMarkup *markup) {
	PopplerRectangle rect;
	PopplerAnnotFileAttachment *att;
	gint type;

	type = poppler_annot_get_annot_type(POPPLER_ANNOT(markup));

	printannotationname(POPPLER_ANNOT(markup));
	printfree(" ", poppler_annot_markup_get_label(markup), "");
	printfree(" ", poppler_annot_markup_get_subject(markup), "");

	if (type == POPPLER_ANNOT_FILE_ATTACHMENT) {
		att = POPPLER_ANNOT_FILE_ATTACHMENT(markup);
		printfree(" ", poppler_annot_file_attachment_get_name(att), "");
	}

	if (! poppler_annot_markup_has_popup(markup)) {
		printf("%s", NEWLINE);
		return 0;
	}
	poppler_annot_markup_get_popup_rectangle(markup, &rect);
	printf(" %g,%g-%g,%g", rect.x1, rect.y1, rect.x2, rect.y2);
	printf("%s", NEWLINE);
	return 0;
}

/*
 * print the annotations in a page
 */
int printannotations(PopplerPage *page) {
	GList *annots, *s;
	int present = FALSE;
	PopplerAnnotMapping *m;
	int type;

	if (! POPPLER_IS_PAGE(page))
		return FALSE;

	annots = poppler_page_get_annot_mapping(page);

	for (s = annots; s != NULL; s = s->next) {
		m = (PopplerAnnotMapping *) s->data;
		type = poppler_annot_get_annot_type(m->annot);

		if (! present && type != POPPLER_ANNOT_LINK) {
			printheader("ANNOTATIONS", page);
			present = TRUE;
		}

		switch (type) {
		case POPPLER_ANNOT_LINK:
			continue;	// links are actions, print there
		case POPPLER_ANNOT_TEXT:
		case POPPLER_ANNOT_FREE_TEXT:
		case POPPLER_ANNOT_LINE:
		case POPPLER_ANNOT_SQUARE:
		case POPPLER_ANNOT_CIRCLE:
		case POPPLER_ANNOT_UNDERLINE:
		case POPPLER_ANNOT_HIGHLIGHT:
		case POPPLER_ANNOT_SQUIGGLY:
		case POPPLER_ANNOT_STRIKE_OUT:
		case POPPLER_ANNOT_FILE_ATTACHMENT:
			printannotationmarkup(POPPLER_ANNOT_MARKUP(m->annot));
			break;
		case POPPLER_ANNOT_STAMP:
		case POPPLER_ANNOT_CARET:
		case POPPLER_ANNOT_WIDGET:
			printannotationname(m->annot);
			break;
		/* others */
		default:
			printf("annotation (%d)\n", type);
		}

		printfree("\tname: ", poppler_annot_get_name(m->annot), "\n");
		printfree("\tcontent: ",
			poppler_annot_get_contents(m->annot), "\n");
	}

	poppler_page_free_annot_mapping(annots);
	return present;
}

/*
 * print the links in a page
 */
int printlinks(PopplerPage *page) {
	gdouble width, height;
	GList *links, *l;
	int present = FALSE;
	PopplerLinkMapping *m;
	PopplerRectangle r;
	PopplerAction *a;
	PopplerActionAny *any;
	PopplerActionGotoDest *linkdest;
	PopplerActionGotoRemote *remote;
	PopplerActionUri *uri;
	PopplerActionNamed *named;
	char *t;

	poppler_page_get_size(page, &width, &height);
	links = poppler_page_get_link_mapping(page);

	for (; links != NULL && links->next != NULL; links = links->next) {
	}

	for (l = links; l != NULL; l = l->prev) {
		if (! present) {
			printheader("ACTIONS", page);
			present = TRUE;
		}
		m = (PopplerLinkMapping *) l->data;
		a = m->action;

		r.x1 = m->area.x1 - 0;
		r.x2 = m->area.x2 + 0;
		r.y1 = height - m->area.y2 - 0;
		r.y2 = height - m->area.y1 + 0;
		// printf("%g,%g - %g,%g\n", r.x1, r.y1, r.x2, r.y2);
		t = poppler_page_get_text_for_area(page, &r);
		if (outformat != html || a->type != POPPLER_ACTION_URI)
			printf("%s%s", t, NEWLINE);

		switch (a->type) {
		case POPPLER_ACTION_NONE:
			any = (PopplerActionAny *) a;
			printf("none: %s", any->title);
			break;
		case POPPLER_ACTION_GOTO_DEST:
			linkdest = (PopplerActionGotoDest *) a;
			printf("link to page %d", linkdest->dest->page_num);
			break;
		case POPPLER_ACTION_GOTO_REMOTE:
			remote = (PopplerActionGotoRemote *) a;
			printf("link to document %s", remote->file_name);
			break;
		/* POPPLER_ACTION_LAUNCH does not make sense */
		case POPPLER_ACTION_URI:
			uri = (PopplerActionUri *) a;
			if (outformat == text)
				printf("uri: %s", uri->uri);
			else
				printf("<p><a href=\"%s\">%s</a></p>",
					uri->uri,
					uri->title != NULL ? uri->title :
					t != NULL && t[0] != '\0' ? t :
						uri->uri);
			break;
		case POPPLER_ACTION_NAMED:
			named = (PopplerActionNamed *) a;
			printf("predefined action: %s", named->named_dest);
			break;
		/* newer actions:
		case POPPLER_ACTION_MOVIE:
		case POPPLER_ACTION_RENDITION:
		case POPPLER_ACTION_OCG_STATE: */
		/* do not support POPPLER_ACTION_JAVASCRIPT */
		default:
			printf("action (%d)", a->type);
		}
		printf("%s", NEWLINE);

		g_free(t);
	}

	poppler_page_free_link_mapping(links);
	return present;
}

/*
 * escape filenames
 */
char *filenameescape(char *filename) {
	char *res;
	int i, j;

	res = malloc(strlen(filename) * 3 + 1);
	for (i = 0, j = 0; filename[i] != '\0'; i++)
		if (filename[i] >= 32 && filename[i] != '%')
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
 * main
 */
int main(int argn, char *argv[]) {
	char *filename, *uri;
	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	int present = 0;

				/* arguments */

	outformat = text;
	first = 0;
	last = -1;

	if (argn - 1 >= 1 && ! strcmp(argv[1], "-w")) {
		outformat = html;
		argn--;
		argv++;
	}
	if (argn - 1 < 1) {
		printf("error: filename missing\n");
		printf("print annotations and actions in a pdf file\n");
		printf("usage:\n\tpdfannot [-w] file.pdf [page]\n");
		exit(EXIT_FAILURE);
	}
	filename = argv[1];
	uri = filenametouri(filename);
	if (argn - 1 > 1) {
		first = atoi(argv[2]);
		last = atoi(argv[2]) + 1;
	}

				/* open document */

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	if (doc == NULL) {
		printf("cannot open %s\n", filename);
		exit(-EXIT_FAILURE);
	}

				/* scan pages */

	npages = poppler_document_get_n_pages(doc);
	if (first < 0 || last >= npages) {
		printf("no such page: %d\n", last - 1);
		return EXIT_FAILURE;
	}

	for (n = first; n < (last == -1 ? npages : last); n++) {
		page = poppler_document_get_page(doc, n);
		present = present | (printannotations(page) << 0);
		present = present | (printlinks(page) << 1);
		g_object_unref(page);
	}

	return present;
}

