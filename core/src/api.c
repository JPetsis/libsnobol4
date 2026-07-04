/**
 * @file api.c
 * @brief High-level public C API for libsnobol4
 *
 * Implements snobol_context_t, snobol_pattern_t, and snobol_match_t along
 * with their lifecycle and query functions declared in snobol/snobol.h.
 *
 * The pipeline for pattern compilation is:
 *   source text → snobol_lexer → snobol_parser (AST) →
 * compile_ast_to_bytecode_c
 *
 * Pattern matching is done via vm_run().
 */

#include "snobol/ast.h"
#include "snobol/compiler.h"
#include "snobol/lexer.h"
#include "snobol/parser.h"
#include "snobol/search.h"
#include "snobol/snobol.h"
#include "snobol/snobol_internal.h"
#include "snobol/vm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Opaque type definitions
 * ---------------------------------------------------------------------------
 */

struct snobol_context {
  int _reserved; /* Future: pattern registry, allocator config, etc. */
};

struct snobol_pattern {
  uint8_t *bc;
  size_t bc_len;
  bool case_insensitive;
  /* Cached search metadata derived from bytecode at compile time.
   * Reused by snobol_pattern_search() and snobol_pattern_search_ex()
   * to avoid re-walking the bytecode on every call. */
  snobol_search_meta_t meta;
  bool meta_initialized;
  /* Cached charclass range metadata — O(1) set_id lookup
   * for get_ranges_ptr().  Built once at compile time. */
  snobol_range_meta_t *range_meta;
  size_t range_meta_count;
  /* Cached DFA automaton (Tier 7). Constructed lazily on first
   * eligible search; freed in snobol_pattern_free(). */
  snobol_dfa_t *automaton;
};

/* Maximum named variables returned from a match */
#define API_MAX_VARS 64

struct snobol_match {
  bool success;

  /* Output buffer (from OP_EMIT_* instructions) */
  char *output;
  size_t output_len;

  /* Named capture variables: var_values[i] points into subject or is a
   * NUL-terminated copy; var_lens[i] is the byte length.
   * Variable names are stored as decimal strings "1", "2", …, "64".
   * Index 0 corresponds to variable name "1" (1-based). */
  char *var_values[API_MAX_VARS];
  size_t var_lens[API_MAX_VARS];
  int var_count;

  /* Match position within the subject. Set by search_ex (and
   * snobol_match() via the same code path). Always 0 for matches
   * returned by the legacy non-stateful snobol_pattern_search()
   * unless the search metadata is supplied; for that code path
   * the position is 0 and match_len is the full subject length. */
  size_t position;
  size_t length;
};

/* ---------------------------------------------------------------------------
 * Context lifecycle
 * ---------------------------------------------------------------------------
 */

snobol_context_t *snobol_context_create(void) {
  snobol_context_t *ctx =
      (snobol_context_t *)snobol_malloc(sizeof(snobol_context_t));
  if (ctx)
    ctx->_reserved = 0;
  return ctx;
}

void snobol_context_destroy(snobol_context_t *ctx) {
  if (ctx)
    snobol_free(ctx);
}

/* ---------------------------------------------------------------------------
 * Pattern lifecycle
 * ---------------------------------------------------------------------------
 */

/**
 * Core compilation helper: lex → parse → compile → allocate pattern.
 */
static snobol_pattern_t *do_compile(const char *source, size_t len,
                                     bool case_insensitive, char **error) {
  if (error)
    *error = NULL;

  snobol_lexer_t *lexer = snobol_lexer_create(source, len);
  snobol_parser_t *parser = snobol_parser_create();

  ast_node_t *ast = snobol_parser_parse(parser, lexer);

  if (!ast || snobol_parser_has_error(parser)) {
    const char *msg = snobol_parser_get_error(parser);
    if (!msg)
      msg = "unknown parse error";
    if (error) {
      size_t mlen = strlen(msg) + 1;
      *error = (char *)malloc(mlen);
      if (*error)
        memcpy(*error, msg, mlen);
    }
    snobol_ast_free(ast);
    snobol_parser_destroy(parser);
    snobol_lexer_destroy(lexer);
    return NULL;
  }

  uint8_t *bc = NULL;
  size_t bc_len = 0;
  int rc = compile_ast_to_bytecode_c(ast, case_insensitive, &bc, &bc_len);
  snobol_ast_free(ast);
  snobol_parser_destroy(parser);
  snobol_lexer_destroy(lexer);

  if (rc != 0) {
    if (error) {
      const char *msg = "compilation failed";
      size_t mlen = strlen(msg) + 1;
      *error = (char *)malloc(mlen);
      if (*error)
        memcpy(*error, msg, mlen);
    }
    return NULL;
  }

  snobol_pattern_t *pat =
      (snobol_pattern_t *)snobol_malloc(sizeof(snobol_pattern_t));
  if (!pat) {
    compiler_free(bc);
    if (error) {
      *error = (char *)malloc(32);
      if (*error)
        memcpy(*error, "out of memory", 14);
    }
    return NULL;
  }
  pat->bc = bc;
  pat->bc_len = bc_len;
  pat->case_insensitive = case_insensitive;
  /* Derive search metadata once at compile time so snobol_pattern_search()
   * and snobol_pattern_search_ex() don't re-walk the bytecode per call. */
  snobol_search_derive_meta(pat->bc, pat->bc_len, &pat->meta);
  pat->meta_initialized = true;
  snobol_build_range_meta(pat->bc, pat->bc_len,
                           &pat->range_meta, &pat->range_meta_count);
  return pat;
}

snobol_pattern_t *snobol_pattern_compile_ex(snobol_context_t *ctx,
                                             const char *source, size_t len,
                                             uint32_t flags, char **error) {
  (void)ctx; /* context owns the pattern conceptually; no registry yet */
  bool case_insensitive = (flags & SNOBOL_FLAG_CASE_INSENSITIVE) != 0;
  /* Unknown flag bits are intentionally ignored (forward-compatible). */
  return do_compile(source, len, case_insensitive, error);
}

snobol_pattern_t *snobol_pattern_compile(snobol_context_t *ctx,
                                         const char *source, size_t len,
                                         char **error) {
  return snobol_pattern_compile_ex(ctx, source, len, 0, error);
}

const uint8_t *snobol_pattern_get_bc(const snobol_pattern_t *pattern) {
  return pattern ? pattern->bc : NULL;
}

size_t snobol_pattern_get_bc_len(const snobol_pattern_t *pattern) {
  return pattern ? pattern->bc_len : 0;
}

/* ---- Internal accessors (used by search.c) ---- */

snobol_dfa_t *snobol_pattern_get_automaton(const snobol_pattern_t *pattern) {
  return pattern ? pattern->automaton : NULL;
}

void snobol_pattern_set_automaton(snobol_pattern_t *pattern,
                                   snobol_dfa_t *dfa) {
  if (pattern) pattern->automaton = dfa;
}

const snobol_search_meta_t *snobol_pattern_get_meta(
    const snobol_pattern_t *pattern) {
  return pattern ? &pattern->meta : NULL;
}

bool snobol_pattern_automaton_available(const snobol_pattern_t *pattern) {
  return pattern && pattern->automaton &&
         pattern->automaton->num_states < SNOBOL_DFA_MAX_STATES;
}

void snobol_pattern_free(snobol_pattern_t *pattern) {
  if (!pattern)
    return;
  compiler_free(pattern->bc);
  if (pattern->range_meta)
    snobol_free(pattern->range_meta);
  if (pattern->automaton)
    snobol_dfa_free(pattern->automaton);
  snobol_free(pattern);
}

/* ---------------------------------------------------------------------------
 * Pattern matching
 * ---------------------------------------------------------------------------
 */

snobol_match_t *snobol_pattern_match(snobol_pattern_t *pattern,
                                     const char *subject, size_t len) {
  if (!pattern || !subject)
    return NULL;

  snobol_match_t *m = (snobol_match_t *)snobol_malloc(sizeof(snobol_match_t));
  if (!m)
    return NULL;
  memset(m, 0, sizeof(snobol_match_t));

  /* Set up output buffer */
  snobol_buf out_buf = {0};
  snobol_buf_init(&out_buf);

  /* ---- Literal-only fast path: bypass VM entirely ---- */
  if (pattern->meta_initialized && pattern->meta.is_literal_only) {
    const uint8_t *bc = pattern->bc;
    size_t bc_len = pattern->bc_len;
    size_t ip = 0;
    /* Skip leading zero-width ops */
    while (ip < bc_len) {
      uint8_t op = bc[ip];
      if (op == OP_NOP || op == OP_FENCE || op == OP_ANCHOR) { ip++; continue; }
      if ((op == OP_POS || op == OP_RPOS) && ip + 5 <= bc_len) { ip += 5; continue; }
      break;
    }
    if (ip + 9 <= bc_len && bc[ip] == OP_LIT) {
      uint32_t lit_off = ((uint32_t)bc[ip + 1] << 24) | ((uint32_t)bc[ip + 2] << 16) |
                         ((uint32_t)bc[ip + 3] << 8) | (uint32_t)bc[ip + 4];
      uint32_t lit_len = ((uint32_t)bc[ip + 5] << 24) | ((uint32_t)bc[ip + 6] << 16) |
                         ((uint32_t)bc[ip + 7] << 8) | (uint32_t)bc[ip + 8];
      if (lit_off <= bc_len && lit_off + lit_len <= bc_len && lit_len > 0) {
        const char *lit = (const char *)(bc + lit_off);
        if (len >= lit_len && memcmp(subject, lit, lit_len) == 0) {
          m->success = true;
          m->position = 0;
          m->length = lit_len;
          m->var_count = 0;
          snobol_buf_free(&out_buf);
          return m;
        }
      }
    }
    m->success = false;
    snobol_buf_free(&out_buf);
    return m;
  }

  /* Initialise VM */
  VM vm;
  memset(&vm, 0, sizeof(VM));
  vm.bc = pattern->bc;
  vm.bc_len = pattern->bc_len;
  vm.range_meta = pattern->range_meta;
  vm.range_meta_count = pattern->range_meta_count;
  vm.s = subject;
  vm.len = len;
  vm.out = &out_buf;

  bool ok = vm_run(&vm);
  m->success = ok;

  if (ok && out_buf.len > 0) {
    m->output = (char *)snobol_malloc(out_buf.len + 1);
    if (m->output) {
      memcpy(m->output, out_buf.data, out_buf.len);
      m->output[out_buf.len] = '\0';
      m->output_len = out_buf.len;
    }
  }

  /* Copy named variables (1-based: var_start[0] = variable "1") */
  int n = (int)vm.var_count;
  if (n > API_MAX_VARS)
    n = API_MAX_VARS;
  m->var_count = n;
  for (int i = 0; i < n; i++) {
    size_t vs = vm.var_start[i];
    size_t ve = vm.var_end[i];
    if (ve > vs && ve <= len) {
      size_t vlen = ve - vs;
      m->var_values[i] = (char *)snobol_malloc(vlen + 1);
      if (m->var_values[i]) {
        memcpy(m->var_values[i], subject + vs, vlen);
        m->var_values[i][vlen] = '\0';
        m->var_lens[i] = vlen;
      }
    }
  }

  snobol_buf_free(&out_buf);
  vm_free_labels(&vm);
  return m;
}

snobol_match_t *snobol_pattern_search(snobol_pattern_t *pattern,
                                      const char *subject, size_t len) {
  if (!pattern || !subject)
    return NULL;

  snobol_match_t *m = (snobol_match_t *)snobol_malloc(sizeof(snobol_match_t));
  if (!m)
    return NULL;
  memset(m, 0, sizeof(snobol_match_t));

  snobol_buf out_buf = {0};
  snobol_buf_init(&out_buf);

  VM vm;
  memset(&vm, 0, sizeof(VM));
  vm.bc = pattern->bc;
  vm.bc_len = pattern->bc_len;
  vm.range_meta = pattern->range_meta;
  vm.range_meta_count = pattern->range_meta_count;
  vm.s = subject;
  vm.len = len;
  vm.out = &out_buf;

  /* Use cached search metadata from compile time. Falls back to local
   * derivation only if the pattern was built without our compile path
   * (defensive — should not happen in practice). */
  snobol_search_meta_t meta;
  bool use_cached = pattern->meta_initialized;
  if (use_cached) {
    meta = pattern->meta;
  } else {
    snobol_search_derive_meta(pattern->bc, pattern->bc_len, &meta);
  }

  /* Build and cache DFA for eligible patterns */
  snobol_dfa_t *dfa = NULL;
  if (meta.automaton_eligible) {
    dfa = build_dfa(pattern->bc, pattern->bc_len, &vm);
    if (dfa) snobol_pattern_set_automaton(pattern, dfa);
  }

  snobol_search_result_t sr;
  bool ok = snobol_search_exec(&vm, subject, len, 0, &meta, dfa,
                               &sr, NULL);
  m->success = ok;
  m->position = sr.match_start;
  m->length = sr.match_end - sr.match_start;

  if (ok && out_buf.len > 0) {
    m->output = (char *)snobol_malloc(out_buf.len + 1);
    if (m->output) {
      memcpy(m->output, out_buf.data, out_buf.len);
      m->output[out_buf.len] = '\0';
      m->output_len = out_buf.len;
    }
  }

  int n = (int)vm.var_count;
  if (n > API_MAX_VARS)
    n = API_MAX_VARS;
  m->var_count = n;
  for (int i = 0; i < n; i++) {
    size_t vs = vm.var_start[i];
    size_t ve = vm.var_end[i];
    if (ve > vs && ve <= len) {
      size_t vlen = ve - vs;
      m->var_values[i] = (char *)snobol_malloc(vlen + 1);
      if (m->var_values[i]) {
        memcpy(m->var_values[i], subject + vs, vlen);
        m->var_values[i][vlen] = '\0';
        m->var_lens[i] = vlen;
      }
    }
  }

  snobol_buf_free(&out_buf);
  vm_free_labels(&vm);
  return m;
}

void snobol_match_free(snobol_match_t *match) {
  if (!match)
    return;
  snobol_free(match->output);
  for (int i = 0; i < API_MAX_VARS; i++) {
    snobol_free(match->var_values[i]);
  }
  snobol_free(match);
}

/* ---------------------------------------------------------------------------
 * Stateful search API
 *
 * For hot loops (PHP Pattern::searchSplit iterates 1000+ times per call),
 * the per-call allocation cost of snobol_match_t, the output buffer, the
 * VM struct, and the search metadata derivation dominates.
 * snobol_pattern_search_ex() amortises all of that.
 *
 * The state holds:
 *   - A reference to the pattern (must outlive the state)
 *   - A reusable VM struct (allocated on first use, fields-only reset
 *     per call via search_reset_vm())
 *   - A reusable output buffer (snobol_buf)
 *   - A reusable snobol_match_t that the caller reads but does not free
 * ---------------------------------------------------------------------------
 */

struct snobol_pattern_search_state {
  const uint8_t *bc; /* borrowed, not owned */
  size_t bc_len;
  snobol_buf out_buf;        /* reused across calls */
  VM vm;                     /* reused, fields-only reset per call */
  snobol_match_t match;      /* overwritten on each call */
  snobol_search_meta_t meta; /* derived once at create time */
  snobol_range_meta_t *range_meta;  /* owned — derived once at create time */
  size_t range_meta_count;
  bool vm_inited;            /* true after first search call sets it up */
  bool buf_inited;           /* true after first out_buf_init */
};

snobol_pattern_search_state_t *
snobol_pattern_search_state_create(const uint8_t *bc, size_t bc_len) {
  if (!bc || bc_len == 0)
    return NULL;
  snobol_pattern_search_state_t *state =
      (snobol_pattern_search_state_t *)snobol_malloc(sizeof(*state));
  if (!state)
    return NULL;
  memset(state, 0, sizeof(*state));
  state->bc = bc;
  state->bc_len = bc_len;
  /* Derive search metadata once — reused across all search calls */
  snobol_search_derive_meta(bc, bc_len, &state->meta);
  snobol_build_range_meta(bc, bc_len,
                           &state->range_meta, &state->range_meta_count);
  return state;
}

void snobol_pattern_search_state_destroy(snobol_pattern_search_state_t *state) {
  if (!state)
    return;
  if (state->buf_inited) {
    snobol_buf_free(&state->out_buf);
  }
  vm_free_labels(&state->vm);
  if (state->range_meta)
    snobol_free(state->range_meta);
  if (state->match.output) {
    snobol_free(state->match.output);
    state->match.output = NULL;
  }
  for (int i = 0; i < API_MAX_VARS; i++) {
    if (state->match.var_values[i]) {
      snobol_free(state->match.var_values[i]);
      state->match.var_values[i] = NULL;
    }
  }
  snobol_free(state);
}

snobol_match_t *snobol_pattern_search_ex(snobol_pattern_search_state_t *state,
                                         const char *subject,
                                         size_t subject_len,
                                         size_t start_offset) {
  if (!state || !subject)
    return NULL;

  /* Lazy init: output buffer on first call, JIT context on first call.
   * The VM struct is initialised fields-only (no memset) on every
   * call — search_reset_vm() handles the per-candidate reset. */
  if (!state->buf_inited) {
    snobol_buf_init(&state->out_buf);
    state->buf_inited = true;
  }

  if (!state->vm_inited) {
    memset(&state->vm, 0, sizeof(VM));
    state->vm.bc = (uint8_t *)state->bc;
    state->vm.bc_len = state->bc_len;
    state->vm.range_meta = state->range_meta;
    state->vm.range_meta_count = state->range_meta_count;
    state->vm.out = &state->out_buf;
    state->vm_inited = true;
  }

  /* Free any output from the previous call */
  if (state->match.output) {
    snobol_free(state->match.output);
    state->match.output = NULL;
    state->match.output_len = 0;
  }
  for (int i = 0; i < API_MAX_VARS; i++) {
    if (state->match.var_values[i]) {
      snobol_free(state->match.var_values[i]);
      state->match.var_values[i] = NULL;
      state->match.var_lens[i] = 0;
    }
  }
  state->match.success = false;
  state->match.var_count = 0;

  /* Reset out_buf length (keeps capacity) */
  state->out_buf.len = 0;
  if (state->out_buf.cap > 0 && state->out_buf.data) {
    state->out_buf.data[0] = '\0';
  }

  /* Use the search metadata derived at state creation time. */
  snobol_search_result_t sr;
  bool ok = snobol_search_exec(&state->vm, subject, subject_len, start_offset,
                               &state->meta, NULL, &sr, NULL);
  state->match.success = ok;
  /* sr.match_start is already an absolute position in the subject
   * (not relative to start_offset). Do NOT add start_offset again. */
  state->match.position = sr.match_start;
  state->match.length = sr.match_end - sr.match_start;

  if (ok && state->out_buf.len > 0) {
    state->match.output = (char *)snobol_malloc(state->out_buf.len + 1);
    if (state->match.output) {
      memcpy(state->match.output, state->out_buf.data, state->out_buf.len);
      state->match.output[state->out_buf.len] = '\0';
      state->match.output_len = state->out_buf.len;
    }
  }

  int n = (int)state->vm.var_count;
  if (n > API_MAX_VARS)
    n = API_MAX_VARS;
  state->match.var_count = n;
  for (int i = 0; i < n; i++) {
    size_t vs = state->vm.var_start[i];
    size_t ve = state->vm.var_end[i];
    if (ve > vs && ve <= subject_len) {
      size_t vlen = ve - vs;
      state->match.var_values[i] = (char *)snobol_malloc(vlen + 1);
      if (state->match.var_values[i]) {
        memcpy(state->match.var_values[i], subject + vs, vlen);
        state->match.var_values[i][vlen] = '\0';
        state->match.var_lens[i] = vlen;
      }
    }
  }

  return &state->match;
}

/* ---------------------------------------------------------------------------
 * Match result access
 * ---------------------------------------------------------------------------
 */

bool snobol_match_success(snobol_match_t *match) {
  return match && match->success;
}

const char *snobol_match_get_output(snobol_match_t *match, size_t *len) {
  if (!match || !match->success) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = match->output_len;
  return match->output ? match->output : "";
}

size_t snobol_match_get_position(const snobol_match_t *match) {
  if (!match || !match->success)
    return 0;
  return match->position;
}

size_t snobol_match_get_length(const snobol_match_t *match) {
  if (!match || !match->success)
    return 0;
  return match->length;
}

const char *snobol_match_get_variable(snobol_match_t *match, const char *name,
                                      size_t *len) {
  if (!match || !match->success || !name) {
    if (len)
      *len = 0;
    return NULL;
  }
  /* Variable names are 1-based decimal integers: "1" → index 0 */
  char *end;
  long idx = strtol(name, &end, 10);
  if (end == name || idx < 1 || idx > API_MAX_VARS) {
    if (len)
      *len = 0;
    return NULL;
  }
  int i = (int)(idx - 1);
  if (i >= match->var_count || !match->var_values[i]) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = match->var_lens[i];
  return match->var_values[i];
}

/* ---------------------------------------------------------------------------
 * One-shot convenience match API
 * ---------------------------------------------------------------------------
 */

#define MATCH_MAX_CAPTURES 64

snobol_match_result_t *snobol_match(const char *pattern, size_t pat_len,
                                    const char *subject, size_t sub_len,
                                    uint32_t flags) {
  snobol_match_result_t *result =
      (snobol_match_result_t *)snobol_malloc(sizeof(snobol_match_result_t));
  if (!result)
    return NULL;
  memset(result, 0, sizeof(snobol_match_result_t));

  /* Compile pattern */
  bool case_insensitive = (flags & SNOBOL_FLAG_CASE_INSENSITIVE) != 0;
  snobol_context_t *ctx =
      NULL; /* not needed for compile, but required by API */
  char *compile_error = NULL;
  snobol_pattern_t *pat = do_compile(pattern, pat_len, case_insensitive,
                                     &compile_error);

  if (!pat) {
    if (compile_error) {
      result->error = compile_error; /* transfer ownership */
    } else {
      result->error = snobol_malloc(16);
      if (result->error)
        memcpy(result->error, "unknown error", 14);
    }
    return result;
  }

  /* Set up output buffer */
  snobol_buf out_buf = {0};
  snobol_buf_init(&out_buf);

  /* Initialise VM */
  VM vm;
  memset(&vm, 0, sizeof(VM));
  vm.bc = pat->bc;
  vm.bc_len = pat->bc_len;
  vm.range_meta = pat->range_meta;
  vm.range_meta_count = pat->range_meta_count;
  vm.s = subject;
  vm.len = sub_len;
  vm.out = &out_buf;

  bool ok = vm_run(&vm);
  result->success = ok;

  if (ok && out_buf.len > 0) {
    result->output = (char *)snobol_malloc(out_buf.len + 1);
    if (result->output) {
      memcpy(result->output, out_buf.data, out_buf.len);
      result->output[out_buf.len] = '\0';
      result->output_len = out_buf.len;
    }
  }

  /* Copy named variables */
  int n = (int)vm.var_count;
  if (n > MATCH_MAX_CAPTURES)
    n = MATCH_MAX_CAPTURES;
  result->capture_count = n;

  if (n > 0) {
    result->captures = (char **)snobol_calloc((size_t)n, sizeof(char *));
    result->capture_lens = (size_t *)snobol_calloc((size_t)n, sizeof(size_t));
    if (result->captures && result->capture_lens) {
      for (int i = 0; i < n; i++) {
        size_t vs = vm.var_start[i];
        size_t ve = vm.var_end[i];
        if (ve > vs && ve <= sub_len) {
          size_t vlen = ve - vs;
          result->captures[i] = (char *)snobol_malloc(vlen + 1);
          if (result->captures[i]) {
            memcpy(result->captures[i], subject + vs, vlen);
            result->captures[i][vlen] = '\0';
            result->capture_lens[i] = vlen;
          }
        }
      }
    }
  }

  snobol_buf_free(&out_buf);
  vm_free_labels(&vm);
  snobol_pattern_free(pat);
  snobol_free(compile_error);

  return result;
}

void snobol_match_result_free(snobol_match_result_t *result) {
  if (!result)
    return;
  snobol_free(result->error);
  snobol_free(result->output);
  if (result->captures) {
    for (int i = 0; i < result->capture_count; i++) {
      snobol_free(result->captures[i]);
    }
    snobol_free(result->captures);
  }
  snobol_free(result->capture_lens);
  snobol_free(result);
}

/* ---------------------------------------------------------------------------
 * Builder API
 * ---------------------------------------------------------------------------
 */

struct snobol_pattern_build {
  int _reserved;
};

snobol_pattern_build_t *snobol_pattern_build_create(void) {
  snobol_pattern_build_t *b =
      (snobol_pattern_build_t *)snobol_malloc(sizeof(snobol_pattern_build_t));
  if (b)
    b->_reserved = 0;
  return b;
}

void snobol_pattern_build_destroy(snobol_pattern_build_t *build) {
  if (build)
    snobol_free(build);
}

ast_node_t *snobol_pattern_build_lit(snobol_pattern_build_t *build,
                                     const char *text, size_t len) {
  (void)build;
  return snobol_ast_create_lit(text, len);
}

ast_node_t *snobol_pattern_build_span(snobol_pattern_build_t *build,
                                      const char *set, size_t len) {
  (void)build;
  return snobol_ast_create_span(set, len);
}

ast_node_t *snobol_pattern_build_brk(snobol_pattern_build_t *build,
                                     const char *set, size_t len) {
  (void)build;
  return snobol_ast_create_break(set, len);
}

ast_node_t *snobol_pattern_build_any(snobol_pattern_build_t *build,
                                     const char *set, size_t len) {
  (void)build;
  return snobol_ast_create_any(set, len);
}

ast_node_t *snobol_pattern_build_notany(snobol_pattern_build_t *build,
                                        const char *set, size_t len) {
  (void)build;
  return snobol_ast_create_notany(set, len);
}

ast_node_t *snobol_pattern_build_len(snobol_pattern_build_t *build, int32_t n) {
  (void)build;
  return snobol_ast_create_len(n);
}

ast_node_t *snobol_pattern_build_arbno(snobol_pattern_build_t *build,
                                       ast_node_t *sub) {
  (void)build;
  return snobol_ast_create_arbno(sub);
}

ast_node_t *snobol_pattern_build_cap(snobol_pattern_build_t *build, int reg,
                                     ast_node_t *sub) {
  (void)build;
  return snobol_ast_create_cap(reg, sub);
}

ast_node_t *snobol_pattern_build_assign(snobol_pattern_build_t *build, int var,
                                        int reg) {
  (void)build;
  return snobol_ast_create_assign(var, reg);
}

ast_node_t *snobol_pattern_build_concat(snobol_pattern_build_t *build,
                                        ast_node_t **parts, size_t count) {
  (void)build;
  return snobol_ast_create_concat(parts, count);
}

ast_node_t *snobol_pattern_build_alt(snobol_pattern_build_t *build,
                                     ast_node_t *left, ast_node_t *right) {
  (void)build;
  return snobol_ast_create_alt(left, right);
}

ast_node_t *snobol_pattern_build_label(snobol_pattern_build_t *build,
                                       const char *name, ast_node_t *target) {
  (void)build;
  /* snobol_ast_create_label takes ownership of name */
  char *name_copy = (char *)snobol_malloc(strlen(name) + 1);
  if (name_copy)
    strcpy(name_copy, name);
  return snobol_ast_create_label(name_copy, target);
}

ast_node_t *snobol_pattern_build_goto(snobol_pattern_build_t *build,
                                      const char *label) {
  (void)build;
  return snobol_ast_create_goto(label);
}

ast_node_t *snobol_pattern_build_pos(snobol_pattern_build_t *build, int32_t n) {
  (void)build;
  return snobol_ast_create_pos(n);
}

ast_node_t *snobol_pattern_build_tab(snobol_pattern_build_t *build, int32_t n) {
  (void)build;
  return snobol_ast_create_tab(n);
}

ast_node_t *snobol_pattern_build_rpos(snobol_pattern_build_t *build,
                                      int32_t n) {
  (void)build;
  return snobol_ast_create_rpos(n);
}

ast_node_t *snobol_pattern_build_rtab(snobol_pattern_build_t *build,
                                      int32_t n) {
  (void)build;
  return snobol_ast_create_rtab(n);
}

ast_node_t *snobol_pattern_build_breakx(snobol_pattern_build_t *build,
                                        const char *set, size_t len) {
  (void)build;
  return snobol_ast_create_breakx(set, len);
}

ast_node_t *snobol_pattern_build_bal(snobol_pattern_build_t *build,
                                     uint32_t open_cp, uint32_t close_cp) {
  (void)build;
  return snobol_ast_create_bal(open_cp, close_cp);
}

ast_node_t *snobol_pattern_build_fence(snobol_pattern_build_t *build) {
  (void)build;
  return snobol_ast_create_fence();
}

ast_node_t *snobol_pattern_build_rem(snobol_pattern_build_t *build) {
  (void)build;
  return snobol_ast_create_rem();
}

ast_node_t *snobol_pattern_build_abort(snobol_pattern_build_t *build) {
  (void)build;
  return snobol_ast_create_abort();
}

ast_node_t *snobol_pattern_build_fail(snobol_pattern_build_t *build) {
  (void)build;
  return snobol_ast_create_fail();
}

ast_node_t *snobol_pattern_build_succeed(snobol_pattern_build_t *build) {
  (void)build;
  return snobol_ast_create_succeed();
}

ast_node_t *snobol_pattern_build_emit(snobol_pattern_build_t *build,
                                      ast_node_t *root) {
  (void)build;
  /* Ownership is transferred to the caller; just return the root. */
  return root;
}
