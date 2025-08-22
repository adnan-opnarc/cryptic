#include "ccrp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <gtk/gtk.h>

/*
 * CCRP Interpreter core
 *
 * Features:
 * - Variables: int and string
 * - Control: if/else, while
 * - I/O: print, input, input_text
 * - Libraries: [src]lib loads src/lib.crh (Crypton), supports Rust-like `fn name(args) {}`
 */

Variable vars[MAX_VARS];
int var_count = 0;

Function functions[MAX_FUNCTIONS];
int function_count = 0;

char active_libs[MAX_LIBS][50];
int lib_count = 0;

ControlState control_state = {0};
char **current_lines = NULL;
int current_line_count = 0;
int current_line_index = 0;

int (*get_input_from_gui)(const char *prompt) = NULL;
char* (*get_text_input_from_gui)(const char *prompt) = NULL;

// ------------------------ GTK lightweight runtime ------------------------
static int gtk_initialized = 0;
static GHashTable *gtk_objects = NULL; // name -> GtkWidget*
static GtkWidget *gtk_main_window = NULL;
static GPtrArray *css_providers = NULL; // keep providers alive

static void ensure_gtk_initialized(void) {
    if (!gtk_initialized) {
        int argc = 0; char **argv = NULL;
        gtk_init(&argc, &argv);
        gtk_objects = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        css_providers = g_ptr_array_new();
        gtk_initialized = 1;
    }
}

static GtkWidget* gtk_get(const char *name) {
    if (!gtk_initialized || !gtk_objects) return NULL;
    return GTK_WIDGET(g_hash_table_lookup(gtk_objects, name));
}

static void gtk_put(const char *name, GtkWidget *w) {
    ensure_gtk_initialized();
    g_hash_table_insert(gtk_objects, g_strdup(name), w);
}

static void handle_gtk_command(const char *line) {
    // Requires gtk library enabled
    if (!lib_enabled("gtk")) {
        printf("Error: 'gtk' library not imported. Add #[gtk] first.\n");
        return;
    }
    ensure_gtk_initialized();

    // Parse create
    {
        char kind[32], type[32], name[64];
        if (sscanf(line, "gtk %31s %31s %63s", kind, type, name) == 3 && strcmp(kind, "create") == 0) {
            GtkWidget *w = NULL;
            if (strcmp(type, "window") == 0) {
                w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
                g_signal_connect(w, "destroy", G_CALLBACK(gtk_main_quit), NULL);
                if (!gtk_main_window) gtk_main_window = w;
            } else if (strcmp(type, "button") == 0) {
                w = gtk_button_new();
            } else if (strcmp(type, "input") == 0 || strcmp(type, "entry") == 0) {
                w = gtk_entry_new();
            } else if (strcmp(type, "boxv") == 0 || strcmp(type, "vbox") == 0) {
                w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            } else if (strcmp(type, "boxh") == 0 || strcmp(type, "hbox") == 0) {
                w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            } else if (strcmp(type, "label") == 0) {
                w = gtk_label_new("");
            } else if (strcmp(type, "image") == 0) {
                w = gtk_image_new();
            }
            if (w) {
                gtk_put(name, w);
            } else {
                printf("Error: unknown gtk create type '%s'\n", type);
            }
            return;
        }
    }

    // Parse set NAME prop ...
    {
        char dummy[8], name[64], prop[32];
        if (sscanf(line, "gtk %7s %63s %31s", dummy, name, prop) >= 3 && strcmp(dummy, "set") == 0) {
            GtkWidget *w = gtk_get(name);
            if (!w) { printf("Error: gtk object '%s' not found\n", name); return; }
            if (strcmp(prop, "title") == 0) {
                char title[256];
                if (sscanf(line, "gtk set %*s title \"%255[^\"]\"", title) == 1) {
                    if (GTK_IS_WINDOW(w)) gtk_window_set_title(GTK_WINDOW(w), title);
                }
            } else if (strcmp(prop, "size") == 0) {
                int wpx=0, hpx=0;
                if (sscanf(line, "gtk set %*s size %dx%d", &wpx, &hpx) == 2) {
                    if (GTK_IS_WINDOW(w)) gtk_window_set_default_size(GTK_WINDOW(w), wpx, hpx);
                    else gtk_widget_set_size_request(w, wpx, hpx);
                }
            } else if (strcmp(prop, "text") == 0) {
                char text[256];
                if (sscanf(line, "gtk set %*s text \"%255[^\"]\"", text) == 1) {
                    if (GTK_IS_BUTTON(w)) gtk_button_set_label(GTK_BUTTON(w), text);
                    else if (GTK_IS_LABEL(w)) gtk_label_set_text(GTK_LABEL(w), text);
                    else if (GTK_IS_ENTRY(w)) gtk_entry_set_text(GTK_ENTRY(w), text);
                }
            } else {
                printf("Error: unknown gtk property '%s'\n", prop);
            }
            return;
        }
    }

    // Parse add PARENT CHILD
    {
        char parent[64], child[64];
        if (sscanf(line, "gtk add %63s %63s", parent, child) == 2) {
            GtkWidget *pw = gtk_get(parent);
            GtkWidget *cw = gtk_get(child);
            if (!pw || !cw) { printf("Error: gtk add missing widgets\n"); return; }
            if (GTK_IS_WINDOW(pw)) {
                GtkWidget *existing = gtk_bin_get_child(GTK_BIN(pw));
                if (existing) gtk_container_remove(GTK_CONTAINER(pw), existing);
                gtk_container_add(GTK_CONTAINER(pw), cw);
            } else if (GTK_IS_BOX(pw)) {
                gtk_box_pack_start(GTK_BOX(pw), cw, FALSE, FALSE, 0);
            } else if (GTK_IS_CONTAINER(pw)) {
                gtk_container_add(GTK_CONTAINER(pw), cw);
            } else {
                printf("Error: parent '%s' not a container\n", parent);
            }
            return;
        }
    }

    // Parse show NAME
    {
        char name[64];
        if (sscanf(line, "gtk show %63s", name) == 1) {
            GtkWidget *w = gtk_get(name);
            if (!w) { printf("Error: gtk object '%s' not found\n", name); return; }
            gtk_widget_show_all(w);
            if (GTK_IS_WINDOW(w)) gtk_main_window = w;
            return;
        }
    }

    // Parse run
    if (strncmp(line, "gtk run", 7) == 0) {
        if (gtk_main_window) {
            gtk_widget_show_all(gtk_main_window);
        }
        gtk_main();
        return;
    }

    printf("Error: unknown gtk command.\n");
}

// Apply CSS from a style block
static void handle_style_block(const char *first_line) {
    ensure_gtk_initialized();
    // Extract target between 'style' and '{'
    char target[64] = "";
    const char *brace = strchr(first_line, '{');
    if (brace) {
        char header[256];
        int len = (int)(brace - first_line);
        if (len > 255) len = 255;
        strncpy(header, first_line, len); header[len] = '\0';
        sscanf(header, " %*s %63s", target);
    }
    // Collect inner CSS lines between matching braces
    int depth = 0; int start_i = current_line_index; int end_i = -1;
    GString *css_inner = g_string_new("");
    for (int i = start_i; i < current_line_count; i++) {
        const char *ln = current_lines[i];
        const char *p = ln;
        while (*p) {
            if (*p == '{') { depth++; if (depth == 1) {
                // include tail after '{' on same line
                const char *after = p + 1;
                while (*after && *after != '}') after++;
            } }
            else if (*p == '}') { depth--; if (depth == 0) { end_i = i; break; } }
            p++;
        }
        if (i > start_i) {
            // Append line content (strip // comments)
            char buf[512]; strncpy(buf, ln, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            char *com = strstr(buf, "//"); if (com) *com = '\0';
            g_string_append(buf[0] ? css_inner : css_inner, buf);
            g_string_append(css_inner, "\n");
        }
        if (end_i != -1) break;
    }
    if (end_i == -1) { g_string_free(css_inner, TRUE); return; }

    // Build final CSS
    GString *css = g_string_new("");
    int is_global = 0;
    if (target[0] == '\0' || strcmp(target, "*") == 0) { is_global = 1; }

    if (!is_global) {
        GtkWidget *w = gtk_get(target);
        if (w) {
            gtk_widget_set_name(w, target);
            g_string_append_printf(css, "#%s {\n%s}\n", target, css_inner->str);
            GtkCssProvider *prov = gtk_css_provider_new();
            gtk_css_provider_load_from_data(prov, css->str, -1, NULL);
            gtk_style_context_add_provider(gtk_widget_get_style_context(w), GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_USER);
            g_ptr_array_add(css_providers, prov);
        } else {
            // Treat as type selector (e.g., button, entry, label, window)
            g_string_append_printf(css, "%s {\n%s}\n", target, css_inner->str);
            GtkCssProvider *prov = gtk_css_provider_new();
            gtk_css_provider_load_from_data(prov, css->str, -1, NULL);
            GdkScreen *screen = gdk_screen_get_default();
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_USER);
            g_ptr_array_add(css_providers, prov);
        }
    } else {
        // Global * selector
        g_string_append_printf(css, "* {\n%s}\n", css_inner->str);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_data(prov, css->str, -1, NULL);
        GdkScreen *screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_USER);
        g_ptr_array_add(css_providers, prov);
    }

    g_string_free(css_inner, TRUE);
    g_string_free(css, TRUE);
    // Advance interpreter to end of block
    current_line_index = end_i;
}

// ------------------------ Utility: split/join lines ------------------------
char** split_lines(const char *code, int *line_count) {
    char *code_copy = g_strdup(code);
    char **lines = NULL;
    int count = 0;
    int capacity = 10;
    
    lines = malloc(capacity * sizeof(char*));
    
    char *line = strtok(code_copy, "\n");
    while (line != NULL) {
        if (count >= capacity) {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(char*));
        }
        lines[count] = g_strdup(line);
        count++;
        line = strtok(NULL, "\n");
    }
    
    g_free(code_copy);
    *line_count = count;
    return lines;
}

void free_lines(char **lines, int line_count) {
    for (int i = 0; i < line_count; i++) {
        g_free(lines[i]);
    }
    free(lines);
}

int find_matching_end(char **lines, int line_count, int start_line, const char *start_keyword, const char *end_keyword) {
    int depth = 1;
    for (int i = start_line + 1; i < line_count; i++) {
        char trimmed[256];
        if (sscanf(lines[i], " %255s", trimmed) == 1) {
            if (strncmp(trimmed, start_keyword, strlen(start_keyword)) == 0) {
                depth++;
            } else if (strncmp(trimmed, end_keyword, strlen(end_keyword)) == 0) {
                depth--;
                if (depth == 0) {
                    return i;
                }
            }
        }
    }
    return -1;
}

// ------------------------ Variables ------------------------
int get_var(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0) return vars[i].value;
    return 0;
}

void set_var(const char *name, int value) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            vars[i].value = value;
            vars[i].is_string = 0;
            return;
        }
    }
    if (var_count < MAX_VARS) {
        strcpy(vars[var_count].name, name);
        vars[var_count].value = value;
        vars[var_count].is_string = 0;
        var_count++;
    } else {
        printf("Error: Max variables reached.\n");
    }
}

char* get_string_var(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0 && vars[i].is_string)
            return vars[i].string_value;
    return "";
}

void set_string_var(const char *name, const char *value) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            strncpy(vars[i].string_value, value, MAX_STRING_LENGTH - 1);
            vars[i].string_value[MAX_STRING_LENGTH - 1] = '\0';
            vars[i].is_string = 1;
            return;
        }
    }
    if (var_count < MAX_VARS) {
        strcpy(vars[var_count].name, name);
        strncpy(vars[var_count].string_value, value, MAX_STRING_LENGTH - 1);
        vars[var_count].string_value[MAX_STRING_LENGTH - 1] = '\0';
        vars[var_count].is_string = 1;
        var_count++;
    } else {
        printf("Error: Max variables reached.\n");
    }
}

// ------------------------ Function table ------------------------
void define_function(const char *name, const char *body, int start_line, int end_line) {
    if (function_count < MAX_FUNCTIONS) {
        strcpy(functions[function_count].name, name);
        strncpy(functions[function_count].body, body, MAX_FUNCTION_BODY - 1);
        functions[function_count].body[MAX_FUNCTION_BODY - 1] = '\0';
        functions[function_count].start_line = start_line;
        functions[function_count].end_line = end_line;
        functions[function_count].param_count = 0;
        function_count++;
    } else {
        printf("Error: Max functions reached.\n");
    }
}

Function* get_function(const char *name) {
    for (int i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0) return &functions[i];
    }
    return NULL;
}

// ------------------------ Library loader ------------------------
static int has_prefix_word(const char *line, const char *word) {
    char first[256];
    if (sscanf(line, " %255s", first) != 1) return 0;
    return strncmp(first, word, strlen(word)) == 0;
}

char* read_library_file(const char *lib_name) {
    char filename[256];
    snprintf(filename, sizeof(filename), "src/%s.crh", lib_name);
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *content = malloc(file_size + 1);
    if (!content) { fclose(file); return NULL; }
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    fclose(file);
    return content;
}

void load_library(const char *lib_name) {
    char *lib_content = read_library_file(lib_name);
    if (!lib_content) return;

    int line_count;
    char **lines = split_lines(lib_content, &line_count);

    for (int i = 0; i < line_count; i++) {
        // Accept both `function` and Rust-like `fn`
        if (has_prefix_word(lines[i], "function") || has_prefix_word(lines[i], "fn")) {
            char func_def[256];
            if (sscanf(lines[i], "%*s %[^{]", func_def) == 1) {
                // Extract name up to '('
                char func_name[MAX_FUNCTION_NAME] = {0};
                const char *paren = strchr(func_def, '(');
                if (paren) {
                    size_t name_len = (size_t)(paren - func_def);
                    if (name_len >= sizeof(func_name)) name_len = sizeof(func_name) - 1;
                    strncpy(func_name, func_def, name_len);
                    func_name[name_len] = '\0';

                    // Find closing '}' matching this block
                    int body_start = i + 1;
                    int depth = 1;
                    int body_end = -1;
                    for (int j = body_start; j < line_count; j++) {
                        if (strstr(lines[j], "{") != NULL) depth++;
                        if (strstr(lines[j], "}") != NULL) {
                            depth--;
                            if (depth == 0) { body_end = j; break; }
                        }
                    }
                    if (body_end > body_start) {
                        char body[MAX_FUNCTION_BODY] = "";
                        for (int j = body_start; j < body_end; j++) {
                            strcat(body, lines[j]);
                            strcat(body, "\n");
                        }
                        define_function(func_name, body, body_start, body_end);
                        i = body_end;
                    }
                }
            }
        }
    }

    free_lines(lines, line_count);
    free(lib_content);
}

// ------------------------ Math builtins (guarded by [src] math) ------------------------
int math_function(const char *func_name, int arg) {
    if (!lib_enabled("math")) {
        printf("Error: 'math' library not imported for %s.\n", func_name);
        return 0;
    }
    if (strcmp(func_name, "sqrt") == 0) return (int)sqrt((double)arg);
    if (strcmp(func_name, "abs") == 0) return abs(arg);
    if (strcmp(func_name, "sin") == 0) return (int)(sin((double)arg) * 1000);
    if (strcmp(func_name, "cos") == 0) return (int)(cos((double)arg) * 1000);
    if (strcmp(func_name, "tan") == 0) return (int)(tan((double)arg) * 1000);
    if (strcmp(func_name, "log") == 0) return (int)log((double)arg);
    if (strcmp(func_name, "exp") == 0) return (int)exp((double)arg);
    printf("Error: Unknown math function '%s'.\n", func_name);
    return 0;
}

int math_function_two_args(const char *func_name, int arg1, int arg2) {
    if (!lib_enabled("math")) {
        printf("Error: 'math' library not imported for %s.\n", func_name);
        return 0;
    }
    if (strcmp(func_name, "pow") == 0) return (int)pow((double)arg1, (double)arg2);
    if (strcmp(func_name, "mod") == 0) return arg1 % arg2;
    if (strcmp(func_name, "max") == 0) return (arg1 > arg2) ? arg1 : arg2;
    if (strcmp(func_name, "min") == 0) return (arg1 < arg2) ? arg1 : arg2;
    printf("Error: Unknown two-argument math function '%s'.\n", func_name);
    return 0;
}

// ------------------------ Expression evaluation ------------------------
int eval_expr(const char *expr) {
    char temp_expr[256];
    strcpy(temp_expr, expr);

    // Support chained additions like x + y + z by summing parts outside parentheses
    {
        int plus_count = 0;
        int depth = 0;
        for (const char *p = temp_expr; *p; ++p) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            else if (*p == '+' && depth == 0) plus_count++;
        }
        if (plus_count >= 2) {
            int sum = 0;
            int depth2 = 0;
            const char *start = temp_expr;
            for (const char *p = temp_expr; ; ++p) {
                if (*p == '(') depth2++;
                else if (*p == ')') depth2--;
                if (*p == '+' && depth2 == 0) {
                    // evaluate segment [start, p)
                    char segment[256];
                    int len = (int)(p - start);
                    if (len > 255) len = 255;
                    strncpy(segment, start, len);
                    segment[len] = '\0';
                    // trim leading/trailing spaces
                    char *s = segment; while (*s == ' ') s++;
                    char *e = s + strlen(s) - 1; while (e >= s && *e == ' ') { *e = '\0'; e--; }
                    sum += eval_expr(s);
                    start = p + 1;
                } else if (*p == '\0') {
                    // last segment
                    char segment[256];
                    int len = (int)(p - start);
                    if (len > 255) len = 255;
                    strncpy(segment, start, len);
                    segment[len] = '\0';
                    char *s = segment; while (*s == ' ') s++;
                    char *e = s + strlen(s) - 1; while (e >= s && *e == ' ') { *e = '\0'; e--; }
                    if (*s) sum += eval_expr(s);
                    return sum;
                }
            }
        }
    }

    // Function call: try user-defined functions first (Rust-like or built-in names)
    char func_name[50];
    char *open_paren = strchr(temp_expr, '(');
    if (open_paren) {
        char *close_paren = strrchr(temp_expr, ')');
        if (close_paren && close_paren > open_paren) {
            int name_len = (int)(open_paren - temp_expr);
            if (name_len > 49) name_len = 49;
            strncpy(func_name, temp_expr, name_len);
            func_name[name_len] = '\0';

            Function *user_func = get_function(func_name);
            if (!user_func) {
                // Fall through to built-in math paths below
            } else {
                // NOTE: For simplicity, user-defined functions return 0 (stub). Extend as needed.
                return 0;
            }
        }
    }

    // Built-in math parsing (existing logic)
    char func[50];
    int arg1, arg2;
    if (sscanf(temp_expr, "%49[^(](%d,%d)", func, &arg1, &arg2) == 3) {
        return math_function_two_args(func, arg1, arg2);
    }

    // Handle function calls with two arguments (variables or mixed)
    char arg1_str[50], arg2_str[50];
    if (sscanf(temp_expr, "%49[^(](%49[^,],%49[^)])", func_name, arg1_str, arg2_str) == 3) {
        // Trim whitespace using pointers
        char *arg1p = arg1_str;
        char *arg2p = arg2_str;
        while (*arg1p == ' ') arg1p++;
        while (*arg2p == ' ') arg2p++;
        
        // Parse first argument
        int val1;
        if (sscanf(arg1p, "%d", &val1) == 1) {
            // number
        } else {
            val1 = get_var(arg1p);
        }
        
        // Parse second argument
        int val2;
        if (sscanf(arg2p, "%d", &val2) == 1) {
            // number
        } else {
            val2 = get_var(arg2p);
        }
        
        return math_function_two_args(func_name, val1, val2);
    }

    int arg;
    if (sscanf(temp_expr, "%49[^(](%d)", func_name, &arg) == 2) {
        return math_function(func_name, arg);
    }

    char var_arg[50];
    if (sscanf(temp_expr, "%49[^(](%49[^)])", func_name, var_arg) == 2) {
        if (strchr(var_arg, ',') != NULL) return 0;
        int num_val; if (sscanf(var_arg, "%d", &num_val) == 1) return math_function(func_name, num_val);
        return math_function(func_name, get_var(var_arg));
    }

    // Parentheses
    if (open_paren) {
        char *close_paren = strrchr(temp_expr, ')');
        if (close_paren && close_paren > open_paren) {
            *close_paren = '\0';
            int paren_result = eval_expr(open_paren + 1);
            char new_expr[256];
            *open_paren = '\0';
            sprintf(new_expr, "%s%d%s", temp_expr, paren_result, close_paren + 1);
            return eval_expr(new_expr);
        }
    }

    // Arithmetic or single value/var
    int val1, val2; char var1[50], var2[50]; char op;
    if (sscanf(temp_expr, "%d %c %d", &val1, &op, &val2) == 3) {
        // numbers
    } else if (sscanf(temp_expr, "%49s %c %49s", var1, &op, var2) == 3) {
        val1 = get_var(var1); val2 = get_var(var2);
    } else if (sscanf(temp_expr, "%49s %c %d", var1, &op, &val2) == 3) {
        val1 = get_var(var1);
    } else if (sscanf(temp_expr, "%d %c %49s", &val1, &op, var2) == 3) {
        val2 = get_var(var2);
    } else if (sscanf(temp_expr, "%d", &val1) == 1) {
        return val1;
    } else if (sscanf(temp_expr, "%49s", var1) == 1) {
        return get_var(var1);
    } else {
        return 0;
    }
    switch (op) {
        case '+': return val1 + val2;
        case '-': return val1 - val2;
        case '*': return val1 * val2;
        case '/': return val2 != 0 ? val1 / val2 : 0;
        case '%': return val2 != 0 ? val1 % val2 : 0;
        default: return 0;
    }
}

// ------------------------ Conditions ------------------------
int eval_condition(const char *condition) {
    char temp_condition[256];
    strcpy(temp_condition, condition);
    char *operators[] = {"==", "!=", "<=", ">=", "<", ">"};
    int op_count = 6;
    for (int i = 0; i < op_count; i++) {
        char *op_pos = strstr(temp_condition, operators[i]);
        if (op_pos) {
            *op_pos = '\0';
            char *left_expr = temp_condition;
            char *right_expr = op_pos + strlen(operators[i]);
            while (*left_expr == ' ') left_expr++;
            while (*right_expr == ' ') right_expr++;
            int left_val = eval_expr(left_expr);
            int right_val = eval_expr(right_expr);
            if (strcmp(operators[i], "==") == 0) return left_val == right_val;
            if (strcmp(operators[i], "!=") == 0) return left_val != right_val;
            if (strcmp(operators[i], "<=") == 0) return left_val <= right_val;
            if (strcmp(operators[i], ">=") == 0) return left_val >= right_val;
            if (strcmp(operators[i], "<") == 0) return left_val < right_val;
            if (strcmp(operators[i], ">") == 0) return left_val > right_val;
        }
    }
    return eval_expr(condition) != 0;
}

// ------------------------ Control flow ------------------------
void handle_if_statement(const char *condition) {
    control_state.in_if_block = 1;
    control_state.if_condition_true = eval_condition(condition);
    control_state.skip_to_end = !control_state.if_condition_true;
}

void handle_else_statement(void) {
    if (control_state.in_if_block) {
        control_state.skip_to_end = control_state.if_condition_true;
    }
}

void handle_endif_statement(void) {
    control_state.in_if_block = 0;
    control_state.if_condition_true = 0;
    control_state.skip_to_end = 0;
}

void handle_while_statement(const char *condition) {
    control_state.in_while_loop = 1;
    control_state.while_condition_true = eval_condition(condition);
    control_state.loop_start_line = current_line_index;
    control_state.skip_to_end = !control_state.while_condition_true;
}

void handle_endwhile_statement(void) {
    if (control_state.in_while_loop && control_state.while_condition_true) {
        char *while_line = current_lines[control_state.loop_start_line];
        char condition[256];
        if (sscanf(while_line, "while %255[^\n]", condition) == 1) {
            control_state.while_condition_true = eval_condition(condition);
            if (control_state.while_condition_true) {
                current_line_index = control_state.loop_start_line;
            } else {
                control_state.in_while_loop = 0;
                control_state.while_condition_true = 0;
                control_state.loop_start_line = 0;
                control_state.skip_to_end = 0;
            }
        }
    } else {
        control_state.in_while_loop = 0;
        control_state.while_condition_true = 0;
        control_state.loop_start_line = 0;
        control_state.skip_to_end = 0;
    }
}

static void handle_function_definition_line(const char *line) {
    char func_def[256];
    if (sscanf(line, "%*s %[^{]", func_def) == 1) {
        char func_name[MAX_FUNCTION_NAME] = {0};
        const char *paren = strchr(func_def, '(');
        if (!paren) return;
        size_t name_len = (size_t)(paren - func_def);
        if (name_len >= sizeof(func_name)) name_len = sizeof(func_name) - 1;
        strncpy(func_name, func_def, name_len);
        func_name[name_len] = '\0';
        int body_start = current_line_index + 1;
        int body_end = find_matching_end(current_lines, current_line_count, current_line_index, "{", "}");
        if (body_end > body_start) {
            char body[MAX_FUNCTION_BODY] = "";
            for (int j = body_start; j < body_end; j++) {
                strcat(body, current_lines[j]);
                strcat(body, "\n");
            }
            define_function(func_name, body, body_start, body_end);
            current_line_index = body_end;
        }
    }
}

// ------------------------ Input ------------------------
void handle_input_statement(const char *line) {
    char var[50], prompt[256] = "";
    if (sscanf(line, "input %49s \"%255[^\"]\"", var, prompt) == 2) {
    } else if (sscanf(line, "input %49s", var) == 1) {
        sprintf(prompt, "Enter value for %s: ", var);
    } else { return; }
    if (get_input_from_gui) {
        int val = get_input_from_gui(prompt);
        set_var(var, val);
    } else {
        printf("%s", prompt); fflush(stdout);
        int val; if (scanf("%d", &val) == 1) set_var(var, val);
        int c; while ((c = getchar()) != '\n' && c != EOF);
    }
}

void handle_input_text_statement(const char *line) {
    char var[50], prompt[256] = "";
    if (sscanf(line, "input_text %49s \"%255[^\"]\"", var, prompt) == 2) {
    } else if (sscanf(line, "input_text %49s", var) == 1) {
        sprintf(prompt, "Enter text for %s: ", var);
    } else { return; }
    if (get_text_input_from_gui) {
        char *text = get_text_input_from_gui(prompt);
        if (text) { set_string_var(var, text); free(text); }
    } else {
        printf("%s", prompt); fflush(stdout);
        char text[MAX_STRING_LENGTH];
        if (fgets(text, MAX_STRING_LENGTH, stdin) != NULL) {
            text[strcspn(text, "\n")] = 0;
            set_string_var(var, text);
        }
    }
}

// ------------------------ Dispatcher ------------------------
void run_line(const char *line) {
    // Strip '//' comments
    char raw[512]; strncpy(raw, line, sizeof(raw)-1); raw[sizeof(raw)-1] = '\0';
    char *cpos = strstr(raw, "//");
    if (cpos) *cpos = '\0';

    char trimmed_line[256];
    if (sscanf(raw, " %255s", trimmed_line) != 1) return;

    if (control_state.skip_to_end &&
        strncmp(trimmed_line, "else", 4) != 0 &&
        strncmp(trimmed_line, "endif", 5) != 0 &&
        strncmp(trimmed_line, "endwhile", 8) != 0) return;

    // Style block
    if (strncmp(trimmed_line, "style", 5) == 0 && strchr(raw, '{')) { handle_style_block(raw); return; }

    // Pragma-based library import: #[lib]
    if (trimmed_line[0] == '#' && trimmed_line[1] == '[') {
        char lib[50];
        if (sscanf(raw, " #[%49[^]]]", lib) == 1 || sscanf(raw, "#[%49[^]]]", lib) == 1) {
            import_lib(lib);
            load_library(lib);
        }
        return;
    }

    // Library import: [src] math
    if (strncmp(trimmed_line, "[src]", 5) == 0) {
        char lib[50];
        if (sscanf(raw, "[src] %49s", lib) == 1 || sscanf(raw, " [src] %49s", lib) == 1) { import_lib(lib); load_library(lib); }
        return;
    }

    // GTK commands: only when 'gtk ' (space) form, not 'gtk.' dot-calls
    {
        const char *p = raw; while (*p == ' ') p++;
        if (strncmp(p, "gtk ", 4) == 0) { handle_gtk_command(p); return; }
    }

    // Dot-call sugar:
    // - gtk.window(name)        -> gtk create window name
    // - gtk.button(name)        -> gtk create button name
    // - obj.title("...")        -> gtk set obj title "..."
    // - obj.text("...")         -> gtk set obj text "..."
    // - obj.size(400x600|w,h)   -> gtk set obj size WxH
    // - parent.add(child)       -> gtk add parent child
    // - obj.show()              -> gtk show obj
    // - gtk.run()               -> gtk run
    {
        char ident[64], method[64], args[256];
        // gtk.<method>(args)
        if (sscanf(raw, "gtk.%63[^ (](%255[^)])", method, args) >= 1) {
            if (strcmp(method, "run") == 0) {
                handle_gtk_command("gtk run");
                return;
            } else {
                // creators take single name
                char name[64];
                if (sscanf(args, " %63[^ )]", name) == 1) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "gtk create %s %s", method, name);
                    handle_gtk_command(buf);
                    return;
                }
            }
        }
        // <ident>.<method>(args)
        if (sscanf(raw, " %63[^ .].%63[^ (](%255[^)])", ident, method, args) >= 2) {
            // trim args whitespace
            char *ap = args; while (*ap==' ') ap++;
            // .show()
            if (strcmp(method, "show") == 0) {
                char buf[256]; snprintf(buf, sizeof(buf), "gtk show %s", ident);
                handle_gtk_command(buf); return;
            }
            // .add(child)
            if (strcmp(method, "add") == 0) {
                char child[64]; if (sscanf(ap, " %63[^ )]", child) == 1) {
                    char buf[256]; snprintf(buf, sizeof(buf), "gtk add %s %s", ident, child);
                    handle_gtk_command(buf); return;
                }
            }
            // .title("...") or .text("...")
            if (strcmp(method, "title") == 0 || strcmp(method, "text") == 0) {
                char str[256]; if (sscanf(raw, " %*[^ .].%*[^ (](\"%255[^\"]\")", str) == 1) {
                    char buf[512]; snprintf(buf, sizeof(buf), "gtk set %s %s \"%s\"", ident, method, str);
                    handle_gtk_command(buf); return;
                }
            }
            // .size(400x600) or .size(w,h)
            if (strcmp(method, "size") == 0) {
                int w=0,h=0;
                if (sscanf(ap, "%dx%d", &w, &h) == 2 || sscanf(ap, "%d , %d", &w, &h) == 2) {
                    char buf[256]; snprintf(buf, sizeof(buf), "gtk set %s size %dx%d", ident, w, h);
                    handle_gtk_command(buf); return;
                }
            }
        }
    }

    // class NAME TYPE  -> shortcut for gtk create TYPE NAME
    if (strncmp(trimmed_line, "class", 5) == 0) {
        char name[64], type[32];
        if (sscanf(raw, "class %63s %31s", name, type) == 2) {
            char buf[160]; snprintf(buf, sizeof(buf), "gtk create %s %s", type, name);
            handle_gtk_command(buf);
        }
        return;
    }

    // Function definition: accept both `function` and `fn`
    if (strncmp(trimmed_line, "function", 8) == 0 || strncmp(trimmed_line, "fn", 2) == 0) {
        handle_function_definition_line(raw);
        return;
    }

    // Control flow
    if (strncmp(trimmed_line, "if", 2) == 0) {
        char condition[256]; if (sscanf(raw, "if %255[^\n]", condition) == 1) handle_if_statement(condition); return;
    }
    if (strncmp(trimmed_line, "else", 4) == 0) { handle_else_statement(); return; }
    if (strncmp(trimmed_line, "endif", 5) == 0) { handle_endif_statement(); return; }
    if (strncmp(trimmed_line, "while", 5) == 0) {
        char condition[256]; if (sscanf(raw, "while %255[^\n]", condition) == 1) handle_while_statement(condition); return;
    }
    if (strncmp(trimmed_line, "endwhile", 8) == 0) { handle_endwhile_statement(); return; }

    // Input
    if (strncmp(raw, "input_text ", 11) == 0) { handle_input_text_statement(raw); return; }
    if (strncmp(raw, "input ", 6) == 0) { handle_input_statement(raw); return; }

    // Print: supports comma-separated items
    if (strncmp(raw, "print ", 6) == 0) {
        char expr[256];
        if (sscanf(raw, "print %255[^\n]", expr) == 1) {
            if (expr[0] == '"' && expr[strlen(expr)-1] == '"') {
                expr[strlen(expr)-1] = '\0'; printf("%s\n", expr + 1);
            } else {
                char *current = expr;
                int has_comma = strchr(expr, ',') != NULL;
                if (!has_comma) {
                    // single item: try string var else eval
                    for (int i = 0; i < var_count; i++) {
                        if (strcmp(vars[i].name, expr) == 0 && vars[i].is_string) { printf("%s\n", vars[i].string_value); return; }
                    }
                    printf("%d\n", eval_expr(expr));
                } else {
                    while (current && *current) {
                        while (*current == ' ') current++;
                        if (!*current) break;
                        char temp[256];
                        char *next_comma = strchr(current, ',');
                        if (next_comma) {
                            int len = (int)(next_comma - current);
                            if (len > 255) len = 255;
                            strncpy(temp, current, len); temp[len] = '\0';
                            current = next_comma + 1;
                        } else {
                            strncpy(temp, current, 255); temp[255] = '\0';
                            current = NULL;
                        }
                        char *t = temp; while (*t == ' ') t++;
                        char *e = t + strlen(t) - 1; while (e > t && *e == ' ') e--; *(e+1)='\0';
                        if (t[0] == '"' && t[strlen(t)-1] == '"') { t[strlen(t)-1] = '\0'; printf("%s", t+1); }
                        else {
                            int printed = 0;
                            for (int i = 0; i < var_count; i++) {
                                if (strcmp(vars[i].name, t) == 0 && vars[i].is_string) { printf("%s", vars[i].string_value); printed=1; break; }
                            }
                            if (!printed) printf("%d", eval_expr(t));
                        }
                    }
                    printf("\n");
                }
            }
        }
        return;
    }

    // Assignment: string or numeric
    char var[50], rhs[256];
    if (sscanf(raw, "%49[^ ] = %255[^\n]", var, rhs) == 2) {
        if (rhs[0] == '"' && rhs[strlen(rhs)-1] == '"') {
            rhs[strlen(rhs)-1] = '\0'; set_string_var(var, rhs + 1);
        } else {
            set_var(var, eval_expr(rhs));
        }
        return;
    }

    // Ignore solitary braces for grouping blocks
    if (strcmp(trimmed_line, "{") == 0 || strcmp(trimmed_line, "}") == 0) {
        return;
    }
}

// ------------------------ Libraries table ------------------------
void import_lib(const char *lib) {
    if (lib_count < MAX_LIBS) {
        strcpy(active_libs[lib_count++], lib);
    } else {
        printf("Error: Max libraries reached.\n");
    }
}

int lib_enabled(const char *lib) {
    for (int i = 0; i < lib_count; i++) if (strcmp(active_libs[i], lib) == 0) return 1;
    return 0;
}

// ------------------------ Interpreter driver ------------------------
void interpret_lines(char **lines, int line_count, int start_line) {
    current_lines = lines; current_line_count = line_count;
    for (current_line_index = start_line; current_line_index < line_count; current_line_index++) {
        run_line(lines[current_line_index]);
        if (current_line_index < start_line) start_line = current_line_index;
    }
}

void interpret(const gchar *code) {
    int line_count; char **lines = split_lines(code, &line_count);
    control_state.in_if_block = 0; control_state.if_condition_true = 0;
    control_state.in_while_loop = 0; control_state.while_condition_true = 0;
    control_state.loop_start_line = 0; control_state.skip_to_end = 0;
    control_state.in_function = 0; control_state.should_return = 0; control_state.function_return_value = 0;
    interpret_lines(lines, line_count, 0);
    free_lines(lines, line_count); current_lines = NULL; current_line_count = 0;
}