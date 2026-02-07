//
// Created by Helix on 2026/1/10.
//

#include "kparser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// 獲取錯誤名稱
const char* get_error_name(KorelinErrorType type) {
    switch (type) {
        case KORELIN_ERROR_NAME_DEFINE: return "NameDefineError";
        case KORELIN_ERROR_BRACKET_NOT_CLOSED: return "BracketNotClosedError";
        case KORELIN_ERROR_MISSING_SEMICOLON: return "MissingSemicolonError";
        case KORELIN_ERROR_DIVISION_BY_ZERO: return "DivisionByZeroError";
        case KORELIN_ERROR_UNKNOWN_CHARACTER: return "UnknownCharacterError";
        case KORELIN_ERROR_MISSING_KEYWORD_OR_SYMBOL: return "MissingKeywordOrSymbolError";
        case KORELIN_ERROR_ILLEGAL_ARGUMENT: return "IllegalArgumentError";
        case KORELIN_ERROR_INDEX_OUT_OF_BOUNDS: return "IndexOutOfBoundsError";
        case KORELIN_ERROR_NIL_REFERENCE: return "NilReferenceError";
        case KORELIN_ERROR_TYPE_MISMATCH: return "TypeMismatchError";
        case KORELIN_ERROR_FILE_NOT_FOUND: return "FileNotFoundError";
        case KORELIN_ERROR_ILLEGAL_SYNTAX: return "IllegalSyntaxError";
        case KORELIN_ERROR_KEYWORD_AS_IDENTIFIER: return "KeywordAsIdentifierError";
        case KORELIN_ERROR_INVALID_TYPE_POSITION: return "InvalidTypePositionError";
        default: return "UnknownError";
    }
}

// --- 前置聲明 ---
static bool is_keyword(KorelinToken type);
static void advance_token(Parser* parser);
static KastStatement* parse_statement(Parser* parser);
static void synchronize(Parser* parser);
static KastStatement* parse_var_declaration(Parser* parser);
static KastBlock* parse_block(Parser* parser); // 新增
static KastStatement* parse_try_catch(Parser* parser);
static KastStatement* parse_if_statement(Parser* parser);
static KastStatement* parse_switch_statement(Parser* parser);
static KastStatement* parse_for_statement(Parser* parser);
static KastStatement* parse_return_statement(Parser* parser); // 新增
static KastStatement* parse_class_declaration(Parser* parser); // 新增
static KastNode* parse_expression(Parser* parser);
static KastNode* parse_literal(Parser* parser);

// 報告錯誤
static int levenshtein_distance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    // Allocate matrix
    int *matrix = malloc((len1 + 1) * (len2 + 1) * sizeof(int));
    if (!matrix) return len1 > len2 ? len1 : len2; // Fallback

    #define M(i, j) matrix[(i) * (len2 + 1) + (j)]

    for (int i = 0; i <= len1; i++) M(i, 0) = i;
    for (int j = 0; j <= len2; j++) M(0, j) = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            int delete_op = M(i - 1, j) + 1;
            int insert_op = M(i, j - 1) + 1;
            int sub_op = M(i - 1, j - 1) + cost;
            
            int min = delete_op < insert_op ? delete_op : insert_op;
            if (sub_op < min) min = sub_op;
            
            M(i, j) = min;
        }
    }
    
    int result = M(len1, len2);
    free(matrix);
    #undef M
    return result;
}

static const char* KEYWORDS[] = {
    "let", "function", "var", "const", "if", "else", "for", "while", "do", "return",
    "try", "catch", "import", "struct", "true", "false", "nil", "break", "continue",
    "switch", "case", "default", "class", "Map", "public", "private", "protected",
    "extends", "super", "new", "void", "int", "float", "bool", "string", NULL
};

static const char* get_suggestion(const char* wrong_word) {
    if (!wrong_word || strlen(wrong_word) < 2) return NULL;
    
    const char* best_match = NULL;
    int min_dist = 100;
    
    for (int i = 0; KEYWORDS[i] != NULL; i++) {
        int dist = levenshtein_distance(wrong_word, KEYWORDS[i]);
        if (dist < min_dist && dist <= 3) { // Threshold 3
            min_dist = dist;
            best_match = KEYWORDS[i];
        }
    }
    return best_match;
}

static void parser_error(Parser* parser, KorelinErrorType type, const char* format, ...) {
    if (parser->panic_mode) return; // Already in panic mode
    
    // Don't panic for missing semicolon, to allow reporting subsequent errors
    if (type != KORELIN_ERROR_MISSING_SEMICOLON) {
        parser->panic_mode = true;
    }

    parser->has_error = true;
    parser->error_type = type;

    va_list args;
    va_start(args, format);
    vsnprintf(parser->error_message, sizeof(parser->error_message), format, args);
    va_end(args);

    // ANSI Color Codes
    const char* COLOR_RED = "\033[31m";
    const char* COLOR_RESET = "\033[0m";
    const char* COLOR_GRAY = "\033[90m";
    const char* COLOR_BOLD = "\033[1m";
    const char* COLOR_CYAN = "\033[36m";

    // Get Line Info
    Token* err_token = &parser->current_token;
    // If missing semicolon and we moved to next line, use previous token
    if (type == KORELIN_ERROR_MISSING_SEMICOLON && parser->previous_token.line < parser->current_token.line && parser->previous_token.line > 0) {
        err_token = &parser->previous_token;
    }
    
    int line = err_token->line;
    int col = err_token->column;
    
    // Fallback if line is 0 (start)
    if (line == 0) line = 1;

    // Get Line Content from Source
    const char* source = parser->lexer->input;
    const char* line_start = source;
    int current_line = 1;
    while (current_line < line && *line_start) {
        if (*line_start == '\n') current_line++;
        line_start++;
    }
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    
    int line_len = line_end - line_start;
    char* line_content = (char*)malloc(line_len + 1);
    strncpy(line_content, line_start, line_len);
    line_content[line_len] = '\0';

    // Print Formatted Error
    fprintf(stderr, "\n%s[%s] %s%s\n", COLOR_RED, get_error_name(type), parser->error_message, COLOR_RESET);
    
    // Did you mean?
    if (err_token->type == KORELIN_TOKEN_IDENT || 
        err_token->type == KORELIN_TOKEN_ERROR) { // Sometimes lexer marks unknown chars as ERROR
        const char* token_val = err_token->value;
        // If error message mentions "Unknown word", or similar, we check suggestion
        // Or just always check if token matches a keyword closely
        const char* suggestion = get_suggestion(token_val);
        if (suggestion) {
             fprintf(stderr, "%s Did you mean '%s'?%s\n", COLOR_CYAN, suggestion, COLOR_RESET);
        }
    }

    // Print Source Line with Line Number
    fprintf(stderr, "\n%s%4d | %s%s\n", COLOR_GRAY, line, COLOR_RESET, line_content);
    
    // Print Pointer
    fprintf(stderr, "%s     | ", COLOR_GRAY);
    for (int i = 0; i < col; i++) fprintf(stderr, " ");
    fprintf(stderr, "%s^%s\n\n", COLOR_RED, COLOR_RESET);

    free(line_content);
}

// --- Forward Declarations ---
static void advance_token(Parser* parser);
static bool check_token(Parser* parser, KorelinToken type);
static bool match(Parser* parser, KorelinToken type);
static void consume(Parser* parser, KorelinToken type, const char* message);

// --- Parser Initialization & Basic Ops ---

void init_parser(Parser* parser, Lexer* lexer) {
    parser->lexer = lexer;
    parser->has_error = false;
    parser->panic_mode = false;
    parser->error_type = KORELIN_NO_ERROR;
    parser->error_message[0] = '\0';
    parser->has_main_function = false;
    
    // Init previous token to EOF or empty
    parser->previous_token.type = KORELIN_TOKEN_EOF;
    parser->previous_token.value = NULL;
    parser->previous_token.length = 0;
    parser->previous_token.line = 0;
    parser->previous_token.column = 0;

    // Read 6 tokens for lookahead
    parser->current_token = next_token(lexer);
    parser->peek_token = next_token(lexer);
    parser->peek_token_2 = next_token(lexer);
    parser->peek_token_3 = next_token(lexer);
    parser->peek_token_4 = next_token(lexer);
    parser->peek_token_5 = next_token(lexer);
}

static void advance_token(Parser* parser) {
    // 釋放之前的 previous_token 內存
    free_token(&parser->previous_token);
    
    // Move current to previous
    parser->previous_token = parser->current_token;

    parser->current_token = parser->peek_token;
    parser->peek_token = parser->peek_token_2;
    parser->peek_token_2 = parser->peek_token_3;
    parser->peek_token_3 = parser->peek_token_4;
    parser->peek_token_4 = parser->peek_token_5;
    parser->peek_token_5 = next_token(parser->lexer);
     
    // printf("ADVANCE: New Current=%d(%s)\n", parser->current_token.type, parser->current_token.value ? parser->current_token.value : "NULL");
}

static bool check_token(Parser* parser, KorelinToken type) {
    return parser->current_token.type == type;
}

static bool match(Parser* parser, KorelinToken type) {
    if (check_token(parser, type)) {
        advance_token(parser);
        return true;
    }
    return false;
}

static void consume(Parser* parser, KorelinToken type, const char* message) {
    if (check_token(parser, type)) {
        advance_token(parser);
        return;
    }
    
    // 根據期望的 Token 類型判斷錯誤類型
    KorelinErrorType errorType = KORELIN_ERROR_ILLEGAL_SYNTAX;
    if (type == KORELIN_TOKEN_SEMICOLON) {
        errorType = KORELIN_ERROR_MISSING_SEMICOLON;
    } else if (type == KORELIN_TOKEN_RPAREN || type == KORELIN_TOKEN_RBRACE || type == KORELIN_TOKEN_RBRACKET) {
        errorType = KORELIN_ERROR_BRACKET_NOT_CLOSED;
    } else if (type == KORELIN_TOKEN_ASSIGN || type == KORELIN_TOKEN_LPAREN || type == KORELIN_TOKEN_LBRACE) {
        errorType = KORELIN_ERROR_MISSING_KEYWORD_OR_SYMBOL;
    }

    parser_error(parser, errorType, "%s (expected %d, actual %d)", message, type, parser->current_token.type);
    // 這裏不再 exit，而是繼續，等待上層處理 panic_mode
}

// --- 表達式解析 (完整版: 優先級處理) ---

// 前置聲明
static KastNode* parse_logic_or(Parser* parser);
static KastNode* parse_logic_and(Parser* parser);
static KastNode* parse_equality(Parser* parser);
static KastNode* parse_comparison(Parser* parser);
static KastNode* parse_term(Parser* parser);
static KastNode* parse_factor(Parser* parser);
static KastNode* parse_unary(Parser* parser);
static KastNode* parse_primary(Parser* parser);
static char* parse_type_definition(Parser* parser);

static KastNode* parse_expression(Parser* parser) {
    return parse_logic_or(parser);
}

static KastNode* parse_logic_or(Parser* parser) {
    KastNode* left = parse_logic_and(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_OR)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_logic_and(parser);
        if (!right) return NULL; // Error handling could be better

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_logic_and(Parser* parser) {
    KastNode* left = parse_equality(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_AND)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_equality(parser);
        if (!right) return NULL;

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_equality(Parser* parser) {
    KastNode* left = parse_comparison(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_EQ) || check_token(parser, KORELIN_TOKEN_NE)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_comparison(parser);
        if (!right) return NULL;

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_comparison(Parser* parser) {
    KastNode* left = parse_term(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_LT) || check_token(parser, KORELIN_TOKEN_GT) ||
           check_token(parser, KORELIN_TOKEN_LE) || check_token(parser, KORELIN_TOKEN_GE)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_term(parser);
        if (!right) return NULL;

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_term(Parser* parser) {
    KastNode* left = parse_factor(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_ADD) || check_token(parser, KORELIN_TOKEN_SUB)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_factor(parser);
        if (!right) return NULL;

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_factor(Parser* parser) {
    KastNode* left = parse_unary(parser);
    if (!left) return NULL;

    while (check_token(parser, KORELIN_TOKEN_MUL) || check_token(parser, KORELIN_TOKEN_DIV) || check_token(parser, KORELIN_TOKEN_MOD)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* right = parse_unary(parser);
        if (!right) return NULL;

        KastBinaryOp* bin = (KastBinaryOp*)malloc(sizeof(KastBinaryOp));
        bin->base.type = KAST_NODE_BINARY_OP;
        bin->operator = op;
        bin->left = left;
        bin->right = right;
        left = (KastNode*)bin;
    }
    return left;
}

static KastNode* parse_unary(Parser* parser) {
    if (check_token(parser, KORELIN_TOKEN_NOT) || check_token(parser, KORELIN_TOKEN_SUB)) {
        KorelinToken op = parser->current_token.type;
        advance_token(parser);
        KastNode* operand = parse_unary(parser);
        if (!operand) return NULL;

        KastUnaryOp* un = (KastUnaryOp*)malloc(sizeof(KastUnaryOp));
        un->base.type = KAST_NODE_UNARY_OP;
        un->operator = op;
        un->operand = operand;
        return (KastNode*)un;
    }
    return parse_primary(parser);
}

static KastNode* parse_literal(Parser* parser) {
    KastLiteral* node = (KastLiteral*)malloc(sizeof(KastLiteral));
    node->base.type = KAST_NODE_LITERAL;
    
    // 深度複製 Token
    node->token = parser->current_token;
    node->token.value = strdup(parser->current_token.value);
    
    advance_token(parser);
    return (KastNode*)node;
}

static KastNode* parse_array_literal(Parser* parser) {
    consume(parser, KORELIN_TOKEN_LBRACKET, "Expected '['");
    
    KastNode** elements = NULL;
    size_t count = 0;
    size_t capacity = 0;
    
    if (!check_token(parser, KORELIN_TOKEN_RBRACKET)) {
        while (!check_token(parser, KORELIN_TOKEN_RBRACKET) && !check_token(parser, KORELIN_TOKEN_EOF)) {
            KastNode* elem = parse_expression(parser);
            if (elem) {
                if (count >= capacity) {
                    capacity = capacity == 0 ? 4 : capacity * 2;
                    elements = (KastNode**)realloc(elements, capacity * sizeof(KastNode*));
                }
                elements[count++] = elem;
            }
            
            if (check_token(parser, KORELIN_TOKEN_COMMA)) {
                advance_token(parser);
            } else {
                break;
            }
        }
    }
    
    consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
    
    KastArrayLiteral* node = (KastArrayLiteral*)malloc(sizeof(KastArrayLiteral));
    node->base.type = KAST_NODE_ARRAY_LITERAL;
    node->elements = elements;
    node->element_count = count;
    return (KastNode*)node;
}

static KastNode* parse_primary(Parser* parser) {
    if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
        return parse_array_literal(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
        advance_token(parser);
        KastNode* expr = parse_expression(parser);
        consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
        return expr;
    }

    // New Expression: new ClassName(...)
    if (check_token(parser, KORELIN_TOKEN_NEW)) {
        advance_token(parser);
        
        // Use parse_type_definition to support Map<K,V> etc.
        // But wait, parse_type_definition consumes generic params too.
        // And standard `new Map<K,V>()` syntax works fine with it.
        // However, we need to be careful if it returns "Map<K,V>[]"
        
        char* type_name = parse_type_definition(parser);
        if (!type_name) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected type name after new");
             return NULL;
        }
        
        bool is_array = false;
        // Check if type_name ends with []? 
        // Or does user write `new int[10]`?
        // parse_type_definition parses `int[]`.
        // If user writes `new int[10]`, parse_type_definition parses `int`, then we see `[`.
        // Wait, parse_type_definition handles `[]`. So `int[]` is parsed as one type.
        // But for `new int[10]`, the `[10]` is array creation, not just type.
        // The current implementation of parse_type_definition consumes `[]` (empty brackets).
        // If we have `new int[10]`, parse_type_definition will see `int` then `[`.
        // If it consumes `[` then checks `]`. If `10` is there, it fails or we need to change it.
        
        // Actually, parse_type_definition consumes `[]` only if it's empty `[]`.
        // Let's modify parse_type_definition or check here.
        // If we use parse_type_definition, it consumes `int[]` for `new int[]`.
        // But `new` usually requires size for arrays or args for objects.
        
        // Let's assume parse_type_definition parses the base type (including generics).
        // If it encounters `[` and it's NOT empty, it stops?
        // My implementation of parse_type_definition consumes `[]` in a loop.
        // It expects `]` immediately after `[`. So `[10]` will cause it to stop (if we implement peek) or fail.
        
        // Let's adjust parse_type_definition to peek for `]`.
        
        // For now, let's revert to simple type parsing for `new` or rely on a smarter way.
        // The issue is `new int[10]` vs `new int[]` (invalid in C#, Java requires size or init).
        // In Korelin `new int[10]` is valid.
        
        // Let's use parse_type_definition BUT we need to handle the array size case.
        // If parse_type_definition consumes `Map<K,V>`, we are good.
        // If it consumes `int`, we are good.
        
        // If `new` is followed by `int` then `[10]`.
        // parse_type_definition(int) -> "int". Next is `[`.
        
        if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
            is_array = true;
            advance_token(parser); // [
        } else if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
            // Object init
            advance_token(parser); // (
        } else {
             // Maybe type was parsed as "int[]" because parse_type_definition consumed `[]`?
             // If user wrote `new int[] { ... }` (initializer) -> supported?
             // If user wrote `new int[10]`, parse_type_definition would fail on `10` if it expects `]`.
             
             // Let's look at parse_type_definition implementation again.
             // It does `consume(RBRACKET)`. So `[10]` would trigger error "Expected ']'".
             // This is problematic for `new int[10]`.
             
             // WE NEED TO FIX parse_type_definition TO NOT CONSUME `[` IF IT'S NOT `[]`.
             // OR handling `new` specially.
             
             // Special handling for NEW:
             // 1. Parse base type (ident or primitive or Map<...>)
             // 2. Check for `[` (Array) or `(` (Object)
             
             consume(parser, KORELIN_TOKEN_LPAREN, "Expected '(' or '['");
        }
        
        // Parse args
        KastNode** args = NULL;
        size_t arg_count = 0;
        
        if (is_array) {
            // Array size
            KastNode* size_expr = parse_expression(parser);
            args = (KastNode**)malloc(sizeof(KastNode*));
            args[0] = size_expr;
            arg_count = 1;
            consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
        } else {
            if (!check_token(parser, KORELIN_TOKEN_RPAREN)) {
                while (!check_token(parser, KORELIN_TOKEN_RPAREN) && !check_token(parser, KORELIN_TOKEN_EOF)) {
                    KastNode* arg = parse_expression(parser);
                    if (arg) {
                        args = (KastNode**)realloc(args, (arg_count + 1) * sizeof(KastNode*));
                        args[arg_count++] = arg;
                    }
                    if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
                    else break;
                }
            }
            consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
        }
        
        KastNew* node = (KastNew*)malloc(sizeof(KastNew));
        node->base.type = KAST_NODE_NEW;
        node->class_name = type_name;
        node->is_array = is_array;
        node->args = args;
        node->arg_count = arg_count;
        return (KastNode*)node;
    }
    
    if (check_token(parser, KORELIN_TOKEN_INT) ||
        check_token(parser, KORELIN_TOKEN_FLOAT) ||
        check_token(parser, KORELIN_TOKEN_STRING) ||
        check_token(parser, KORELIN_TOKEN_TRUE) ||
        check_token(parser, KORELIN_TOKEN_FALSE) ||
        check_token(parser, KORELIN_TOKEN_NIL)) {
        return parse_literal(parser);
    }

    // Super Call: super(args) - used in constructor
    if (check_token(parser, KORELIN_TOKEN_SUPER)) {
        // Treat as function call expression essentially, but with "super" as callee
        // For simplicity, we can create a Call expression where callee is Identifier("super")
        // or a special Super node. Let's reuse Identifier for now.
        
        KastIdentifier* ident = (KastIdentifier*)malloc(sizeof(KastIdentifier));
        ident->base.type = KAST_NODE_IDENTIFIER;
        ident->name = strdup("super");
        advance_token(parser); // consume super
        
        // Expect (
        if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
            advance_token(parser);
            
            KastCall* call = (KastCall*)malloc(sizeof(KastCall));
            call->base.type = KAST_NODE_CALL;
            call->callee = (KastNode*)ident;
            call->args = NULL;
            call->arg_count = 0;
            
            if (!check_token(parser, KORELIN_TOKEN_RPAREN)) {
                size_t capacity = 0;
                while (!check_token(parser, KORELIN_TOKEN_RPAREN) && !check_token(parser, KORELIN_TOKEN_EOF)) {
                    KastNode* arg = parse_expression(parser);
                    if (arg) {
                        if (call->arg_count >= capacity) {
                            capacity = capacity == 0 ? 2 : capacity * 2;
                            call->args = (KastNode**)realloc(call->args, capacity * sizeof(KastNode*));
                        }
                        call->args[call->arg_count++] = arg;
                    }
                    if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
                    else break;
                }
            }
            consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
            return (KastNode*)call;
        } else {
             // super.method() ?
             // If we just return 'super' identifier, the member access parser logic should handle it if it follows.
             return (KastNode*)ident;
        }
    }

    if (check_token(parser, KORELIN_TOKEN_IDENT) || check_token(parser, KORELIN_TOKEN_KEYWORD_STRING)) {
         KastNode* current = NULL;
         
         // Check for Class::Member (Static access)
         if (parser->peek_token.type == KORELIN_TOKEN_SCOPE) {
              char* class_name = strdup(parser->current_token.value);
              advance_token(parser); // ident
              advance_token(parser); // ::
              if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
                  parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member name");
                  free(class_name);
                  return NULL;
              }
              char* member_name = strdup(parser->current_token.value);
              advance_token(parser);
              
              KastScopeAccess* node = (KastScopeAccess*)malloc(sizeof(KastScopeAccess));
              node->base.type = KAST_NODE_SCOPE_ACCESS;
              node->class_name = class_name;
              node->member_name = member_name;
              current = (KastNode*)node;
         } else {
              KastIdentifier* node = (KastIdentifier*)malloc(sizeof(KastIdentifier));
              node->base.type = KAST_NODE_IDENTIFIER;
              node->name = strdup(parser->current_token.value);
              advance_token(parser);
              current = (KastNode*)node;
         }
         
         // Check for member access (obj.member) or Call (func()) or Array Access (arr[index])
           while (check_token(parser, KORELIN_TOKEN_DOT) || check_token(parser, KORELIN_TOKEN_LPAREN) || check_token(parser, KORELIN_TOKEN_LBRACKET)) {
               if (check_token(parser, KORELIN_TOKEN_DOT)) {
                   advance_token(parser);
                   if (!check_token(parser, KORELIN_TOKEN_IDENT) && !is_keyword(parser->current_token.type)) {
                        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member name");
                        return current;
                   }
                   char* member = strdup(parser->current_token.value);
                   advance_token(parser);
                   
                   KastMemberAccess* acc = (KastMemberAccess*)malloc(sizeof(KastMemberAccess));
                   acc->base.type = KAST_NODE_MEMBER_ACCESS;
                   acc->object = current;
                   acc->member_name = member;
                   current = (KastNode*)acc;
               } else if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
                   // Call
                   advance_token(parser);
                   KastNode** args = NULL;
                   size_t arg_count = 0;
                   if (!check_token(parser, KORELIN_TOKEN_RPAREN)) {
                       while (!check_token(parser, KORELIN_TOKEN_RPAREN) && !check_token(parser, KORELIN_TOKEN_EOF)) {
                           KastNode* arg = parse_expression(parser);
                           if (arg) {
                               args = (KastNode**)realloc(args, (arg_count + 1) * sizeof(KastNode*));
                               args[arg_count++] = arg;
                           }
                           if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
                           else break;
                       }
                   }
                   consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
                   
                   KastCall* call = (KastCall*)malloc(sizeof(KastCall));
                   call->base.type = KAST_NODE_CALL;
                   call->callee = current;
                   call->args = args;
                   call->arg_count = arg_count;
                   current = (KastNode*)call;
               } else if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
                   // Array Access
                   advance_token(parser); // [
                   KastNode* index = parse_expression(parser);
                   consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
                   
                   KastArrayAccess* acc = (KastArrayAccess*)malloc(sizeof(KastArrayAccess));
                   acc->base.type = KAST_NODE_ARRAY_ACCESS;
                   acc->array = current;
                   acc->index = index;
                   current = (KastNode*)acc;
               }
           }
           return current;
      }
    
    parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected expression, but at '%s'", parser->current_token.value ? parser->current_token.value : "EOF");
    return NULL;
}

// --- Assignment Parsing ---
// Syntax: ident = expr ;

static KastStatement* parse_assignment(Parser* parser) {
    if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected variable name");
        return NULL;
    }
    
    // Parse lvalue (identifier)
    KastIdentifier* ident = (KastIdentifier*)malloc(sizeof(KastIdentifier));
    ident->base.type = KAST_NODE_IDENTIFIER;
    ident->name = strdup(parser->current_token.value);
    advance_token(parser);
    
    consume(parser, KORELIN_TOKEN_ASSIGN, "Assignment expects '='");
    
    KastNode* value = parse_expression(parser);
    if (parser->panic_mode) { free(ident->name); free(ident); return NULL; }
    
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    if (value == NULL) { free(ident->name); free(ident); return NULL; }
    
    KastAssignment* stmt = (KastAssignment*)malloc(sizeof(KastAssignment));
    if (stmt == NULL) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Memory allocation failed");
        free(ident->name); free(ident);
        return NULL;
    }
    stmt->base.type = KAST_NODE_ASSIGNMENT;
    stmt->lvalue = (KastNode*)ident;
    stmt->value = value;
    return (KastStatement*)stmt;
}

// --- 代碼塊解析 ---
// 語法: { stmt; ... }

static KastBlock* parse_block(Parser* parser) {
    KastBlock* block = (KastBlock*)malloc(sizeof(KastBlock));
    block->base.type = KAST_NODE_BLOCK;
    block->statements = NULL;
    block->statement_count = 0;
    
    consume(parser, KORELIN_TOKEN_LBRACE, "Block expects '{'");
    if (parser->panic_mode) { free(block); return NULL; }

    size_t capacity = 0;
    while (!check_token(parser, KORELIN_TOKEN_RBRACE) && !check_token(parser, KORELIN_TOKEN_EOF)) {
        KastStatement* stmt = parse_statement(parser);
        if (stmt) {
            if (block->statement_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                block->statements = (KastStatement**)realloc(block->statements, capacity * sizeof(KastStatement*));
            }
            block->statements[block->statement_count++] = stmt;
        } 
        
        if (parser->panic_mode) {
            synchronize(parser);
        }
    }

    consume(parser, KORELIN_TOKEN_RBRACE, "Block expects '}'");
    if (parser->panic_mode) {
        // 這裏需要釋放已解析的語句
        for (size_t i = 0; i < block->statement_count; i++) {
            free_ast_node((KastNode*)block->statements[i]);
        }
        if (block->statements) free(block->statements);
        free(block);
        return NULL;
    }

    return block;
}

// --- Try-Catch 解析 ---
// 語法: try { ... } catch (Error) { ... }

static KastStatement* parse_try_catch(Parser* parser) {
    KastTryCatch* stmt = (KastTryCatch*)malloc(sizeof(KastTryCatch));
    stmt->base.type = KAST_NODE_TRY_CATCH;
    stmt->try_block = NULL;
    stmt->catch_blocks = NULL;
    stmt->catch_count = 0;

    consume(parser, KORELIN_TOKEN_TRY, "Expected 'try'");
    if (parser->panic_mode) { free(stmt); return NULL; }

    stmt->try_block = parse_block(parser);
    if (stmt->try_block == NULL) { free(stmt); return NULL; }

    size_t capacity = 0;
    while (check_token(parser, KORELIN_TOKEN_CATCH)) {
        advance_token(parser); // 消耗 catch

        // 期望 (ErrorType)
        consume(parser, KORELIN_TOKEN_LPAREN, "Catch block expects '('");
        if (parser->panic_mode) { 
            // 簡單的釋放邏輯，爲了簡潔略過深層釋放
            free_ast_node((KastNode*)stmt); return NULL; 
        }

        // 錯誤類型目前是標識符 (或者我們可以定義特定的 Token)
        // 假設錯誤類型如 DivisionByZeroError 是標識符
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Catch block expects error type name");
            free_ast_node((KastNode*)stmt); return NULL;
        }
        
        char* error_type = strdup(parser->current_token.value);
        advance_token(parser);

        consume(parser, KORELIN_TOKEN_RPAREN, "Catch block expects ')'");
        if (parser->panic_mode) { 
            free(error_type); free_ast_node((KastNode*)stmt); return NULL; 
        }

        // 解析 catch 代碼塊
        KastBlock* catch_body = parse_block(parser);
        if (catch_body == NULL) {
             free(error_type); free_ast_node((KastNode*)stmt); return NULL;
        }

        // 添加到數組
        if (stmt->catch_count >= capacity) {
            capacity = capacity == 0 ? 2 : capacity * 2;
            stmt->catch_blocks = (KorelinCatchBlock*)realloc(stmt->catch_blocks, capacity * sizeof(KorelinCatchBlock));
        }
        stmt->catch_blocks[stmt->catch_count].error_type = error_type;
        stmt->catch_blocks[stmt->catch_count].body = catch_body;
        stmt->catch_count++;
    }

    return (KastStatement*)stmt;
}

static bool is_keyword(KorelinToken type) {
    return (type >= KORELIN_TOKEN_LET && type <= KORELIN_TOKEN_NEW) ||
           (type >= KORELIN_TOKEN_KEYWORD_STRING && type <= KORELIN_TOKEN_VOID) ||
           type == KORELIN_TOKEN_TRUE || type == KORELIN_TOKEN_FALSE || type == KORELIN_TOKEN_NIL;
}

// 解析類型定義 (Type Definition)
// 支持: int, MyClass, int[], MyClass[], Map<K,V>, Array<T>
// 返回類型名字符串 (需要在外部 free)
static char* parse_type_definition(Parser* parser);
// 聲明 parse_parameter_list
static KastNode** parse_parameter_list(Parser* parser, size_t* arg_count, bool enforce_self);

// 解析函數聲明 (Type Name(...) {})
static KastStatement* parse_function_declaration(Parser* parser, char* return_type, char* name) {
    // 1. 泛型參數 (可選) <T>
    char** generic_params = NULL;
    size_t generic_count = 0;
    
    if (check_token(parser, KORELIN_TOKEN_LT)) {
        advance_token(parser); // consume <
        
        while (!check_token(parser, KORELIN_TOKEN_GT) && !check_token(parser, KORELIN_TOKEN_EOF)) {
            if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
                parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected generic parameter name");
                break;
            }
            
            char* param = strdup(parser->current_token.value);
            advance_token(parser);
            
            generic_params = (char**)realloc(generic_params, (generic_count + 1) * sizeof(char*));
            generic_params[generic_count++] = param;
            
            if (check_token(parser, KORELIN_TOKEN_COMMA)) {
                advance_token(parser);
            } else {
                break;
            }
        }
        consume(parser, KORELIN_TOKEN_GT, "Expected '>'");
    }
    
    // 2. 主函數檢查
    if (strcmp(name, "main") == 0) {
        if (parser->has_main_function) {
            parser_error(parser, KORELIN_ERROR_NAME_DEFINE, "Main function 'main' redefined");
        }
        parser->has_main_function = true;
    }
    
    // 3. 參數列表
    size_t arg_count = 0;
    KastNode** args = parse_parameter_list(parser, &arg_count, false); // false: 不強制 self
    
    // 4. 函數體
    KastBlock* body = parse_block(parser);
    
    // 5. 構建節點
    KastFunctionDecl* func = (KastFunctionDecl*)malloc(sizeof(KastFunctionDecl));
    func->base.type = KAST_NODE_FUNCTION_DECL;
    func->name = name;
    func->return_type = return_type;
    func->args = args;
    func->arg_count = arg_count;
    func->body = body;
    func->generic_params = generic_params;
    func->generic_count = generic_count;
    func->parent_class_name = NULL;
    func->access = KAST_ACCESS_PUBLIC;
    
    return (KastStatement*)func;
}

// 解析類型化的聲明 (Type Name ... -> Func or Var)
static KastStatement* parse_typed_declaration(Parser* parser) {
    // 1. 解析類型
    char* type_name = parse_type_definition(parser);
    if (!type_name) return NULL;
    
    // 2. 解析名稱
    if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected identifier");
        free(type_name);
        return NULL;
    }
    char* name = strdup(parser->current_token.value);
    advance_token(parser);
    
    // Check for :: (Out of class method definition)
    if (check_token(parser, KORELIN_TOKEN_SCOPE)) {
        char* class_name = name; // Previous identifier is class name
        advance_token(parser); // eat ::
        
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected method name after ::");
             free(class_name);
             free(type_name);
             return NULL;
        }
        char* method_name = strdup(parser->current_token.value);
        advance_token(parser);
        
        // Must be function definition
        if (check_token(parser, KORELIN_TOKEN_LPAREN) || check_token(parser, KORELIN_TOKEN_LT)) {
             KastStatement* decl = parse_function_declaration(parser, type_name, method_name);
             if (decl && decl->base.type == KAST_NODE_FUNCTION_DECL) {
                 ((KastFunctionDecl*)decl)->parent_class_name = class_name;
             } else {
                 free(class_name);
             }
             return decl;
        } else {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected '(' after method name in definition");
             free(class_name);
             free(method_name);
             free(type_name);
             return NULL;
        }
    }
    
    // 3. 分發: '(' -> 函數, '<' -> 泛型函數(可能), 其他 -> 變量
    if (check_token(parser, KORELIN_TOKEN_LPAREN) || check_token(parser, KORELIN_TOKEN_LT)) {
        // 可能是函數
        // 如果是 <，檢查是否是泛型函數定義: Type Name<T>(...)
        if (check_token(parser, KORELIN_TOKEN_LT)) {
            // 這是一個強信號，變量聲明不會有 <T> 在名字後面 (除非是類型的一部分，但類型已經解析完了)
            return parse_function_declaration(parser, type_name, name);
        }
        
        // 如果是 (，則是函數
        if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
            return parse_function_declaration(parser, type_name, name);
        }
    }
    
    // 否則是變量聲明: Type Name [= ...] ;
    KastVarDecl* var_decl = (KastVarDecl*)malloc(sizeof(KastVarDecl));
    var_decl->base.type = KAST_NODE_VAR_DECL;
    var_decl->is_global = false; // 默認爲 false，具體看上下文，但在 AST 中通常不區分 global/local 節點類型，而是由 scope 決定
    var_decl->is_constant = false; // 默認可變
    var_decl->type_name = type_name;
    var_decl->name = name;
    var_decl->is_array = false;
    
    // 檢查數組後綴 Name[]
    if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
        advance_token(parser);
        consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
        var_decl->is_array = true;
    } else {
        // 檢查類型是否自帶數組
        size_t len = strlen(type_name);
        if (len >= 2 && strcmp(type_name + len - 2, "[]") == 0) {
            var_decl->is_array = true;
        }
    }
    
    // 初始值
    if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
        advance_token(parser);
        var_decl->init_value = parse_expression(parser);
    } else {
        var_decl->init_value = NULL;
    }
    
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    return (KastStatement*)var_decl;
}

static char* parse_type_definition(Parser* parser) {
    char* type_name = NULL;
    
    // 1. 基礎類型或標識符
    if (check_token(parser, KORELIN_TOKEN_STRUCT)) {
        advance_token(parser); // struct
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected struct name");
            return NULL;
        }
        type_name = strdup(parser->current_token.value);
        advance_token(parser);
    } else if (check_token(parser, KORELIN_TOKEN_IDENT) || 
        (parser->current_token.type >= KORELIN_TOKEN_KEYWORD_STRING && parser->current_token.type <= KORELIN_TOKEN_BOOL) ||
        parser->current_token.type == KORELIN_TOKEN_VOID) {
        
        // Use a dynamic buffer for dotted names
        char buffer[256];
        buffer[0] = '\0';
        strcat(buffer, parser->current_token.value);
        advance_token(parser);
        
        // Handle dotted names (e.g. std.io.File)
        while (check_token(parser, KORELIN_TOKEN_DOT)) {
            strcat(buffer, ".");
            advance_token(parser); // .
            if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
                 parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected type name part");
                 return NULL;
            }
            strcat(buffer, parser->current_token.value);
            advance_token(parser);
        }
        
        type_name = strdup(buffer);
    } else if (check_token(parser, KORELIN_TOKEN_MAP)) {
        // Map<K, V>
        advance_token(parser);
        consume(parser, KORELIN_TOKEN_LT, "Expected '<'");
        
        char* key_type = parse_type_definition(parser);
        if (!key_type) return NULL;
        
        consume(parser, KORELIN_TOKEN_COMMA, "Expected ','");
        
        char* value_type = parse_type_definition(parser);
        if (!value_type) {
            free(key_type);
            return NULL;
        }
        
        consume(parser, KORELIN_TOKEN_GT, "Expected '>'");
        
        // Construct "Map<Key,Value>" string
        // Using fixed buffer for simplicity, better use dynamic string builder
        // Assuming max length
        size_t len = strlen("Map<") + strlen(key_type) + strlen(",") + strlen(value_type) + strlen(">") + 1;
        type_name = (char*)malloc(len);
        sprintf(type_name, "Map<%s,%s>", key_type, value_type);
        
        free(key_type);
        free(value_type);
    } else {
        return NULL;
    }
    
    // 2. 泛型參數 <T> (Generic Type)
    // E.g. Array<T>, MyClass<T>
    if (check_token(parser, KORELIN_TOKEN_LT)) {
        advance_token(parser);
        char* param_type = parse_type_definition(parser);
        consume(parser, KORELIN_TOKEN_GT, "Expected '>'");
        
        if (param_type) {
            size_t len = strlen(type_name) + strlen("<") + strlen(param_type) + strlen(">") + 1;
            char* new_name = (char*)malloc(len);
            sprintf(new_name, "%s<%s>", type_name, param_type);
            free(type_name);
            free(param_type);
            type_name = new_name;
        }
    }
    
    // 3. 數組標記 []
    // 注意：這裏需要 Peek，如果是 [10] (用於 new) 則不應該吞掉。
    // 僅當是 [] 時才作爲類型的一部分。
    while (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
        if (parser->peek_token.type == KORELIN_TOKEN_RBRACKET) {
            advance_token(parser); // [
            advance_token(parser); // ]
            
            size_t len = strlen(type_name) + strlen("[]") + 1;
            char* new_name = (char*)malloc(len);
            sprintf(new_name, "%s[]", type_name);
            free(type_name);
            type_name = new_name;
        } else {
            break; // 可能是 [10]，留給 new 處理
        }
    }
    
    return type_name;
}

// --- 變量聲明解析 ---
// 語法: (var|let) [const] [type] ident [= expr] ;

static KastStatement* parse_var_declaration(Parser* parser) {
    KastVarDecl* stmt = (KastVarDecl*)malloc(sizeof(KastVarDecl));
    stmt->base.type = KAST_NODE_VAR_DECL;
    
    // 1. 確定作用域 (var -> local, let -> global)
    if (check_token(parser, KORELIN_TOKEN_LET)) {
        stmt->is_global = true;
    } else if (check_token(parser, KORELIN_TOKEN_VAR)) {
        stmt->is_global = false;
    } else {
        free(stmt);
        return NULL; 
    }
    advance_token(parser); // 消耗 var/let

    // 2. 檢查 const
    if (check_token(parser, KORELIN_TOKEN_CONST)) {
        stmt->is_constant = true;
        advance_token(parser);
    } else {
        stmt->is_constant = false;
    }

    // 3. 類型聲明 (可選) - 使用 parse_type_definition 支持複雜類型
    // 需要預判下一個 token 是否是類型
    char* type_name = parse_type_definition(parser);
    stmt->type_name = type_name;

    // 4. 變量名 (必須是標識符)
    // 邏輯調整：支持類型推導 (var x = 1)
    // 如果 type_name 不爲空，但當前 token 不是 IDENT，且當前 token 是 = 或 ;
    // 說明之前的 "type_name" 其實是變量名
    if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
        bool handled = false;
        if (stmt->type_name && 
           (check_token(parser, KORELIN_TOKEN_ASSIGN) || check_token(parser, KORELIN_TOKEN_SEMICOLON))) {
            // 之前的 "type" 其實是名字
            stmt->name = stmt->type_name;
            stmt->type_name = NULL;
            handled = true;
        }

        if (!handled) {
            KorelinErrorType errType = KORELIN_ERROR_ILLEGAL_SYNTAX;
            const char* msg = "Expected variable name";

            // 檢查是否爲關鍵字
            KorelinToken t = parser->current_token.type;
            bool is_keyword = (t >= KORELIN_TOKEN_LET && t <= KORELIN_TOKEN_NEW) ||
                              t == KORELIN_TOKEN_BOOL || t == KORELIN_TOKEN_VOID;
            
            // 針對 int/float/string 特殊處理 (區分關鍵字和字面量)
            if (!is_keyword && parser->current_token.value) {
                 if (t == KORELIN_TOKEN_INT && strcmp(parser->current_token.value, "int") == 0) is_keyword = true;
                 else if (t == KORELIN_TOKEN_FLOAT && strcmp(parser->current_token.value, "float") == 0) is_keyword = true;
                 else if (t == KORELIN_TOKEN_KEYWORD_STRING && strcmp(parser->current_token.value, "string") == 0) is_keyword = true;
            }

            if (is_keyword) {
                errType = KORELIN_ERROR_KEYWORD_AS_IDENTIFIER;
                msg = "Keyword cannot be used as variable name";
            }

            parser_error(parser, errType, msg);
            if (stmt->type_name) free(stmt->type_name);
            free(stmt);
            return NULL;
        }
    } else {
        stmt->name = strdup(parser->current_token.value);
        advance_token(parser);
    }

    // [新增] 檢查數組標記 [] (後綴數組語法)
    // 注意: parse_type_definition 可能已經處理了類型中的 [] (例如 int[])
    // 這裏處理的是 var int a[]; 這種形式
    if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
        advance_token(parser);
        consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
        stmt->is_array = true;
    } else {
        // 如果類型本身已經是數組 (例如 int[])，is_array 也應該爲 true
        // 簡單的檢查字符串後綴
        if (stmt->type_name) {
            size_t len = strlen(stmt->type_name);
            if (len >= 2 && strcmp(stmt->type_name + len - 2, "[]") == 0) {
                stmt->is_array = true;
            } else {
                stmt->is_array = false;
            }
        } else {
            stmt->is_array = false;
        }
    }

    // [新增检查] 檢查是否錯誤的在變量名後跟了類型 (例如: var x int = 1)
    if (check_token(parser, KORELIN_TOKEN_INT) || 
        check_token(parser, KORELIN_TOKEN_FLOAT) ||
        check_token(parser, KORELIN_TOKEN_KEYWORD_STRING) ||
        check_token(parser, KORELIN_TOKEN_BOOL) ||
        check_token(parser, KORELIN_TOKEN_VOID)) {
         
         parser_error(parser, KORELIN_ERROR_INVALID_TYPE_POSITION, "Type declaration should be before variable name (e.g., var %s %s)", parser->current_token.value, stmt->name);
         if (stmt->name) free(stmt->name);
         if (stmt->type_name) free(stmt->type_name);
         free(stmt);
         return NULL;
    }

    // 5. 初始值 (可選，通過 = )
    stmt->init_value = NULL;
    if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
        advance_token(parser); // 消耗 =
        stmt->init_value = parse_expression(parser);
        if (stmt->init_value == NULL && parser->panic_mode) {
             // 表達式解析失敗
             if (stmt->name) free(stmt->name);
             if (stmt->type_name) free(stmt->type_name);
             free(stmt);
             return NULL;
        }
    }

    // DEBUG: Print token before consuming semicolon
    // printf("DEBUG: In parse_typed_declaration, before semicolon. Current: %d (%s)\n", parser->current_token.type, parser->current_token.value ? parser->current_token.value : "NULL");

    // 6. 分號 (必須)
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    if (parser->panic_mode) {
        if (stmt->name) free(stmt->name);
        if (stmt->type_name) free(stmt->type_name);
        if (stmt->init_value) free_ast_node(stmt->init_value);
        free(stmt);
        return NULL;
    }

    return (KastStatement*)stmt;
}

// Parse function parameter list (Type name, Type name)
// Returns array of parameter nodes, updates arg_count
// Used for func definition
// Note: For class methods, if enforce_self is true, first param must be self
static KastNode** parse_parameter_list(Parser* parser, size_t* arg_count, bool enforce_self) {
    // printf("DEBUG: Entering parse_parameter_list. Current: %d\n", parser->current_token.type);
    KastNode** args = NULL;
    *arg_count = 0;
    
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");

    if (check_token(parser, KORELIN_TOKEN_RPAREN)) {
        if (enforce_self) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Class method's first parameter must be 'self'");
        }
        advance_token(parser); // consume )
        return args; // Simple return
    }
    
    bool first_param = true;
    while (!check_token(parser, KORELIN_TOKEN_RPAREN) && !check_token(parser, KORELIN_TOKEN_EOF)) {
        // printf("DEBUG: In param loop. Current: %d, Value: %s\n", parser->current_token.type, parser->current_token.value ? parser->current_token.value : "NULL");
        // Check for self (as first param)
        if (enforce_self && first_param) {
        // printf("DEBUG: Checking self. Current type: %d, value: %s\n", parser->current_token.type, parser->current_token.value);
        if (check_token(parser, KORELIN_TOKEN_IDENT) && strcmp(parser->current_token.value, "self") == 0) {
            // Found self
            advance_token(parser);
                
                KastVarDecl* param = (KastVarDecl*)malloc(sizeof(KastVarDecl));
                param->base.type = KAST_NODE_VAR_DECL;
                param->is_global = false;
                param->is_constant = true; 
                param->type_name = strdup("self"); 
                param->is_array = false;
                param->name = strdup("self");
                param->init_value = NULL;
                
                args = (KastNode**)realloc(args, (*arg_count + 1) * sizeof(KastNode*));
                args[(*arg_count)++] = (KastNode*)param;
                
                if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
                first_param = false;
                continue;
            } else {
                 parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Class method's first parameter must be 'self'");
            }
        }
        
        char* type_name = parse_type_definition(parser);
        if (!type_name) {
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected parameter type");
            return args; 
        }
        
        // Handle 'self' as a named parameter with implicit Any type (for non-enforced contexts)
         if (strcmp(type_name, "self") == 0 && (check_token(parser, KORELIN_TOKEN_RPAREN) || check_token(parser, KORELIN_TOKEN_COMMA))) {
              // If static (enforce_self == false), we treat 'self' as a normal parameter named 'self'
              // instead of ignoring it. This allows user to define static methods with 'self' argument.
              
              char* param_name = strdup("self");
              free(type_name);
              type_name = strdup("Any");
              
              KastVarDecl* param = (KastVarDecl*)malloc(sizeof(KastVarDecl));
              param->base.type = KAST_NODE_VAR_DECL;
              param->is_global = false;
              param->is_constant = false;
              param->type_name = type_name;
              param->is_array = false;
              param->name = param_name;
              param->init_value = NULL;
              
              args = (KastNode**)realloc(args, (*arg_count + 1) * sizeof(KastNode*));
              args[(*arg_count)++] = (KastNode*)param;
              
              if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
              else break;
              
              first_param = false;
              continue;
         }
        
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected parameter name");
            free(type_name);
            return args;
        }
        char* param_name = strdup(parser->current_token.value);
        advance_token(parser);
        
        KastVarDecl* param = (KastVarDecl*)malloc(sizeof(KastVarDecl));
        param->base.type = KAST_NODE_VAR_DECL;
        param->is_global = false;
        param->is_constant = false;
        param->type_name = type_name;
        param->is_array = false; 

        if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
            advance_token(parser);
            consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
            param->is_array = true; 
        } else {
             size_t len = strlen(type_name);
             if (len >= 2 && strcmp(type_name + len - 2, "[]") == 0) {
                 param->is_array = true;
             }
        }

        param->name = param_name;
        param->init_value = NULL;
        
        args = (KastNode**)realloc(args, (*arg_count + 1) * sizeof(KastNode*));
        args[(*arg_count)++] = (KastNode*)param;
        
        if (check_token(parser, KORELIN_TOKEN_COMMA)) advance_token(parser);
        else break;
        
        first_param = false;
    }
    
    if (enforce_self && *arg_count == 0) {
         parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Class method's first parameter must be 'self'");
    }
    
    // printf("DEBUG: Before consume RPAREN. Current: %d, Value: %s\n", parser->current_token.type, parser->current_token.value ? parser->current_token.value : "NULL");
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
    return args;
}

// --- 語句解析 ---

// if 語句解析
static KastStatement* parse_if_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_IF, "Expected 'if'");
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");
    KastNode* condition = parse_expression(parser);
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
    
    KastStatement* then_branch = parse_statement(parser);
    KastStatement* else_branch = NULL;
    
    if (check_token(parser, KORELIN_TOKEN_ELSE)) {
        advance_token(parser);
        else_branch = parse_statement(parser);
    }
    
    KastIf* node = (KastIf*)malloc(sizeof(KastIf));
    node->base.type = KAST_NODE_IF;
    node->condition = condition;
    node->then_branch = then_branch;
    node->else_branch = else_branch;
    return (KastStatement*)node;
}

// switch 語句解析
static KastStatement* parse_switch_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_SWITCH, "Expected 'switch'");
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");
    KastNode* condition = parse_expression(parser);
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
    consume(parser, KORELIN_TOKEN_LBRACE, "Expected '{'");
    
    KastSwitch* node = (KastSwitch*)malloc(sizeof(KastSwitch));
    node->base.type = KAST_NODE_SWITCH;
    node->condition = condition;
    node->cases = NULL;
    node->case_count = 0;
    node->default_branch = NULL;
    
    size_t capacity = 0;
    
    while (!check_token(parser, KORELIN_TOKEN_RBRACE) && !check_token(parser, KORELIN_TOKEN_EOF)) {
        if (check_token(parser, KORELIN_TOKEN_CASE)) {
            advance_token(parser);
            KastNode* val = parse_expression(parser);
            consume(parser, KORELIN_TOKEN_COLON, "Expected ':'");
            
            // 解析 case 體 (直到下一個 case/default/rbrace)
            // 爲簡化，我們要求 case 後面跟一個語句（通常是 Block）
            // 或者我們這裏做一個特殊的邏輯：收集語句直到遇到 case/default/rbrace
            
            // 簡單實現：使用 parse_statement。如果是 Block 則包含多條語句。
            // 如果用戶寫 case 1: print(x); break; 
            // 這裏的 print(x) 是一個語句。break 也是一個語句（暫未實現 break AST，當作 ExpressionStmt?）
            // 這裏的 parse_statement 只會解析 print(x)。break 會被遺漏或者作爲下一個語句解析。
            // 爲了支持多語句 case，我們應該收集語句列表。
            
            KastBlock* block = (KastBlock*)malloc(sizeof(KastBlock));
            block->base.type = KAST_NODE_BLOCK;
            block->statements = NULL;
            block->statement_count = 0;
            size_t block_cap = 0;
            
            while (!check_token(parser, KORELIN_TOKEN_CASE) && 
                   !check_token(parser, KORELIN_TOKEN_DEFAULT) && 
                   !check_token(parser, KORELIN_TOKEN_RBRACE) && 
                   !check_token(parser, KORELIN_TOKEN_EOF)) {
                
                // 处理 break (目前没有 break 节点，暂时跳过或者作为标识符解析?)
                // 如果 break 是关键字，parse_statement 应该能处理或者我们需要特殊处理
                // if (check_token(parser, KORELIN_TOKEN_BREAK)) { ... } 
                // 现在 parse_statement 可以处理 break

                KastStatement* stmt = parse_statement(parser);
                if (stmt) {
                    if (block->statement_count >= block_cap) {
                        block_cap = block_cap == 0 ? 2 : block_cap * 2;
                        block->statements = (KastStatement**)realloc(block->statements, block_cap * sizeof(KastStatement*));
                    }
                    block->statements[block->statement_count++] = stmt;
                }
            }
            
            if (node->case_count >= capacity) {
                capacity = capacity == 0 ? 2 : capacity * 2;
                node->cases = (KastCase*)realloc(node->cases, capacity * sizeof(KastCase));
            }
            node->cases[node->case_count].value = val;
            node->cases[node->case_count].body = (KastStatement*)block;
            node->case_count++;
            
        } else if (check_token(parser, KORELIN_TOKEN_DEFAULT)) {
            advance_token(parser);
            consume(parser, KORELIN_TOKEN_COLON, "Expected ':'");
            
            // 解析 default 体
             KastBlock* block = (KastBlock*)malloc(sizeof(KastBlock));
            block->base.type = KAST_NODE_BLOCK;
            block->statements = NULL;
            block->statement_count = 0;
            size_t block_cap = 0;
            
            while (!check_token(parser, KORELIN_TOKEN_CASE) && 
                   !check_token(parser, KORELIN_TOKEN_DEFAULT) && 
                   !check_token(parser, KORELIN_TOKEN_RBRACE) && 
                   !check_token(parser, KORELIN_TOKEN_EOF)) {
                 
                KastStatement* stmt = parse_statement(parser);
                if (stmt) {
                    if (block->statement_count >= block_cap) {
                        block_cap = block_cap == 0 ? 2 : block_cap * 2;
                        block->statements = (KastStatement**)realloc(block->statements, block_cap * sizeof(KastStatement*));
                    }
                    block->statements[block->statement_count++] = stmt;
                }
            }
            node->default_branch = (KastStatement*)block;
        } else {
             // 错误或空
             advance_token(parser);
        }
    }
    
    consume(parser, KORELIN_TOKEN_RBRACE, "Expected '}'");
    return (KastStatement*)node;
}

// --- For 循环解析 ---
// 语法: for (init; condition; increment) { ... }
static KastStatement* parse_for_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_FOR, "Expected 'for'");
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");

    // 1. Init (Statement)
    KastStatement* init = NULL;
    if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    } else if (check_token(parser, KORELIN_TOKEN_VAR) || check_token(parser, KORELIN_TOKEN_LET)) {
        init = parse_var_declaration(parser);
    } else if (check_token(parser, KORELIN_TOKEN_INT) || 
               check_token(parser, KORELIN_TOKEN_FLOAT) || 
               check_token(parser, KORELIN_TOKEN_KEYWORD_STRING) || 
               check_token(parser, KORELIN_TOKEN_BOOL)) {
        // C-style typed declaration: int i = 0;
        init = parse_typed_declaration(parser);
    } else if (check_token(parser, KORELIN_TOKEN_IDENT)) {
        // init expression or assignment
        KastNode* expr = parse_expression(parser);
        if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
            advance_token(parser);
            KastNode* val = parse_expression(parser);
            KastAssignment* assign = (KastAssignment*)malloc(sizeof(KastAssignment));
            assign->base.type = KAST_NODE_ASSIGNMENT;
            assign->lvalue = expr;
            assign->value = val;
            init = (KastStatement*)assign;
        } else {
            init = (KastStatement*)expr;
        }
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    } else {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "For 循环初始化只能是变量声明或赋值");
        // 尝试恢复
        while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_EOF)) {
            advance_token(parser);
        }
        if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) advance_token(parser);
    }

    // 2. Condition (Expression)
    KastNode* condition = NULL;
    if (!check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
        condition = parse_expression(parser);
    }
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");

    // 3. Increment (Expression or Assignment without semicolon)
    KastNode* increment = NULL;
    if (!check_token(parser, KORELIN_TOKEN_RPAREN)) {
         KastNode* expr = parse_expression(parser);
         if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
             advance_token(parser); // eat =
             
             KastNode* val = parse_expression(parser);
             
             KastAssignment* assign = (KastAssignment*)malloc(sizeof(KastAssignment));
             assign->base.type = KAST_NODE_ASSIGNMENT;
             assign->lvalue = expr;
             assign->value = val;
             increment = (KastNode*)assign;
         } else {
             increment = expr;
         }
    }
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");

    // 4. Body
    KastStatement* body = parse_statement(parser);

    KastFor* node = (KastFor*)malloc(sizeof(KastFor));
    node->base.type = KAST_NODE_FOR;
    node->init = init;
    node->condition = condition;
    node->increment = increment;
    node->body = body;
    return (KastStatement*)node;
}

// --- While 循环解析 ---
static KastStatement* parse_while_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_WHILE, "Expected 'while'");
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");
    KastNode* condition = parse_expression(parser);
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
    
    KastStatement* body = parse_statement(parser);
    
    KastWhile* node = (KastWhile*)malloc(sizeof(KastWhile));
    node->base.type = KAST_NODE_WHILE;
    node->condition = condition;
    node->body = body;
    return (KastStatement*)node;
}

// --- Do-While 循环解析 ---
static KastStatement* parse_do_while_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_DO, "Expected 'do'");
    KastStatement* body = parse_statement(parser);
    
    consume(parser, KORELIN_TOKEN_WHILE, "Expected 'while'");
    consume(parser, KORELIN_TOKEN_LPAREN, "Expected '('");
    KastNode* condition = parse_expression(parser);
    consume(parser, KORELIN_TOKEN_RPAREN, "Expected ')'");
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    
    KastDoWhile* node = (KastDoWhile*)malloc(sizeof(KastDoWhile));
    node->base.type = KAST_NODE_DO_WHILE;
    node->body = body;
    node->condition = condition;
    return (KastStatement*)node;
}

// --- Return 语句解析 ---
static KastStatement* parse_return_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_RETURN, "Expected 'return'");
    
    KastReturn* stmt = (KastReturn*)malloc(sizeof(KastReturn));
    stmt->base.type = KAST_NODE_RETURN;
    stmt->value = NULL;
    
    if (!check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
        stmt->value = parse_expression(parser);
    }
    
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    return (KastStatement*)stmt;
}

static KastStatement* parse_break_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_BREAK, "Expected 'break'");
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    KastBreak* stmt = (KastBreak*)malloc(sizeof(KastBreak));
    stmt->base.type = KAST_NODE_BREAK;
    return (KastStatement*)stmt;
}

static KastStatement* parse_continue_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_CONTINUE, "Expected 'continue'");
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    KastContinue* stmt = (KastContinue*)malloc(sizeof(KastContinue));
    stmt->base.type = KAST_NODE_CONTINUE;
    return (KastStatement*)stmt;
}

static KastStatement* parse_throw_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_THROW, "Expected 'throw'");
    KastNode* value = parse_expression(parser);
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    
    KastThrow* stmt = (KastThrow*)malloc(sizeof(KastThrow));
    stmt->base.type = KAST_NODE_THROW;
    stmt->value = value;
    return (KastStatement*)stmt;
}

// --- Struct 解析 ---
static KastStatement* parse_struct_declaration(Parser* parser) {
    consume(parser, KORELIN_TOKEN_STRUCT, "Expected 'struct'");
    if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected struct name");
        return NULL;
    }
    char* struct_name = strdup(parser->current_token.value);
    advance_token(parser);
    
    // Check if it is a definition '{' or variable declaration 'varName'
    if (check_token(parser, KORELIN_TOKEN_LBRACE)) {
        // --- Struct Definition ---
        advance_token(parser); // Consume '{'
        
        KastStructDecl* struct_node = (KastStructDecl*)malloc(sizeof(KastStructDecl));
        struct_node->base.type = KAST_NODE_STRUCT_DECL;
        struct_node->name = struct_name;
        struct_node->members = NULL;
        struct_node->member_count = 0;
        struct_node->init_var = NULL;
        
        size_t capacity = 0;
        
        while (!check_token(parser, KORELIN_TOKEN_RBRACE) && !check_token(parser, KORELIN_TOKEN_EOF)) {
        // Struct members are implicitly public (usually) and just fields
        // For simplicity, we reuse KastClassMember but ensure it's a property
        
        char* type_name = NULL;
        char* member_name = NULL;
        
        // Type
        type_name = parse_type_definition(parser);
        if (!type_name) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member type");
             // recover
             while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_RBRACE)) advance_token(parser);
             if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) advance_token(parser);
             continue;
        }
        
        // Name
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member name");
             if (type_name) free(type_name);
             while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_RBRACE)) advance_token(parser);
             if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) advance_token(parser);
             continue;
        }
        member_name = strdup(parser->current_token.value);
        advance_token(parser);
        
        KastClassMember* member = (KastClassMember*)malloc(sizeof(KastClassMember));
        member->base.type = KAST_NODE_MEMBER_DECL;
        member->access = KAST_ACCESS_PUBLIC; // Default public
        member->is_static = false;
        member->name = member_name;
        member->member_type = KAST_MEMBER_PROPERTY;
        member->type_name = type_name;
        member->return_type = NULL;
        member->body = NULL;
        member->init_value = NULL;
        
        // Optional Init value? Structs usually just declarations, but maybe allow default?
        // User example: int is_array; // no init
        // Let's support simple declaration: type name;
        
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
        
        if (struct_node->member_count >= capacity) {
            capacity = capacity == 0 ? 2 : capacity * 2;
            struct_node->members = (KastClassMember**)realloc(struct_node->members, capacity * sizeof(KastClassMember*));
        }
        struct_node->members[struct_node->member_count++] = member;
    }
    
    consume(parser, KORELIN_TOKEN_RBRACE, "Expected '}'");
    
    // Check for inline variable declaration: struct S { ... } s;
    if (check_token(parser, KORELIN_TOKEN_IDENT)) {
        char* var_name = strdup(parser->current_token.value);
        advance_token(parser);
        
        // Create a VarDecl for this instance
        // var s : S = (implicitly new S or uninit?)
        // The syntax struct S { ... } s; in C implies s is an instance.
        // In Korelin (reference types?), it might be reference.
        // Let's treat it as: var s : S;
        
        KastVarDecl* var_decl = (KastVarDecl*)malloc(sizeof(KastVarDecl));
        var_decl->base.type = KAST_NODE_VAR_DECL;
        var_decl->is_global = false; // or true if at top level? assume var semantics
        var_decl->is_constant = false;
        var_decl->type_name = strdup(struct_name);
        
        // Check for array declaration: struct S { ... } s[];
        if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
            advance_token(parser);
            
            KastNode* size_expr = NULL;
            if (!check_token(parser, KORELIN_TOKEN_RBRACKET)) {
                size_expr = parse_expression(parser);
            }
            
            consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
            var_decl->is_array = true;
            
            // Create init value: new StructName[size]
            KastNew* init_new = (KastNew*)malloc(sizeof(KastNew));
            init_new->base.type = KAST_NODE_NEW;
            init_new->class_name = strdup(struct_name);
            init_new->is_array = true;
            if (size_expr) {
                init_new->arg_count = 1;
                init_new->args = (KastNode**)malloc(sizeof(KastNode*));
                init_new->args[0] = size_expr;
            } else {
                 // Error: Array declaration requires size in this context or handle dynamic?
                 // C requires size for stack array. Let's assume size required.
                 parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Array size required for struct array declaration");
                 init_new->arg_count = 0;
                 init_new->args = NULL;
            }
            var_decl->init_value = (KastNode*)init_new;
            
        } else {
            var_decl->is_array = false;
            // Create init value: new StructName()
            KastNew* init_new = (KastNew*)malloc(sizeof(KastNew));
            init_new->base.type = KAST_NODE_NEW;
            init_new->class_name = strdup(struct_name);
            init_new->is_array = false;
            init_new->arg_count = 0;
            init_new->args = NULL;
            var_decl->init_value = (KastNode*)init_new;
        }

        var_decl->name = var_name;
        // var_decl->init_value was NULL before
        
        struct_node->init_var = (KastStatement*)var_decl;
        
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    } else {
        // Optional semicolon after struct decl? struct S { ... };
        // If not present, that's fine too if next token is valid start of stmt?
        // C requires semicolon after struct decl.
        // Let's require it if not inline var.
        if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
             advance_token(parser);
        }
    }
    
    return (KastStatement*)struct_node;
    
    } else if (check_token(parser, KORELIN_TOKEN_IDENT)) {
        // --- Struct Variable Declaration: struct Point p; ---
        char* var_name = strdup(parser->current_token.value);
        advance_token(parser);
        
        bool is_array = false;
        KastNode* size_expr = NULL;
        
        if (check_token(parser, KORELIN_TOKEN_LBRACKET)) {
            advance_token(parser);
            if (!check_token(parser, KORELIN_TOKEN_RBRACKET)) {
                size_expr = parse_expression(parser);
            }
            consume(parser, KORELIN_TOKEN_RBRACKET, "Expected ']'");
            is_array = true;
        }
        
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
        
        KastVarDecl* var_decl = (KastVarDecl*)malloc(sizeof(KastVarDecl));
        var_decl->base.type = KAST_NODE_VAR_DECL;
        var_decl->is_global = false; // Will be determined by compiler scope
        var_decl->is_constant = false;
        var_decl->type_name = struct_name; // reused
        var_decl->name = var_name;
        var_decl->is_array = is_array;
        
        // Create init value: new StructName() or new StructName[size]
        KastNew* init_new = (KastNew*)malloc(sizeof(KastNew));
        init_new->base.type = KAST_NODE_NEW;
        init_new->class_name = strdup(struct_name);
        init_new->is_array = is_array;
        
        if (is_array && size_expr) {
            init_new->arg_count = 1;
            init_new->args = (KastNode**)malloc(sizeof(KastNode*));
            init_new->args[0] = size_expr;
        } else {
            init_new->arg_count = 0;
            init_new->args = NULL;
        }
        var_decl->init_value = (KastNode*)init_new;
        
        return (KastStatement*)var_decl;
        
    } else {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected '{' for struct definition or identifier for variable declaration");
        free(struct_name);
        return NULL;
    }
}

static KastStatement* parse_import_statement(Parser* parser) {
    consume(parser, KORELIN_TOKEN_IMPORT, "Expected 'import'");
    
    char** path_parts = NULL;
    size_t part_count = 0;
    
    // Parse first part
    if (!check_token(parser, KORELIN_TOKEN_IDENT) && !check_token(parser, KORELIN_TOKEN_KEYWORD_STRING)) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected package path after import");
        return NULL;
    }
    
    char* part = strdup(parser->current_token.value);
    advance_token(parser);
    
    path_parts = (char**)realloc(path_parts, (part_count + 1) * sizeof(char*));
    path_parts[part_count++] = part;
    
    // Parse subsequent parts (.ident)
    while (check_token(parser, KORELIN_TOKEN_DOT)) {
        advance_token(parser); // .
        if (!check_token(parser, KORELIN_TOKEN_IDENT) && !check_token(parser, KORELIN_TOKEN_KEYWORD_STRING)) {
            // TODO: Support wildcard .*
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected package path part");
            break;
        }
        part = strdup(parser->current_token.value);
        advance_token(parser);
        
        path_parts = (char**)realloc(path_parts, (part_count + 1) * sizeof(char*));
        path_parts[part_count++] = part;
    }
    
    consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
    
    KastImport* import_node = (KastImport*)malloc(sizeof(KastImport));
    import_node->base.type = KAST_NODE_IMPORT;
    import_node->path_parts = path_parts;
    import_node->part_count = part_count;
    // Default alias is last part
    import_node->alias = strdup(path_parts[part_count - 1]);
    import_node->is_wildcard = false;
    
    return (KastStatement*)import_node;
}

static void synchronize(Parser* parser) {
    parser->panic_mode = false;

    while (parser->current_token.type != KORELIN_TOKEN_EOF) {
        if (parser->previous_token.type == KORELIN_TOKEN_SEMICOLON) return;

        switch (parser->current_token.type) {
            case KORELIN_TOKEN_CLASS:
            case KORELIN_TOKEN_FUNCTION:
            case KORELIN_TOKEN_VAR:
            case KORELIN_TOKEN_LET:
            case KORELIN_TOKEN_FOR:
            case KORELIN_TOKEN_IF:
            case KORELIN_TOKEN_WHILE:
            case KORELIN_TOKEN_RETURN:
            case KORELIN_TOKEN_IMPORT:
                return;
            default:
                ;
        }

        advance_token(parser);
    }
}

static KastStatement* parse_statement(Parser* parser) {
    if (check_token(parser, KORELIN_TOKEN_IMPORT)) {
        return parse_import_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_STRUCT)) {
        return parse_struct_declaration(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_FUNCTION)) {
        advance_token(parser);
        char* type_name = parse_type_definition(parser);
        if (!type_name) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected return type after 'function'");
             return NULL;
        }
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected function name");
             free(type_name);
             return NULL;
        }
        char* name = strdup(parser->current_token.value);
        advance_token(parser);
        return parse_function_declaration(parser, type_name, name);
    }

    if (check_token(parser, KORELIN_TOKEN_VAR) || check_token(parser, KORELIN_TOKEN_LET)) {
        return parse_var_declaration(parser);
    }
    
    if (check_token(parser, KORELIN_TOKEN_TRY)) {
        return parse_try_catch(parser);
    }
    
    if (check_token(parser, KORELIN_TOKEN_IF)) {
        return parse_if_statement(parser);
    }
    
    if (check_token(parser, KORELIN_TOKEN_SWITCH)) {
        return parse_switch_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_FOR)) {
        return parse_for_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_WHILE)) {
        return parse_while_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_DO)) {
        return parse_do_while_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_RETURN)) {
        return parse_return_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_BREAK)) {
        return parse_break_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_CONTINUE)) {
        return parse_continue_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_THROW)) {
        return parse_throw_statement(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_LBRACE)) {
        return (KastStatement*)parse_block(parser);
    }
    
    if (check_token(parser, KORELIN_TOKEN_CLASS)) {
        return parse_class_declaration(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_STRUCT)) {
        return parse_struct_declaration(parser);
    }

    // 尝试解析类型化的声明 (函数或变量)
    // 启发式判断：以类型开头 (基础类型, Map, 或 标识符+标识符/泛型/数组, 或 struct)
    bool is_decl = false;
    KorelinToken t = parser->current_token.type;
    
    if (t == KORELIN_TOKEN_MAP || t == KORELIN_TOKEN_STRUCT) {
        is_decl = true;
    } else if ((t >= KORELIN_TOKEN_INT && t <= KORELIN_TOKEN_BOOL) || t == KORELIN_TOKEN_VOID || t == KORELIN_TOKEN_KEYWORD_STRING) {
        // 基础类型后跟标识符或数组
        if (parser->peek_token.type == KORELIN_TOKEN_IDENT) is_decl = true;
        else if (parser->peek_token.type == KORELIN_TOKEN_LBRACKET && parser->peek_token_2.type == KORELIN_TOKEN_RBRACKET) is_decl = true;
    } else if (t == KORELIN_TOKEN_IDENT) {
        // 标识符开头
        if (parser->peek_token.type == KORELIN_TOKEN_IDENT) is_decl = true; // Type Name
        else if (parser->peek_token.type == KORELIN_TOKEN_LT) is_decl = true; // Type<T> ... (假设是声明)
        else if (parser->peek_token.type == KORELIN_TOKEN_LBRACKET && parser->peek_token_2.type == KORELIN_TOKEN_RBRACKET) is_decl = true; // Type[] Name
    }
    
    if (is_decl) {
        return parse_typed_declaration(parser);
    }

    if (check_token(parser, KORELIN_TOKEN_IDENT)) {
          // 检查是否是静态方法调用 Class::Method()
          if (parser->peek_token.type == KORELIN_TOKEN_SCOPE) {
               // Check if it is a definition: Class::Method(...) {
               bool is_definition = false;
               if (parser->peek_token_2.type == KORELIN_TOKEN_IDENT && 
                   parser->peek_token_3.type == KORELIN_TOKEN_LPAREN) {
                   
                   // Case 1: Empty params () {
                   if (parser->peek_token_4.type == KORELIN_TOKEN_RPAREN && 
                       parser->peek_token_5.type == KORELIN_TOKEN_LBRACE) {
                       is_definition = true;
                   } 
                   // Case 2: Typed params (Type Name)
                   else {
                       KorelinToken t = parser->peek_token_4.type;
                       // Primitive types or void
                       if ((t >= KORELIN_TOKEN_INT && t <= KORELIN_TOKEN_BOOL) || t == KORELIN_TOKEN_VOID || t == KORELIN_TOKEN_KEYWORD_STRING || t == KORELIN_TOKEN_MAP) {
                           is_definition = true;
                       }
                       // Identifier types
                       else if (t == KORELIN_TOKEN_IDENT) {
                           KorelinToken next = parser->peek_token_5.type;
                           if (next == KORELIN_TOKEN_IDENT) is_definition = true; // Type Name
                           else if (next == KORELIN_TOKEN_LBRACKET) is_definition = true; // Type[]
                           else if (next == KORELIN_TOKEN_LT) is_definition = true; // Type<T>
                       }
                   }
               }

               if (is_definition) {
                   char* class_name = strdup(parser->current_token.value);
                   advance_token(parser); // Class
                   advance_token(parser); // ::
                   char* method_name = strdup(parser->current_token.value);
                   advance_token(parser); // Method
                   
                   // Default return type to "void" if not specified
                   KastStatement* decl = parse_function_declaration(parser, strdup("void"), method_name);
                   if (decl && decl->base.type == KAST_NODE_FUNCTION_DECL) {
                       ((KastFunctionDecl*)decl)->parent_class_name = class_name;
                   } else {
                       free(class_name);
                   }
                   return decl;
               }

               KastNode* expr = parse_expression(parser);
               if (expr) {
                  consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
                  return (KastStatement*)expr;
               }
               return NULL;
          }
 
          // 解析表达式 (可能是左值，也可能是函数调用)
          KastNode* expr = parse_expression(parser);
          
          // 检查是否是赋值
          if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
              advance_token(parser);
              KastNode* value = parse_expression(parser);
              consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
              
              KastAssignment* assign = (KastAssignment*)malloc(sizeof(KastAssignment));
              assign->base.type = KAST_NODE_ASSIGNMENT;
              assign->lvalue = expr;
              assign->value = value;
              return (KastStatement*)assign;
          }
          
          // 否则是表达式语句 (如调用)
          if (expr) {
              consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
              return (KastStatement*)expr;
          }
          return NULL;
      }
    
    // 默认作为表达式语句
    KastNode* expr = parse_expression(parser);
    if (expr) {
        consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
        return (KastStatement*)expr;
    }
    
    // 如果不是以上情况，可能是空语句或错误
    if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
        advance_token(parser);
        return NULL; // 空语句
    }
    
    parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Invalid statement");
    advance_token(parser);
    return NULL;
}

// --- Class Parsing ---
static KastStatement* parse_class_declaration(Parser* parser) {
    consume(parser, KORELIN_TOKEN_CLASS, "Expected 'class'");
    if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
        parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected class name");
        return NULL;
    }
    char* class_name = strdup(parser->current_token.value);
    advance_token(parser);
    
    char* parent_name = NULL;
    if (check_token(parser, KORELIN_TOKEN_EXTENDS)) {
        advance_token(parser);
        if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
            parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected parent class name after extends");
            free(class_name);
            return NULL;
        }
        parent_name = strdup(parser->current_token.value);
        advance_token(parser);
    }
    
    consume(parser, KORELIN_TOKEN_LBRACE, "Expected '{'");
    
    KastClassDecl* class_node = (KastClassDecl*)malloc(sizeof(KastClassDecl));
    class_node->base.type = KAST_NODE_CLASS_DECL;
    class_node->name = class_name;
    class_node->parent_name = parent_name;
    class_node->members = NULL;
    class_node->member_count = 0;
    
    size_t capacity = 0;
    
    while (!check_token(parser, KORELIN_TOKEN_RBRACE) && !check_token(parser, KORELIN_TOKEN_EOF)) {
        KastAccessModifier access = KAST_ACCESS_DEFAULT;
        if (check_token(parser, KORELIN_TOKEN_PUBLIC)) {
            access = KAST_ACCESS_PUBLIC;
            advance_token(parser);
        } else if (check_token(parser, KORELIN_TOKEN_PRIVATE)) {
            access = KAST_ACCESS_PRIVATE;
            advance_token(parser);
        } else if (check_token(parser, KORELIN_TOKEN_PROTECTED)) {
            access = KAST_ACCESS_PROTECTED;
            advance_token(parser);
        }
        
        bool is_static = false;
        if (check_token(parser, KORELIN_TOKEN_IDENT) && strcmp(parser->current_token.value, "static") == 0) {
            is_static = true;
            advance_token(parser);
        }
        
        if (check_token(parser, KORELIN_TOKEN_FUNCTION)) {
            advance_token(parser);
        }

        char* member_type_str = NULL;
        bool has_var = false;
        if (check_token(parser, KORELIN_TOKEN_VAR)) {
            advance_token(parser);
            has_var = true;
        }
        
        // Check for const
        bool is_constant = false;
        if (check_token(parser, KORELIN_TOKEN_CONST)) {
            is_constant = true;
            advance_token(parser);
        }

        // Try to parse type if present
        // If has_var is true, type is optional.
        // We peek to see if next token looks like a type.
        // Heuristic: if next is ident and after that is another ident (name), then first is type.
        // Or if next is primitive type keyword (int, string, etc).
        
        char* explicit_type = NULL;
        
        // Check if current token is a type start
        bool looks_like_type = false;
        KorelinToken t = parser->current_token.type;
        if ((t >= KORELIN_TOKEN_INT && t <= KORELIN_TOKEN_BOOL) || t == KORELIN_TOKEN_VOID || t == KORELIN_TOKEN_KEYWORD_STRING || t == KORELIN_TOKEN_MAP) {
            looks_like_type = true;
        } else if (t == KORELIN_TOKEN_IDENT) {
            // Identifier could be type or name.
            // If has_var, and we have "IDENT IDENT", first is type.
            // If "IDENT ;" or "IDENT =", first is name.
            if (parser->peek_token.type == KORELIN_TOKEN_IDENT || parser->peek_token.type == KORELIN_TOKEN_LT || parser->peek_token.type == KORELIN_TOKEN_LBRACKET) {
                 looks_like_type = true;
            }
        }
        
        if (looks_like_type) {
             explicit_type = parse_type_definition(parser);
        }
        
        if (has_var) {
             if (explicit_type) {
                 member_type_str = explicit_type;
             } else {
                 member_type_str = strdup("var");
             }
        } else {
             // No var, must have type (unless constructor/method?)
             // Actually methods start with type (or void).
             // If explicit_type is null, and we didn't have var, maybe it's invalid or just "IDENT" (constructor name without return type?)
             // But my parser expects type for methods too.
             member_type_str = explicit_type;
        }

        char* member_name = NULL;
        bool is_constructor = false;
        
        if (!member_type_str) {
             parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member type (constructor _init also needs return type, e.g. void)");
             // simple recovery
             while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_RBRACE)) advance_token(parser);
             if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) advance_token(parser);
             continue;
        } else {
             if (!check_token(parser, KORELIN_TOKEN_IDENT)) {
                 if (member_type_str) {
                     if (strcmp(member_type_str, "_init") == 0) {
                         parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Constructor '_init' must declare return type (e.g. 'void _init')");
                     } else {
                         parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Expected member name");
                     }
                     free(member_type_str);
                 }
                 while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_RBRACE)) advance_token(parser);
                 if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) advance_token(parser);
                 continue;
             }
             
             if (strcmp(parser->current_token.value, "_init") == 0 || strcmp(parser->current_token.value, "_init_") == 0) {
                 is_constructor = true;
                 member_name = strdup("_init");
             } else {
                 member_name = strdup(parser->current_token.value);
             }
             
             advance_token(parser);
        }
        
        KastClassMember* member = (KastClassMember*)malloc(sizeof(KastClassMember));
        member->base.type = KAST_NODE_MEMBER_DECL;
        member->access = access;
        member->is_static = is_static;
        member->is_constant = is_constant;
        member->name = member_name;
        
        if (check_token(parser, KORELIN_TOKEN_LPAREN)) {
            member->member_type = KAST_MEMBER_METHOD;
            member->return_type = member_type_str;
            member->type_name = NULL; // It's return type now
            member->init_value = NULL;
            
            // Constructor and non-static methods must include self
            bool enforce_self = !is_static;
            
            member->args = parse_parameter_list(parser, &member->arg_count, enforce_self);
            
            member->body = parse_block(parser);
        } else {
            member->member_type = KAST_MEMBER_PROPERTY;
            member->type_name = member_type_str;
            member->return_type = NULL;
            member->body = NULL;
            member->args = NULL; // Init args to NULL for property
            member->arg_count = 0;
            
            if (check_token(parser, KORELIN_TOKEN_ASSIGN)) {
                advance_token(parser);
                member->init_value = parse_expression(parser);
            } else {
                member->init_value = NULL;
            }
            consume(parser, KORELIN_TOKEN_SEMICOLON, "Expected ';'");
        }
        
        if (class_node->member_count >= capacity) {
            capacity = capacity == 0 ? 2 : capacity * 2;
            class_node->members = (KastClassMember**)realloc(class_node->members, capacity * sizeof(KastClassMember*));
        }
        class_node->members[class_node->member_count++] = member;
    }
    
    consume(parser, KORELIN_TOKEN_RBRACE, "Expected '}'");
    return (KastStatement*)class_node;
}

// --- 程序解析 ---

KastProgram* parse_program(Parser* parser) {
    KastProgram* program = (KastProgram*)malloc(sizeof(KastProgram));
    program->base.type = KAST_NODE_PROGRAM;
    program->statements = NULL;
    program->statement_count = 0;

    // 动态数组的简单实现
    size_t capacity = 0;

    while (!check_token(parser, KORELIN_TOKEN_EOF)) {
        KastStatement* stmt = parse_statement(parser);
        if (stmt) {
            if (program->statement_count >= capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                program->statements = (KastStatement**)realloc(program->statements, capacity * sizeof(KastStatement*));
            }
            program->statements[program->statement_count++] = stmt;
        } else {
            // 如果解析失败且处于 panic 模式，尝试同步到下一个语句边界 (例如分号)
            if (parser->panic_mode) {
                // 简单的错误恢复：跳过 token 直到分号或 EOF
                while (!check_token(parser, KORELIN_TOKEN_SEMICOLON) && !check_token(parser, KORELIN_TOKEN_EOF)) {
                    advance_token(parser);
                }
                if (check_token(parser, KORELIN_TOKEN_SEMICOLON)) {
                    advance_token(parser);
                }
                // 恢复 panic 模式，继续解析
                parser->panic_mode = false;
            }
        }
    } // Close while loop

    // if (!parser->has_main_function) {
    //     parser_error(parser, KORELIN_ERROR_ILLEGAL_SYNTAX, "Missing 'main' function. A program must have exactly one main function.");
    //     // We continue to return the program so memory can be freed, but the error flag is set.
    // }

    return program;
}

// --- 内存释放 ---

void free_ast_node(KastNode* node) {
    if (!node) return;

    switch (node->type) {
        case KAST_NODE_IMPORT: {
            KastImport* import_node = (KastImport*)node;
            for (size_t i = 0; i < import_node->part_count; i++) {
                free(import_node->path_parts[i]);
            }
            if (import_node->path_parts) free(import_node->path_parts);
            if (import_node->alias) free(import_node->alias);
            break;
        }
        case KAST_NODE_PROGRAM: {
            KastProgram* prog = (KastProgram*)node;
            for (size_t i = 0; i < prog->statement_count; i++) {
                free_ast_node((KastNode*)prog->statements[i]);
            }
            if (prog->statements) free(prog->statements);
            break;
        }
        case KAST_NODE_RETURN: {
            KastReturn* ret = (KastReturn*)node;
            if (ret->value) free_ast_node(ret->value);
            break;
        }
        case KAST_NODE_BREAK:
        case KAST_NODE_CONTINUE:
            break;
        case KAST_NODE_FUNCTION_DECL: {
            KastFunctionDecl* func = (KastFunctionDecl*)node;
            if (func->name) free(func->name);
            if (func->return_type) free(func->return_type);
            for (size_t i = 0; i < func->arg_count; i++) {
                free_ast_node((KastNode*)func->args[i]);
            }
            if (func->args) free(func->args);
            if (func->body) free_ast_node((KastNode*)func->body);
            for (size_t i = 0; i < func->generic_count; i++) {
                free(func->generic_params[i]);
            }
            if (func->generic_params) free(func->generic_params);
            break;
        }
        case KAST_NODE_VAR_DECL: {
            KastVarDecl* decl = (KastVarDecl*)node;
            if (decl->name) free(decl->name);
            if (decl->type_name) free(decl->type_name);
            if (decl->init_value) free_ast_node(decl->init_value);
            break;
        }
        case KAST_NODE_ASSIGNMENT: {
            KastAssignment* assign = (KastAssignment*)node;
            if (assign->lvalue) free_ast_node(assign->lvalue);
            if (assign->value) free_ast_node(assign->value);
            break;
        }
        case KAST_NODE_BLOCK: {
            KastBlock* block = (KastBlock*)node;
            for (size_t i = 0; i < block->statement_count; i++) {
                free_ast_node((KastNode*)block->statements[i]);
            }
            if (block->statements) free(block->statements);
            break;
        }
        case KAST_NODE_TRY_CATCH: {
            KastTryCatch* stmt = (KastTryCatch*)node;
            if (stmt->try_block) free_ast_node((KastNode*)stmt->try_block);
            for (size_t i = 0; i < stmt->catch_count; i++) {
                if (stmt->catch_blocks[i].error_type) free(stmt->catch_blocks[i].error_type);
                if (stmt->catch_blocks[i].body) free_ast_node((KastNode*)stmt->catch_blocks[i].body);
            }
            if (stmt->catch_blocks) free(stmt->catch_blocks);
            break;
        }
        case KAST_NODE_IF: {
            KastIf* stmt = (KastIf*)node;
            if (stmt->condition) free_ast_node(stmt->condition);
            if (stmt->then_branch) free_ast_node((KastNode*)stmt->then_branch);
            if (stmt->else_branch) free_ast_node((KastNode*)stmt->else_branch);
            break;
        }
        case KAST_NODE_SWITCH: {
            KastSwitch* stmt = (KastSwitch*)node;
            if (stmt->condition) free_ast_node(stmt->condition);
            for (size_t i = 0; i < stmt->case_count; i++) {
                if (stmt->cases[i].value) free_ast_node(stmt->cases[i].value);
                if (stmt->cases[i].body) free_ast_node((KastNode*)stmt->cases[i].body);
            }
            if (stmt->cases) free(stmt->cases);
            if (stmt->default_branch) free_ast_node((KastNode*)stmt->default_branch);
    break;
}
case KAST_NODE_FOR: {
            KastFor* stmt = (KastFor*)node;
            if (stmt->init) free_ast_node((KastNode*)stmt->init);
            if (stmt->condition) free_ast_node(stmt->condition);
            if (stmt->increment) free_ast_node(stmt->increment);
            if (stmt->body) free_ast_node((KastNode*)stmt->body);
            break;
        }
        case KAST_NODE_WHILE: {
            KastWhile* stmt = (KastWhile*)node;
            if (stmt->condition) free_ast_node(stmt->condition);
            if (stmt->body) free_ast_node((KastNode*)stmt->body);
            break;
        }
        case KAST_NODE_DO_WHILE: {
            KastDoWhile* stmt = (KastDoWhile*)node;
            if (stmt->condition) free_ast_node(stmt->condition);
            if (stmt->body) free_ast_node((KastNode*)stmt->body);
            break;
        }
        case KAST_NODE_CLASS_DECL: {
            KastClassDecl* decl = (KastClassDecl*)node;
            if (decl->name) free(decl->name);
            for (size_t i = 0; i < decl->member_count; i++) {
                free_ast_node((KastNode*)decl->members[i]);
            }
            if (decl->members) free(decl->members);
            break;
        }
        case KAST_NODE_STRUCT_DECL: {
            KastStructDecl* decl = (KastStructDecl*)node;
            if (decl->name) free(decl->name);
            for (size_t i = 0; i < decl->member_count; i++) {
                free_ast_node((KastNode*)decl->members[i]);
            }
            if (decl->members) free(decl->members);
            if (decl->init_var) free_ast_node((KastNode*)decl->init_var);
            break;
        }
        case KAST_NODE_MEMBER_DECL: {
            KastClassMember* member = (KastClassMember*)node;
            if (member->name) free(member->name);
            if (member->member_type == KAST_MEMBER_PROPERTY) {
                if (member->type_name) free(member->type_name);
                if (member->init_value) free_ast_node(member->init_value);
            } else {
                if (member->return_type) free(member->return_type);
                if (member->body) free_ast_node((KastNode*)member->body);
                for (size_t i = 0; i < member->arg_count; i++) {
                    free_ast_node((KastNode*)member->args[i]);
                }
                if (member->args) free(member->args);
            }
            break;
        }
        case KAST_NODE_NEW: {
            KastNew* n = (KastNew*)node;
            if (n->class_name) free(n->class_name);
            for (size_t i = 0; i < n->arg_count; i++) {
                free_ast_node(n->args[i]);
            }
            if (n->args) free(n->args);
            break;
        }
        case KAST_NODE_MEMBER_ACCESS: {
            KastMemberAccess* acc = (KastMemberAccess*)node;
            if (acc->object) free_ast_node(acc->object);
            if (acc->member_name) free(acc->member_name);
            break;
        }
        case KAST_NODE_SCOPE_ACCESS: {
            KastScopeAccess* acc = (KastScopeAccess*)node;
            if (acc->class_name) free(acc->class_name);
            if (acc->member_name) free(acc->member_name);
            break;
        }
        case KAST_NODE_CALL: {
            KastCall* call = (KastCall*)node;
            if (call->callee) free_ast_node(call->callee);
            for (size_t i = 0; i < call->arg_count; i++) {
                free_ast_node(call->args[i]);
            }
            if (call->args) free(call->args);
            break;
        }
        case KAST_NODE_ARRAY_ACCESS: {
            KastArrayAccess* acc = (KastArrayAccess*)node;
            if (acc->array) free_ast_node(acc->array);
            if (acc->index) free_ast_node(acc->index);
            break;
        }
        case KAST_NODE_ARRAY_LITERAL: {
            KastArrayLiteral* lit = (KastArrayLiteral*)node;
            for (size_t i = 0; i < lit->element_count; i++) {
                free_ast_node(lit->elements[i]);
            }
            if (lit->elements) free(lit->elements);
            break;
        }
        case KAST_NODE_BINARY_OP: {
            KastBinaryOp* op = (KastBinaryOp*)node;
            if (op->left) free_ast_node(op->left);
            if (op->right) free_ast_node(op->right);
            break;
        }
        case KAST_NODE_UNARY_OP: {
            KastUnaryOp* op = (KastUnaryOp*)node;
            if (op->operand) free_ast_node(op->operand);
            break;
        }
        case KAST_NODE_LITERAL: {
            KastLiteral* lit = (KastLiteral*)node;
            if (lit->token.value) free((void*)lit->token.value);
            break;
        }
        case KAST_NODE_IDENTIFIER: {
            KastIdentifier* ident = (KastIdentifier*)node;
            if (ident->name) free(ident->name);
            break;
        }
        default:
            break;
    }
    free(node);
}