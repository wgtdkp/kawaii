#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define caar(list)  (car(car(list)))
#define cadr(list)  (car(cdr(list)))
#define cddr(list)  (cdr(cdr(list)))
#define caddr(list) (car(cdr(cdr(list))))

#define ENV_INIT_SIZE (16)
#define STRING(cstr) (string_t){cstr, sizeof(cstr) - 1}

typedef struct string {
  const char* begin;
  int len;
} string_t;

static inline bool string_eq(string_t lhs, string_t rhs) {
  return lhs.len == rhs.len && strncasecmp(lhs.begin, rhs.begin, lhs.len) == 0;
}

typedef enum type {
  EMPTY, INT, BOOL, /*CELL,*/ LIST, /*QUOTED,*/ SYMB, FUNC,
  PRIM_ADD, // '+'
  PRIM_SUB, // '-'
  PRIM_MUL, // '*'
  PRIM_DIV, // '/'
  PRIM_EQ, // '='
  PRIM_GT, // '>'
  PRIM_LT, // '<'
  PRIM_LE, // '<='
  PRIM_GE, // '>='
  PRIM_NE, // '!=' (extension)
  PRIM_NOT, // 'not'
  PRIM_IF,  // 'if'
  PRIM_DEF, // 'define'
  PRIM_LAMBDA, // 'lambda'
  //PRIM_LIST,
  //PRIM_CONS,
  // Add prim functions below
  NUM_TYPE
} type_t;

typedef struct list list_t;
typedef struct value {
  union {
    // Interger
    int64_t as_int;

    // Bool
    bool as_bool;

    // Function
    struct {
      // Symbol list
      list_t* params;
      list_t* body;
    } as_func;

    // List, represent 'quote
    struct list* as_list;

    // Symbol
    string_t as_symb;
  };
  type_t t;
} value_t;

static inline value_t* alloc_value(type_t t) {
  value_t* ret = calloc(1, sizeof(value_t));
  ret->t = t;
  return ret;
}
/*
static void free_value(value_t* val) { free(val); }
*/

struct list {
  value_t* val;
  struct list* next;
};

static inline value_t* car(list_t* list) { return list->val; }
static inline list_t*  cdr(list_t* list) { return list->next; }
static inline list_t* alloc_list(value_t* val, list_t* next) {
  list_t* ret = malloc(sizeof(list_t));
  ret->val = val;
  ret->next = next;
  return ret;
}

static inline void free_list(list_t* list) {
  free(list);
}

static inline value_t* make_empty() {
  static value_t* ret = NULL;
  return ret ? ret : (ret = alloc_value(EMPTY));
}

static inline value_t* make_int(int64_t v) {
  value_t* ret = alloc_value(INT);
  ret->as_int = v;
  return ret;
}

static inline value_t* make_bool(bool v) {
  value_t* ret = alloc_value(BOOL);
  ret->as_bool = v;
  return ret;
}

static inline value_t* make_func(list_t* params, list_t* body) {
  value_t* ret = alloc_value(FUNC);
  ret->as_func.params = params;
  ret->as_func.body = body;
  return ret;
}

static inline value_t* make_symb(string_t symb) {
  value_t* ret = alloc_value(SYMB);
  ret->as_symb = symb;
  return ret;
}

static inline value_t* make_list(list_t* list) {
  if (!list) return make_empty();
  value_t* ret = alloc_value(LIST);
  ret->as_list = list;
  return ret;
}

static inline value_t* make_atom(type_t prim) {
  static value_t* atoms[NUM_TYPE - PRIM_ADD] = {NULL};
  value_t** ret = &atoms[prim - PRIM_ADD];
  return *ret ? *ret : (*ret = alloc_value(prim));
}

typedef struct env_entry {
  uint64_t key_hash;
  string_t key;
  value_t* val;
  struct env_entry* next;
} env_entry_t;

typedef struct env {
  env_entry_t** slots;
  uint32_t size;
  uint32_t num_entries;
  struct env* parent;
} env_t;

static inline env_entry_t* alloc_env_entry(uint64_t h,
    string_t key, value_t* val) {
  env_entry_t* ret = malloc(sizeof(env_entry_t));
  ret->key_hash = h;
  ret->key = key;
  ret->val = val;
  ret->next = NULL;
  return ret;
}

static inline void free_env_entry(env_entry_t* entry) { free(entry); }

static inline env_t* alloc_env(env_t* papa, uint32_t size) {
  env_t* ret = malloc(sizeof(env_t));
  ret->size = size;
  ret->num_entries = 0;
  ret->slots = calloc(ret->size, sizeof(env_entry_t*));
  ret->parent = papa;
  return ret;
}

static inline void free_env(env_t* env) {
  for (int i = 0; i < env->size; ++i) {
    env_entry_t* entry = env->slots[i];
    while (entry) {
      env_entry_t* next = entry->next;
      free_env_entry(entry);
      entry = next;
    }
  }
  free(env->slots);
  free(env);
}

static inline uint64_t hash(string_t key) {
  uint64_t ret = 0;
  for (int i = 0; i < key.len; ++i) {
    ret = (ret << 1) + key.begin[i] * 131;
  }
  return ret;
}

static inline env_entry_t* __env_lookup(env_t* env, string_t key) {
  uint64_t h = hash(key);
  env_entry_t* entry = env->slots[h % env->size];
  while (entry) {
    if (h == entry->key_hash && string_eq(key, entry->key))
      return entry;
    entry = entry->next;
  }
  return NULL;
}

static inline value_t* env_lookup(env_t* env, string_t key) {
  env_entry_t* entry = __env_lookup(env, key);
  return entry ? entry->val : NULL;
}

static void env_add(env_t* env, string_t key, value_t* val) {
  if (env->num_entries * 2 >= env->size) {
    // Load factor >= 0.5, rehash
    env_t* new_env = alloc_env(env->parent, env->size * 2 + 1);
    for (int i = 0; i < env->size; ++i) {
      env_entry_t* entry = env->slots[i];
      while (entry) {
        env_add(new_env, entry->key, entry->val);
        entry = entry->next;
      }
    }
    free(env->slots);
    *env = *new_env;
  }

  env_entry_t* entry = __env_lookup(env, key);
  if (entry) {
    entry->val = val;
  } else {
    uint64_t h = hash(key);
    env_entry_t* slot = env->slots[h % env->size];
    entry = alloc_env_entry(h, key, val);
    entry->next = slot;
    env->slots[h % env->size] = entry;
    ++env->num_entries;
  }
}

static env_t* g_env;
static env_t* cur_env;

static const char* raw;
static const char* p;

static inline void exit_on(bool cond, const char* fmt, ...) {
  if (cond) {
    fflush(stdout);
    fprintf(stderr, "error: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(-1);
  }
}

static void load(const char* file_name) {
  FILE* f = fopen(file_name, "r");
  fseek(f, 0, SEEK_END);
  uint64_t size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* text = malloc((size + 1) * sizeof(char));
  assert(text);
  fread(text, size, 1, f);
  text[size] = 0;
  p = raw = text;
}

static inline int readc() {
  return *p++;
}

static inline int peekc() {
  return *p;
}

static inline bool tryc(int c) {
  if (peekc() == c) {
    readc();
    return true;
  }
  return false;
}

static inline void putback() {
  assert(p > raw);
  --p;
}

static inline const char* cursor() {
  return p;
}

static inline void read_spaces() {
  while (isspace(readc())) {}
  putback();
}

static inline void read_comment() {
  int c;
  while ((c = readc()) && c != '\n') {}
  if (!c) putback();
}

static inline string_t read_symb() {
  string_t ret = {cursor()};
  int c;
  while ((c = readc()) && !isspace(c) && c != '(' && c != ')') {}
  putback();
  ret.len = cursor() - ret.begin;
  return ret;
}

/* Deciding if a sumbol is a number
 * (\+|\-)[0-9]+
 */
static inline bool is_number(string_t symb) {
  for (int i = 0; i < symb.len; ++i) {
    int c = symb.begin[i];
    if (i == 0 && symb.len > 1 && (c == '+' || c == '-')) {
      continue;
    }
    if (!isdigit(c)) {
      return false;
    }
  }
  return true;
}

static value_t* read();
static list_t* read_list() {
  value_t* val = read();
  if (!val) return NULL;
  return alloc_list(val, read_list());
}

static value_t* read() {
  read_spaces();
  switch (peekc()) {
  case '\0': printf("program done\n"); exit(0); return NULL;
  case ';': read_comment(); return read();
  case ')': readc(); return NULL;
  case '(': {
    readc();
    read_spaces();
    if (tryc(')')) return make_empty();
    return make_list(read_list());
  }
  default: {
    string_t symb = read_symb();
    return is_number(symb) ? make_int(atoll(symb.begin)) : make_symb(symb);
  }
  }
}

static value_t* eval(value_t* val);
static inline void bind(string_t symb, value_t* val) {
  env_add(cur_env, symb, val);
}
static list_t* eval_each(list_t* list) {
  if (!list) return NULL;
  value_t* val = eval(car(list));
  return alloc_list(val, eval_each(cdr(list)));
}

static value_t* apply(value_t* func, list_t* args) {
  assert(func->t == FUNC);
  env_t* backup_env = cur_env;
  cur_env = alloc_env(backup_env, ENV_INIT_SIZE);

  list_t* params = func->as_func.params;
  while (params && args) {
    exit_on(car(params)->t != SYMB, "parameter is not symbol");
    bind(car(params)->as_symb, car(args));
    params = cdr(params);
    args = cdr(args);
  }
  exit_on(params || args, params ? "too less args" : "too many args");
  list_t* exprs = eval_each(func->as_func.body);
  value_t* ans = make_empty();
  while (exprs) {
    ans = car(exprs);
    exprs = cdr(exprs);
  }

  free_env(cur_env);
  cur_env = backup_env;
  return ans;
}

static inline value_t* prim_add(list_t* args) {
  int64_t res = 0;
  while (args) {
    exit_on(car(args)->t != INT, "argument type unmatched");
    res += car(args)->as_int;
    args = cdr(args);
  }
  return make_int(res);
}

static inline value_t* prim_sub(list_t* args) {
  int64_t res = 0;
  if (args && cdr(args)) {
    res = car(args)->as_int;
    args = cdr(args);
  }
  while (args) {
    exit_on(car(args)->t != INT, "argument type unmatched");
    res -= car(args)->as_int;
    args = cdr(args);
  }
  return make_int(res);
}

static inline value_t* prim_mul(list_t* args) {
  int64_t res = 1;
  while (args) {
    exit_on(car(args)->t != INT, "argument type unmatched");
    res *= car(args)->as_int;
    args = cdr(args);
  }
  return make_int(res);
}

static inline value_t* prim_div(list_t* args) {
  exit_on(!args, "div needs at least one arguments");
  exit_on(car(args)->t != INT, "argument type unmatched");
  int64_t res = car(args)->as_int;
  args = cdr(args);
  while (args) {
    exit_on(car(args)->t != INT, "argument type unmatched");
    res /= car(args)->as_int;
    args = cdr(args);
  }
  return make_int(res);
}

static inline bool is_lambda(list_t* list) {
  return car(list)->t == SYMB &&
         string_eq(car(list)->as_symb, STRING("lambda"));
}

// (define <symbol> <expression>)
// (define (<symbol> <paramters>) <expressions>)
static value_t* prim_def(list_t* list) {
  exit_on(!cdr(list), "expect expression(s)");
  if (car(list)->t == SYMB) {
    string_t name = car(list)->as_symb;
    value_t* expr = cadr(list);
    if (expr->t == LIST && is_lambda(expr->as_list)) {
      expr = eval(expr);
    }
    bind(name, expr);
    return car(list);
  } else if (car(list)->t == LIST) {
    list_t* name_and_params = car(list)->as_list;
    value_t* name = car(name_and_params);
    exit_on(name->t != SYMB, "expect symbol");
    value_t* func = make_func(cdr(name_and_params), cdr(list));
    bind(name->as_symb, func);
    return name;
  } else {
    exit_on(true, "define: syntax error, expect symbol or list");
    return NULL; // Make compiler happy
  }
}

// (lambda (<params>) <value-list>)
static inline value_t* prim_lambda(list_t* list) {
  exit_on(car(list)->t != LIST && car(list)->t != EMPTY,
          "expect parameter list");
  exit_on(!cdr(list), "expect expression");
  list_t* params = car(list)->as_list;
  return make_func(params, cdr(list));
}

// =, >, <, >=, <= and extensional '!='
static inline value_t* prim_rel(type_t op, list_t* list) {
  exit_on(!list || !cdr(list), "expect two operands");
  // Support only integer!
  exit_on(car(list)->t != INT || cadr(list)->t != INT, "expect number");
  int64_t lhs = car(list)->as_int, rhs = cadr(list)->as_int;
  switch (op) {
  case PRIM_EQ: return make_bool(lhs == rhs);
  case PRIM_GT: return make_bool(lhs > rhs);
  case PRIM_LT: return make_bool(lhs < rhs);
  case PRIM_LE: return make_bool(lhs <= rhs);
  case PRIM_GE: return make_bool(lhs >= rhs);
  case PRIM_NE: return make_bool(lhs != rhs);
  default: assert(false); return NULL;
  }
}

static inline value_t* prim_not(list_t* list) {
  exit_on(!list || car(list)->t != BOOL, "expect bool expression");
  return make_bool(!car(list)->as_bool);
}

// (if <cond> <consequent> <alternate>)
// (if <cond> <consequent>)
static inline value_t* prim_if(list_t* list) {
  exit_on(!list || !cdr(list), "expect expression");
  value_t* cond = eval(car(list));
  exit_on(cond->t != BOOL, "expect bool expression");
  // TODO(wgtdkp): syntax checking, ensure there is at most too branches
  if (cond->as_bool) {
    return eval(cadr(list));
  } else if (cddr(list)) {
    return eval(caddr(list));
  } else {
    return make_empty();
  }
}

static inline value_t* lookup(string_t symb) {
  env_t* env = cur_env;
  while (env) {
    value_t* val = env_lookup(env, symb);
    if (val) return val;
    env = env->parent;
  }
  return NULL;
}

static value_t* eval(value_t* val) {
  //return val;
  if (val->t == LIST) {
    list_t* list = val->as_list;
    value_t* val = car(list)->t == SYMB ?
        lookup(car(list)->as_symb) : eval(car(list));
    // exit_on(car(list)->t == SYMB, "unbound symbol");
    exit_on(!val, "unbound symbol");
    switch (val->t) {
    case EMPTY: case INT: case LIST: return val;
    case SYMB: return eval(val);
    case FUNC: return apply(val, eval_each(cdr(list)));
    case PRIM_ADD: return prim_add(eval_each(cdr(list)));
    case PRIM_SUB: return prim_sub(eval_each(cdr(list)));
    case PRIM_MUL: return prim_mul(eval_each(cdr(list)));
    case PRIM_DIV: return prim_div(eval_each(cdr(list)));
    case PRIM_EQ: case PRIM_GT: case PRIM_LT:
    case PRIM_LE: case PRIM_GE: case PRIM_NE:
        return prim_rel(val->t, eval_each(cdr(list)));
    case PRIM_DEF: return prim_def(cdr(list));
    case PRIM_LAMBDA: return prim_lambda(cdr(list));
    case PRIM_IF:  return prim_if(cdr(list));
    case PRIM_NOT: return prim_not(eval_each(cdr(list)));
    default: assert(false); return NULL;
    }
  } else if (val->t == SYMB) {
    value_t* ret = lookup(val->as_symb);
    exit_on(!ret, "unbound symbol");
    return eval(ret);
  } else {
    return val;
  }
}

static inline void print_str(string_t str) {
  for (int i = 0; i < str.len; ++i) {
    putchar(str.begin[i]);
  }
}

static void print_func(value_t* func) {
  assert(func->t == FUNC);
  printf("#[function]");
}

static void print(value_t* val);
static void print_list(list_t* list) {
  putchar('(');
  list_t* p = list;
  while (p) {
    print(car(p));
    putchar(' ');
    p = p->next;
  }
  putchar(')');
}

static void print(value_t* val) {
  switch (val->t) {
  case EMPTY: break;
  case INT:   printf("%ld", val->as_int); break;
  case BOOL:  printf("#%c", val->as_bool ? 't' : 'f'); break;
  case FUNC:  print_func(val); break;
  case LIST:  print_list(val->as_list); break;
  case SYMB:  print_str(val->as_symb); break;
  default:    assert(false); break;
  }
}

static inline void init_g_env() {
  env_add(g_env, STRING("+"), make_atom(PRIM_ADD));
  env_add(g_env, STRING("-"), make_atom(PRIM_SUB));
  env_add(g_env, STRING("*"), make_atom(PRIM_MUL));
  env_add(g_env, STRING("/"), make_atom(PRIM_DIV));
  env_add(g_env, STRING("="), make_atom(PRIM_EQ));
  env_add(g_env, STRING(">"), make_atom(PRIM_GT));
  env_add(g_env, STRING("<"), make_atom(PRIM_LT));
  env_add(g_env, STRING(">="), make_atom(PRIM_GE));
  env_add(g_env, STRING("<="), make_atom(PRIM_LE));
  env_add(g_env, STRING("!="), make_atom(PRIM_NE));
  env_add(g_env, STRING("define"), make_atom(PRIM_DEF));
  env_add(g_env, STRING("lambda"), make_atom(PRIM_LAMBDA));
  env_add(g_env, STRING("if"), make_atom(PRIM_IF));
  env_add(g_env, STRING("not"), make_atom(PRIM_NOT));
}

static void repl() {
  g_env = alloc_env(NULL, ENV_INIT_SIZE);
  init_g_env();
  cur_env = g_env;
  while (true) {
    printf("==> ");
    print(eval(read()));
    printf("\n");
  }
}

static void usage() {
  printf("usage: \n");
  printf("    kawaii <file_name> \n");
  exit(-2);
}

int main(int argc, const char* argv[]) {
  if (argc > 1) {
    load(argv[1]);
  } else {
    usage();
  }

  repl();
  return 0;
}
