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
 * print a string and free it
 */
void printfree(gchar *prefix, gchar *s, gchar *suffix) {
	if (s == NULL)
		return;
	printf("%s%s%s", prefix, s, suffix);
	g_free(s);
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
			printf("stamp:\n");
			break;
		case POPPLER_ANNOT_CARET:
			printf("caret:\n");
			break;
		case POPPLER_ANNOT_WIDGET:
			printf("widget (unsupported)\n");
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
		printf("\n");
		return 0;
	}
	poppler_annot_markup_get_popup_rectangle(markup, &rect);
	printf(" %g,%g-%g,%g", rect.x1, rect.y1, rect.x2, rect.y2);
	printf("\n");
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
		if (! present) {
			printf("================== ANNOTATIONS ON PAGE ");
			printf("%d\n", poppler_page_get_index(page) + 1);
			present = TRUE;
		}
		m = (PopplerAnnotMapping *) s->data;

		type = poppler_annot_get_annot_type(m->annot);
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
	GList *links, *l;
	int present = FALSE;
	PopplerLinkMapping *m;
	PopplerAction *a;
	PopplerActionAny *any;
	PopplerActionGotoDest *linkdest;
	PopplerActionGotoRemote *remote;
	PopplerActionUri *uri;
	PopplerActionNamed *named;

	links = poppler_page_get_link_mapping(page);

	for (l = links; l != NULL; l = l->next) {
		if (! present) {
			printf("================== ACTIONS ON PAGE ");
			printf("%d\n", poppler_page_get_index(page) + 1);
			present = TRUE;
		}
		m = (PopplerLinkMapping *) l->data;
		a = m->action;
		switch (a->type) {
		case POPPLER_ACTION_NONE:
			any = (PopplerActionAny *) a;
			printf("none: %s\n", any->title);
			break;
		case POPPLER_ACTION_GOTO_DEST:
			linkdest = (PopplerActionGotoDest *) a;
			printf("link to page %d\n", linkdest->dest->page_num);
			break;
		case POPPLER_ACTION_GOTO_REMOTE:
			remote = (PopplerActionGotoRemote *) a;
			printf("link to document %s\n", remote->file_name);
			break;
		/* POPPLER_ACTION_LAUNCH does not make sense */
		case POPPLER_ACTION_URI:
			uri = (PopplerActionUri *) a;
			printf("uri: %s\n", uri->uri);
			break;
		case POPPLER_ACTION_NAMED:
			named = (PopplerActionNamed *) a;
			printf("predefined action: %s\n", named->named_dest);
			break;
		/* newer actions:
		case POPPLER_ACTION_MOVIE:
		case POPPLER_ACTION_RENDITION:
		case POPPLER_ACTION_OCG_STATE: */
		/* do not support POPPLER_ACTION_JAVASCRIPT */
		default:
			printf("action (%d)\n", a->type);
		}
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
	double width, height;
	int present = 0;

				/* arguments */

	if (argn - 1 < 1) {
		printf("error: filename missing\n");
		printf("print annotations and actions in a pdf file\n");
		printf("usage:\n\tpdfannot file.pdf\n");
		exit(EXIT_FAILURE);
	}
	filename = argv[1];
	uri = filenametouri(filename);

				/* open document */

	doc = poppler_document_new_from_file(uri, NULL, NULL);
	if (doc == NULL) {
		printf("cannot open %s\n", filename);
		exit(-EXIT_FAILURE);
	}

				/* scan pages */

	npages = poppler_document_get_n_pages(doc);

	for (n = 0; n < npages; n++) {
		page = poppler_document_get_page(doc, n);
		poppler_page_get_size(page, &width, &height);
		present = present | (printannotations(page) << 0);
		present = present | (printlinks(page) << 1);
		g_object_unref(page);
	}

	return present;
}

