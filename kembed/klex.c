#include "klex.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief -----------------------------------------------------------------------------
 * 輔助函數聲明
 * -----------------------------------------------------------------------------
 */

/**
 * @brief 將詞法分析器的讀取指針向前移動一位
 * 
 * @param lexer 指向詞法分析器實例的指標
 */
static void advance(Lexer* lexer) {
    if (lexer->read_position >= strlen(lexer->input)) {
        lexer->current_char = '\0'; /** @brief 到達文件末尾，使用空字符標記 */
    } else {
        lexer->current_char = lexer->input[lexer->read_position];
    }
    lexer->position = lexer->read_position;
    lexer->read_position += 1;
}

/**
 * @brief 查看下一個字符，但不移動指針（向前瞻看）
 * 
 * @param lexer 指向詞法分析器實例的指標
 * @return char 下一個字符，如果到達末尾則返回 '\0'
 */
static char peek(const Lexer* lexer) {
    if (lexer->read_position >= strlen(lexer->input)) {
        return '\0';
    }
    return lexer->input[lexer->read_position];
}

/**
 * @brief 跳過所有空白字符（空格、製表符、換行符、回車符）
 * 
 * @param lexer 指向詞法分析器實例的指標
 */
static void skip_whitespace(Lexer* lexer) {
    while (lexer->current_char == ' ' || lexer->current_char == '\t' ||
           lexer->current_char == '\n' || lexer->current_char == '\r') {
        advance(lexer);
    }
}

/**
 * @brief 讀取一個完整的標識符或關鍵字
 * 
 * @param lexer 指向詞法分析器實例的指標
 * @return Token 解析出的標識符 Token
 */
static Token read_identifier(Lexer* lexer) {
    size_t start_pos = lexer->position;
    
    /** 
     * @brief 允許標識符以字母或下劃線開頭，後續可以是字母、數字或下劃線
     * 注意：標準C的 isalpha 僅檢查字母，這裡我們假設標識符首字符只能是字母或下劃線
     * 如果支持首字符為數字則邏輯不同，但通常不支持
     */
    while (isalpha(lexer->current_char) || lexer->current_char == '_' || isdigit(lexer->current_char)) {
        /** 
         * @brief 注意：循環條件中加入了 isdigit，因為標識符中間可以包含數字（如 var1）
         * 但 read_identifier 是在 isalpha||_ 判斷後調用的，所以首字符已確認為非數字
         */
        advance(lexer);
    }
    
    size_t len = lexer->position - start_pos;
    char* literal = (char*)malloc(len + 1);
    if (!literal) {
        fprintf(stderr, "Critical Error: Memory allocation failed in read_identifier\n");
        exit(EXIT_FAILURE);
    }
    strncpy(literal, lexer->input + start_pos, len);
    literal[len] = '\0';
    
    Token token;
    token.type = KORELIN_TOKEN_IDENT; /** @brief 默認為標識符，後續會檢查是否為關鍵字 */
    token.value = literal;
    token.length = len;
    return token;
}

/**
 * @brief 讀取一個完整的數字（支持整數和浮點數）
 * 
 * @param lexer 指向詞法分析器實例的指標
 * @return Token 解析出的數字 Token
 */
static Token read_number(Lexer* lexer) {
    size_t start_pos = lexer->position;
    bool is_float = false;

    while (isdigit(lexer->current_char)) {
        advance(lexer);
    }

    /** @brief 檢查是否包含小數點，且小數點後緊跟數字（避免將對象屬性訪問如 obj.prop 誤判為浮點數） */
    if (lexer->current_char == '.' && isdigit(peek(lexer))) {
        is_float = true;
        advance(lexer); /** @brief 消耗小數點 */
        while (isdigit(lexer->current_char)) {
            advance(lexer);
        }
    }

    size_t len = lexer->position - start_pos;
    char* literal = (char*)malloc(len + 1);
    if (!literal) {
        fprintf(stderr, "Critical Error: Memory allocation failed in read_number\n");
        exit(EXIT_FAILURE);
    }
    strncpy(literal, lexer->input + start_pos, len);
    literal[len] = '\0';
    
    Token token;
    token.type = is_float ? KORELIN_TOKEN_FLOAT : KORELIN_TOKEN_INT;
    token.value = literal;
    token.length = len;
    return token;
}

/**
 * @brief 讀取字符串字面量
 * 
 * @param lexer 指向詞法分析器實例的指標
 * @return Token 解析出的字符串 Token
 */
static Token read_string(Lexer* lexer, char quote_char) {
    size_t start_pos = lexer->position + 1; /** @brief 跳過開頭的引號 (僅用於回退/調試?) */
    
    /** @brief Dynamic buffer */
    int capacity = 32;
    int len = 0;
    char* literal = (char*)malloc(capacity);
    
    advance(lexer); /** @brief 消耗開頭的引號 */

    while (lexer->current_char != quote_char && lexer->current_char != '\0') {
        char c = lexer->current_char;
        if (c == '\\') {
            advance(lexer);
            if (lexer->current_char == '\0') break; /** @brief 意外的 EOF */
            char next = lexer->current_char;
            switch (next) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '0': c = '\0'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '\'': c = '\''; break;
                default: c = next; break; /** @brief 如果不是特殊字符則保留原字符 */
            }
        }
        
        if (len + 1 >= capacity) {
            capacity *= 2;
            char* new_literal = (char*)realloc(literal, capacity);
            if (!new_literal) { free(literal); exit(1); }
            literal = new_literal;
        }
        literal[len++] = c;
        advance(lexer);
    }
    
    literal[len] = '\0';

    if (lexer->current_char == quote_char) {
        advance(lexer); /** @brief 消耗結尾的引號 */
    } else {
        /** @brief EOF handling */
    }

    Token token;
    token.type = KORELIN_TOKEN_STRING;
    token.value = literal;
    token.length = len;
    return token;
}

/**
 * @brief 根據標識符的字面量查找其對應的 Token 類型（區分關鍵字和普通標識符）
 * 
 * @param ident 標識符字符串
 * @return KorelinToken 對應的 Token 類型
 */
static KorelinToken lookup_ident(const char* ident) {
    /** @brief printf("DEBUG LOOKUP: '%s'\n", ident); */
    /** @brief 關鍵字映射 */
    if (strcmp(ident, "let") == 0) return KORELIN_TOKEN_LET;
    if (strcmp(ident, "function") == 0) return KORELIN_TOKEN_FUNCTION;
    if (strcmp(ident, "var") == 0) return KORELIN_TOKEN_VAR;
    if (strcmp(ident, "const") == 0) return KORELIN_TOKEN_CONST;
    if (strcmp(ident, "if") == 0) return KORELIN_TOKEN_IF;
    if (strcmp(ident, "else") == 0) return KORELIN_TOKEN_ELSE;
    if (strcmp(ident, "for") == 0) return KORELIN_TOKEN_FOR;
    if (strcmp(ident, "while") == 0) return KORELIN_TOKEN_WHILE;
    if (strcmp(ident, "do") == 0) return KORELIN_TOKEN_DO;
    if (strcmp(ident, "return") == 0) return KORELIN_TOKEN_RETURN;
    if (strcmp(ident, "try") == 0) return KORELIN_TOKEN_TRY;
    if (strcmp(ident, "catch") == 0) return KORELIN_TOKEN_CATCH;
    if (strcmp(ident, "throw") == 0) return KORELIN_TOKEN_THROW;
    if (strcmp(ident, "import") == 0) return KORELIN_TOKEN_IMPORT;
    if (strcmp(ident, "struct") == 0) return KORELIN_TOKEN_STRUCT;
    if (strcmp(ident, "true") == 0) return KORELIN_TOKEN_TRUE;
    if (strcmp(ident, "false") == 0) return KORELIN_TOKEN_FALSE;
    if (strcmp(ident, "nil") == 0) return KORELIN_TOKEN_NIL;
    if (strcmp(ident, "break") == 0) return KORELIN_TOKEN_BREAK;
    if (strcmp(ident, "continue") == 0) return KORELIN_TOKEN_CONTINUE;
    if (strcmp(ident, "switch") == 0) return KORELIN_TOKEN_SWITCH;
    if (strcmp(ident, "case") == 0) return KORELIN_TOKEN_CASE;
    if (strcmp(ident, "default") == 0) return KORELIN_TOKEN_DEFAULT;
    if (strcmp(ident, "class") == 0) return KORELIN_TOKEN_CLASS;
    if (strcmp(ident, "struct") == 0) return KORELIN_TOKEN_STRUCT;
    if (strcmp(ident, "Map") == 0) return KORELIN_TOKEN_MAP;
    if (strcmp(ident, "public") == 0) return KORELIN_TOKEN_PUBLIC;
    if (strcmp(ident, "private") == 0) return KORELIN_TOKEN_PRIVATE;
    if (strcmp(ident, "protected") == 0) return KORELIN_TOKEN_PROTECTED;
    if (strcmp(ident, "extends") == 0) return KORELIN_TOKEN_EXTENDS;
    if (strcmp(ident, "super") == 0) return KORELIN_TOKEN_SUPER;
    if (strcmp(ident, "new") == 0) return KORELIN_TOKEN_NEW;
    
    /** @brief 類型關鍵字（根據 klex.h 定義，這些也作為 Token 類型存在） */
    if (strcmp(ident, "void") == 0) return KORELIN_TOKEN_VOID;
    if (strcmp(ident, "int") == 0) return KORELIN_TOKEN_INT;
    if (strcmp(ident, "float") == 0) return KORELIN_TOKEN_FLOAT;
    if (strcmp(ident, "bool") == 0) return KORELIN_TOKEN_BOOL;
    if (strcmp(ident, "string") == 0) return KORELIN_TOKEN_KEYWORD_STRING;

    return KORELIN_TOKEN_IDENT;
}

/** @brief -----------------------------------------------------------------------------
 * 公共函數實現
 * -----------------------------------------------------------------------------
 */

/**
 * @brief 初始化 Lexer 實例
 * 
 * @param lexer 指向需要初始化的 Lexer 結構體
 * @param input 源代碼字符串
 */
void init_lexer(Lexer* lexer, const char* input) {
    lexer->input = input;
    lexer->position = 0;
    lexer->read_position = 0;
    lexer->current_char = '\0';
    /** @brief 調用一次 advance 來讀取第一個字符並初始化 current_char */
    advance(lexer);

    /** @brief 如果存在 BOM (EF BB BF) 則跳過 */
    unsigned char b0 = (unsigned char)lexer->input[0];
    unsigned char b1 = (unsigned char)lexer->input[1];
    unsigned char b2 = (unsigned char)lexer->input[2];
    
    /** @brief printf("DEBUG LEX: %02X %02X %02X\n", b0, b1, b2); */

    if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
        /** @brief printf("DEBUG LEX: BOM Detected\n"); */
        advance(lexer);
        advance(lexer);
        advance(lexer);
    }
}

/**
 * @brief 核心函數：獲取輸入流中的下一個 Token
 * 
 * @param lexer 指向 Lexer 實例
 * @return Token 解析出的下一個 Token
 */
Token next_token(Lexer* lexer) {
    Token token;
    token.value = NULL;
    token.length = 0;

    skip_whitespace(lexer);

    switch (lexer->current_char) {
        case '=':
            if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_EQ;
                token.value = "==";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_ASSIGN;
                token.value = "=";
                token.length = 1;
            }
            break;
        case '+':
            if (peek(lexer) == '+') {
                advance(lexer);
                token.type = KORELIN_TOKEN_INC;
                token.value = "++";
                token.length = 2;
            } else if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_ADD_ASSIGN;
                token.value = "+=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_ADD;
                token.value = "+";
                token.length = 1;
            }
            break;
        case '-':
            if (peek(lexer) == '-') {
                advance(lexer);
                token.type = KORELIN_TOKEN_DEC;
                token.value = "--";
                token.length = 2;
            } else if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_SUB_ASSIGN;
                token.value = "-=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_SUB;
                token.value = "-";
                token.length = 1;
            }
            break;
        case '*':
             if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_MUL_ASSIGN;
                token.value = "*=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_MUL;
                token.value = "*";
                token.length = 1;
            }
            break;
        case '/':
             if (peek(lexer) == '/') {
                 /** @brief 註釋：跳過直到換行符 */
                 while (lexer->current_char != '\n' && lexer->current_char != '\0') {
                     advance(lexer);
                 }
                 return next_token(lexer);
             } else if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_DIV_ASSIGN;
                token.value = "/=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_DIV;
                token.value = "/";
                token.length = 1;
            }
            break;
        case '%':
             if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_MOD_ASSIGN;
                token.value = "%=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_MOD;
                token.value = "%";
                token.length = 1;
            }
            break;
        case '!':
            if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_NE;
                token.value = "!=";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_NOT;
                token.value = "!";
                token.length = 1;
            }
            break;
        case '<':
             if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_LE;
                token.value = "<=";
                token.length = 2;
            } else {
                /** 
                 * @brief 上下文未定時，默認返回 LT (小於號)
                 * 在語法分析階段可能需要根據語境判斷是否為 LANGLE (泛型左括號)
                 */
                token.type = KORELIN_TOKEN_LT; 
                token.value = "<";
                token.length = 1;
            }
            break;
        case '>':
             if (peek(lexer) == '=') {
                advance(lexer);
                token.type = KORELIN_TOKEN_GE;
                token.value = ">=";
                token.length = 2;
            } else {
                /** 
                 * @brief 上下文未定時，默認返回 GT (大於號)
                 * 在語法分析階段可能需要根據語境判斷是否為 RANGLE (泛型右括號)
                 */
                token.type = KORELIN_TOKEN_GT; 
                token.value = ">";
                token.length = 1;
            }
            break;
        case '&':
            if (peek(lexer) == '&') {
                advance(lexer);
                token.type = KORELIN_TOKEN_AND;
                token.value = "&&";
                token.length = 2;
            } else {
                /** 
                 * @brief 如果 klex.h 中未定義位運算符，則暫視為錯誤或需要擴展定義
                 * 暫時返回 ERROR
                 */
                token.type = KORELIN_TOKEN_ERROR;
                token.value = "&";
                token.length = 1;
            }
            break;
        case '|':
            if (peek(lexer) == '|') {
                advance(lexer);
                token.type = KORELIN_TOKEN_OR;
                token.value = "||";
                token.length = 2;
            } else {
                /** @brief 同上，未定義位運算符 OR */
                token.type = KORELIN_TOKEN_ERROR;
                token.value = "|";
                token.length = 1;
            }
            break;
        case ':':
            if (peek(lexer) == ':') {
                advance(lexer);
                token.type = KORELIN_TOKEN_SCOPE;
                token.value = "::";
                token.length = 2;
            } else {
                token.type = KORELIN_TOKEN_COLON;
                token.value = ":";
                token.length = 1;
            }
            break;
        case ';':
            token.type = KORELIN_TOKEN_SEMICOLON;
            token.value = ";";
            token.length = 1;
            break;
        case '(':
            token.type = KORELIN_TOKEN_LPAREN;
            token.value = "(";
            token.length = 1;
            break;
        case ')':
            token.type = KORELIN_TOKEN_RPAREN;
            token.value = ")";
            token.length = 1;
            break;
        case ',':
            token.type = KORELIN_TOKEN_COMMA;
            token.value = ",";
            token.length = 1;
            break;
        case '{':
            token.type = KORELIN_TOKEN_LBRACE;
            token.value = "{";
            token.length = 1;
            break;
        case '}':
            token.type = KORELIN_TOKEN_RBRACE;
            token.value = "}";
            token.length = 1;
            break;
        case '[':
            token.type = KORELIN_TOKEN_LBRACKET;
            token.value = "[";
            token.length = 1;
            break;
        case ']':
            token.type = KORELIN_TOKEN_RBRACKET;
            token.value = "]";
            token.length = 1;
            break;
        case '.':
            token.type = KORELIN_TOKEN_DOT;
            token.value = ".";
            token.length = 1;
            break;
        case '@':
            token.type = KORELIN_TOKEN_AT;
            token.value = "@";
            token.length = 1;
            break;
        case '"':
            token = read_string(lexer, '"');
            return token; /** @brief read_string 內部處理了 advance */
        case '\'':
            token = read_string(lexer, '\'');
            return token;
        case '\0':
            token.type = KORELIN_TOKEN_EOF;
            token.value = "";
            token.length = 0;
            break;
        default:
            if (isalpha(lexer->current_char) || lexer->current_char == '_') {
                token = read_identifier(lexer);
                token.type = lookup_ident(token.value);
                return token;
            } else if (isdigit(lexer->current_char)) {
                token = read_number(lexer);
                return token;
            } else {
                token.type = KORELIN_TOKEN_ERROR;
                token.value = "UNKNOWN"; 
                token.length = 1;
            }
            break;
    }

    advance(lexer);
    return token;
}

/**
 * @brief 釋放 Token 中動態分配的內存
 * 
 * @param token 需要釋放的 Token 指標
 */
void free_token(Token* token) {
    /** 
     * @brief 檢查是否為動態分配的類型
     * 注意：如果是關鍵字（如 func, let），lookup_ident 返回的類型是特定的（如 KORELIN_TOKEN_FUNC），
     * 但 token.value 仍然指向 read_identifier 分配的堆內存，因此也需要釋放。
     * read_identifier 總是分配內存，不管它最後被判定為什麼類型。
     * read_number 和 read_string 同理。
     * 
     * 只有那些在 next_token switch 中直接賦值字面量（如 "="）的 token 不需要釋放。
     * 如何區分？ 
     * 方法1: 根據 token.type。但關鍵字類型（如 KORELIN_TOKEN_FUNC）的 value 也是動態分配的。
     * 方法2: 我們約定，凡是通過 read_identifier, read_number, read_string 返回的，value 都是 malloc 的。
     *       凡是在 switch 中直接設置的，value 是字符串常量。
     * 
     * 這裡的邏輯需要修正：
     * 在 next_token 中，如果是符號（如 + - * /），value 指向常量區，不能 free。
     * 如果是 identifier/keyword/number/string，value 指向堆內存，必須 free。
     * 
     * 我們可以列出所有可能來自 identifier/number/string 的類型
     */
    bool is_dynamic = false;

    /** @brief 1. 標識符和字面量 */
    if (token->type == KORELIN_TOKEN_IDENT || 
        token->type == KORELIN_TOKEN_STRING || 
        token->type == KORELIN_TOKEN_INT || 
        token->type == KORELIN_TOKEN_FLOAT) {
        is_dynamic = true;
    }
    /** @brief 2. 所有關鍵字類型（因為它們是由 read_identifier 讀取並分配內存的） */
    else if (token->type >= KORELIN_TOKEN_LET && token->type <= KORELIN_TOKEN_NEW) {
        is_dynamic = true;
    }
    /** @brief 3. 類型關鍵字 (int, void, bool 等) 如果被 lookup_ident 識別 */
    else if (token->type == KORELIN_TOKEN_VOID ||
             token->type == KORELIN_TOKEN_BOOL) {
             /** 
              * @brief 注意：KORELIN_TOKEN_INT 和 KORELIN_TOKEN_STRING 已經在上面覆蓋了
              * 但如果是 type keyword 如 'int'，lookup_ident 返回 KORELIN_TOKEN_INT，value 是 malloc 的 "int"
              */
             is_dynamic = true;
    }
    
    if (is_dynamic && token->value != NULL) {
        free((void*)token->value);
        token->value = NULL;
    }
}
