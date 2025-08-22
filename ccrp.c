#include "ccrp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/*
 * CCRP interpreter core
 * - Variables: integers and strings
 * - Control flow: if/else/endif, while/endwhile
 * - I/O: input (int), input_text (string), print (supports comma-separated parts)
 * - Libraries: [src]lib loads src/lib.cr (Crypton), supports Rust-like `fn name(args) {}`
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
    snprintf(filename, sizeof(filename), "src/%s.cr", lib_name);
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
    char trimmed_line[256];
    if (sscanf(line, " %255s", trimmed_line) != 1) return;

    if (control_state.skip_to_end &&
        strncmp(trimmed_line, "else", 4) != 0 &&
        strncmp(trimmed_line, "endif", 5) != 0 &&
        strncmp(trimmed_line, "endwhile", 8) != 0) return;

    // Library import: [src] math
    if (strncmp(trimmed_line, "[src]", 5) == 0) {
        char lib[50];
        if (sscanf(line, "[src] %49s", lib) == 1) { import_lib(lib); load_library(lib); }
        return;
    }

    // Function definition: accept both `function` and `fn`
    if (strncmp(trimmed_line, "function", 8) == 0 || strncmp(trimmed_line, "fn", 2) == 0) {
        handle_function_definition_line(line);
        return;
    }

    // Control flow
    if (strncmp(trimmed_line, "if", 2) == 0) {
        char condition[256]; if (sscanf(line, "if %255[^\n]", condition) == 1) handle_if_statement(condition); return;
    }
    if (strncmp(trimmed_line, "else", 4) == 0) { handle_else_statement(); return; }
    if (strncmp(trimmed_line, "endif", 5) == 0) { handle_endif_statement(); return; }
    if (strncmp(trimmed_line, "while", 5) == 0) {
        char condition[256]; if (sscanf(line, "while %255[^\n]", condition) == 1) handle_while_statement(condition); return;
    }
    if (strncmp(trimmed_line, "endwhile", 8) == 0) { handle_endwhile_statement(); return; }

    // Input
    if (strncmp(line, "input_text ", 11) == 0) { handle_input_text_statement(line); return; }
    if (strncmp(line, "input ", 6) == 0) { handle_input_statement(line); return; }

    // Print: supports comma-separated items
    if (strncmp(line, "print ", 6) == 0) {
        char expr[256];
        if (sscanf(line, "print %255[^\n]", expr) == 1) {
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
    if (sscanf(line, "%49[^ ] = %255[^\n]", var, rhs) == 2) {
        if (rhs[0] == '"' && rhs[strlen(rhs)-1] == '"') {
            rhs[strlen(rhs)-1] = '\0'; set_string_var(var, rhs + 1);
        } else {
            set_var(var, eval_expr(rhs));
        }
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