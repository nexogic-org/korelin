
#include "keditor.h"
#include "klex.h"
#include "kparser.h"
#include "kcode.h"
#include "kvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
    #include <io.h>
    #define strdup _strdup
#else
    #include <unistd.h>
    #include <termios.h>
    #include <sys/ioctl.h>
    #include <sys/types.h>
#endif

/** @brief 定義與常量 */

#define KEDITOR_VERSION "0.1.0"
#define KEDITOR_TAB_STOP 4
#define KEDITOR_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/** @brief 數據結構 */

typedef struct {
    char* chars;
    int len;
    int render_len; /** @brief 考慮 Tab 等 */
} ERow;

typedef struct {
    int cx, cy; /** @brief 光標在文件中的位置 (x: 列, y: 行) */
    int rx;     /** @brief 光標在渲染行的位置 (處理 Tab) */
    int rowoff; /** @brief 行滾動偏移 */
    int coloff; /** @brief 列滾動偏移 */
    int screen_rows;
    int screen_cols;
    
    int numrows;
    ERow* row;
    
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    
    int dirty;
    int quit_times;
} EditorConfig;

static EditorConfig E;

#ifdef _WIN32
static HANDLE hConsole;
static DWORD originalConsoleMode;
#else
static struct termios orig_termios;
#endif

/** @brief Append Buffer */

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = (char*)realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/** @brief 輔助函數聲明 */
void editorSetStatusMessage(const char* fmt, ...);
void editorRefreshScreen();
char* editorRowsToString(int* buflen);
void editorSave();
void editorRun();

/** @brief 終端控制 (跨平台) */

void die(const char *s) {
    /** @brief 清屏並退出 */
    write(1, "\x1b[2J", 4);
    write(1, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode() {
#ifdef _WIN32
    SetConsoleMode(hConsole, originalConsoleMode);
#else
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
#endif
}

void enableRawMode() {
#ifdef _WIN32
    hConsole = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hConsole, &originalConsoleMode);
    atexit(disableRawMode);
    
    /** @brief Enable ANSI escape codes for Windows 10+ */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    /** @brief Set UTF-8 Output */
    SetConsoleOutputCP(65001);

    DWORD mode = originalConsoleMode;
    /** 
     * @brief 禁用行緩衝和回顯，但在 Windows 上 _getch() 已經是不緩衝的
     * 我們主要是為了處理一些特殊鍵
     */
    /** mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT); */
    /** SetConsoleMode(hConsole, mode); */
#else
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
#endif
}

int editorReadKey() {
    int nread;
    char c;
#ifdef _WIN32
    /** @brief Windows 使用 _getch() */
    int key = _getch();
    if (key == 0 || key == 224) {
        int seq = _getch();
        switch (seq) {
            case 72: return ARROW_UP;
            case 80: return ARROW_DOWN;
            case 75: return ARROW_LEFT;
            case 77: return ARROW_RIGHT;
            case 73: return PAGE_UP;
            case 81: return PAGE_DOWN;
            case 71: return HOME_KEY;
            case 79: return END_KEY;
            case 83: return DEL_KEY;
            default: return 0;
        }
    } else if (key == 8) {
        return BACKSPACE;
    } else if (key == 13) {
        return '\r'; /** @brief Enter */
    }
    return key;
#else
    /** @brief Linux Escape Sequences */
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    
    if (c == '\x1b') {
        char seq[3];
        
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        
        return '\x1b';
    } else {
        return c;
    }
#endif
}

int getWindowSize(int *rows, int *cols) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return 0;
    }
    return -1;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
#endif
}

/** @brief 行操作 */

void editorInsertRow(int at, char* s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    
    E.row = (ERow*)realloc(E.row, sizeof(ERow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(ERow) * (E.numrows - at));
    
    E.row[at].len = len;
    E.row[at].chars = (char*)malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    
    E.numrows++;
    E.dirty++;
}

void editorFreeRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    free(E.row[at].chars);
    memmove(&E.row[at], &E.row[at + 1], sizeof(ERow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(ERow* row, int at, int c) {
    if (at < 0 || at > row->len) at = row->len;
    row->chars = (char*)realloc(row->chars, row->len + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->len - at + 1);
    row->len++;
    row->chars[at] = c;
    E.dirty++;
}

void editorRowDelChar(ERow* row, int at) {
    if (at < 0 || at >= row->len) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->len - at);
    row->len--;
    E.dirty++;
}

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        ERow* row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->len - E.cx);
        row = &E.row[E.cy]; /** @brief Realloc 可能改變了地址 */
        row->len = E.cx;
        row->chars[row->len] = '\0';
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    
    ERow* row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        /** @brief 合並行 */
        E.cx = E.row[E.cy - 1].len;
        ERow* prev = &E.row[E.cy - 1];
        prev->chars = (char*)realloc(prev->chars, prev->len + row->len + 1);
        memcpy(&prev->chars[prev->len], row->chars, row->len);
        prev->len += row->len;
        prev->chars[prev->len] = '\0';
        editorFreeRow(E.cy);
        E.cy--;
    }
}

/** @brief 文件 I/O */

char* editorRowsToString(int* buflen) {
    int totallen = 0;
    for (int i = 0; i < E.numrows; i++) {
        totallen += E.row[i].len + 1;
    }
    *buflen = totallen;
    
    char* buf = (char*)malloc(totallen + 1);
    char* p = buf;
    for (int i = 0; i < E.numrows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].len);
        p += E.row[i].len;
        *p = '\n';
        p++;
    }
    *p = '\0';
    return buf;
}

void editorOpen(const char* filename) {
    free(E.filename);
    E.filename = strdup(filename);
    
    FILE* fp = fopen(filename, "r");
    if (!fp) return;
    
    char* line = NULL;
    size_t linecap = 0;
    /** @brief 跨平台 getline 需要自己實現或用 fgets */
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        int len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
        editorInsertRow(E.numrows, buf, len);
    }
    fclose(fp);
    E.dirty = 0;
}

void editorSave() {
    if (E.filename == NULL) return;
    
    int len;
    char* buf = editorRowsToString(&len);
    
    FILE* fp = fopen(E.filename, "w");
    if (fp) {
        fwrite(buf, 1, len, fp);
        fclose(fp);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("Saved to disk");
    } else {
        free(buf);
        editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    }
}

/** @brief 運行代碼 */

void editorRun() {
    editorSetStatusMessage("Running...");
    editorRefreshScreen(); 
    
    /** @brief 1. 獲取代碼 */
    int len;
    char* source = editorRowsToString(&len);
    
    /** @brief 2. 準備環境 */
    /** @brief 退出 raw mode */
    disableRawMode();
    
    /** @brief 清屏並重置光標 */
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    printf("--- Building & Running %s ---\n\n", E.filename ? E.filename : "Untitled");
    
    Lexer lexer;
    init_lexer(&lexer, source);
    
    Parser parser;
    init_parser(&parser, &lexer);
    KastProgram* program = parse_program(&parser);
    
    if (parser.has_error) {
        printf("\x1b[31m[Parser Error] %s\x1b[0m\n", parser.error_message);
    } else {
        KBytecodeChunk chunk;
        init_chunk(&chunk);
        
        if (compile_ast(program, &chunk) == 0) {
            KVM vm;
            kvm_init(&vm);
            printf("--- Output ---\n");
            
            clock_t start = clock();
            int result = kvm_interpret(&vm, &chunk);
            clock_t end = clock();
            
            printf("\n--- End (Exit Code: %d, Time: %.2fms) ---\n", 
                   result, (double)(end - start) / CLOCKS_PER_SEC * 1000);
        } else {
             printf("\x1b[31m[Compiler Error] Compilation failed.\x1b[0m\n");
        }
        
        free_chunk(&chunk);
        free_ast_node((KastNode*)program);
    }
    
    free(source);
    
    printf("\nPress any key to return to editor...");
#ifdef _WIN32
    _getch();
#else
    getchar();
#endif
    
    /** @brief 恢復 Raw Mode */
    enableRawMode();
}

/** @brief 語法高亮 (ANSI) */

void editorDrawSyntaxHighlighted(struct abuf *ab, ERow* row, int col_offset, int len) {
    /** 
     * @brief 為了簡單起見，我們這裡只做關鍵字高亮
     * 使用 Lexer 對整行進行處理
     */
    
    Lexer lexer;
    init_lexer(&lexer, row->chars);
    
    Token token;
    int current_idx = 0;
    
    while ((token = next_token(&lexer)).type != KORELIN_TOKEN_EOF) {
        int token_offset = (int)(token.value - row->chars);
        int token_len = token.length;
        
        /** @brief 打印 token 之間的空白/未知字符 */
        if (token_offset > current_idx) {
            abAppend(ab, "\x1b[39m", 5); /** @brief Default color */
            for (int i = current_idx; i < token_offset; i++) {
                if (i >= col_offset && i < col_offset + len) abAppend(ab, &row->chars[i], 1);
            }
        }
        
        const char *color = "\x1b[39m";
        switch (token.type) {
            case KORELIN_TOKEN_LET:
            case KORELIN_TOKEN_VAR:
            case KORELIN_TOKEN_CONST:
            case KORELIN_TOKEN_IF:
            case KORELIN_TOKEN_ELSE:
            case KORELIN_TOKEN_FOR:
            case KORELIN_TOKEN_WHILE:
            case KORELIN_TOKEN_RETURN:
            case KORELIN_TOKEN_IMPORT:
            case KORELIN_TOKEN_CLASS:
            case KORELIN_TOKEN_STRUCT:
            case KORELIN_TOKEN_NEW:
            case KORELIN_TOKEN_TRUE:
            case KORELIN_TOKEN_FALSE:
            case KORELIN_TOKEN_NIL:
            case KORELIN_TOKEN_PUBLIC:
            case KORELIN_TOKEN_PRIVATE:
            case KORELIN_TOKEN_VOID:
            case KORELIN_TOKEN_BOOL:
                color = "\x1b[34m"; /** @brief Blue */
                break;
            case KORELIN_TOKEN_STRING:
                color = "\x1b[31m"; /** @brief Red */
                break;
            case KORELIN_TOKEN_INT:
            case KORELIN_TOKEN_FLOAT:
                color = "\x1b[32m"; /** @brief Green */
                break;
            case KORELIN_TOKEN_IDENT:
                if (isupper(token.value[0])) {
                    color = "\x1b[33m"; /** @brief Yellow */
                }
                break;
            default:
                break;
        }
        
        abAppend(ab, color, strlen(color));
        for (int i = 0; i < token_len; i++) {
            int pos = token_offset + i;
            if (pos >= col_offset && pos < col_offset + len) abAppend(ab, &row->chars[pos], 1);
        }
        
        current_idx = token_offset + token_len;
        
        /** @brief Free token value if dynamic */
        if (token.type == KORELIN_TOKEN_IDENT || 
            token.type == KORELIN_TOKEN_STRING || 
            token.type == KORELIN_TOKEN_INT || 
            token.type == KORELIN_TOKEN_FLOAT ||
            (token.type >= KORELIN_TOKEN_LET && token.type <= KORELIN_TOKEN_NEW) ||
            token.type == KORELIN_TOKEN_VOID || token.type == KORELIN_TOKEN_BOOL) {
             if (token.value) free((void*)token.value);
        }
    }
    
    /** @brief 剩餘部分 (註釋等) */
    abAppend(ab, "\x1b[39m", 5);
    for (int i = current_idx; i < row->len; i++) {
         if (i >= col_offset && i < col_offset + len) {
             /** @brief 簡單註釋檢測 */
             if (row->chars[i] == '/' && i+1 < row->len && row->chars[i+1] == '/') {
                 abAppend(ab, "\x1b[32m", 5); /** @brief Green */
             }
             abAppend(ab, &row->chars[i], 1);
         }
    }
}

/** @brief 渲染 */

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    
    /** @brief 隱藏光標 */
    abAppend(&ab, "\x1b[?25l", 6);
    /** @brief 移動到 (1,1) */
    abAppend(&ab, "\x1b[H", 3);
    
    for (int y = 0; y < E.screen_rows; y++) {
        int filerow = y + E.rowoff;
        
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Korelin Editor -- Version %s", KEDITOR_VERSION);
                if (welcomelen > E.screen_cols) welcomelen = E.screen_cols;
                int padding = (E.screen_cols - welcomelen) / 2;
                if (padding) { abAppend(&ab, "~", 1); padding--; }
                while (padding--) abAppend(&ab, " ", 1);
                abAppend(&ab, welcome, welcomelen);
            } else {
                abAppend(&ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].len - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screen_cols) len = E.screen_cols;
            
            editorDrawSyntaxHighlighted(&ab, &E.row[filerow], E.coloff, E.screen_cols);
            
            /** @brief 重置顏色 */
            abAppend(&ab, "\x1b[39m", 5);
        }
        
        /** @brief 清除行尾 */
        abAppend(&ab, "\x1b[K", 3);
        abAppend(&ab, "\r\n", 2);
    }
    
    /** @brief 狀態欄 */
    abAppend(&ab, "\x1b[7m", 4); /** @brief 反色 */
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.filename ? E.filename : "[No Name]", E.numrows,
        E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
    if (len > E.screen_cols) len = E.screen_cols;
    abAppend(&ab, status, len);
    while (len < E.screen_cols) {
        if (E.screen_cols - len == rlen) {
            abAppend(&ab, rstatus, rlen);
            break;
        } else {
            abAppend(&ab, " ", 1);
            len++;
        }
    }
    abAppend(&ab, "\x1b[m", 3); /** @brief 恢復正常 */
    abAppend(&ab, "\r\n", 2);
    
    /** @brief 消息欄 */
    abAppend(&ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screen_cols) msglen = E.screen_cols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(&ab, E.statusmsg, msglen);
        
    /** @brief 恢復光標 */
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));
    
    /** @brief 顯示光標 */
    abAppend(&ab, "\x1b[?25h", 6);
    
    write(1, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/** @brief 輸入處理 */

void editorProcessKeypress() {
    int c = editorReadKey();
    
    switch (c) {
        case '\r':
            editorInsertNewline();
            break;
            
        case CTRL_KEY('q'):
            if (E.dirty && E.quit_times > 0) {
                editorSetStatusMessage("WARNING! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", E.quit_times);
                E.quit_times--;
                return;
            }
            write(1, "\x1b[2J", 4);
            write(1, "\x1b[H", 3);
            exit(0);
            break;
            
        case CTRL_KEY('s'):
            editorSave();
            break;
            
        case CTRL_KEY('r'):
            editorRun();
            break;
            
        case HOME_KEY:
            E.cx = 0;
            break;
            
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].len;
            break;
            
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) {
                /** @brief TODO: Delete key */
            } else {
                editorDelChar();
            }
            break;
            
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            {
                if (c == ARROW_UP) {
                    if (E.cy != 0) E.cy--;
                } else if (c == ARROW_DOWN) {
                    if (E.cy < E.numrows) E.cy++;
                } else if (c == ARROW_LEFT) {
                    if (E.cx != 0) E.cx--;
                    else if (E.cy > 0) {
                        E.cy--;
                        E.cx = E.row[E.cy].len;
                    }
                } else if (c == ARROW_RIGHT) {
                    if (E.cy < E.numrows) {
                        if (E.cx < E.row[E.cy].len) E.cx++;
                        else if (E.cx == E.row[E.cy].len) {
                            E.cy++;
                            E.cx = 0;
                        }
                    }
                }
            }
            break;
            
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else {
                    E.cy = E.rowoff + E.screen_rows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }
                
                int times = E.screen_rows;
                while (times--) {
                    if (c == PAGE_UP) { if (E.cy > 0) E.cy--; }
                    else { if (E.cy < E.numrows) E.cy++; }
                }
            }
            break;
            
        case CTRL_KEY('l'):
        case '\x1b':
            break;
            
        default:
            editorInsertChar(c);
            break;
    }
    
    E.quit_times = KEDITOR_QUIT_TIMES;
    
    /** @brief 修正光標位置 */
    if (E.cy >= E.numrows) E.cx = 0; 
    else if (E.cx > E.row[E.cy].len) E.cx = E.row[E.cy].len;
    
    /** @brief 滾動處理 */
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screen_rows) {
        E.rowoff = E.cy - E.screen_rows + 1;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screen_cols) {
        E.coloff = E.cx - E.screen_cols + 1;
    }
}

/** @brief 初始化 */

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.quit_times = KEDITOR_QUIT_TIMES;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    
    if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1) die("getWindowSize");
    E.screen_rows -= 2;
}

void keditor_run(const char* filename) {
    enableRawMode();
    initEditor();
    if (filename) {
        editorOpen(filename);
    }
    
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-R = Run");
    
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
}
