#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isConst;
  int shadowed;
} Local;

typedef struct {
  Local* locals;
  int localCapacity;
  int localCount;
  int scopeDepth;
  Table names;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

static Chunk* currentChunk(void) {
  return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance(void) {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitReturn(void) {
  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void emitVariable(uint8_t op, int arg) {
  if (arg > UINT8_MAX) {
    emitByte(op);
    emitBytes((uint8_t)((arg >> 8) & 0xff), (uint8_t)(arg & 0xff));
  } else {
    emitBytes(op, (uint8_t)arg);
  }
}

static void initCompiler(Compiler* compiler) {
  compiler->locals = NULL;
  compiler->localCapacity = 0;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  initTable(&compiler->names);
  current = compiler;
}

static void endCompiler(void) {
  emitReturn();
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
#endif
}

static void beginScope(void) {
  current->scopeDepth++;
}

static void endScope(void) {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
            current->scopeDepth) {
    Local* local = &current->locals[current->localCount - 1];
    ObjString* key = copyString(local->name.start, local->name.length);
    if (local->shadowed == -1) {
      tableDelete(&current->names, key);
    } else {
      tableSet(&current->names, key, NUMBER_VAL((double)local->shadowed));
    }

    emitByte(OP_POP);
    current->localCount--;
  }
}

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t globalSlot(Token* name) {
  ObjString* identifier = copyString(name->start, name->length);
  Value slot;
  if (tableGet(&vm.globalSlots, identifier, &slot)) {
    return (uint8_t)AS_NUMBER(slot);
  }

  if (vm.globalValues.count > UINT8_MAX) {
    error("Too many global variables.");
    return 0;
  }

  uint8_t index = (uint8_t)vm.globalValues.count;
  writeValueArray(&vm.globalValues, UNDEFINED_VAL);
  writeValueArray(&vm.globalNames, OBJ_VAL(identifier));
  writeValueArray(&vm.globalConsts, BOOL_VAL(false));
  tableSet(&vm.globalSlots, identifier, NUMBER_VAL((double)index));
  return index;
}

static int findLocal(Compiler* compiler, Token* name) {
  Value index;
  ObjString* key = copyString(name->start, name->length);
  if (!tableGet(&compiler->names, key, &index)) return -1;
  return (int)AS_NUMBER(index);
}

static int resolveLocal(Compiler* compiler, Token* name) {
  int i = findLocal(compiler, name);
  if (i != -1 && compiler->locals[i].depth == -1) {
    error("Can't read local variable in its own initializer.");
  }

  return i;
}

static void addLocal(Token name) {
  if (current->localCount == UINT16_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  if (current->localCapacity < current->localCount + 1) {
    int oldCapacity = current->localCapacity;
    current->localCapacity = GROW_CAPACITY(oldCapacity);
    current->locals = GROW_ARRAY(Local, current->locals,
                                 oldCapacity, current->localCapacity);
  }

  Local* local = &current->locals[current->localCount];
  local->name = name;
  local->depth = -1;
  local->isConst = false;
  local->shadowed = findLocal(current, &name);

  ObjString* key = copyString(name.start, name.length);
  tableSet(&current->names, key, NUMBER_VAL((double)current->localCount));
  current->localCount++;
}

static void declareVariable(void) {
  if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;
  int i = findLocal(current, name);
  if (i != -1) {
    Local* local = &current->locals[i];
    if (local->depth == -1 || local->depth == current->scopeDepth) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default: return;
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    default: return;
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                  parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  bool isConst;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
    if (arg > UINT8_MAX) {
      getOp = OP_GET_LOCAL_LONG;
      setOp = OP_SET_LOCAL_LONG;
    }
    isConst = current->locals[arg].isConst;
  } else {
    arg = globalSlot(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
    isConst = AS_BOOL(vm.globalConsts.values[arg]);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    if (isConst) {
      error("Can't assign to a constant variable.");
    }
    expression();
    emitVariable(setOp, arg);
  } else {
    emitVariable(getOp, arg);
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
    case TOKEN_BANG: emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default: return;
  }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CONST]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) return 0;

  return globalSlot(&parser.previous);
}

static void markInitialized(void) {
  current->locals[current->localCount - 1].depth =
      current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression(void) {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void block(void) {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration(bool isConst) {
  uint8_t global = parseVariable("Expect variable name.");

  if (isConst) {
    consume(TOKEN_EQUAL, "Expect '=' after constant name.");
    expression();
  } else if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON,
          "Expect ';' after variable declaration.");

  if (current->scopeDepth > 0) {
    current->locals[current->localCount - 1].isConst = isConst;
  } else {
    vm.globalConsts.values[global] = BOOL_VAL(isConst);
  }
  defineVariable(global);
}

static void expressionStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static void printStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void synchronize(void) {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_CONST:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:
        ;
    }

    advance();
  }
}

static void declaration(void) {
  if (match(TOKEN_VAR)) {
    varDeclaration(false);
  } else if (match(TOKEN_CONST)) {
    varDeclaration(true);
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void statement(void) {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler);
  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  endCompiler();
  freeTable(&compiler.names);
  FREE_ARRAY(Local, compiler.locals, compiler.localCapacity);
  return !parser.hadError;
}
