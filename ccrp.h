#ifndef CCRP_H
#define CCRP_H

#include <glib.h>

#define MAX_VARS 100
#define MAX_LIBS 10
#define MAX_STACK_DEPTH 100
#define MAX_STRING_LENGTH 256
#define MAX_FUNCTIONS 50
#define MAX_FUNCTION_NAME 50
#define MAX_FUNCTION_BODY 1000

typedef struct {
    char name[50];
    int value;
    char string_value[MAX_STRING_LENGTH];
    int is_string;
} Variable;

typedef struct {
    char name[MAX_FUNCTION_NAME];
    char body[MAX_FUNCTION_BODY];
    int start_line;
    int end_line;
    int param_count;
    char params[10][50]; // Support up to 10 parameters
} Function;

// Control flow state
typedef struct {
    int in_if_block;
    int if_condition_true;
    int in_while_loop;
    int while_condition_true;
    int loop_start_line;
    int skip_to_end;
    int in_function;
    char current_function[MAX_FUNCTION_NAME];
    int function_return_value;
    int should_return;
} ControlState;

// Function pointer for getting GUI input
extern int (*get_input_from_gui)(const char *prompt);
extern char* (*get_text_input_from_gui)(const char *prompt);

// Core interpreter functions
void interpret(const gchar *code);
void interpret_lines(char **lines, int line_count, int start_line);
int get_var(const char *name);
void set_var(const char *name, int value);
char* get_string_var(const char *name);
void set_string_var(const char *name, const char *value);
int eval_expr(const char *expr);
int eval_condition(const char *condition);
void run_line(const char *line);
void import_lib(const char *lib);
int lib_enabled(const char *lib);

// Function management
void define_function(const char *name, const char *body, int start_line, int end_line);
Function* get_function(const char *name);
int call_function(const char *name, char **args, int arg_count);
void parse_function_parameters(const char *func_def, char *name, char params[10][50], int *param_count);

// Library loading
void load_library(const char *lib_name);
char* read_library_file(const char *lib_name);

// Math functions
int math_function(const char *func_name, int arg);
int math_function_two_args(const char *func_name, int arg1, int arg2);

// Control flow functions
void handle_if_statement(const char *condition);
void handle_else_statement(void);
void handle_endif_statement(void);
void handle_while_statement(const char *condition);
void handle_endwhile_statement(void);
void handle_function_definition(const char *line);
void handle_return_statement(const char *line);

// Input functions
void handle_input_statement(const char *line);
void handle_input_text_statement(const char *line);

// Utility functions
char** split_lines(const char *code, int *line_count);
void free_lines(char **lines, int line_count);
int find_matching_end(char **lines, int line_count, int start_line, const char *start_keyword, const char *end_keyword);

#endif // CCRP_H