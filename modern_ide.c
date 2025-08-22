#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Minimal Cryptic IDE: one editor, open/save, run in external terminal using cride_interpreter

typedef struct {
	GtkWidget *window;
	GtkWidget *header;
	GtkWidget *open_button;
	GtkWidget *save_button;
	GtkWidget *run_button;
	GtkWidget *scroller;
	GtkSourceView *source_view;
	GtkSourceBuffer *source_buffer;
	GtkWidget *statusbar;
	guint status_ctx;
	gchar *current_path; // loaded file path or NULL
} App;

static void status(App *app, const gchar *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	gchar *msg = g_strdup_vprintf(fmt, args);
	va_end(args);
	gtk_statusbar_pop(GTK_STATUSBAR(app->statusbar), app->status_ctx);
	gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), app->status_ctx, msg);
	g_free(msg);
}

static gchar *choose_file(GtkWindow *parent, GtkFileChooserAction action) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		action == GTK_FILE_CHOOSER_ACTION_OPEN ? "Open File" : "Save File",
		parent,
		action,
		"_Cancel", GTK_RESPONSE_CANCEL,
		action == GTK_FILE_CHOOSER_ACTION_OPEN ? "_Open" : "_Save", GTK_RESPONSE_ACCEPT,
		NULL);

	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, ".crp files");
	gtk_file_filter_add_pattern(filter, "*.crp");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	gchar *filename = NULL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	}
	gtk_widget_destroy(dialog);
	return filename; // caller must g_free
}

static void load_file_into_buffer(App *app, const gchar *path) {
	gchar *contents = NULL;
	gsize len = 0;
	GError *err = NULL;
	if (!g_file_get_contents(path, &contents, &len, &err)) {
		status(app, "Failed to open %s: %s", path, err ? err->message : "unknown error");
		if (err) g_error_free(err);
		return;
	}
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(app->source_buffer), contents, (gint)len);
	g_free(contents);
	g_free(app->current_path);
	app->current_path = g_strdup(path);
	status(app, "Opened %s", path);
}

static gboolean save_buffer_to_file(App *app, const gchar *path) {
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(app->source_buffer), &start);
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(app->source_buffer), &end);
	gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->source_buffer), &start, &end, FALSE);
	GError *err = NULL;
	gboolean ok = g_file_set_contents(path, text, strlen(text), &err);
	if (!ok) {
		status(app, "Failed to save %s: %s", path, err ? err->message : "unknown error");
		if (err) g_error_free(err);
	} else {
		status(app, "Saved %s", path);
		g_free(app->current_path);
		app->current_path = g_strdup(path);
	}
	g_free(text);
	return ok;
}

static void on_open(GtkButton *btn, gpointer user_data) {
	App *app = (App *)user_data;
	gchar *path = choose_file(GTK_WINDOW(app->window), GTK_FILE_CHOOSER_ACTION_OPEN);
	if (!path) return;
	load_file_into_buffer(app, path);
	g_free(path);
}

static void on_save(GtkButton *btn, gpointer user_data) {
	App *app = (App *)user_data;
	gchar *path = NULL;
	if (app->current_path) {
		path = g_strdup(app->current_path);
	} else {
		path = choose_file(GTK_WINDOW(app->window), GTK_FILE_CHOOSER_ACTION_SAVE);
		if (!path) return;
		if (!g_str_has_suffix(path, ".crp")) {
			gchar *with_ext = g_strconcat(path, ".crp", NULL);
			g_free(path);
			path = with_ext;
		}
	}
	(void)save_buffer_to_file(app, path);
	g_free(path);
}

static gboolean launch_in_terminal(const gchar *cmdline) {
	// Try common terminals
	const gchar *templates[] = {
		"gnome-terminal -- bash -lc '%s; echo; echo Press Enter to close; read'",
		"xterm -e bash -lc '%s; echo; echo Press Enter to close; read'",
		"konsole -e bash -lc '%s; echo; echo Press Enter to close; read'",
		NULL
	};

	for (int i = 0; templates[i]; i++) {
		gchar *full = g_strdup_printf(templates[i], cmdline);
		int rc = system(full);
		g_free(full);
		if (rc == 0) return TRUE;
	}
	return FALSE;
}

static void on_run(GtkButton *btn, gpointer user_data) {
	App *app = (App *)user_data;

	// Ensure we have content saved to a temp file
	GError *err = NULL;
	gint fd = -1;
	gchar *tmp_path = NULL;
	fd = g_file_open_tmp("ccrp_temp_XXXXXX.crp", &tmp_path, &err);
	if (fd == -1 || tmp_path == NULL) {
		status(app, "Temp file error: %s", err ? err->message : "unknown");
		if (err) g_error_free(err);
		return;
	}
	close(fd);

	// Write current buffer into tmp_path
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(app->source_buffer), &start);
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(app->source_buffer), &end);
	gchar *text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->source_buffer), &start, &end, FALSE);
	if (!g_file_set_contents(tmp_path, text, -1, &err)) {
		status(app, "Write temp failed: %s", err ? err->message : "unknown");
		if (err) g_error_free(err);
		g_free(text);
		g_free(tmp_path);
		return;
	}
	g_free(text);

	// Build command to run interpreter
	gchar *quoted = g_shell_quote(tmp_path);
	gchar *cmdline = g_strdup_printf("./cride_interpreter %s", quoted);
	g_free(quoted);

	if (!launch_in_terminal(cmdline)) {
		status(app, "No compatible terminal found to run code.");
	} else {
		status(app, "Running…");
	}
	g_free(cmdline);
	// Keep tmp file so terminal can access; user can clean later
	g_free(tmp_path);
}

static void setup_language_theme(App *app) {
	GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();
	GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "ccrp");
	if (!lang) {
		// Fallback to plain text
	} else {
		gtk_source_buffer_set_language(app->source_buffer, lang);
	}
	GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
	GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(sm, "oblivion");
	if (scheme) {
		gtk_source_buffer_set_style_scheme(app->source_buffer, scheme);
	}
}

static void activate(GtkApplication *gapp, gpointer user_data) {
	App *app = g_new0(App, 1);

	app->window = gtk_application_window_new(gapp);
	gtk_window_set_title(GTK_WINDOW(app->window), "Cryptic IDE");
	gtk_window_set_default_size(GTK_WINDOW(app->window), 900, 600);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app->window), vbox);

	app->header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app->header), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app->header), "Cryptic IDE");
	gtk_box_pack_start(GTK_BOX(vbox), app->header, FALSE, FALSE, 0);

	app->open_button = gtk_button_new_with_label("Open");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header), app->open_button);
	g_signal_connect(app->open_button, "clicked", G_CALLBACK(on_open), app);

	app->save_button = gtk_button_new_with_label("Save");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header), app->save_button);
	g_signal_connect(app->save_button, "clicked", G_CALLBACK(on_save), app);

	app->run_button = gtk_button_new_with_label("Run");
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header), app->run_button);
	g_signal_connect(app->run_button, "clicked", G_CALLBACK(on_run), app);

	app->scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app->scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), app->scroller, TRUE, TRUE, 0);

	app->source_buffer = GTK_SOURCE_BUFFER(gtk_source_buffer_new(NULL));
	app->source_view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(app->source_buffer));
	gtk_widget_set_hexpand(GTK_WIDGET(app->source_view), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(app->source_view), TRUE);
	gtk_container_add(GTK_CONTAINER(app->scroller), GTK_WIDGET(app->source_view));
	gtk_source_view_set_show_line_numbers(app->source_view, TRUE);
	gtk_source_view_set_highlight_current_line(app->source_view, TRUE);
	setup_language_theme(app);

	app->statusbar = gtk_statusbar_new();
	gtk_box_pack_end(GTK_BOX(vbox), app->statusbar, FALSE, FALSE, 0);
	app->status_ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(app->statusbar), "main");
	status(app, "Ready");

	g_object_set_data_full(G_OBJECT(app->window), "app", app, (GDestroyNotify)g_free);
	gtk_widget_show_all(app->window);
}

int main(int argc, char **argv) {
	GtkApplication *app = gtk_application_new("com.cryptic.ide", G_APPLICATION_FLAGS_NONE);
	int status_code;
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status_code = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status_code;
} 