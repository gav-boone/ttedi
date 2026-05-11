/* includes */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <conio.h>
#include <windows.h>
#include <wincon.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>

/* defines */
#define CTRL_KEY(k) ((k) & 0x1f)
#define TEXT_ED_VERSION "0.0.1"
#define TEXT_ED_TAB_STOP (E.syntax ? E.syntax->tab_stop : 4)
#define TEXT_ED_QUIT_TIMES 1
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,
};

enum editorHighlight {
    HL_DEFAULT = 0,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_MATCH,
    HL_NUMBER,
    HL_MLCOMMENT,
    HL_FUNC,
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

/* data */
struct editorSyntax {
    char* filetype;
    char** filematch;
    char** keywords;
    char* singleline_comment_start;
    char* ml_comment_start;
    char* ml_comment_end;
    int flags;
    int tab_stop;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char* chars;
    char* render;
    unsigned char* hl;
    int hl_open_comment;
} erow;

struct editorConfig {
    DWORD originalMode;
    HANDLE hStdin;
    HANDLE hStdout;
    int screenRows;
    int screenCols;
    int cx, cy;
    int rx;
    int saved_cx;
    int numRows;
    int rowoff;
    int coloff;
    erow* row;
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int dirty;
    struct editorSyntax* syntax;
};

struct editorConfig E;

/* filetypes */
char* C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char* C_HL_keywords[] = {
    "#include", "#define", "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|", "const|",  NULL
};

char* PY_HL_extensions[] = { ".py", NULL };
char* PY_HL_keywords[] = {
    "import", "from", "as", "if", "elif", "else", "for", "while", "break", "continue", "return", "def", "class", "try", "except", "finally", "raise", "with", "yield", "pass", "lambda", "and", "or", "not", "in", "is", "global", "nonlocal", "assert", "del",
    "int|", "float|", "str|", "bool|", "list|", "dict|", "tuple|", "set|", "None|", "True|", "False|",  NULL
};

char* JS_HL_extensions[] = { ".js", ".ts", ".jsx", ".tsx", NULL };
char* JS_HL_keywords[] = {
    "if", "else", "for", "while", "do", "switch", "case", "break", "continue", "return", "function", "class", "new", "delete", "typeof", "instanceof", "try", "catch", "finally", "throw", "import", "export", "default", "from", "const", "let", "var", "async", "await", "yield", "of", "in",
    "undefined|", "null|", "true|", "false|", "NaN|", "Infinity|", "this|", "super|",  NULL
};

char* JAVA_HL_extensions[] = { ".java", NULL };
char* JAVA_HL_keywords[] = {
    "if", "else", "for", "while", "do", "switch", "case", "break", "continue", "return", "class", "interface", "extends", "implements", "new", "instanceof", "try", "catch", "finally", "throw", "throws", "import", "package", "public", "private", "protected", "static", "final", "abstract", "synchronized", "volatile", "transient", "native", "enum", "assert", "default", "super", "this",
    "int|", "long|", "double|", "float|", "char|", "byte|", "short|", "boolean|", "void|", "String|", "null|", "true|", "false|",  NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4,
    },
    {
        "python",
        PY_HL_extensions,
        PY_HL_keywords,
        "#", "\"\"\"", "\"\"\"",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4,
    },
    {
        "js",
        JS_HL_extensions,
        JS_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        2,
    },
    {
        "java",
        JAVA_HL_extensions,
        JAVA_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS,
        4,
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* prototypes */
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorPrompt(char* prompt, void (*callback)(char*, int));

/* append buffer */
struct abuf {
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab) {
    free(ab->b);
}

/* terminal funcs */
void die(const char* s) {
    DWORD written;
    abAppend(E.hStdout, "\x1b[2J", 4);
    WriteConsole(E.hStdout, "\x1b[H", 3, &written, NULL);
    fprintf(stderr, "%s: error %lu\n", s, GetLastError());
    exit(1);
}

void disableRawMode() {
    if (!SetConsoleMode(E.hStdin, E.originalMode))
        die("SetConsoleMode");
}

void enableRawMode() {
    E.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(E.hStdin, &E.originalMode))
        die("GetConsoleMode");

    DWORD raw = E.originalMode;
    raw &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    raw |= ENABLE_VIRTUAL_TERMINAL_INPUT;

    if (!SetConsoleMode(E.hStdin, raw))
        die("SetConsoleMode");

    E.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode;
    if (!GetConsoleMode(E.hStdout, &outMode))
        die("GetConsoleMode");
    if (!SetConsoleMode(E.hStdout, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        die("SetConsoleMode");

    atexit(disableRawMode);
}

int handleEscSeq() {
    char seq[3];
    DWORD bytesRead;

    if (!ReadConsole(E.hStdin, &seq[0], 1, &bytesRead, NULL))
        return '\x1b';
    if (!ReadConsole(E.hStdin, &seq[1], 1, &bytesRead, NULL))
        return '\x1b';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (!ReadConsole(E.hStdin, &seq[2], 1, &bytesRead, NULL))
                return '\x1b';
            if (seq[2] == '~') {
                switch (seq[1]) {
                case '1':
                case '7':
                    return HOME;
                case '5':
                    return PAGE_UP;
                case '6':
                    return PAGE_DOWN;
                case '8':
                case '4':
                    return END;
                case '3':
                    return DEL;
                }
            }
            return '\x1b';
        }

        switch (seq[1]) {
        case 'A':
            return ARROW_UP;
        case 'B':
            return ARROW_DOWN;
        case 'C':
            return ARROW_RIGHT;
        case 'D':
            return ARROW_LEFT;
        case 'H':
            return HOME;
        case 'F':
            return END;
        }
        return '\x1b';
    }
    else if (seq[0] == 'O') {
        switch (seq[1]) {
        case 'H':
            return HOME;
        case 'F':
            return END;
        }
    }
    return '\x1b';
}

int editorReadKey() {
    char c;
    DWORD bytesRead;
    ReadConsole(E.hStdin, &c, 1, &bytesRead, NULL);

    if (c == '\x1b') {
        return handleEscSeq();
    }

    return c;
}

int getWindowSize(int* rows, int* cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(E.hStdout, &csbi))
        return -1;
    *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
}

/* syntax highlighting */
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow* row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_DEFAULT, row->rsize);

    if (E.syntax == NULL) return;

    char** keywords = E.syntax->keywords;

    char* scs = E.syntax->singleline_comment_start;
    char* mcs = E.syntax->ml_comment_start;
    char* mce = E.syntax->ml_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_ml_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_DEFAULT;

        if (scs_len && !in_string && !in_ml_comment) {
            if (!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_ml_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_ml_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                else {
                    i++;
                    continue;
                }
            }
            else if (!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_ml_comment = 1;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            }
            else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (!strncmp(&row->render[i], "(", 1)) {
            int j = i - 1;
            while (j >= 0 && !is_separator(row->render[j])) {
                row->hl[j] = HL_FUNC;
                j--;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if (kw2) klen--;

                if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }
    int changed = (row->hl_open_comment != in_ml_comment);
    row->hl_open_comment = in_ml_comment;
    if (changed && row->idx + 1 < E.numRows)
        editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
    case HL_MLCOMMENT:
    case HL_COMMENT: return 92; //bright green
    case HL_STRING: return 33; //yellow 
    case HL_KEYWORD1: return 35; //magenta
    case HL_KEYWORD2: return 36; //cyan
    case HL_NUMBER: return 32; //green
    case HL_MATCH: return 100; //highlight gray
    case HL_FUNC: return 93; // bright yellow
    default: return 37; //white 
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    char* ext = strchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax* s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strcmp(E.filename, s->filematch[i]))) {
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numRows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }
            i++;
        }
    }
}

/* row ops */
void editorUpdateRow(erow* row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (TEXT_ED_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TEXT_ED_TAB_STOP != 0) row->render[idx++] = ' ';
        }
        else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char* s, size_t len) {
    if (at < 0 || at > E.numRows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRows - at));
    for (int j = at + 1; j <= E.numRows; j++) E.row[j].idx++;

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]);

    E.numRows++;
    E.dirty++;
}

void editorFreeRow(erow* row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numRows) return;

    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numRows - at - 1));
    for (int j = at; j <= E.numRows - 1; j++) E.row[j].idx--;
    E.numRows--;
    E.dirty++;
}

void editorRowDelChar(erow* row, int at) {
    if (at < 0 || at >= row->size) return;

    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowInsertChar(erow* row, int at, int c) {
    if (at < 0 || at > row->size)
        at = row->size;

    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->chars[at] = c;
    row->size++;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow* row, char* s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

int editorCxtoRx(erow* row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (TEXT_ED_TAB_STOP - 1) - (rx % TEXT_ED_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRxtoCx(erow* row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            cur_rx += (TEXT_ED_TAB_STOP - 1) - (cur_rx % TEXT_ED_TAB_STOP);

        cur_rx++;

        if (cur_rx > rx) return cx;
    }
    return cx;
}

/* editor ops */
void editorInsertChar(int c) {
    if (E.cy == E.numRows)
        editorInsertRow(E.numRows, "", 0);

    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewLine() {
    erow* row = &E.row[E.cy];
    int indent = 0;
    while (indent < row->size && row->chars[indent] == ' ') indent++;

    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    }
    else {
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;

    erow* newrow = &E.row[E.cy];
    for (int i = 0; i < indent; i++) {
        editorRowInsertChar(newrow, i, ' ');
    }
    E.cx = indent;
}

void editorDelChar() {
    if (E.cy == E.numRows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow* row = &E.row[E.cy];
    if (E.cx <= 0) {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
        return;
    }

    if (E.cx >= TEXT_ED_TAB_STOP && E.cx % TEXT_ED_TAB_STOP == 0) {
        int i;
        for (i = 1; i <= TEXT_ED_TAB_STOP; i++)
            if (row->chars[E.cx - i] != ' ') break;
        if (i > TEXT_ED_TAB_STOP) {
            for (i = 0; i < TEXT_ED_TAB_STOP; i++)
                editorRowDelChar(row, E.cx - TEXT_ED_TAB_STOP);
            E.cx -= TEXT_ED_TAB_STOP;
            return;
        }
    }

    editorRowDelChar(row, E.cx - 1);
    E.cx--;
}

/* file io */
ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) return -1;
    size_t pos = 0;
    int c;
    if (!*lineptr) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1;
    }
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            *n *= 2;
            char* tmp = realloc(*lineptr, *n);
            if (!tmp) return -1;
            *lineptr = tmp;
        }
        (*lineptr)[pos++] = c;
        if (c == '\n') break;
    }
    if (pos == 0 && c == EOF) return -1;
    (*lineptr)[pos] = '\0';
    return pos;
}

char* editorRowsToString(int* buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numRows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char* buf = malloc(totlen);
    char* p = buf;
    for (j = 0; j < E.numRows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editorSave() {
    if (!E.filename) {
        E.filename = editorPrompt("Save as: %s (ESC 3 times to cancel)", NULL);
        if (!E.filename) {
            editorSetStatusMessage("Save aborted");
            return;
        }

        editorSelectSyntaxHighlight();
    }

    int len;
    int len_written;
    char* buf = editorRowsToString(&len);

    FILE* fp = fopen(E.filename, "w");
    if (fp) {
        len_written = fwrite(buf, 1, len, fp);
        fclose(fp);
        editorSetStatusMessage("%d bytes written to disk", len_written);
        E.dirty = 0;
    }
    else {
        editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    }

    free(buf);
}

void editorOpen(char* filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    FILE* fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;

        editorInsertRow(E.numRows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

/* find */
void editorFindCallback(char* query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char* saved_hl = NULL;

    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    }
    else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for (i = 0; i < E.numRows; i++) {
        current += direction;
        if (current == -1) current = E.numRows - 1;
        else if (current == E.numRows) current = 0;

        erow* row = &E.row[current];
        char* match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRxtoCx(row, match - row->render);
            E.rowoff = E.numRows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char* query = editorPrompt("Search: %s (ESC 3 times to cancel | Arrows to cycle | Enter to confirm)", editorFindCallback);

    if (query) {
        free(query);
    }
    else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

/* input */
char* editorPrompt(char* prompt, void (*callback)(char*, int)) {
    size_t bufsize = 128;
    char* buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        }
        else if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        }
        else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    erow* row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];

    switch (key) {
    case ARROW_UP:
        if (E.saved_cx == -1) E.saved_cx = E.cx;
        if (E.cy != 0)
            E.cy--;
        break;
    case ARROW_DOWN:
        if (E.saved_cx == -1) E.saved_cx = E.cx;
        if (E.cy < E.numRows)
            E.cy++;
        break;
    case ARROW_LEFT:
        if (E.cx != 0) {
            if (row && E.cx >= TEXT_ED_TAB_STOP && E.cx % TEXT_ED_TAB_STOP == 0) {
                int i;
                for (i = 1; i <= TEXT_ED_TAB_STOP; i++)
                    if (row->chars[E.cx - i] != ' ') break;
                if (i > TEXT_ED_TAB_STOP) { E.cx -= TEXT_ED_TAB_STOP; break; }
            }
            E.cx--;
        }
        else if (E.cy > 0) {
            E.cy--;
            E.cx = E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row && E.cx < row->size) {
            if (E.cx + TEXT_ED_TAB_STOP <= row->size && E.cx % TEXT_ED_TAB_STOP == 0) {
                int i;
                for (i = 0; i < TEXT_ED_TAB_STOP; i++)
                    if (row->chars[E.cx + i] != ' ') break;
                if (i == TEXT_ED_TAB_STOP) { E.cx += TEXT_ED_TAB_STOP; break; }
            }
            E.cx++;
        }
        else if (row && E.cx == row->size) {
            E.cy++;
            E.cx = 0;
        }
        break;
    }

    row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (key == ARROW_UP || key == ARROW_DOWN) {
        E.cx = (E.saved_cx < rowlen) ? E.saved_cx : rowlen;
    } else {
        if (E.cx > rowlen) E.cx = rowlen;
        E.saved_cx = -1;
    }
}

void editorProcessKeypress() {
    static int quit_times = TEXT_ED_QUIT_TIMES;

    int c = editorReadKey();
    DWORD written;

    switch (c) {
    case '\r':
        editorInsertNewLine();
        E.saved_cx = -1;
        break;

    case CTRL_KEY('q'):
        if (E.dirty && quit_times > 0) {
            editorSetStatusMessage(
                "WARNING!!! File has unsaved changes. \nPress Ctrl+Q %d more times to quit.",
                quit_times
            );
            quit_times--;
            return;
        }
        WriteConsole(E.hStdout, "\x1b[2J", 4, &written, NULL);
        WriteConsole(E.hStdout, "\x1b[H", 3, &written, NULL);
        exit(0);
        break;

    case CTRL_KEY('f'):
        editorFind();
        break;

    case CTRL_KEY('s'):
        editorSave();
        break;

    case HOME:
        E.cx = 0;
        break;
    case END:
        if (E.cy < E.numRows)
            E.cx = E.row[E.cy].size;
        break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL:
        if (c == DEL) editorMoveCursor(ARROW_RIGHT);
        editorDelChar();
        E.saved_cx = -1;
        break;

    case PAGE_UP:
    case PAGE_DOWN: {
        if (c == PAGE_UP) {
            E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN) {
            E.cy = E.rowoff + E.screenRows - 1;
            if (E.cy > E.numRows) E.cy = E.numRows;
        }

        int times = E.screenRows;
        while (times--) {
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    }

    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_RIGHT:
        editorMoveCursor(c);
        break;

    case CTRL_KEY('l'):
    case '\x1b':
        // intentionally ignore these keys
        break;

    case '\t': {
        int j;
        for (j = 0; j < TEXT_ED_TAB_STOP; j++) {
            editorInsertChar(32);
        }
        break;
    }

    default:
        editorInsertChar(c);
        E.saved_cx = -1;
        break;
    }

    quit_times = TEXT_ED_QUIT_TIMES;
}

/* ouptut */
void editorDrawRows(struct abuf* ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numRows) {
            if (E.numRows == 0 && y == E.screenRows / 3) {
                char welcome[80];
                int welcomeLen = snprintf(welcome, sizeof(welcome), "Welcome to Text Ed v%s", TEXT_ED_VERSION);
                if (welcomeLen > E.screenCols)
                    welcomeLen = E.screenCols;

                int padding = (E.screenCols - welcomeLen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--)
                    abAppend(ab, " ", 1);

                abAppend(ab, welcome, welcomeLen);
            }
            else {
                abAppend(ab, "~", 1);
            }
        }
        else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screenCols) len = E.screenCols;
            char* c = &E.row[filerow].render[E.coloff];
            unsigned char* hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        abAppend(ab, buf, clen);
                    }
                }
                else if (hl[j] == HL_DEFAULT) {
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39;49m", 8);
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                }
                else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39;49m", 8);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numRows) {
        E.rx = editorCxtoRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenRows) {
        E.rowoff = E.cy - E.screenRows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screenCols) {
        E.coloff = E.rx = E.screenCols + 1;
    }
}

void editorDrawStatusBar(struct abuf* ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(
        status,
        sizeof(status),
        "%.20s - %d lines %s",
        E.filename ? E.filename : "[No Name]",
        E.numRows,
        E.dirty ? "(unsaved changes)" : ""
    );
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no type", E.cy + 1, E.numRows);
    if (len > E.screenCols) len = E.screenCols;
    abAppend(ab, status, len);
    while (len < E.screenCols) {
        if (E.screenCols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorSetStatusMessage(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void editorDrawMessageBar(struct abuf* ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screenCols) msglen = E.screenCols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    DWORD written;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    getWindowSize(&E.screenRows, &E.screenCols);
    E.screenRows -= 2;
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    WriteConsole(E.hStdout, ab.b, ab.len, &written, NULL);
    abFree(&ab);
}

/* init */
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.saved_cx = -1;
    E.numRows = 0;
    E.row = NULL;
    E.rowoff = 0;
    E.coloff = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.dirty = 0;
    E.syntax = NULL;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");

    E.screenRows -= 2;
}

/* main */
int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl+S = save | Ctrl+Q = quit | Ctrl+F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
