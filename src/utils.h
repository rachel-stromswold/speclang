#ifndef SPCL_UTILS_H
#define SPCL_UTILS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "s8.h"

#define BLK_MAX			16	//the maximum number of nested blocks
#define LST_MAX			8	//the maximum number of nested lists for a flatten statement

#define BEG_PAR			'('
#define END_PAR			')'
#define BEG_SQR			'['
#define END_SQR			']'
#define BEG_CRL			'{'
#define END_CRL			'}'
#define BEG_QTE			'\"'
#define END_QTE			'\"'

//this is a macro to initialize a dummy line_buffer named fs->
/*#define str_to_fs(str)				\
    spcl_fstream fs;				\
    fs.lines = alloca(sizeof(char*));		\
    fs.line_sizes = alloca(sizeof(size_t));	\
    fs.lines[0] = str;				\
    fs.line_sizes[0] = strlen(str);		\
    fs.n_lines = 1*/

//generic stack class
#define TYPED_A(NAME,TYPE) NAME##TYPE
#define TYPED(NAME,TYPE) TYPED_A(NAME, _##TYPE)
#define TYPED3(NAME,TA,TB) TYPED(TYPED_A(NAME,_##TA),TB)
#define PAIR_DEF(TA,TB) struct TYPED3(PAIR,TA,TB) {					\
    TA a;										\
    TB b;										\
} TYPED3(PAIR,TA,TB);									\
struct TYPED3(PAIR,TA,TB) TYPED3(MAKE_PAIR,TA,TB)(TA pa, TB pb) {			\
    struct TYPED3(PAIR,TA,TB) ret;ret.a = pa;ret.b = pb;				\
    return ret;										\
}											\
typedef TYPED3(PAIR,TA,TB) p(TA,TB)
#define stack(TYPE,N) struct TYPED3(STACK,TYPE,N)

//a really hacky way of doing template like thingies in C
#define STACK_DEF(TYPE,N) stack(TYPE,N) {						\
    size_t ptr;										\
    TYPE buf[N];									\
};											\
stack(TYPE,N) TYPED3(MAKE_STACK,TYPE,N)() {						\
    stack(TYPE,N) ret;									\
    ret.ptr = 0;									\
    memset(ret.buf, 0, sizeof(TYPE)*N);							\
    return ret;										\
}											\
void TYPED3(DESTROY_STACK,TYPE,N) (stack(TYPE,N)* s, void (*destroy_el)(TYPE*)) {	\
    for (size_t i = 0; destroy_el && i < s->ptr; ++i)					\
	destroy_el(s->buf + i);								\
    s->ptr = 0;										\
}											\
int TYPED3(STACK_PUSH,TYPE,N) (stack(TYPE,N)* s, TYPE el) {				\
    if (s->ptr == N)									\
	return 1;									\
    s->buf[s->ptr++] = el;								\
    return 0;										\
}											\
int TYPED3(STACK_POP,TYPE,N)(stack(TYPE,N)* s, TYPE* sto) {				\
    if (s->ptr == 0 || s->ptr >= N) return 1;						\
    s->ptr -= 1;									\
    if (sto) *sto = s->buf[s->ptr];							\
    return 0;										\
}											\
int TYPED3(STACK_PEEK,TYPE,N)(stack(TYPE,N)* s, size_t ind, TYPE* sto) {		\
    if (ind == 0 || ind > s->ptr || ind > N) return 1;					\
    if (sto) *sto = s->buf[s->ptr-ind];							\
    return 0;										\
}
/**
 * create a pair by shallow copying arguments a and b
 * a: argument to shallow copy
 * b: argument to shallow copy
 */
#define make_p(TA,TB) TYPED3(MAKE_PAIR,TA,TB)
/**
 * Create an empty stack
 */
#define make_stack(TYPE,N) TYPED3(MAKE_STACK,TYPE,N)
/**
 * Cleanup all memory associated with the stack s.
 * s: the stack to destroy 
 * destroy_el: A callable function which deallocates memory associated for each element. Set to NULL if nothing needs to be done.
 * returns: 0 on success, 1 on failure (i.e. out of memory)
 */
#define destroy_stack(TYPE,N) TYPED3(DESTROY_STACK,TYPE,N)
/**
 * Push a shallow copy onto the stack.
 * s: the stack to push onto
 * el: the element to push. Ownership is transferred to the stack, so make a copy if you need it
 * returns: 0 on success, 1 on failure (i.e. out of memory)
 */
#define push(TYPE,N) TYPED3(STACK_PUSH,TYPE,N)
/**
 * Pop the object from the top of the stack and store it to the pointer sto. The caller is responsible for cleaning up any memory after a call to pop. 
 * s: the stack to pop from
 * sto: the location to save to (may be set to NULL)
 * returns: 0 on success, 1 on failure (i.e. popping from an empty stack)
 */
#define pop(TYPE,N) TYPED3(STACK_POP,TYPE,N) 
/**
 * Examine the object ind indices down from the top of the stack and store it to the pointer sto without removal. Do not deallocate memory for this, as only a shallow copy is returned
 * s: the stack to peek in
 * ind: the index in the stack to read. Note that indices start at 1 as opposed to zero.
 * sto: the location to save to (may be set to NULL)
 * returns: 0 on success, 1 on failure (i.e. popping from an empty stack)
 */
#define peek(TYPE,N) TYPED3(STACK_PEEK,TYPE,N)

/** ============================ custom allocators ============================ **/
//TODO

//These functions work like malloc and realloc, but abort execution if allocation failed.
static inline void* xmalloc(size_t n) {
    void* ret = malloc(n);
    if (!ret) {
	fprintf(stderr, "Ran out of memory!\n");
	exit(1);
    }
    return ret;
}
static inline void* xrealloc(void* p, size_t n) {
    p = realloc(p, n);
    if (!p) {
	fprintf(stderr, "Ran out of memory!\n");
	exit(1);
    }
    return p;
}
static inline void xfree(void* p) {
    free(p);
}

/**
 * check if a character is whitespace
 */
static inline int is_whitespace(char c) {
    if (c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r')
	return 1;
    return 0;
}

/**
 * Helper function for token_block which tests whether the character c is a token terminator
 */
static inline unsigned char is_char_sep(char c) {
    if (is_whitespace(c) || c == ';' || c == '+'  || c == '-' || c == '*'  || c == '/' || c == BEG_PAR || c == END_PAR || c == BEG_SQR || c == END_SQR || c == BEG_CRL || c == END_CRL)
	return 1;
    return 0;
}

inline int namecmp(const char* a, const char* b, size_t n) {
    size_t i = 0;size_t j = 0;
    while (i < n && is_char_sep(a[i]))
	++i;
    while (j < n && is_char_sep(b[j]))
	++j;
    unsigned char hit_end = 0;// hit_end&1 did a hit a whitespace block? hit_end&2 did b hit a whitespace block?
    while (i < n && j < n) {
	hit_end = is_char_sep(a[i]) | (is_char_sep(b[j]) << 1);
	//if both strings hit the end and we haven't already exited, that means there was a match
	if (hit_end == 3)
	    return 0;
	//if only one string hit the end, then there wasn't a match
	if (hit_end != 0)
	    return 2*hit_end - 3;//since this case implies, hit_end == 1 or 2, this hack tells us whether a or b is larger
	//if we haven't hit the end of either string and there's a mismatch, return the difference
	if (hit_end == 0 && a[i] != b[j])
	    return (int)(a[i] - b[j]);
	++i;
	++j;
    }
    hit_end = is_char_sep(a[i]) | (is_char_sep(b[j]) << 1);
    if (hit_end == 3 || hit_end == 0)
	return 0;
    return 2*hit_end - 3;
}

//check wheter s and e are matching delimeters
static inline char get_match(char s) {
    switch (s) {
	case BEG_PAR: return END_PAR;
	case BEG_SQR: return END_SQR;
	case BEG_CRL: return END_CRL;
	case '*': return '*';
	case '\"': return '\"';
	case '\'': return '\'';
	default: return 0;
    }
    return 0;
}

/**
 * write a floating point value to the string str
 */
inline int write_numeric(char* str, size_t n, double x) {
    if (x >= 1000000)
	return snprintf(str, n, "%e", x);
    return snprintf(str, n, "%f", x);
}

/**
  * Remove the whitespace surrounding a word
  * returns: a new string without any of the surrounding whitespace
  */
static inline s8 trim_whitespace(s8 str) {
    s8 ret;
    ret.s = NULL;
    ret.n = 0;
    if (!str.s || str.n == 0)
	return ret;
    psize start_ind = -1, last_non = -1;
    for (psize i = 0; i < str.n; ++i) {
        if (!is_whitespace(str.s[i])) {
            last_non = i;
            if (start_ind < 0)
		start_ind = i;
        }
    }
    //return an empty string if we didn't find any non-whitespace
    if (start_ind < 0 || last_non < start_ind)
	return ret;
    ret.s = str.s + start_ind;
    ret.n = last_non - start_ind + 1;
    return ret;
}

/**
 * Acts like strdup for sized strings
 */
static inline s8 s8dup(s8 s) {
    s8 d = (s8){NULL, 0};
    if (!s.s || !s.n)
	return d;
    //writing unit tests in c++ was a horrible decision
    d.s = (u8*)malloc(sizeof(u8)*s.n);
    d.n = s.n;
    memcpy(d.s, s.s, sizeof(u8)*s.n);
    return d;
}

#endif
