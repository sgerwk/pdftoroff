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
 * separate pages or not
 */
gboolean headers;

/*
 * formatting elements
 */
struct outformat {
	char *newline;
	char *separator;
	char *startpar;
	char *endpar;
	char *startheader;
	char *endheader;
	char *startdestination;
	char *enddestination;
} *outformat;
struct outformat textformat = {
	"\n",
	"\n=================\n",
	"",
	"\n-------\n",
	"==================",
	"",
	"%sdestination: ",
	""
};
struct outformat htmlformat = {
	"<br />\n",
	"\n<hr />\n",
	"<p>\n",
	"</p>\n",
	"<h2>\n",
	"</h2>\n",
	"\n<blockquote>\n",
	"</blockquote>\n",
};

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
	if (! headers)
		return;
	fputs(outformat->startheader, stdout);
	printf(" %s ON PAGE %d", title, poppler_page_get_index(page) + 1);
	fputs(outformat->endheader, stdout);
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
			printf("stamp:%s", outformat->newline);
			break;
		case POPPLER_ANNOT_CARET:
			printf("caret:%s", outformat->newline);
			break;
		case POPPLER_ANNOT_WIDGET:
			printf("widget (unsupported)%s", outformat->newline);
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
		printf("%s", outformat->newline);
		return 0;
	}
	poppler_annot_markup_get_popup_rectangle(markup, &rect);
	printf(" %g,%g-%g,%g", rect.x1, rect.y1, rect.x2, rect.y2);
	printf("%s", outformat->newline);
	return 0;
}

/*
 * print content in a rectangle
 */
int printcontent(PopplerPage *dpage, PopplerRectangle r, char *indent) {
	char *d;

	d = poppler_page_get_selected_text(dpage, POPPLER_SELECTION_LINE, &r);
	printf(outformat->startdestination, indent);
	printf("%s", d);
	printf(outformat->enddestination, indent);
	free(d);
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
	PopplerRectangle r;

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

		r = m->area;

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

		printfree("\tname: ", poppler_annot_get_name(m->annot),
		                      outformat->newline);
		printfree("\tcontent: ",
			poppler_annot_get_contents(m->annot),
			              outformat->newline);

		printcontent(page, r, "	");
		fputs(outformat->separator, stdout);
	}

	poppler_page_free_annot_mapping(annots);
	return present;
}

/*
 * print the links in a page
 */
#define DESTCONTENT 0x01
int printlinks(PopplerDocument *doc, PopplerPage *page, int flags) {
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
	PopplerDest *dest, *inter;
	PopplerPage *dpage;
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
		t = poppler_page_get_selected_text(page,
			POPPLER_SELECTION_LINE, &r);

		fputs(outformat->startpar, stdout);
		if (outformat != &htmlformat || a->type != POPPLER_ACTION_URI)
			printf("%s%s", t, outformat->newline);

		switch (a->type) {
		case POPPLER_ACTION_NONE:
			any = (PopplerActionAny *) a;
			printf("none: %s", any->title);
			break;
		case POPPLER_ACTION_GOTO_DEST:
			linkdest = (PopplerActionGotoDest *) a;
			dest = linkdest->dest;
			printf("link ");
			while (dest != NULL &&
			       dest->type == POPPLER_DEST_NAMED) {
				printf("to %s: ", dest->named_dest);
				inter = poppler_document_find_dest(doc,
					dest->named_dest);
				if (dest != linkdest->dest)
					poppler_dest_free(dest);
				dest = inter;
			}
			if (dest == NULL) {
				printf("to nowhere");
				break;
			}
			printf("to page %d, ", dest->page_num);
			switch (dest->type) {
			case POPPLER_DEST_XYZ:
				printf("point %g,%g", dest->left, dest->top);
				r.x1 = dest->left - 20;
				r.y1 = height - dest->top - 20;
				r.x2 = dest->left + 20;
				r.y2 = height - dest->top + 20;
				break;
			case POPPLER_DEST_FIT:
				// whole page
				// todo: other fit modes
				break;
			default:
				printf("rectangle ");
				printf("%g,%g - ", dest->left, dest->top);
				printf("%g,%g", dest->right, dest->bottom);
				r.x1 = dest->left;
				r.y1 = height - dest->top;
				r.x2 = dest->right;
				r.y2 = height - dest->bottom;
				break;
			}
			if (flags & DESTCONTENT) {
				dpage = poppler_document_get_page(doc,
					dest->page_num - 1);
				if (dpage != NULL)
					printcontent(dpage, r, "\n");
			}
			if (dest != linkdest->dest)
				poppler_dest_free(dest);
			break;
		case POPPLER_ACTION_GOTO_REMOTE:
			remote = (PopplerActionGotoRemote *) a;
			printf("link to document %s", remote->file_name);
			break;
		/* POPPLER_ACTION_LAUNCH does not make sense */
		case POPPLER_ACTION_URI:
			uri = (PopplerActionUri *) a;
			if (outformat == &textformat)
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

		fputs(outformat->endpar, stdout);
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
	int opt, usage;
	char *filename, *uri;
	int first, last;
	gboolean annotations, links;
	int flags;
	PopplerDocument *doc;
	PopplerPage *page;
	int npages, n;
	int present;

				/* arguments */

	usage = 0;
	headers = TRUE;
	outformat = &textformat;
	annotations = TRUE;
	links = TRUE;
	first = 0;
	last = -1;
	flags = 0;

	while (-1 != (opt = getopt(argn, argv, "wtaldh")))
		switch (opt) {
		case 't':
			outformat = &textformat;
			break;
		case 'w':
			outformat = &htmlformat;
			break;
		case 'a':
			links = FALSE;
			break;
		case 'l':
			annotations = FALSE;
			break;
		case 'd':
			flags |= DESTCONTENT;
			break;
		case 'h':
			usage = 1;
			break;
		default:
			usage = 2;
			break;
		}
	if (usage == 0 && argn - optind < 1) {
		printf("error: filename missing\n");
		usage = 2;
	}
	if (usage > 0) {
		printf("print annotations and actions in a pdf file\n");
		printf("usage:\n\tpdfannot [-t] [-w] [-a] [-l] [-d] [-h] ");
		printf("file.pdf [page]\n");
		printf("\t\t-t\toutput is text-only\n");
		printf("\t\t-w\toutput is html\n");
		printf("\t\t-a\tonly output annotations\n");
		printf("\t\t-a\tonly output links\n");
		printf("\t\t-d\tprint text at destination of inner links\n");
		printf("\t\t-h\tthis help\n");
		exit(usage == 1 ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	filename = argv[optind];
	uri = filenametouri(filename);
	if (argn - optind > 1) {
		first = atoi(argv[optind + 1]) - 1;
		last = atoi(argv[optind + 1]) - 1 + 1;
		headers = FALSE;
	}

				/* open document */

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	if (doc == NULL) {
		printf("cannot open %s\n", filename);
		exit(-EXIT_FAILURE);
	}

				/* scan pages */

	npages = poppler_document_get_n_pages(doc);
	if (first < 0 || last > npages) {
		printf("no such page: %d\n", last - 1);
		return EXIT_FAILURE;
	}

	present = 0;
	for (n = first; n < (last == -1 ? npages : last); n++) {
		page = poppler_document_get_page(doc, n);
		if (annotations)
			present = present | (printannotations(page) << 0);
		if (links)
			present = present | (printlinks(doc, page, flags) << 1);
		g_object_unref(page);
	}

	return present;
}

