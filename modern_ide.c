#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Minimal Cryptic IDE: one editor, open/save, run in external terminal using cride_interpreter

typedef enum {
	COL_DISPLAY = 0,
	COL_PATH,
	COL_IS_DIR,
	COL_COUNT
} TreeCols;

typedef struct {
	GtkWidget *window;
	GtkWidget *header;
	GtkWidget *open_button;
	GtkWidget *open_folder_button;
	GtkWidget *save_button;
	GtkWidget *run_button;
	GtkWidget *pick_interp_button;
	GtkWidget *scroller;
	GtkSourceView *source_view;
	GtkSourceBuffer *source_buffer;
	GtkWidget *statusbar;
	guint status_ctx;
	gchar *current_path; // loaded file path or NULL
	gchar *interp_path; // selected interpreter path or NULL
	// Sidebar
	GtkWidget *paned;
	GtkTreeStore *store;
	GtkWidget *tree;
	gchar *root_dir;
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

static gchar *config_path(void) {
	gchar *dir = g_build_filename(g_get_user_config_dir(), "cryptic-ide", NULL);
	g_mkdir_with_parents(dir, 0700);
	gchar *cfg = g_build_filename(dir, "config.ini", NULL);
	g_free(dir);
	return cfg; // caller frees
}

static void load_config(App *app) {
	GKeyFile *kf = g_key_file_new();
	gchar *cfg = config_path();
	GError *err = NULL;
	if (g_key_file_load_from_file(kf, cfg, G_KEY_FILE_NONE, &err)) {
		gchar *ip = g_key_file_get_string(kf, "general", "interpreter", NULL);
		if (ip) {
			g_free(app->interp_path);
			app->interp_path = ip;
		}
	}
	if (err) g_error_free(err);
	g_key_file_unref(kf);
	g_free(cfg);
}

static void save_config(App *app) {
	GKeyFile *kf = g_key_file_new();
	if (app->interp_path) g_key_file_set_string(kf, "general", "interpreter", app->interp_path);
	gsize len = 0;
	gchar *data = g_key_file_to_data(kf, &len, NULL);
	gchar *cfg = config_path();
	GError *err = NULL;
	g_file_set_contents(cfg, data ? data : "", len, &err);
	if (err) g_error_free(err);
	g_free(cfg);
	g_free(data);
	g_key_file_unref(kf);
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

static gchar *choose_executable(GtkWindow *parent) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		"Select Interpreter",
		parent,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Select", GTK_RESPONSE_ACCEPT,
		NULL);
	gchar *filename = NULL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	}
	gtk_widget_destroy(dialog);
	return filename; // caller frees
}

static gchar *choose_folder(GtkWindow *parent) {
	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		"Open Folder",
		parent,
		GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
		"_Cancel", GTK_RESPONSE_CANCEL,
		"_Open", GTK_RESPONSE_ACCEPT,
		NULL);
	gchar *folder = NULL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	}
	gtk_widget_destroy(dialog);
	return folder; // caller must g_free
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

	// Resolve interpreter path: user-selected, APPDIR/usr/bin, local ./, or PATH
	gchar *interp = NULL;
	if (app->interp_path && g_file_test(app->interp_path, G_FILE_TEST_IS_EXECUTABLE)) {
		interp = g_strdup(app->interp_path);
	} else {
		const gchar *appdir = g_getenv("APPDIR");
		if (appdir) {
			interp = g_build_filename(appdir, "usr", "bin", "cride_interpreter", NULL);
		} else if (g_file_test("./cride_interpreter", G_FILE_TEST_IS_EXECUTABLE)) {
			interp = g_strdup("./cride_interpreter");
		} else {
			interp = g_strdup("cride_interpreter");
		}
	}

	gchar *q_interp = g_shell_quote(interp);
	gchar *q_tmp = g_shell_quote(tmp_path);
	gchar *cmdline = g_strdup_printf("%s %s", q_interp, q_tmp);
	g_free(q_interp);
	g_free(q_tmp);
	g_free(interp);

	if (!launch_in_terminal(cmdline)) {
		status(app, "No compatible terminal found to run code.");
	} else {
		status(app, "Running…");
	}
	g_free(cmdline);
	// Keep tmp file so terminal can access; user can clean later
	g_free(tmp_path);
}

static void on_pick_interpreter(GtkButton *btn, gpointer user_data) {
	App *app = (App *)user_data;
	gchar *sel = choose_executable(GTK_WINDOW(app->window));
	if (!sel) return;
	g_free(app->interp_path);
	app->interp_path = sel;
	save_config(app);
	status(app, "Interpreter set: %s", app->interp_path);
}

static void setup_language_theme(App *app) {
	GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default();

	// Extend search path to include local project and user dir where ccrp.lang is installed
	GPtrArray *paths = g_ptr_array_new();
	const gchar * const *old_paths = gtk_source_language_manager_get_search_path(lm);
	if (old_paths) {
		for (int i = 0; old_paths[i]; i++) g_ptr_array_add(paths, g_strdup(old_paths[i]));
	}
	gchar *cwd = g_get_current_dir();
	g_ptr_array_add(paths, cwd);
	gchar *user_spec = g_build_filename(g_get_home_dir(), ".local", "share", "gtksourceview-3.0", "language-specs", NULL);
	g_ptr_array_add(paths, user_spec);
	g_ptr_array_add(paths, NULL);
	gtk_source_language_manager_set_search_path(lm, (gchar **)paths->pdata);
	// Keep strings alive; free only the container
	g_ptr_array_free(paths, FALSE);

	GtkSourceLanguage *lang = gtk_source_language_manager_get_language(lm, "ccrp");
	if (lang) {
		gtk_source_buffer_set_language(app->source_buffer, lang);
	}

	GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default();
	GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme(sm, "oblivion");
	if (!scheme) scheme = gtk_source_style_scheme_manager_get_scheme(sm, "solarized-dark");
	if (!scheme) scheme = gtk_source_style_scheme_manager_get_scheme(sm, "classic");
	if (scheme) gtk_source_buffer_set_style_scheme(app->source_buffer, scheme);
}

static void populate_tree_dir(App *app, GtkTreeIter *parent, const gchar *dir_path) {
	GError *err = NULL;
	GDir *dir = g_dir_open(dir_path, 0, &err);
	if (!dir) return;
	const gchar *name;
	while ((name = g_dir_read_name(dir)) != NULL) {
		if (g_str_equal(name, ".") || g_str_equal(name, "..")) continue;
		gchar *child_path = g_build_filename(dir_path, name, NULL);
		gboolean is_dir = g_file_test(child_path, G_FILE_TEST_IS_DIR);
		GtkTreeIter it;
		gtk_tree_store_append(app->store, &it, parent);
		gtk_tree_store_set(app->store, &it,
			COL_DISPLAY, name,
			COL_PATH, child_path,
			COL_IS_DIR, is_dir,
			-1);
		if (is_dir) populate_tree_dir(app, &it, child_path);
		g_free(child_path);
	}
	g_dir_close(dir);
}

static void rebuild_tree(App *app) {
	gtk_tree_store_clear(app->store);
	if (!app->root_dir) return;
	GtkTreeIter root;
	gtk_tree_store_append(app->store, &root, NULL);
	gtk_tree_store_set(app->store, &root,
		COL_DISPLAY, app->root_dir,
		COL_PATH, app->root_dir,
		COL_IS_DIR, TRUE,
		-1);
	populate_tree_dir(app, &root, app->root_dir);
}

static void on_tree_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	App *app = (App *)user_data;
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		gchar *fpath = NULL;
		gboolean is_dir = FALSE;
		gtk_tree_model_get(model, &iter, COL_PATH, &fpath, COL_IS_DIR, &is_dir, -1);
		if (fpath && !is_dir) {
			load_file_into_buffer(app, fpath);
		}
		if (fpath) g_free(fpath);
	}
}

static void on_open_folder(GtkButton *btn, gpointer user_data) {
	App *app = (App *)user_data;
	gchar *folder = choose_folder(GTK_WINDOW(app->window));
	if (!folder) return;
	g_free(app->root_dir);
	app->root_dir = folder;
	rebuild_tree(app);
	status(app, "Opened folder %s", app->root_dir);
}

static void activate(GtkApplication *gapp, gpointer user_data) {
	App *app = g_new0(App, 1);

	app->window = gtk_application_window_new(gapp);
	gtk_window_set_title(GTK_WINDOW(app->window), "Cryptic IDE");
	gtk_window_set_default_size(GTK_WINDOW(app->window), 1000, 650);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(app->window), vbox);

	app->header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app->header), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(app->header), "Cryptic IDE");
	gtk_window_set_titlebar(GTK_WINDOW(app->window), app->header);

	app->open_button = gtk_button_new_with_label("Open");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header), app->open_button);
	g_signal_connect(app->open_button, "clicked", G_CALLBACK(on_open), app);

	app->open_folder_button = gtk_button_new_with_label("Open Folder");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header), app->open_folder_button);
	g_signal_connect(app->open_folder_button, "clicked", G_CALLBACK(on_open_folder), app);

	app->save_button = gtk_button_new_with_label("Save");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header), app->save_button);
	g_signal_connect(app->save_button, "clicked", G_CALLBACK(on_save), app);

	app->pick_interp_button = gtk_button_new_with_label("Interpreter");
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header), app->pick_interp_button);
	g_signal_connect(app->pick_interp_button, "clicked", G_CALLBACK(on_pick_interpreter), app);

	app->run_button = gtk_button_new_with_label("Run");
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header), app->run_button);
	g_signal_connect(app->run_button, "clicked", G_CALLBACK(on_run), app);

	// Paned with sidebar + editor
	app->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(vbox), app->paned, TRUE, TRUE, 0);

	// Sidebar tree
	app->store = gtk_tree_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	app->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->store));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", COL_DISPLAY, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree), col);
	GtkWidget *tree_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(tree_scroller), app->tree);
	gtk_widget_set_size_request(tree_scroller, 260, -1);
	gtk_paned_add1(GTK_PANED(app->paned), tree_scroller);
	g_signal_connect(app->tree, "row-activated", G_CALLBACK(on_tree_row_activated), app);

	// Editor
	app->scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app->scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_paned_add2(GTK_PANED(app->paned), app->scroller);

	app->source_buffer = GTK_SOURCE_BUFFER(gtk_source_buffer_new(NULL));
	app->source_view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(app->source_buffer));
	gtk_widget_set_hexpand(GTK_WIDGET(app->source_view), TRUE);
	gtk_widget_set_vexpand(GTK_WIDGET(app->source_view), TRUE);
	gtk_container_add(GTK_CONTAINER(app->scroller), GTK_WIDGET(app->source_view));
	gtk_source_view_set_show_line_numbers(app->source_view, TRUE);
	gtk_source_view_set_highlight_current_line(app->source_view, TRUE);

	// Load settings and language/theme
	load_config(app);
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