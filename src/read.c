#include "spcl_utils.h"
#include "exec.h"
#include "speclang.h"

static s8 spcl_keywords[SPCL_N_KEYS] = {s8(" "), s8("import"), s8("true"), s8("false"), s8("class"), s8("if"), s8("for"), s8("else"), s8("while"), s8("break"), s8("continue"), s8("return"), s8("fn")};
static const char* const errnames[N_ERRORS] =
{"SUCCESS", "NO_FILE", "LACK_TOKENS", "BAD_SYNTAX", "BAD_VALUE", "BAD_TYPE", "NOMEM", "NAN", "UNDEFINED_TOKEN", "OUT_OF_BOUNDS", "ASSERT"};
static const char* const valnames[N_VALTYPES] = {"none", "error", "numeric", "string", "array", "list", "fn", "obj"};

//aliases for optree operations
static const s8 OP_ALIAS[]  = {s8(""), s8("+"), s8("-"), s8("*"), s8("/"), s8("%"), s8("**"), s8("|"), s8("&"), s8("<<"), s8(">>"), s8("=="), s8("!="), s8(">"), s8("<"), s8(">="), s8("<="), s8("!"), s8("||"), s8("&&"), s8("++"), s8("--"), s8("in"), s8("is"), s8("?"), s8("()"), s8("{}"), s8("="), s8("()"), s8(","), s8("["), s8("["), s8("if"), s8("for"), s8("while"), s8("."), s8("?"), s8(":")};
static const int OP_PRECS[] = {0, 4, 4, 3, 3, 3, 2, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 8, 8, 8, 4, 4, 9, 10, 1, 1, 1, 12, 9, 8, 1, 1, 10, 10, 10, 1, 2, 1};
static const int OP_LENS[] = {1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 2, 3, 3, 2, 2, 2, 2, 4, 2, 3, 3, 3, 2, 3, 2, 3, 3, 2, 3, 3, 2, 2, 2, 1, 3, 3, 3, 3, 1, 1, 4, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

#define spcl_isfalse(v) (v.type == VAL_UNDEF || (v.type == VAL_INT && v.val.i == 0) || v.n_els == 0)
#define spcl_istrue(v) (!spcl_isfalse(v))

//dumb forward declarations
psize fs_end(const spcl_fstream* fs) {
    if (!fs->f)
	return fs->clen;
    return fs->flen;
}
/**
 * returns the character at position pos
 */
static inline char fs_get(const spcl_fstream *fs, psize pos) {
    //TODO: this won't work correctly once we switch to actually streaming files
    if (pos >= fs->clen)
        return 0;
    return fs->cache[pos];
}

/** ======================================================== utility functions ======================================================== **/

#define MAX_ASCII 0x7f
#define MAX_OP_PREC  7
static const int OP1_PRECS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 0, 3, 4, 0, 4, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 5, 7, 5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int OP2_PRECS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 7, 7, 0, 7, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0};

/**
 * Print an error message
 */
static inline void _print_error(FILE *f, spcl_val er, read_state rs) {
    s8 line = fs_read(rs.b, rs.start, fs_line_end(rs.b, rs.start));
    fprintf(f, "\e[1m\033[31mError\033[0m\e[1m %s on line %lu:\e[m %.*s\n\t%s\n", errnames[er.val.e->c], fs_find_line(rs.b, rs.start)+1, (int)line.n, line.s, er.val.e->msg);
}
/**
 * Find the length of an operator sequence e.g. '==', '=', '+=' etc.
 */
static inline int get_oplen(const spcl_fstream *fs, psize pos) {
    unsigned char op = fs_get(fs, pos);
    unsigned char next = fs_get(fs, pos+1);
    //return 0 if the character isn't an operator
    if (op < 0 || op > MAX_ASCII || (OP1_PRECS[op] == 0 && OP2_PRECS[op] == 0))
	return 0;
    //only the '?' operator does not accept an '=' operator immediately after
    if (op == '?')
	return 1;
    //matches characters '!', '?', '+', '-', '*', '/', '<', '=', '>', '.', and ','. hopefully those last two don't cause problems
    if ( op == '!' || op == '^' || (op >= '*' && op <= '/') || (op >= '<' && op <= '>') ) {
	if (next == '=')
	    return 2;
	return 1;
    } else if ( (op == '|' || op == '&') && next == op ) {
	if (next == op)
	    return 2;
	return 1;
    }
    return op == ':';
}

static inline optr_op name_to_op(spcl_fstream *fs, psize pos) {
    char c = fs_get(fs, pos);
    if (fs_get(fs, pos+1) == c) {
    switch (c) {
    case '+': return OPTR_INC;
    case '-': return OPTR_DEC;
    case '*': return OPTR_EXP;
    case '<': return OPTR_SHL;
    case '>': return OPTR_SHR;
    case '=': return OPTR_EQ;
    case '&': return OPTR_AND;
    case '|': return OPTR_OR;
    default: break;
    }
    }
    switch (c) {
    case '+': return OPTR_ADD;
    case '-': return OPTR_SUB;
    case '*': return OPTR_MUL;
    case '/': return OPTR_DIV;
    case '%': return OPTR_MOD;
    case '<': return (fs_get(fs, pos+1)=='=')? OPTR_LE : OPTR_LT;
    case '>': return (fs_get(fs, pos+1)=='=')? OPTR_GE : OPTR_GT;
    case '!': return (fs_get(fs, pos+1) == '=')? OPTR_NEQ : OPTR_NOT;
    case '=': return OPTR_ASSGN;
    case '&': return OPTR_BAND;
    case '|': return OPTR_BOR;
    case ',': return OPTR_APPND;
    case '.': return OPTR_DREF;
    case '?': return OPTR_TRNQ;
    case ':': return OPTR_TRNC;
    case 'f': return (fs_get(fs,pos+1)=='o' && fs_get(fs,pos+2)=='r' && is_whitespace(fs_get(fs,pos+3)))? OPTR_FOR : 0;
    default: break;
    }
    return 0;
}

/**
 * Test whether a > b in asciibetical order. The result should be equivalent to strncmp(a.s, b.s).
 */
int spcl_strcmp(spcl_val a, spcl_val b) {
    size_t n_min = (a.n_els < b.n_els) ? a.n_els : b.n_els;
    for (psize i = 0; i < n_min; ++i) {
	if (a.val.s[i] - b.val.s[i])
	    return a.val.s[i] - b.val.s[i];
    }
    //if we've gotten this far, then we need to use length as a tiebreaker
    return a.n_els - b.n_els;
}

/**
 * get the psize of a spcl_context
 */
static inline size_t con_size(const spcl_inst* c) {
    if (!c)
	return 0;
    return 1 << c->t_bits;
}

/**
 * iterate through a spcl_inst by finding defined entries in the table
 */
static inline size_t con_it_next(const spcl_inst* c, size_t i) {
    for (; i < con_size(c); ++i) {
	if (c->table[i].v.type)
	    return i;
    }
    return i;
}

/**
 * Helper function that finds the start of first token before the index s in the read state rs. The returned spcl_val is greater than or equal to zero.
 * fs: the line buffer to read from
 * s: the current position
 * stop: do not return any tokens before this index
 */
static inline psize find_token_before(const spcl_fstream* fs, psize s, psize stop) {
    int started = 0;
    while (s > stop) {
	s -= 1;
	char c = fs_get(fs, s);
	if (!is_whitespace(c))
	    started = 1;
	if ((!started && is_char_sep(c)) || s ==0)
	    return s;
    }
    return stop;
}

STACK_DEF(char,BLK_MAX)
/**
 * Find the index of the first character c that isn't nested inside a block or NULL if an error occurred
 */
static inline psize strchr_block_rs(const spcl_fstream* fs, psize s, psize e, char c) {
    stack(char,BLK_MAX) blk_stk = make_stack(char,BLK_MAX)();
    char prev;
    char cur = 0;
    while (s < e && s < fs->flen) {
	prev = cur;
	cur = fs_get(fs, s);
	if (!cur)
	    break;
	if (cur == BEG_PAR || cur == BEG_SQR || cur == BEG_CRL) {
	    if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	} else if (cur == END_CRL || cur == END_SQR ||cur == END_PAR) {
	    if ( pop(char,BLK_MAX)(&blk_stk, &prev) || cur != get_match(prev) ) return e;
	} else if (cur == '\"') {
	    if (blk_stk.ptr != 0 && !peek(char,BLK_MAX)(&blk_stk, 1, &prev) && prev == '\"') {
		if ( pop(char,BLK_MAX)(&blk_stk, NULL) ) return e;
	    } else {
		if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	    }
	} else if (cur == '\'') {
	    //quotes are more complicated
	    if (blk_stk.ptr != 0 && !peek(char,BLK_MAX)(&blk_stk, 1, &prev) && prev == '\'') {
		if ( pop(char,BLK_MAX)(&blk_stk, NULL) ) return e;
	    } else {
		if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	    }
	}
	//now look for matches
	if (cur == c && blk_stk.ptr == 0)
	    return s;
	++s;
    }
    return e;
}

/**
 * Find the first instance of a token (i.e. surrounded by whitespace) in the string str which matches comp
 */
static inline psize token_block(const spcl_fstream* fs, psize s, psize e, const char* cmp, size_t cmp_len) {
    if (!fs || !cmp)
	return e;
    stack(char,BLK_MAX) blk_stk = make_stack(char,BLK_MAX)();
    char prev;
    char cur = 0;
    while (s != e && s < fs->flen) {
	prev = cur;
	cur = fs_get(fs, s);
	if (!cur)
	    break;
	//check whether we're at the root level and there was a seperation terminator
	if (blk_stk.ptr == 0 && is_char_sep(prev)) {
	    int found = 1;
	    for (size_t j = 0; j < cmp_len; ++j) {
		//make sure the first cmp_len characters match
		if ( cmp[j] != fs_get(fs, s+j) ) {
		    found = 0;
		    break;
		}
	    }
	    //make sure its ended by a separator
	    if ( found && is_char_sep(fs_get(fs, s+cmp_len)) )
		return s;
	}
	if (cur == BEG_PAR || cur == BEG_SQR || cur == BEG_CRL) {
	    if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	} else if (cur == END_CRL || cur == END_SQR || cur == END_PAR) {
	    if ( pop(char,BLK_MAX)(&blk_stk, &prev)  || cur != get_match(prev) ) return e;
	//comments use more then one character
	} else if (prev == '/' && cur == '*') {
	    if ( push(char,BLK_MAX)(&blk_stk, '*') ) return e;
	} else if (prev == '*' && cur == '/') {
	    if ( pop(char,BLK_MAX)(&blk_stk, &prev) || cur != get_match(prev) ) return e;
	} else if (prev == '/' && cur == '/') {
	    if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	//quotes are more complicated 
	} else if (cur == '\"') {
	    if (blk_stk.ptr != 0 && !peek(char,BLK_MAX)(&blk_stk, 1, &prev) && prev == '\"') {
		if ( pop(char,BLK_MAX)(&blk_stk, NULL) ) return e;
	    } else {
		if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	    }
	} else if (cur == '\'') {
	    //quotes are more complicated
	    if (blk_stk.ptr != 0 && !peek(char,BLK_MAX)(&blk_stk, 1, &prev) && prev == '\'') {
		if ( pop(char,BLK_MAX)(&blk_stk, NULL) ) return e;
	    } else {
		if ( push(char,BLK_MAX)(&blk_stk, cur) ) return e;
	    }
	}
	++s;
    }
    return e;
}

/** ============================ psize ============================ **/

spcl_local read_state make_read_state(const spcl_fstream* fs, psize s, psize e) {
    read_state rs;
    rs.b = fs;
    rs.start = s;
    rs.end = e;
    return rs;
}

/** ============================ custom allocators ============================ **/

typedef struct _freeblk {
    struct _freeblk *next;
    psize len;
} freeblk;

typedef struct _heapblk {
    struct _heapblk *next;
    freeblk *first;
    psize len;
} heapblk;

#define padfor(ptr, align) ((align - ((usize)ptr & (align-1))) & (align-1))
static const psize minsz = sizeof(freeblk) + sizeof(heapblk);

/**
 * Create a new heapblk that can hold at least n bytes (plus overhead)
 * n: the total size of the heap
 * imsize: the size which must be allocated immediately. The resultant pointer will always be sizeof(heapblk)+sizeof(usize) bytes after the return value
 */
static inline void *make_heapblk(size_t n, size_t imsize) {
    debug_assert(n > minsz+imsize);
    heapblk *hp = malloc(n);
    if (!hp)
	return NULL;
    hp->next = NULL;
    hp->len = n;
    hp->first = (freeblk*)((char*)hp+sizeof(heapblk)+imsize);
    //if there is an immediate allocation, then we have to add its length to the first element
    if (imsize)
	hp->first = (freeblk*)((char*)hp->first + sizeof(usize));
    //initialize the first block
    hp->first->next = NULL;
    hp->first->len = n - imsize;
    return hp;
}

/**
 * Allocate memory for the process vp
 */
static inline void *xmalloc(size_t n, vproc *vp) {
    void *tmp = malloc(n);
    if (!tmp)
	exit(1);
    return tmp;
    /*heapblk *hp = (heapblk*)vp->heap;
    freeblk **stonxt = &(hp->first);
    freeblk *blk = hp->first;
    while (hp->next) {
	while (blk) {
	    //check if the current block has enough space
	    if (blk->len >= n+sizeof(usize)) {
		//try dividing up the memory. First allocate find the block after the current block and its size identifier
		char *cblk = ((char*)blk);
		char *next = cblk + n + sizeof(usize);
		next += padfor(next, alignof(freeblk));
		//if there isn't enough room to hold a new freeblk, then allocate all of the available space
		if (next + sizeof(freeblk) > cblk + blk->len) {
		    *stonxt = blk->next;
		    ((usize*)blk)[0] = blk->len;
		    return cblk + sizeof(usize);
		}
		//rearrange the freeblk linked list
		freeblk *tmp = (freeblk*)next;
		tmp->next = blk->next;
		tmp->len = cblk + blk->len - next;
		*stonxt = tmp;
		((usize*)blk)[0] = next - cblk;
		return cblk + sizeof(usize);
	    }
	    stonxt = &(blk->next);
	    blk = blk->next;
	}
	//if that failed, try going to the next heap page
	hp = hp->next;
	stonxt = &(hp->first);
	blk = hp->first;
    }
    //find the smallest power of two larger than sizeof(heapblk+n
    size_t to_alloc = 64;
    for (; to_alloc < minsz+n; to_alloc *= 2)
	assert(to_alloc < SIZE_MAX/2);
    //if we reach this point, then there wasn't an available block. Thus, we must create a new heappage.
    hp->next = make_heapblk(to_alloc, n);
    if (!hp->next) {
	fprintf(stderr, "Ran out of memory trying to allocate %lu bytes!\n", n);
	exit(1);
    }
    char *ret = (char*)hp->next + sizeof(heapblk);
    //finally we may do the immediate allocation
    ((usize*)ret)[0] = (char*)hp->next->first - ret;
    return hp->next + sizeof(heapblk) + sizeof(usize);*/
}
static inline void *xrealloc(void *p, size_t n, vproc *vp) {
    void *tmp = realloc(p, n);
    if (!tmp)
	exit(1);
    return tmp;
    /*heapblk *hp = (heapblk*)vp->heap;
    //TODO: something better
    if (n >= SIZE_MAX/4) {
	fprintf(stderr, "Ran out of memory!\n");
	exit(1);
    }
    void *tmp = xmalloc(n, vp);
    memmove(tmp, p, *((usize*)p - 1));
    freeblk *b = (freeblk*)p;
    b->len = *((usize*)p);
    b->next = hp->first;
    hp->first = b;

    return tmp;*/
}
static inline void xfree(void* p, vproc *vp) {
    free(p);
    /*heapblk *hp = (heapblk*)vp->heap;
    freeblk *b = (freeblk*)p;
    b->len = *((usize*)p);
    b->next = hp->first;
    hp->first = b;*/
}
/** ============================ arena allocator ============================ **/

void init_arena(arena *a, psize len, vproc *vp) {
    a->beg = xmalloc(len, vp);
    a->end = a->beg + len;
    a->head = a->beg;
}

void cleanup_arena(arena *a, vproc *vp) {
    xfree(a->beg, vp);
}

static inline void *grow_arena(arena *a) {
    //TODO: something smarter than crashing
    fprintf(stderr, "arena ran out of space!\n");
    exit(1);
    /*psize new_size = 2*(a->end - a->beg);
    psize head_off = a->head - a->beg;
    if (new_size >= SIZE_MAX/4 || (a->beg = realloc(a->beg, new_size)) == NULL)
	fprintf(stderr, "cannot allocate %ld bytes for arena\n", new_size);
	exit(1);
    }
    a->end = a->beg + new_size;
    a->head = a->beg + head_off;*/
}

static inline void aappend(arena *a, char c) {
    //make sure the arena has enough room and append
    if (a->head == a->end)
	grow_arena(a);
    *(char*)(a->head)++ = c;
}

void *alloc(arena *a, psize size, psize count, psize align) {
    psize padding = padfor((psize)(a->head), align);
    psize avail = a->end - a->head - padding;
    //try reallocating if we ran out of space
    if (avail < 0 || count*size > avail)
	grow_arena(a);
    //align the return value and increment the head
    void *p = a->head + padding;
    a->head = p + count*size;
    return p;
}

void dealloc(arena *a, psize size) {
    void *tmp = a->head - size;
    if (tmp >= a->beg)
	a->head = tmp;
}

void reset(arena *a) {
    a->head = a->beg;
}

/** ============================ spcl_fstream ============================ **/

/**
 * Grow the cache used for the fstream fs by attempting to double its size
 * returns: 0 on failure or 1 on success
 */
static inline int grow_fstream(spcl_fstream* fs) {
    //detect overflows
    if (fs->clen > PSIZE_MAX >> 2)
	return 0;
    //reallocate and check for success
    char* tmp_cache = realloc(fs->cache, 2*fs->clen);
    if (!tmp_cache)
	return 0;
    //if successful then adjust the size and the cache
    fs->clen *= 2;
    fs->cache = tmp_cache;
    return 1;
}
static inline spcl_fstream* alloc_fstream(psize hint) {
    spcl_fstream* fs = malloc(sizeof(spcl_fstream));
    memset(fs, 0, sizeof(spcl_fstream));
    //set the cache to have hint bytes if applicable
    if (hint > 0) {
	char* tmp = calloc(hint, sizeof(char));
	if (!tmp)
	    return fs;
	fs->cache = tmp;
	fs->clen = hint;
    }
    return fs;
}
spcl_fstream *make_spcl_fstream_str(const char *str, size_t n) {
    spcl_fstream* fs = alloc_fstream(n);
    fs->cache = malloc(n);
    if (!fs->cache) {
	free(fs);
	return NULL;
    }
    fs->f = NULL;
    fs->cst = 0;
    fs->flen = n;
    fs->clen = n;
    memcpy(fs->cache, str, n);
    return fs;
}
spcl_fstream *make_spcl_fstreamn(const char *p_fname, size_t n) {
    if (!p_fname)
	return alloc_fstream(0);

    //this is so fucking dumb, i hate null-terminated strings
    char* tmp_fname = strndup(p_fname, n);
    FILE* fp = fopen(tmp_fname, "r");
    free(tmp_fname);
    if (fp) {
	//allocate memory for the stream and initialize to be empty
	spcl_fstream* fs = alloc_fstream(SPCL_STR_BSIZE);
	fs->f = fp;
	//figure out the length of the file
	fseek(fp, 0, SEEK_END);
	fs->flen = ftell(fs->f);
	fseek(fp, 0, SEEK_SET);
	//TODO: reading the entire file into memory is dumb, we should stream from it instead
	psize j = 0;
	int res = fgetc(fp);
	while (1) {
	    //if we reached the end of the buffer, try growing and return NULL if that fails
	    if (j == fs->clen) {
		if (!grow_fstream(fs)) {
		    destroy_spcl_fstream(fs);
		    return NULL;
		}
	    }
	    //NULL terminate and exit at the end of the file
	    if (res == EOF) {
		fs->cache[j] = 0;
		return fs;
	    }
	    //otherwise, append
	    fs->cache[j++] = (char)res;
	    res = fgetc(fp);
	}
	return fs;
    }
    return NULL;
}
void destroy_spcl_fstream(spcl_fstream *fs) {
    if (!fs)
	return;
    if (fs->cache)
	free(fs->cache);
    if (fs->f)
	fclose(fs->f);
    free(fs);
}
psize fs_find_line(const spcl_fstream* fs, psize s) {
    psize ret = 0;
    for (psize i = 0; i < s; ++i) {
	if (fs_get(fs, i) == '\n')
	    ++ret;
    }
    return ret;
}
psize fs_line_end(const spcl_fstream* fs, psize s) {
    while (s < fs->flen && fs_get(fs, s) != '\n')
	++s;
    return s;
}
//Below are protected functions in fstream. They are not intended to be used by external libraries.
spcl_local s8 fs_read(const spcl_fstream* fs, psize s, psize e) {
    if (s >= fs->clen)
	return (s8){NULL, 0};
    if (e >= fs->clen)
	e = fs->clen;
    return (s8){fs->cache+s, e-s};
}

/**
 * Helper function which jumps to the first non-whitespace character after rs.
 * fs: the filestream to use
 * s: the index to start from
 * e: ensure that s does not read past this value. Set this value to be negative to indicate reading to the end of the file
 * force: if non-zero, then the returned value is guaranteed to move rs.start by at least one character. For instance, if you know that the rs.start is on an open parentheses and you want to act on the enclosed string, set this to one
 * returns: the index of the first non-whitespace character after s before e
 */
static inline psize skip_ws(const spcl_fstream* fs, psize s, psize e, int force) {
    if (force)
	++s;
    //ensure that we don't read past the end of the file
    if (e < s || e > fs->flen)
	e = fs->flen;
    while (is_whitespace(fs_get(fs, s)) && s < e)
	++s;
    return s;
}

/** ======================================================== spcl_fn_call ======================================================== **/

void cleanup_spcl_fn_call(spcl_fn_call* f) {
    if (f) {
	for (size_t i = 0; i < f->n_args; ++i)
	    cleanup_spcl_val(f->args+i, NULL);
    }
}

/** ======================================================== name_val_pair ======================================================== **/

void cleanup_name_val_pair(name_val_pair nv) {
    if (nv.s.s)
	free(nv.s.s);
    cleanup_spcl_val(&nv.v, NULL);
}

/** ======================================================== builtin functions ======================================================== **/
spcl_val get_sigerr(spcl_fn_call f, size_t min_args, size_t max_args, const valtype* sig, vproc *vp) {
    if (!sig || max_args < min_args)
	return spcl_make_none();
    if (f.n_args < min_args)
	return spcl_make_err(E_LACK_TOKENS, vp, "%.*s expected %lu arguments, got %lu", f.name.n, f.name.s, min_args, f.n_args);
    if (f.n_args > max_args)
	return spcl_make_err(E_LACK_TOKENS, vp, "%.*s with too many arguments, %lu", f.name.n, f.name.s, f.n_args);
    for (size_t i = 0; i < f.n_args; ++i) {
	//treat undefined as allowing for arbitrary type
	if (sig[i] && f.args[i].type != sig[i]) {
	    //if the type is an error, let it pass through
	    if (f.args[i].type == VAL_ERR)
		return f.args[i];
	    return spcl_make_err(E_BAD_TYPE, vp, "%.*s expected args[%lu].type=%s, got %s", f.name.n, f.name.s, i, valnames[sig[i]], valnames[f.args[i].type]);
	}
	if (sig[i] > VAL_NUM && f.args[i].val.s == NULL)
	    return spcl_make_err(E_BAD_TYPE, vp, "%.*s found empty %s at args[%lu]", f.name.n, f.name.s, valnames[sig[i]], i);
    }
    return spcl_make_none();
}
static const valtype ANY1_SIG[] = {VAL_UNDEF};
static const valtype NUM1_SIG[] = {VAL_NUM};
static const valtype ARR1_SIG[] = {VAL_ARRAY};
spcl_val spcl_assert(spcl_fn_call f, vproc *vp) {
    static const valtype ASSERT_SIG[] = {VAL_UNDEF, VAL_STR};
    spcl_sigcheck_opts(f, 1, ASSERT_SIG, vp);
    if (f.args[0].val.x == 0)
	return (f.n_args == 1)? spcl_make_err(E_ASSERT, vp, "") : spcl_make_err(E_ASSERT, vp, "%s", f.args[1].val.s);
    return spcl_make_num(f.args[0].val.x);
}

spcl_val spcl_typeof(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, ANY1_SIG, vp);
    spcl_val sto = (spcl_val){0};
    sto.type = VAL_STR;
    //handle instances as a special case
    if (f.args[0].type == VAL_INST) {
	spcl_val t = spcl_find(f.args[0].val.c, s8("__type__"));
	if (t.type == VAL_STR)
	    return t;
	return spcl_make_none();
    }
    sto.n_els = strlen(valnames[f.args[0].type])+1;
    sto.val.s = valnames[f.args[0].type];
    return sto;
}
spcl_val spcl_len(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, ANY1_SIG, vp);
    return spcl_make_num(f.args[0].n_els);
}
//create a list with undefined elements
static const valtype LIST_SIG[] = {VAL_NUM};
spcl_val spcl_list(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, LIST_SIG, vp);
    if (f.args[0].val.x < 0)
	return spcl_make_err(E_OUT_OF_RANGE, vp, "cannot create list with negative number of elements");
    spcl_val ret;
    ret.type = VAL_LIST;
    ret.n_els = (size_t)(f.args[0].val.x);
    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els, vp);
    memset(ret.val.l, 0, sizeof(spcl_val)*ret.n_els);
    return ret;
}
static const valtype RANGE_SIG[] = {VAL_NUM, VAL_NUM, VAL_NUM};
spcl_val spcl_range(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck_opts(f, 1, RANGE_SIG, vp);
    double min, max, inc;
    //interpret arguments depending on how many were provided
    if (f.n_args == 1) {
	min = 0;
	max = f.args[0].val.x;
	inc = 1;
    } else {
	min = f.args[0].val.x;
	max = f.args[1].val.x;
	inc = 1;
    }
    if (f.n_args >= 3)
	inc = f.args[2].val.x;
    //make sure arguments are valid
    if ((max-min)*inc <= 0)
	return spcl_make_err(E_BAD_VALUE, vp, "range(%f, %f, %f) with invalid increment", min, max, inc);
    spcl_val ret;
    ret.type = VAL_ARRAY;
    ret.n_els = (max - min) / inc;
    ret.val.a = xmalloc(sizeof(double)*ret.n_els, vp);
    for (size_t i = 0; i < ret.n_els; ++i)
	ret.val.a[i] = i*inc + min;
    return ret;
}
static const valtype LINSPACE_SIG[] = {VAL_NUM, VAL_NUM, VAL_NUM};
spcl_val spcl_linspace(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, LINSPACE_SIG, vp);
    spcl_val ret;
    ret.type = VAL_ARRAY;
    ret.n_els = (size_t)(f.args[2].val.x);
    //prevent divisions by zero
    if (ret.n_els < 2)
	return spcl_make_err(E_BAD_VALUE, vp, "cannot make linspace with size %lu", ret.n_els);
    ret.val.a = xmalloc(sizeof(double)*ret.n_els, vp);
    double step = (f.args[1].val.x - f.args[0].val.x)/(ret.n_els - 1);
    for (size_t i = 0; i < ret.n_els; ++i) {
	ret.val.a[i] = step*i + f.args[0].val.x;
    }
    return ret;
}
typedef struct {spcl_val v;size_t i;} svi;
STACK_DEF(spcl_val,LST_MAX)
STACK_DEF(size_t,LST_MAX)
static const valtype FLATTEN_SIG[] = {VAL_LIST};
spcl_val spcl_flatten(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, FLATTEN_SIG, vp);
    spcl_val ret = spcl_make_none();
    spcl_val cur_list = f.args[0];
    //flattening an empty list is the identity op.
    if (cur_list.n_els == 0 || cur_list.val.l == NULL) {
	ret.type = VAL_LIST;
	return ret;
    }
    size_t cur_st = 0;
    //there may potentially be nested lists, we need to be able to find our way back to the parent and the index once we're done
    void *stk = vp->a.head;
    svi *st_start = anew(&vp->a, svi);
    *st_start = (svi){cur_list, 0};

    //this is used for estimating the size of the buffer we need. Take however many elements were needed for this list and assume each sub-list has the same number of elements
    size_t base_n_els = cur_list.n_els;
    //start with the number of elements in the lowest order of the list
    size_t buf_size = cur_list.n_els;
    ret.val.l = xmalloc(sizeof(spcl_val)*buf_size, vp);
    size_t j = 0;
    do {
	size_t i = cur_st;
	size_t start_depth = (svi*)(vp->a.head) - st_start;
	for (; i < cur_list.n_els; ++i) {
	    if (cur_list.val.l[i].type == VAL_LIST) {
		*anew(&vp->a, svi) = (svi){cur_list, i+1};
		cur_list = cur_list.val.l[i];
		cur_st = 0;
		break;
	    }
	    if (j >= buf_size) {
		//-1 since we already have at least one element. no base_n_els=0 check is needed since that case will ensure the for loop is never evaluated
		buf_size += (base_n_els-1)*(i+1);
		spcl_val* tmp_val = xrealloc(ret.val.l, sizeof(spcl_val)*buf_size, vp);
		if (!tmp_val) {
		    xfree(ret.val.l, vp);
		    cleanup_spcl_fn_call(&f);
		    exit(1);
		    /*destroy_stack(spcl_val,LST_MAX)(&lists, &cleanup_spcl_val);
		    return spcl_make_err(E_NOMEM, "");*/
		}
		ret.val.l = tmp_val;
	    }
	    ret.val.l[j++] = copy_spcl_val(cur_list.val.l[i], vp);
	}
	//if we reached the end of a list without any sublists then we should return back to the parent list
	if ((svi*)(vp->a.head) - st_start <= start_depth) {
	    vp->a.head -= sizeof(svi);
	    svi tmp = *((svi*)vp->a.head);
	    cur_list = tmp.v;
	    cur_st = tmp.i;
	}
    } while (vp->a.head < st_start);
    //reset the arena
    vp->a.head = stk;
    ret.type = VAL_LIST;
    ret.n_els = j;
    return ret;
}
spcl_val spcl_cat(spcl_fn_call f, vproc *vp) {
    spcl_val sto;
    if (f.n_args < 2)
	return spcl_make_err(E_LACK_TOKENS, vp, "cat() expected 2 arguments but got %lu", f.n_args);
    spcl_val l = f.args[0];
    spcl_val r = f.args[1];
    size_t l1 = l.n_els;
    size_t l2 = (r.type == VAL_LIST || r.type == VAL_ARRAY)? r.n_els : 1;
    //special case for matrices, just append a new row
    if (l.type == VAL_MAT && r.type == VAL_ARRAY) {
	sto.type = VAL_MAT;
	sto.n_els = l1 + 1;
	sto.val.l = xmalloc(sizeof(spcl_val)*sto.n_els, vp);
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(l.val.l[i], vp);
	sto.val.l[l1] = copy_spcl_val(r, vp);
	return sto;
    }
    //otherwise we have to do something else
    if (l.type != VAL_LIST && r.type != VAL_ARRAY)
	return spcl_make_err(E_BAD_TYPE, vp, "called cat() with types <%s> <%s>", valnames[l.type], valnames[r.type]);
    sto.type = l.type;
    sto.n_els = l1 + l2;
    //deep copy the first list/array
    if (sto.type == VAL_LIST) {
	sto.val.l = xmalloc(sizeof(spcl_val)*sto.n_els, vp);
	if (!sto.val.l) return spcl_make_err(E_NOMEM, vp, "");
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(l.val.l[i], vp);
	if (r.type == VAL_LIST) {
	    //list -> list
	    for (size_t i = 0; i < l2; ++i)
		sto.val.l[i+l1] = copy_spcl_val(r.val.l[i], vp);
	} else if (r.type == VAL_ARRAY) {
	    //array -> list
	    for (size_t i = 0; i < l2; ++i)
		sto.val.l[i+l1] = spcl_make_num(r.val.a[i]);
	} else {
	    //anything -> list
	    sto.val.l[l1] = copy_spcl_val(r, vp);
	}
    } else {
	sto.val.a = xmalloc(sizeof(double)*sto.n_els, vp);
	if (!sto.val.a) return spcl_make_err(E_NOMEM, vp, "");
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(f.args[0].val.l[i], vp);
	if (r.type == VAL_LIST) {
	    //list -> array
	    for (size_t i = 0; i < l2; ++i) {
		if (r.val.l[i].type != VAL_NUM) {
		    xfree(sto.val.a, vp);
		    return spcl_make_err(E_BAD_TYPE, vp, "can only concatenate numeric lists to arrays");
		}
		sto.val.a[i+l1] = r.val.l[i].val.x;
	    }
	} else if (r.type == VAL_ARRAY) {
	    //array -> array
	    for (size_t i = 0; i < l2; ++i)
		sto.val.a[i+l1] = r.val.a[i];
	} else if (r.type == VAL_NUM) {
	    //number -> array
	    sto.val.a[l1] = r.val.x;
	} else {
	    return spcl_make_err(E_BAD_TYPE, vp, "called cat() with types <%s> <%s>", valnames[l.type], valnames[r.type]);
	}
    }
    return sto;
}
/**
 * print the elements to the console
 */
spcl_val spcl_print(spcl_fn_call f, vproc *vp) {
    spcl_val ret = spcl_make_none();
    char buf[SPCL_STR_BSIZE];
    //TODO: allow writing to other files, by allowing a file pointer to be the first argument
    if (f.n_args < 1)
	return spcl_make_none();
    size_t j = 1;
    if (f.args[0].type == VAL_STR) {
	for (int i = 0; i < f.args[0].n_els; ++i) {
	    char* c = f.args[0].val.s + i;
	    if (*c == '\\') {
		switch (c[1]) {
		    case 't': fputc('\t', stdout);break;
		    case 'n': fputc('\n', stdout);break;
		    case '\\': fputc('\\', stdout);break;
		    case '%': fputc('%', stdout);break;
		    case '\"': fputc('\"', stdout);break;
		    case '\'': fputc('\'', stdout);break;
		    default: return spcl_make_err(E_BAD_SYNTAX, vp, "unrecognized escape sequence %c%c", c[0], c[1]);
		}
		++i;
	    } else if (*c == '%') {
		if (j >= f.n_args)
		    return spcl_make_err(E_LACK_TOKENS, vp, "too few tokens for format string");
		spcl_stringify(f.args[j++], buf, SPCL_STR_BSIZE);
		fputs(buf, stdout);
	    } else {
		fputc(*c, stdout);
	    }
	}
    }
    for (; j < f.n_args; ++j) {
	spcl_stringify(f.args[j], buf, SPCL_STR_BSIZE);
	fputs(buf, stdout);
    }
    return ret;
}
/**
 * Make a vector argument with the x,y, and z coordinates supplied
 */
static const valtype ARRAY_SIG[] = {VAL_LIST};
spcl_val spcl_array(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(f, ARRAY_SIG, vp);
    //treat matrices with one row as vectors
    if (f.n_args == 1) {
	if (f.args[0].val.l[0].type == VAL_LIST)
	    return spcl_cast(f.args[0], VAL_MAT, vp);
	else
	    return spcl_cast(f.args[0], VAL_ARRAY, vp);
    }
    spcl_val ret;
    //otherwise we need to do more work
    size_t n_cols = f.args[0].n_els;
    ret.type = VAL_MAT;
    ret.n_els = f.n_args;
    ret.val.l = xmalloc(sizeof(spcl_val)*f.n_args, vp);
    //iterate through rows
    for (size_t i = 0; i < f.n_args; ++i) {
	if (f.args[i].type == VAL_LIST) {
	    xfree(ret.val.l, vp);
	    return spcl_make_err(E_BAD_TYPE, vp, "non list encountered in matrix");
	}
	if (f.args[i].n_els != n_cols) {
	    xfree(ret.val.l, vp);
	    return spcl_make_err(E_BAD_VALUE, vp, "can't create matrix from ragged array");
	}
	ret.val.l[i] = spcl_cast(f.args[i], VAL_ARRAY, vp);
	//check for errors
	if (ret.val.l[i].type == VAL_ERR) {
	    xfree(ret.val.l, vp);
	    ret = copy_spcl_val(ret.val.l[i], vp);
	    return ret;
	}
    }
    return ret;
}

spcl_val spcl_vec(spcl_fn_call f, vproc *vp) {
    spcl_val ret = spcl_make_none();
    //just copy the elements
    ret.type = VAL_ARRAY;
    ret.n_els = f.n_args;
    //skip copying an empty list
    if (ret.n_els == 0)
	return ret;
    ret.val.a = xmalloc(sizeof(double)*ret.n_els, vp);
    for (size_t i = 0; i < f.n_args; ++i) {
	if (f.args[i].type != VAL_NUM) {
	    xfree(ret.val.a, vp);
	    return spcl_make_err(E_BAD_TYPE, vp, "cannot cast list with non-numeric types to array");
	}
	ret.val.a[i] = f.args[i].val.x;
    }
    return ret;
}

//math functions
/**
 * Wrap a mathematical function that takes a single floating point argument
 * FN: the function to wrap
 */
#define WRAP_MATH_FN(FN) spcl_val TYPED(spcl,FN)(spcl_fn_call f, vproc *vp) {		\
    spcl_val sto = get_sigerr(f, SIGLEN(NUM1_SIG), SIGLEN(NUM1_SIG), NUM1_SIG, vp);	\
    if (sto.type == 0)									\
	return spcl_make_num( FN(f.args[0].val.x) );					\
    cleanup_spcl_val(&sto, vp);								\
    sto = get_sigerr(f, SIGLEN(ARR1_SIG), SIGLEN(ARR1_SIG), ARR1_SIG, vp);		\
    if (sto.type == 0) {								\
	sto.type = VAL_ARRAY;								\
	sto.n_els = f.args[0].n_els;							\
	sto.val.a = xmalloc(sizeof(double)*sto.n_els, vp);				\
	if (!sto.val.a)									\
	    return spcl_make_err(E_NOMEM, vp, "");					\
	for (size_t i = 0; i < f.args[0].n_els; ++i)					\
	    sto.val.a[i] = FN(f.args[0].val.a[i]);					\
    }											\
    return sto;										\
}
WRAP_MATH_FN(sin)
WRAP_MATH_FN(cos)
WRAP_MATH_FN(tan)
WRAP_MATH_FN(exp)
WRAP_MATH_FN(asin)
WRAP_MATH_FN(acos)
WRAP_MATH_FN(atan)
WRAP_MATH_FN(log)
WRAP_MATH_FN(sqrt)
WRAP_MATH_FN(floor)
WRAP_MATH_FN(ceil)
WRAP_MATH_FN(fabs)
#define lcmp(c,l) ((c|0x20)==l) //macro that compares the character c against the lowercase letter l and returns whether they are equal ignoring case
#define read_base(s, n) ( (n < 2 || s[0] != 0)? 10 : lcmp(s[1],'b')? 2 : lcmp(s[1],'o')? 8 : lcmp(s[1],'x')? 16 : 10 )
/**
 * Convert the string s to a double precision floating point value and return the answer.
 * f.args[0]: a string to be converted
 * f.args[1] (optional): the base to use when converting
 * returns: 0 on success or 1 if an invalid string was detected
 */
spcl_val spcl_strtod(spcl_fn_call f, vproc *vp) {
    static const valtype STRTOD_SIG[] = {VAL_STR, VAL_NUM};
    spcl_sigcheck_opts(f, 1, STRTOD_SIG, vp);

    //create aliases so we have less typing
    char* s = f.args[0].val.s;
    size_t n = f.args[0].n_els;
    //figure out the base and stuff
    int base = read_base(f.args[0].val.s, f.args[0].n_els);
    double x = 0, sgn = 1;
    int buf = 0, dloc = n;
    for (psize i = 0; i < n; ++i) {
	//handle signed numbers
	if (i == 0 && s[i] == '-') {
	    sgn = -1;
	    continue;
	}
	if (s[i] >= '0' && s[i] <= '9' && (s[i]-'0')<base) {
	    buf = buf*base + (s[i]-'0');
	} else if (base > 10 && (s[i]|0x20) >= 'a' && (s[i]|0x20) <= 'a'+base-9) {
	    buf = buf*base + ((s[i]|0x20) - 'a') + 10;
	} else if (s[i] == '.' && dloc == n) {
	    //store the location of the dot, save the integer part of x to buf and reset
	    dloc = i;
	    x = buf;
	    buf = 0;
	} else if (base < 14 && (s[i]|0x20) == 'e') {
	    //check for scientific notation
	    if (dloc < n) {
		x += buf*pow(base, dloc-n+1);
		dloc = n;
	    }
	    //this is a cheap hack to read the exponent
	    f.args[0].val.s = f.args[0].val.s+i+1;
	    spcl_val exp = spcl_strtod(f, vp);
	    if (exp.type != VAL_NUM)
		return exp;
	    return spcl_make_num( sgn*x*pow(base, exp.val.x) );
	} else {
	    return spcl_make_err(E_BAD_SYNTAX, vp, "invalid numeric literal");
	}
    }
    if (dloc < n)
	x += buf*pow(base, dloc-n+1);
    return spcl_make_num(sgn*x);
}

/** ============================ struct spcl_val ============================ **/

spcl_val spcl_make_none() {
    spcl_val v;
    v.type = VAL_UNDEF;
    v.n_els = 0;
    v.val.x = 0;
    return v;
}

spcl_val spcl_make_err(parse_ercode code, vproc *vp, const char* format, ...) {
    spcl_val ret;
    ret.type = VAL_ERR;
    //a nomemory error obviously won't be able to allocate any more memory
    if (code == E_NOMEM) {
	fprintf(stderr, "kernel panic: out of memory!\n");
	ret.val.e = NULL;
	return ret;
    }
    ret.val.e = xmalloc(sizeof(struct spcl_error), vp);
    ret.val.e->c = code;
    va_list args;
    va_start(args, format);
    int tmp = vsnprintf(ret.val.e->msg, ERR_BSIZE, format, args);
    va_end(args);
    ret.n_els = (tmp < 0)? ERR_BSIZE : (size_t)tmp;
    return ret;
}

spcl_val spcl_make_int(int i) {
    spcl_val v;
    v.type = VAL_INT;
    v.n_els = 1;
    v.val.i = i;
    return v;
}
spcl_val spcl_make_num(double x) {
    spcl_val v;
    v.type = VAL_NUM;
    v.n_els = 1;
    v.val.x = x;
    return v;
}

spcl_val spcl_make_str(const char* s, psize n, vproc *vp) {
    spcl_val v;
    v.type = VAL_STR;
    if (n < 0) {
	v.val.s = s;
	v.n_els = 0;
	return v;
    }
    v.n_els = n;
    //we allocate one more than the actual length to null terminate
    v.val.s = xmalloc(sizeof(char)*(v.n_els+1), vp);
    if (s)
	memcpy(v.val.s, s, n);
    v.val.s[n] = 0;
    return v;
}
spcl_val spcl_make_array(double* vs, size_t n, vproc *vp) {
    spcl_val v;
    v.type = VAL_ARRAY;
    v.n_els = n;
    v.val.a = xmalloc(sizeof(double)*v.n_els, vp);
    if (vs)
	memcpy(v.val.a, vs, sizeof(double)*n);
    return v;
}
spcl_val spcl_make_list(const spcl_val* vs, size_t n_vs, vproc *vp) {
    spcl_val v;
    v.type = VAL_LIST;
    v.n_els = n_vs;
    v.val.l = xmalloc(sizeof(spcl_val)*v.n_els, vp);
    if (!vs)
	return v;
    for (size_t i = 0; i < v.n_els; ++i) v.val.l[i] = copy_spcl_val(vs[i], vp);
    return v;
}
spcl_val spcl_make_fn(const char* name, psize n_ret, lib_call p_exec, vproc *vp) {
    spcl_val ret;
    ret.type = VAL_FN;
    ret.n_els = n_ret;
    ret.val.f = make_spcl_uf_ex(p_exec, vp);
    return ret;
}
spcl_val spcl_make_inst(spcl_inst* parent, const char* s, vproc *vp) {
    spcl_val v;
    v.type = VAL_INST;
    v.val.c = make_spcl_inst(parent, vp);
    if (s && s[0] != 0) {
	spcl_val tmp = spcl_make_str(s, strlen(s), vp);
	spcl_set_sub_val(v.val.c, "__type__", tmp, 0, vp);
    }
    return v;
}
spcl_val copy_spcl_val(spcl_val o, vproc *vp) {
    spcl_val ret;
    ret.type = o.type;
    ret.n_els = o.n_els;
    //strings or lists must be copied
    switch (o.type) {
	case VAL_ERR:
	    ret.val.e = xmalloc(sizeof(spcl_error), vp);
	    memcpy(ret.val.e, o.val.e, sizeof(spcl_error));
	break;
	//case VAL_STR:	ret.val.s = xmalloc(o.n_els); strncpy(ret.val.s, o.val.s, o.n_els); break;
	case VAL_STR:
	    ret.val.s = xmalloc(o.n_els, vp);
	    memcpy(ret.val.s, o.val.s, o.n_els);
	break;
	case VAL_ARRAY:
	    ret.val.a = xmalloc(sizeof(double)*o.n_els, vp);
	    memcpy(ret.val.a, o.val.a, sizeof(double)*o.n_els);
	break;
	case VAL_LIST:
	    ret.val.l = xmalloc(sizeof(spcl_val)*o.n_els, vp);
	    for (size_t i = 0; i < o.n_els; ++i)
		ret.val.l[i] = copy_spcl_val(o.val.l[i], vp);
	break;
	case VAL_MAT:
	    ret.val.l = xmalloc(sizeof(spcl_val)*o.n_els, vp);
	    for (size_t i = 0; i < o.n_els; ++i)
		ret.val.l[i] = copy_spcl_val(o.val.l[i], vp);
	break;
	//TODO: these can only be implemented after we get rid of copy_spcl_val
	case VAL_INST:
	    ret.val.c = copy_spcl_inst(o.val.c, vp);
	break;
	case VAL_FN:
	    ret.val.f = copy_spcl_uf(o.val.f, vp);
	break;
	default:
	    ret.val.x = o.val.x;
	break;
    }
    return ret;
}
spcl_val spcl_valcmp(spcl_val a, spcl_val b, vproc *vp) {
    if (a.type == VAL_INT && b.type == VAL_INT)
	return spcl_make_int( a.val.i - b.val.i );
    else if (a.type == VAL_INT && b.type == VAL_NUM)
	return spcl_make_int( a.val.i - (int)(b.val.x) );
    else if (a.type == VAL_NUM && b.type == VAL_INT)
	return spcl_make_int( (int)(a.val.x) - b.val.i );

    if (a.type != b.type || a.type == VAL_ERR || b.type == VAL_ERR)
	return spcl_make_err(E_BAD_VALUE, vp, "cannot compare types %s and %s", valnames[a.type], valnames[b.type]);
    if (a.type == VAL_NUM) {
	return spcl_make_int((int)a.val.x - (int)b.val.x);
    } else if (a.type == VAL_STR) {
	return spcl_make_int(spcl_strcmp(a, b));
    } else if (a.type == VAL_LIST) {
	if (a.n_els != b.n_els)
	    return spcl_make_int(a.n_els - b.n_els);
	spcl_val tmp;
	for (size_t i = 0; i < a.n_els; ++i) {
	    tmp = spcl_valcmp(a.val.l[i], b.val.l[i], vp);
	    if (tmp.val.i)
		return tmp;
	}
	return spcl_make_int(0);
    } else if (a.type == VAL_ARRAY) {
	if (a.n_els != b.n_els)
	    return spcl_make_int(a.n_els - b.n_els);
	for (size_t i = 0; i < a.n_els; ++i) {
	    if (a.val.a[i] != b.val.a[i])
		return spcl_make_int(a.val.a[i] - b.val.a[i]);
	}
	return spcl_make_int(0);
    }
    return spcl_make_none();
}

int spcl_val_str_cmp(spcl_val a, const char* b) {
    if (a.type != VAL_STR)
	return 0;
    return strcmp(a.val.s, b);
}
static inline size_t spcl_est_strlen(spcl_val v) {
    switch (v.type) {
	case VAL_UNDEF: return strlen("none");
	case VAL_ERR:	return v.n_els;
	case VAL_NUM:	return MAX_NUM_SIZE;
	case VAL_STR:	return v.n_els;
	case VAL_ARRAY: return MAX_NUM_SIZE + 2*v.n_els + 3;
	case VAL_LIST:  size_t ret = 2*v.n_els + 3;
			for (size_t i = 0; i < v.n_els; ++i)
			    ret += spcl_est_strlen(v.val.l[i]);
			return ret;
	default:	return strlen("<undefined at 0xffffffffffff>")+2;
    }
}
char* spcl_stringify(spcl_val v, char* buf, size_t n) {
    if (!buf || n < 3)
	return buf;
    //exit if there isn't enough space to write the null terminator
    if (v.type == VAL_STR) {
	//copy at most n_els
	if (v.n_els < n)
	    n = v.n_els;
	return stpncpy(buf, v.val.s, n);
    } else if (v.type == VAL_ARRAY) {
	size_t off = 1;
	buf[0] = BEG_CRL;//}
	for (size_t i = 0; i < v.n_els; ++i) {
	    size_t rem = n-off;
	    if (rem > MAX_NUM_SIZE)
		rem = MAX_NUM_SIZE;
	    int tmp = write_numeric(buf+off, rem, v.val.a[i]);
	    if (tmp < 0) {
		buf[off] = 0;
		return buf+off;
	    }
	    if (tmp >= rem) {
		buf[n-1] = 0;
		return buf+n-1;
	    }
	    off += (size_t)tmp;
	    if (i+1 < v.n_els)
		buf[off++] = ',';
	}
	if (off >= 0 && off < n)
	    buf[off++] = END_CRL;
	buf[off] = 0;
	return buf+off;
    } else if (v.type == VAL_NUM) {
	if (n > MAX_NUM_SIZE)
	    n = MAX_NUM_SIZE;
	int tmp = write_numeric(buf, n, v.val.x);
	if (tmp < 0) {
	    buf[0] = 0;
	    return buf;
	}
	return buf+(size_t)tmp;
    } else if (v.type == VAL_LIST) {
	char* cur = buf+1;
	buf[0] = BEG_SQR;
	size_t elsize = (v.n_els >= MAX_PRINT_ELS)? n/MAX_PRINT_ELS - 5: n/v.n_els - 2;
	if (elsize < 3) {
	    return stpncpy(buf, "[...]", strlen("[...]"));
	}
	for (size_t i = 0; i < v.n_els; ++i) {
	    cur = spcl_stringify(v.val.l[i], cur, elsize);
	    if (i+1 < v.n_els)
		*cur++ = ',';
	    if (i == MAX_PRINT_ELS)
		return stpncpy(cur, "...]", strlen("...]"));
	}
	*cur++ = END_SQR;
	return cur;
    } else if (v.type < N_VALTYPES) { 
	int tmp = snprintf(buf, n, "<%s at %p>", valnames[v.type], v.val.s);
	return buf+tmp;
    }
    int tmp = snprintf(buf, n, "<unknown at %p>", v.val.s);
    return buf+tmp;
}

spcl_val spcl_cast(spcl_val v, valtype t, vproc *vp) {
    if (v.type == VAL_UNDEF)
	return spcl_make_err(E_BAD_TYPE, vp, "cannot cast <undefined> to <%s>", valnames[t]);
    //trivial casts should just be copies
    if (v.type == t)
	return copy_spcl_val(v, vp);
    spcl_val ret;
    ret.type = t;
    ret.n_els = v.n_els;
    if (t == VAL_LIST) {
	if (v.type == VAL_ARRAY) {
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els, vp);
	    for (size_t i = 0; i < ret.n_els; ++i)
		ret.val.l[i] = spcl_make_num(v.val.a[i]);
	    return ret;
	} else if (v.type == VAL_INST) {
	    //instance -> list
	    ret.n_els = v.val.c->n_memb;
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els, vp);
	    memset(ret.val.l, 0, sizeof(spcl_val)*ret.n_els);
	    for (size_t i = con_it_next(v.val.c, 0); i < con_size(v.val.c); i = con_it_next(v.val.c, i+1))
		ret.val.l[i] = copy_spcl_val(v.val.c->table[i].v, vp);
	    ret.n_els = v.n_els;
	    return ret;
	} else if (v.type == VAL_MAT) {
	    //matrices are basically just an alias for lists
	    ret = copy_spcl_val(v, vp);
	    ret.type = t;
	    return ret;
	}
    } else if (t == VAL_MAT) {
	if (v.type == VAL_LIST) {
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els, vp);
	    for (size_t i = 0; i < ret.n_els; ++i) {
		//first try making the element an array
		spcl_val tmp = spcl_cast(v.val.l[i], VAL_ARRAY, vp);
		if (tmp.type == VAL_ERR) {
		    //if that doesn't work try making it a matrix
		    cleanup_spcl_val(&tmp, NULL);
		    tmp = spcl_cast(v.val.l[i], VAL_MAT, vp);
		    if (tmp.type == VAL_ERR) {
			//if both of those failed, give up
			xfree(ret.val.l, vp);
			return tmp;
		    }
		}
		ret.val.l[i] = tmp;
	    }
	    return ret;
	}
    } else if (t == VAL_ARRAY) {
	if (v.type == VAL_LIST) {
	    //list -> array
	    ret.n_els = v.n_els;
	    ret.val.a = xmalloc(sizeof(double)*ret.n_els, vp);
	    for (size_t i = 0; i < ret.n_els; ++i) {
		if (v.val.l[i].type != VAL_NUM) {
		    xfree(ret.val.a, vp);
		    return spcl_make_err(E_BAD_TYPE, vp, "cannot cast list with non-numeric types to array");
		}
		ret.val.a[i] = v.val.l[i].val.x;
	    }
	    return ret;
	}
    } else if (t == VAL_STR) {
	//anything -> string
	ret.val.s = xmalloc(sizeof(char)*SPCL_STR_BSIZE, vp);
	char* end = spcl_stringify(v, ret.val.s, SPCL_STR_BSIZE);
	ret.n_els = (size_t)(end-ret.val.s);
    }
    //if we reach this point in execution then there was an error
    ret.type = VAL_UNDEF;
    ret.n_els = 0;
    return ret;
}

void cleanup_spcl_val(spcl_val *v, vproc *vp) {
    if (v->type == VAL_ERR) {
	xfree(v->val.e, vp);
    } else if ((v->type == VAL_STR && v->val.s) || (v->type == VAL_ARRAY && v->val.a)) {
	xfree(v->val.s, vp);
    } else if ((v->type == VAL_LIST || v->type == VAL_MAT) && v->val.l) {
	for (size_t i = 0; i < v->n_els; ++i)
	    cleanup_spcl_val(v->val.l + i, vp);
	xfree(v->val.l, vp);
    } else if (v->type == VAL_ARRAY && v->val.a) {
	xfree(v->val.a, vp);
    } else if (v->type == VAL_INST && v->val.c) {
	destroy_spcl_inst(v->val.c, vp);
    } else if (v->type == VAL_FN && v->val.f) {
	destroy_spcl_uf(v->val.f, vp);
    }
    v->type = VAL_UNDEF;
    v->val.x = 0;
    v->n_els = 0;
}

/**
 * swap the spcl_vals stored at a and b
 */
void swap_val(spcl_val* a, spcl_val* b) {
    //swap type and number of elements
    valtype tmp = a->type;
    a->type = b->type;
    b->type = tmp;
    size_t tmp_n = a->n_els;
    a->n_els = b->n_els;
    b->n_els = tmp_n;
    union V tmp_v = a->val;
    a->val = b->val;
    b->val = tmp_v;
}

/**
 * handle an error at index i in a matrix
 * returns: whether there was an error
 */
static inline int matrix_err(spcl_val* l, size_t i, vproc *vp) {
    if (i >= l->n_els || l->val.l[i].type == VAL_LIST || l->val.l[i].type == VAL_ARRAY)
	return 0;
    //handle incorrect types
    if (l->val.l[i].type != VAL_ERR) {
	cleanup_spcl_val(l, NULL);
	*l = spcl_make_err(E_BAD_TYPE, vp, "matrix contains type <%s>", valnames[l->val.l[i].type]);
	return 1;
    }
    //move the error to overwrite the list
    spcl_val tmp = l->val.l[i];
    for (size_t j = 0; j < l->n_els; ++j) {
	if (i != j)
	    cleanup_spcl_val(l->val.l + j, NULL);
    }
    xfree(l->val.l, vp);
    *l = tmp;
    return 1;
}
spcl_local void val_add(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_UNDEF && r.type == VAL_NUM) {
	*l = r;
    } else if (l->type == VAL_INT && r.type == VAL_INT) {
	*l = spcl_make_int( (l->val.i)+(r.val.i) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	*l = spcl_make_num( (l->val.x)+(r.val.i) );
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.i)+(r.val.x) );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)+(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot add arrays of length %lu and %lu", l->n_els, r.n_els);
	} else {
	    for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] += r.val.a[i];
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i)
	    l->val.a[i] += r.val.x;
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot add lists of length %lu and %lu", l->n_els, r.n_els);	
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_add(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else if (l->type == VAL_LIST) {
	++l->n_els;
	l->val.l = xrealloc(l->val.l, l->n_els, vp);
	l->val.l[l->n_els-1] = copy_spcl_val(r, vp);
    } else if (l->type == VAL_STR) {
	size_t l_len = l->n_els;
	size_t r_len = spcl_est_strlen(r);
	//create a new string and copy
	l->val.s = xrealloc(l->val.s, l_len+r_len+1, vp); //+1 for null terminator
	char* tmp = spcl_stringify(r, l->val.s+l_len, r_len);
	tmp[0] = 0;
	//now set the spcl_val
	l->n_els = (size_t)(tmp - l->val.s);
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot add types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_sub(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_UNDEF && r.type == VAL_NUM) {
	*l = spcl_make_num(-r.val.x);
    } else if (l->type == VAL_INT && r.type == VAL_INT) {
	*l = spcl_make_int( (l->val.i)-(r.val.i) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	*l = spcl_make_num( (l->val.x)-(r.val.i) );
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.i)-(r.val.x) );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)-(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot subtract arrays of length %lu and %lu", l->n_els, r.n_els);
	} else {
	    for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] -= r.val.a[i];
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i)
	    l->val.a[i] -= r.val.x;
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot subtract lists of length %lu and %lu", l->n_els, r.n_els);	
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_sub(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot subtract types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_mul(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_INT && r.type == VAL_INT) {
	*l = spcl_make_int( (l->val.i)*(r.val.i) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	*l = spcl_make_num( (l->val.x)*(r.val.i) );
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.i)*(r.val.x) );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)*(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot multiply arrays of length %lu and %lu", l->n_els, r.n_els);
	    return;
	} else {
	    for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] *= r.val.a[i];
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i)
	    l->val.a[i] *= r.val.x;
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot multiply lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mul(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l,i,vp))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mul(l->val.l+i, r, vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot multiply types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_div(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_INT && r.type == VAL_INT) {
	if (r.val.i == 0) {
	    *l = spcl_make_err(E_BAD_VALUE, vp, "division by zero");
	    return;
	}
	*l = spcl_make_int( (l->val.i)/(r.val.i) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	if (r.val.i == 0) {
	    *l = spcl_make_err(E_BAD_VALUE, vp, "division by zero");
	    return;
	}
	*l = spcl_make_num( (l->val.x)/(r.val.i) );
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.i)/(r.val.x) );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)/(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot divide arrays of length %lu and %lu", l->n_els, r.n_els);
	    return;
	} else {
	    for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] /= r.val.a[i];
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i)
	    l->val.a[i] /= r.val.x;
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot divide lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_div(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l,i,vp))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_div(l->val.l+i, r, vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot divide types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_mod(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_INT && r.type == VAL_INT) {
	*l = spcl_make_int( (l->val.i)%(r.val.i) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	double div = l->val.x / r.val.i;
	l->val.x -= floor(div)*r.val.i;
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	double div = l->val.i / r.val.x;
	*l = spcl_make_num( (double)(l->val.i) - floor(div)*r.val.i );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	double div = l->val.x / r.val.x;
	l->val.x -= floor(div)*r.val.x;
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot divide arrays of length %lu and %lu", l->n_els, r.n_els);
	    return;
	} else {
	    for (size_t i = 0; i < l->n_els; ++i) {
		double div = l->val.a[i] / r.val.a[i];
		l->val.a[i] -= floor(div)*r.val.a[i];
	    }
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    double div = l->val.a[i] / r.val.x;
	    l->val.a[i] -= floor(div)*r.val.x;
	    l->val.a[i] /= r.val.x;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot divide lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mod(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l,i,vp))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mod(l->val.l+i, r, vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot divide types %s and %s", valnames[l->type], valnames[r.type]);
    }
}

spcl_local void val_exp(spcl_val* l, spcl_val r, vproc *vp) {
    if (l->type == VAL_INT && r.type == VAL_INT) {
	*l = spcl_make_int( (int)pow((double)(l->val.i), (double)(r.val.i)) );
    } else if (l->type == VAL_NUM && r.type == VAL_INT) {
	*l = spcl_make_num( pow(l->val.x, r.val.i) );
    } else if (l->type == VAL_INT && r.type == VAL_NUM) {
	*l = spcl_make_num( pow(l->val.i, r.val.x) );
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( pow(l->val.x, r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot raise arrays of length %lu and %lu", l->n_els, r.n_els);
	    return;
	} else {
	    for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] = pow(l->val.a[i], r.val.a[i]);
	}
    } else if (l->type == VAL_ARRAY && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i)
		l->val.a[i] = pow(l->val.a[i], r.val.x);
    } else if (l->type == VAL_MAT && r.type == VAL_MAT) {
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l, vp);
	    *l = spcl_make_err(E_OUT_OF_RANGE, vp, "cannot raise matrices of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_exp(l->val.l+i, r.val.l[i], vp);
	    if (matrix_err(l,i,vp))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_exp(l->val.l+i, r, vp);
	    if (matrix_err(l, i, vp))
		return;
	}
    } else {
	cleanup_spcl_val(l, vp);
	*l = spcl_make_err(E_BAD_TYPE, vp, "cannot raise types %s and %s", valnames[l->type], valnames[r.type]);
    }
}

/** ============================ spcl_inst ============================ **/

//helper to convert possibly negative index spcl_vals to real C indices
static inline size_t index_to_abs(spcl_val* ind, size_t max_n, vproc *vp) {
    if (-(ind->val.x) > max_n || ind->val.x >= max_n) {
	*ind = spcl_make_err(E_OUT_OF_RANGE, vp, "index %d out of bounds for list of size %lu", (int)ind->val.x, max_n);
	return 0;
    }
    if (ind->val.x < 0)
	return max_n - (size_t)(-ind->val.x);
    return (size_t)(ind->val.x);
}

//non-cryptographically hash the string str reading only the first n bytes
static inline size_t fnv_1(s8 str, unsigned char t_bits) {
    if (str.s == NULL || str.n == 0)
	return 0;
    size_t ret = FNV_OFFSET;
    for (size_t i = 0; i < str.n/* && str.s[i]*/; ++i) {
	if (!is_whitespace(str.s[i])) {
	    ret = ret^str.s[i];
	    ret = ret*FNV_PRIME;
	}
    }
    return ((ret >> t_bits) ^ ret) % (1 << t_bits);
    /**TODO: try using this if the above gives poor dispersion
#if TABLE_BITS > 15
    return (ret >> t_bits) ^ (ret & TABLE_MASK(t_bits));
#else
    return ((ret >> t_bits) ^ ret) & TABLE_MASK(t_bits);
#endif
*/
}

/**
 * Get the index i that contains the string name
 * c: the spcl_inst to look in
 * name: the name to look for
 * ind: the location where we find the matching index
 * returns: 1 if a match was found, 0 otherwise
 */
static inline int find_ind(const struct spcl_inst* c, s8 name, psize* ind) {
    size_t ii = fnv_1(name, c->t_bits);
    size_t i = ii;
    while (c->table[i].s.n) {
	//if (s8cmp(name, c->table[i].s) == 0) {
	if (s8eq(name, c->table[i].s)) {
	    *ind = i;
	    return 1;
	}
	//i = (i+1) if i+1 < table_size or 0 otherwise (note table_size == con_size(c))
	i = (i+1) & (con_size(c)-1);
	if (i == ii)
	    break;
    }
    *ind = i;
    return 0;
}

/**
 * Grow the spcl_inst if necessary
 * returns: 1 if growth was performed
 */
static inline int grow_inst(struct spcl_inst* c, vproc *vp) {
    if (c && c->n_memb*GROW_LOAD_DEN > con_size(c)*GROW_LOAD_NUM) {
	//create a new spcl_inst with twice as many elements
	struct spcl_inst nc;
	nc.parent = c->parent;
	nc.t_bits = c->t_bits + 1;
	nc.n_memb = 0;
	nc.table = xmalloc(sizeof(name_val_pair)*con_size(&nc), vp);
	memset(nc.table, 0, sizeof(name_val_pair)*con_size(&nc));
	//we have to rehash every member in the old table
	for (size_t i = 0; i < con_size(c); ++i) {
	    if (c->table[i].s.n == 0)
		continue;
	    //only move non-null members
	    psize new_ind;
	    if (!find_ind(&nc, c->table[i].s, &new_ind))
		++nc.n_memb;
	    nc.table[new_ind] = c->table[i];
	}
	//deallocate old table and replace it with the new one
	xfree(c->table, vp);
	*c = nc;
	return 1;
    }
    return 0;
}
/**
 * include builtin functions
 * TODO: make this not dumb
 */
static inline void setup_builtins(vproc *vp) {
    //create builtins
    /*spcl_set_val(vp, "false",	spcl_make_num(0), 0);
    spcl_set_val(vp, "true",	spcl_make_num(1), 0);//create horrible (if amusing bugs when someone tries to assign to true or false*/
    spcl_add_fn(spcl_assert,	"assert", vp);
    spcl_add_fn(spcl_typeof,	"typeof", vp);
    spcl_add_fn(spcl_len,	"len", vp);
    spcl_add_fn(spcl_list,	"list", vp);
    spcl_add_fn(spcl_range,	"range", vp);
    spcl_add_fn(spcl_linspace,	"linspace", vp);
    spcl_add_fn(spcl_flatten,	"flatten", vp);
    spcl_add_fn(spcl_array,	"array", vp);
    spcl_add_fn(spcl_vec,	"vec", vp);
    spcl_add_fn(spcl_cat,	"cat", vp);
    spcl_add_fn(spcl_print,	"print", vp);
    //TODO: this is a really dumb way of adding namespaces
    //math stuff
    spcl_val tmp = spcl_make_inst(vp->c, "math", vp);
    spcl_inst* math_c = tmp.val.c;
    spcl_set_sub_val(math_c, "pi", 	spcl_make_num(M_PI), 0, vp);
    spcl_set_sub_val(math_c, "e", 	spcl_make_num(M_E), 0, vp);
    spcl_add_sub_fn(math_c, spcl_sin,	"sin", vp);
    spcl_add_sub_fn(math_c, spcl_cos,	"cos", vp);
    spcl_add_sub_fn(math_c, spcl_tan,	"tan", vp);
    spcl_add_sub_fn(math_c, spcl_asin,	"asin", vp);
    spcl_add_sub_fn(math_c, spcl_acos,	"acos", vp);
    spcl_add_sub_fn(math_c, spcl_atan,	"atan", vp);
    spcl_add_sub_fn(math_c, spcl_exp,	"exp", vp);
    spcl_add_sub_fn(math_c, spcl_log,	"log", vp);
    spcl_add_sub_fn(math_c, spcl_sqrt,	"sqrt", vp);
    spcl_add_sub_fn(math_c, spcl_floor,	"floor", vp);
    spcl_add_sub_fn(math_c, spcl_ceil,	"ceil", vp);
    spcl_add_sub_fn(math_c, spcl_fabs,	"abs", vp);
    spcl_set_val("math", tmp, 0, vp);
    tmp = spcl_make_inst(vp->c, "sys", vp);
    spcl_set_val("sys", tmp, 0, vp);
}

struct spcl_inst* make_spcl_inst(spcl_inst* parent, vproc *vp) {
    spcl_inst* c = xmalloc(sizeof(spcl_inst), vp);
    c->parent = parent;
    c->n_memb = 0;
    c->t_bits = DEF_TAB_BITS;
    //double the allocated size for root insts (since they're likely to hold more stuff)
    if (!parent) c->t_bits++;
    c->table = xmalloc(sizeof(name_val_pair)*con_size(c), vp);
    memset(c->table, 0, sizeof(name_val_pair)*con_size(c));
    if (!parent) {
	//setup_builtins(c);
    }
    return c;
}

struct spcl_inst* copy_spcl_inst(const spcl_inst* o, vproc *vp) {
    if (!o)
	return NULL;
    spcl_inst* c = xmalloc(sizeof(spcl_inst), vp);
    c->table = xmalloc(sizeof(name_val_pair)*con_size(o), vp);
    memset(c->table, 0, sizeof(name_val_pair)*con_size(o));
    c->parent = o->parent;
    c->n_memb = o->n_memb;
    c->t_bits = o->t_bits;
    for (size_t i = con_it_next(o, 0); i < con_size(o); i = con_it_next(o, i+1)) {
	c->table[i].s.s = strdup(o->table[i].s.s);
	c->table[i].s.n = o->table[i].s.n;
	c->table[i].v = copy_spcl_val(o->table[i].v, vp);
    }
    return c;
}

void destroy_spcl_inst(struct spcl_inst* c, vproc *vp) {
    if (!c)
	return;
    //erase the hash table
    for (size_t i = con_it_next(c, 0); i < con_size(c); i = con_it_next(c,i+1))
	cleanup_name_val_pair(c->table[i]);
    xfree(c->table, vp);
    xfree(c, vp);
}
/**
 * Identify the keyword starting at rs->start up to rs->end. If a key is found, then rs->start is updated to the first character after the keyword.
 * returns: the spck_key code for the matched key.
 */
spcl_local spcl_key get_keyword(read_state *rs) {
    rs->start = skip_ws(rs->b, rs->start, rs->end, 0);
    //identify keywords. All keywords, except "fn", must come at the start of a parsed value or they are invalid. There is an exception for "fn" since foo = fn(bar) {...} is a valid expression. However, even in this case, "fn" will start the expression after handling the next operator.
    s8 expr = fs_read(rs->b, rs->start, rs->end);
    psize chn = expr.n;
    for (spcl_key i = 1; i < SPCL_N_KEYS; ++i) {
	//only do a comparison if we have enough bytes
	if (chn >= spcl_keywords[i].n) {
	    //before we do a comparison we must use the same number of bytes
	    expr.n = spcl_keywords[i].n;
	    if (s8cmp(expr, spcl_keywords[i]) == 0) {
		rs->start += spcl_keywords[i].n;
		rs->start = skip_ws(rs->b, rs->start, rs->end, 0);
		return i;
	    }
	}
    }
    return KEY_NONE;
}

/**
 * Get the keyword associated with the string str
 */
spcl_local spcl_key get_keyword_str(s8 str) {
    for (int i = 0; i < SPCL_N_KEYS; ++i) {
	if (s8eq(str, spcl_keywords[i]))
	    return i;
    }
    return KEY_NONE;
}

//#define SM_EXIT		0	//finished reading 
//#define SM_EX_LSTART	1	//expect line end
//#define SM_EX_LEND	2	//expect line end
//#define SM_EX_NTUP	3	//expect the next entry in a tuple
//#define SM_EX_RVAL	4	//expect the rvalue after an operator
//#define SM_EX_BPAR	5	//expect an open parentheses
//#define SM_EX_BCRL	6	//expect an open curly brace
//#define SM_IN_PAR	7	//inside a parenthetical expression
//#define SM_IN_BLK	8	//inside a block expression (enclosed by curly braces)
//#define SM_IN_LST	9	//inside a list expression
//#define SM_IN_STR	10	//inside a string literal
//#define SM_IN_NUM	11	//inside a numeric literal
//#define SM_IN_NAME	12	//inside a variable name or keyword
//#define SM_IN_OP	13	//inside a variable name or keyword
typedef enum {SM_EXIT, SM_EX_LSTART, SM_EX_LEND, SM_EX_NTUP, SM_EX_RVAL, SM_EX_BPAR, SM_EX_BCRL, SM_IN_PAR, SM_IN_BLK, SM_IN_LST, SM_IN_STR, SM_IN_NUM, SM_IN_NAME, N_SM_STATES} sm_state;

typedef struct {
    usize *insts;	//the instructions written (allocated on the arena a)
    usize n_written;	//the number of instructions written
    psize line_end;	//the end of the line read by tokenize
} line_tokens;

static inline optree_nd *optree_insert_op(optree_nd *nd, optr_op curop, arena *a) {
    optree_nd *tmp = make_nd(nd, 0, a);
    memcpy(tmp, nd, sizeof(optree_nd));
    init_opnd(nd, curop);
    tmp->parent = nd;
    nd->l = tmp;
    return tmp;
}

static inline optree_nd *parse_op(read_state *rs, optr_op curop, optree_nd *nd, vproc *vp, spcl_val *er) {
    debug_assert(nd != NULL);
    rs->start += OP_ALIAS[curop].n;
    int oprec = OP_PRECS[curop];
    //check if this is a relative assignment
    if (fs_get(rs->b, rs->start) == '=') {
	if (curop >= OPTR_INC) {
	    *er = spcl_make_err(E_BAD_SYNTAX, vp, "invalid operator %.*s",
		    OP_ALIAS[curop].n+1,
		    rs->b->cache + rs->start - OP_ALIAS[curop].n);
	    return NULL;
	}
	++rs->start;
	oprec = REL_ASSGN_PREC;
    }

    //find the lowest operator with a higher precedence
    while (nd->parent) {
	//"YOU SHALL NOT PASS!" -Gandalf regarding parenthetical instructions calls or lists
	if ((nd->parent->flags & ND_ISOP) && (nd->parent->v.val.i >= OPTR_CALL || oprec < OP_PRECS[nd->parent->v.val.i]))
	    break;
	nd = nd->parent;
    }

    optree_nd *tmp = optree_insert_op(nd, curop, &vp->a);
    //relative assignments are a special case that use two operators. First change the current node to an assignment, set the left node to the previously read value and set the right node to the old operator
    if (oprec == REL_ASSGN_PREC) {
	nd->v.val.i = OPTR_ASSGN;
	nd->r = make_nd(nd, curop, &vp->a);
	nd = nd->r;
	nd->l = tmp;
    }
    //regardless of whether this is a relative assignment, we need to set up pointers
    nd->r = make_nd(nd, 0, &vp->a);
    return nd->r;
}
//helper for rs_to_numeric
static inline int char_to_digit(char c, int base) {
    if (base <= 10) {
	return (c >= '0' && c < '0'+base)? c - '0' : -1;
    } else if (base > 10) {
	return (c >= '0' && c <= '9')? c-'0' : ((c >= 'a' && c < 'a'+base-10)? c-'a'+10 : ((c >= 'A' && c < 'A'+base-10)? c-'A'+10 : -1));
    }
    //base must be > 0
    return -1;
}
/**
 * Read the buffer starting at rs and convert it to a numeric value.
 * rs: the stream of characters to read from
 */
static inline spcl_val rs_to_numeric(read_state *rs, vproc *vp) {
    psize init_start = rs->start;
    double flt_res=0;
    int res=0, base=10, sign=1, digit=0, after_point=0;
    char c = fs_get(rs->b, rs->start);
    //first decide on the sign
    if (c == '-') {
	sign = -1;
	c = fs_get(rs->b, ++rs->start);
    } else if (c == '+') {
	c = fs_get(rs->b, ++rs->start);
    }
    //decide what base to use
    if (c == '0') {
	switch (fs_get(rs->b, ++rs->start)) {
	    case 'b': base = 2;++rs->start;break;
	    case 'o': base = 8;++rs->start;break;
	    case 'x': base = 16;++rs->start;break;
	    default: break;
	}
    }
    //now read the reset of the integer
    for (; rs->start < rs->end; ++rs->start) {
	c = fs_get(rs->b, rs->start);
	if ((digit = char_to_digit(c, base)) < 0) {
	    //if the character is a decimal or a scientific notation indicator, interpret as a float
	    if (c == '.') {
		if (base != 10)
		    return spcl_make_err(E_BAD_SYNTAX, vp, "floats may only be specified in base 10");
		//can't have more than one period
		if (after_point)
		    return spcl_make_err(E_BAD_SYNTAX, vp, "invalid numeric literal %.*s", rs->b->cache+init_start, rs->start-init_start+1);
		after_point = 1;
		flt_res = (double)res;
		res = 0;
	    } else if (c == 'e' || c == 'E') {
		++rs->start;
		spcl_val exp = rs_to_numeric(rs, vp);
		if (exp.type != VAL_INT)
		    return spcl_make_err(E_BAD_SYNTAX, vp, "invalid numeric literal %.*s", rs->b->cache+init_start, rs->start-init_start+1);
		if (after_point)
		    return spcl_make_num( sign*pow(base, exp.val.i)*(flt_res + pow(base, 1-after_point)*res) );
		else
		    return spcl_make_num(pow(base, exp.val.i)*(double)res*(double)sign);
	    } else {
		return (after_point)? spcl_make_num(sign*(flt_res + pow(base, 1-after_point)*res)) : spcl_make_int(sign*res);
	    }
	} else {
	    if (after_point)
		++after_point;
	    res = res*base + digit;
	}
    }
    return (after_point)? spcl_make_num(sign*(flt_res + pow(base, 1-after_point)*res)) : spcl_make_int(sign*res);
}
#define change_state(new_state) rs.start = _tokenize(vp, rs, nd, new_state, er);if (er->type == VAL_ERR) return rs.start;
spcl_local psize _tokenize(vproc *vp, read_state rs, optree_nd *nd, sm_state state, spcl_val *er);
/**
 * Helper for _tokenize which reads the next token from the stream associated with rs. This is used for both SM_EX_LSTART and SM_EX_RVAL which perform the same actions but have different behaviors after successful completion
 * returns: the position of the read index if a valid token was found or -1 if one was not found
 */
static inline psize _read_next(vproc *vp, read_state rs, optree_nd *nd, spcl_val *er) {
    rs.start = skip_ws(rs.b, rs.start, rs.end, 0);
    if (rs.start == rs.end)
	return rs.end;
    char c = fs_get(rs.b, rs.start);
    optr_op curop;
    //the initial state has to decide what the first token is and jump
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-') {
	//we need to handle expressions like -foo or -(1+2) as 0-foo or 0-(1+2)
	if ((c == '+' || c == '-') && ((c = fs_get(rs.b, rs.start+1)) < '0' || c > '9') && c != '.') {
	    init_cnstnd(nd, ND_ISCNST, spcl_make_int(0));
	    curop = name_to_op(rs.b, rs.start);
	    nd = parse_op(&rs, curop, nd, vp, er);
	    if (er->type == VAL_ERR)
		return rs.end;
	    change_state(SM_EX_RVAL);
	} else {
	    change_state(SM_IN_NUM);
	}
	return rs.start;
    } else if (c > MAX_ASCII || (c >= '_' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
	change_state(SM_IN_NAME);
	return rs.start;
    } else  if ((curop = name_to_op(rs.b, rs.start)) != 0) {
	//not statements are allowed since they only act on one value
	if (curop == OPTR_NOT) {
	    nd = parse_op(&rs, curop, nd, vp, er);
	    if (er->type == VAL_ERR)
		return rs.end;
	    change_state(SM_EX_RVAL);
	    return rs.start;
	}
	*er = spcl_make_err(E_BAD_SYNTAX, vp, "unexpected %.*s", OP_ALIAS[curop].n, OP_ALIAS[curop].s);
	return rs.end;
    } else if (c == '\"' || c == '\'') {
	change_state(SM_IN_STR);
	return rs.start;
    } else if (c == BEG_PAR || c == BEG_CRL || c == BEG_SQR) {
	psize old_end = rs.end;
	rs.end = strchr_block_rs(rs.b, rs.start, rs.end, get_match(c));
	if (rs.end >= old_end) {
	    *er = spcl_make_err(E_BAD_SYNTAX, vp, "expected matching '%c'", get_match(c));
	    return old_end;
	}
	rs.start += 1;
	//now set tokens appropriately
	if (c == BEG_PAR) {
	    //TODO: I think that removing this insertion won't break PEMDAS?
	    optree_nd *tmp = optree_insert_op(nd, OPTR_PAREN, &vp->a);
	    nd = nd->l;
	} else if (c == BEG_SQR) {
	    optree_nd *tmp = optree_insert_op(nd, OPTR_LSTDF, &vp->a);
	    nd = nd->l;
	} else if (c == BEG_CRL) {
	    nd = init_opnd(nd, OPTR_BLK);
	    nd->l = make_nd(nd, 0, &vp->a);
	    nd->r = make_nd(nd, 0, &vp->a);
	    nd->l->v = spcl_make_int(rs.start);
	    nd->r->v = spcl_make_int(rs.end);
	    return rs.end+1;
	}
	change_state(SM_EX_LSTART);
	//now start reading from the end of the block
	rs.start = rs.end+1;
	rs.end = old_end;
	return rs.start;
    }
    return -1;
}
spcl_local psize _tokenize(vproc *vp, read_state rs, optree_nd *nd, sm_state state, spcl_val *er) {
    //store the working string here
    spcl_val tmpv;
    char c;
    optr_op curop;
    switch (state) {
    case SM_EX_LSTART:
	rs.start = _read_next(vp, rs, nd, er);
	if (er->type == VAL_ERR || rs.start < 0)
	    return rs.end;
	change_state(SM_EX_LEND);
    break;
    case SM_EX_RVAL:
	rs.start = _read_next(vp, rs, nd, er);
	if (er->type == VAL_ERR)
	    return rs.start;
	//make sure we found a token and imediately put it on the tree
	if (rs.start < 0) {
	    *er = spcl_make_err(E_BAD_SYNTAX, vp, "expected rval before end of block");
	    return rs.end;
	}
	change_state(SM_EX_LEND);
    break;
    case SM_EX_LEND:
	//look for line ends
	for (;; ++rs.start) {
	    if (rs.start == rs.end)
		return rs.start;
	    c = fs_get(rs.b, rs.start);
	    if (c == ';' || c == '\n')
		return rs.start;
	    if (!is_whitespace(c))
		break;
	}
	//the initial state has to decide what the first token is and jump
	if ((curop = name_to_op(rs.b, rs.start)) != 0) {
	    nd = parse_op(&rs, curop, nd, vp, er);
	    if (er->type == VAL_ERR)
		return rs.end;
	    change_state(SM_EX_RVAL);
	} else {
	    //otherwise there was an unexpected token and we should end
	    psize old_s = rs.start;
	    for (; rs.start < rs.end && !is_whitespace(fs_get(rs.b, rs.start)); ++rs.start) ;
	    *er = spcl_make_err(E_BAD_SYNTAX, vp, "expected line end instead of \'%.*s\'", rs.start-old_s, rs.b->cache+old_s);
	    return rs.end;
	}
    break;
    case SM_IN_NUM:
	init_cnstnd(nd, ND_ISCNST, rs_to_numeric(&rs, vp));
	if (nd->v.type == VAL_ERR) {
	    *er = tmpv;
	    memset(nd, 0, sizeof(optree_nd));
	    return rs.end;
	}
	return rs.start;
    break;
    case SM_IN_NAME:
	//see if its a keyword
	spcl_key key = get_keyword(&rs);
	switch (key) {
	    case KEY_NONE: break;
	    case KEY_TRUE: init_cnstnd(nd, ND_ISCNST, spcl_make_int(1));return rs.start;break;
	    case KEY_FALSE: init_cnstnd(nd, ND_ISCNST, spcl_make_int(0));return rs.start;break;
			    //TODO
	    default: break;
	}
	//otherwise its a variable name
	init_cnstnd(nd, ND_ISLF, spcl_make_str(vp->a.head, -1, vp));
	psize init_start = rs.start;
	for (; rs.start < rs.end; ++rs.start) {
	    c = fs_get(rs.b, rs.start);
	    //check for array accesses or function calls
	    if (c == BEG_PAR || c == BEG_SQR || (is_whitespace(c) && c != '\n')) {
		read_state sub_rs = rs;
		sub_rs.start = skip_ws(sub_rs.b, sub_rs.start, sub_rs.end, 0);
		if (fs_get(rs.b, rs.start) == BEG_PAR) {
		    optree_nd *tmp = optree_insert_op(nd, OPTR_CALL, &vp->a);
		    nd->r = make_nd(nd, 0, &vp->a);
		    nd = nd->r;
		    //now read the contents of the call. TODO: find a way to avoid code duplication
		    sub_rs.end = strchr_block_rs(sub_rs.b, sub_rs.start, sub_rs.end, get_match(c));
		    if (sub_rs.end >= rs.end) {
			*er = spcl_make_err(E_BAD_SYNTAX, vp, "expected matching '%c'", END_PAR);
			return rs.end;
		    }
		    sub_rs.start += 1;
		    rs = sub_rs;
		    change_state(SM_EX_LSTART);
		    return sub_rs.end+1;
		} else if (fs_get(rs.b, rs.start) == BEG_SQR) {
		    optree_nd *tmp = optree_insert_op(nd, OPTR_LSTRD, &vp->a);
		    nd->r = make_nd(nd, 0, &vp->a);
		    nd = nd->r;
		    //now read the contents of the access
		    sub_rs.end = strchr_block_rs(sub_rs.b, sub_rs.start, sub_rs.end, get_match(c));
		    if (sub_rs.end >= rs.end) {
			*er = spcl_make_err(E_BAD_SYNTAX, vp, "expected matching '%c'", END_SQR);
			return rs.end;
		    }
		    sub_rs.start += 1;
		    rs = sub_rs;
		    change_state(SM_EX_LSTART);
		    return sub_rs.end+1;
		}
		//return rs.start;
	    }
	    //other invalid characters terminate the name
	    if (((c < '_' || c > 'z') && (c < '0' || c > '9') && (c < 'A' || c > 'Z') && c <= MAX_ASCII) || rs.start == rs.end)
		return rs.start;
	    aappend(&vp->a, c);
	    ++nd->v.n_els;
	}
    break;
    case SM_IN_STR:
	c = fs_get(rs.b, rs.start++);
	char match = c;
	init_cnstnd(nd, ND_ISCNST, spcl_make_str(vp->a.head, -1, vp));
	for (; rs.start < rs.end; ++rs.start) {
	    c = fs_get(rs.b, rs.start);
	    if (c == '\\') {
		c = fs_get(rs.b, rs.start+1);
		switch (c) {
		    case 't': c = '\t';++rs.start;break;
		    case 'n': c = '\n';++rs.start;break;
		    case '\\': c = '\\';++rs.start;break;
		    case '\"': c = '\"';++rs.start;break;
		    case '\'': c = '\'';++rs.start;break;
			       //TODO: octal and hex escape sequences
		    default: *er = spcl_make_err(E_BAD_SYNTAX, vp, "unrecognized escape sequence \\%c", c);return rs.end;
		}
	    } else if (c == match) {
		return skip_ws(rs.b, rs.start, rs.end, 1);
	    }
	    aappend(&vp->a, c);
	    ++nd->v.n_els;
	}
	*er = spcl_make_err(E_BAD_SYNTAX, vp, "expected matching \'%c\'", match);
	return rs.end;
    break;
    default: break;
    }
    return rs.start;
}
optree_nd *tokenize_str(vproc *vp, const char *str, size_t n) {
    //setup a read state from the string
    read_state rs;
    rs.b = make_spcl_fstream_str(str, n);
    rs.start = 0;
    rs.end = n;
    //storage pointers
    spcl_val er = (spcl_val){0};
    optree_nd *nd = make_nd(NULL, 0, &vp->a);
    //finally we're ready to do the tokenizing
    _tokenize(vp, rs, nd, SM_EX_LSTART, &er);
    //deallocate memory
    destroy_spcl_fstream(rs.b);
    return nd;
}
/**
 * Helper which unfolds list definitions
 */
static inline unfold_res _list_def_unfold(vproc *vp, optree_nd *nd, usize *insts, usize n_insts, spcl_val *er) {
    unfold_res ret = (unfold_res){0};
    //check for empty lists
    if (!nd->l) {
	ret.l = L_HEP;
	ret.v.hp = anew(&vp->a, spcl_val);
	memset(ret.v.hp, 0, sizeof(spcl_val));
	ret.v.hp->type = VAL_LIST;
	return ret;
    }
    //otherwise walk along the tree to count the length of the array
    int allcnst = 1;
    psize n_els = 0;
    unfold_res tr;
    optree_nd *tmp = nd->l;
    //check for fancy-schmancy list comprehensions
    if (tmp && (tmp->flags & ND_ISOP) && tmp->v.val.i == OPTR_FOR) {
	/*if ((tmp->r->flags & ND_ISOP) == 0 || tmp->r->v.val.i != OP_IN) {
	    *er = spcl_make_err(E_BAD_SYNTAX, "expected keyword 'in' for list comprehension");
	    return ret;
	}
	//we have to unfold the right child of the in operator to know what we iterate over
	ret = unfold_optree(vp, tmp->r->r, insts, n_insts, er);
	if (er->type == VAL_ERR)
	    return ret;
	//check that the value is a list. TODO: allow for iterables
	insts[ret.n_written++] = gen_op2(OP_PUSH, L_LIT);
	insts[ret.n_written++] = 0;
	insts[ret.n_written++] = gen_op3(OP_TYPE, L_STK, ret.l);
	insts[ret.n_written++] = 0;
	insts[ret.n_written++] = (ret.l == L_STK)? ret.v.st : (ret.l == L_LIT)? ret.v.st : (usize)ret.v.hp;
	insts[ret.n_written++] = gen_op3(OP_SUB | RELOP_BIT, L_STK, L_LIT);
	insts[ret.n_written++] = 0;
	insts[ret.n_written++] = VAL_LIST;
	insts[ret.n_written++] = gen_op3(OP_JZR | RELOP_BIT, L_STK, L_LIT);
	insts[ret.n_written++] = 2;
	insts[ret.n_written++] = VAL_LIST;
	insts[ret.n_written++] = set_meta(gen_op2(OP_THROW, L_LIT), E_BAD_TYPE);
	insts[ret.n_written++] = 0;
	spcl_set_valn(vp, tmp->r->l->v.val.s, tmp->r->l->v.n_els, spcl_make_int(vp->sp), 0);*/
    }
    //otherwise its a boring old comma separated list
    while (tmp) {
	if (tmp->flags & ND_ISLF || ((tmp->flags & ND_ISOP) && (tmp->v.val.i != OPTR_APPND || !(tmp->l->flags & ND_ISCNST))))
	    allcnst = 0;
	++n_els;
	tmp = tmp->r;
    }
    //if it was all constant elements, return a constant array
    if (allcnst) {
	//TODO: ommit pointer referencing and implement lists as simple headers
	ret.type = VAL_LIST;
	ret.l = L_HEP;
	ret.v.hp = xmalloc(sizeof(spcl_val), vp);
	ret.v.hp->val.l = xmalloc(sizeof(spcl_val)*n_els, vp);
	ret.v.hp->type = VAL_LIST;
	ret.v.hp->n_els = n_els;
	tmp = nd->l;
	for (psize i = 0; i < n_els; ++i) {
	    ret.v.hp->val.l[i] = (i+1 < n_els) ? copy_spcl_val(tmp->l->v, vp) : copy_spcl_val(tmp->v, vp);
	    tmp = tmp->r;
	}
	return ret;
    }
    //otherwise we'll have to produce instructions to write the array
    ret.n_written = 0;
    insts[ret.n_written++] = set_meta(gen_op2(OP_ALLOC, L_LIT), VAL_LIST);
    insts[ret.n_written++] = n_els;
    //we have to keep track of the location of the list on the stack
    tmp = nd->l;
    //minus one since the last element is always a list element
    for (psize i = 0; i < n_els; ++i) {
	unfold_res tr = (i+1<n_els)?
	    unfold_optree(vp, tmp->l, insts, n_insts, er) : unfold_optree(vp, tmp, insts, n_insts, er);
	ret.n_written += tr.n_written;
	ret.n_pushed += tr.n_pushed;
	psize this_root_st = ret.n_pushed;
	//write address always pops off the stack, so we have to make sure that's correct
	if (tr.l != L_STK || tr.v.st != 0) {
	    insts[ret.n_written++] = gen_op2(OP_PUSH, tr.l);
	    insts[ret.n_written++] = (tr.l == L_LIT)? tr.v.st : (usize)tr.v.hp;
	    ++this_root_st;
	}
	insts[ret.n_written++] = gen_op3(OP_WEA, L_STK, L_LIT);
	insts[ret.n_written++] = this_root_st;
	insts[ret.n_written++] = i;
	tmp = tmp->r;
    }
    ret.type = VAL_LIST;
    ret.l = L_STK;
    ret.v.st = ret.n_pushed;
    ++ret.n_pushed;
    //execute all instructions as they're performed
    vproc_exec(vp, insts, ret.n_written);
    return ret;
}
/**
 * Unfold a function call
 */
static inline unfold_res _call_unfold(vproc *vp, optree_nd *nd, usize *insts, usize n_insts, spcl_val *er) {
    unfold_res ret = (unfold_res){0};
    if (nd->l == NULL || ndflag(nd->l, ND_ISLF) == 0) {
	*er = spcl_make_err(E_BAD_SYNTAX, vp, "expected function name before call");
	return ret;
    }
    unfold_res fres = unfold_optree(vp, nd->l, insts, n_insts, er);
    spcl_val fval = (fres.l == L_STK)? vp->stack[fres.v.st] : (fres.l == L_LIT)? spcl_make_int(fres.v.st) : *fres.v.hp;
    if (fval.type != VAL_FN) {
	*er = spcl_make_err(E_BAD_TYPE, vp, "cannot call non-function %s", valnames[fval.type]);
	return ret;
    }
    //now push each argument onto the stack
    psize n_els = 0;
    int allcnst = 1;
    psize n_before;
    optree_nd *tmp = nd->r;
    while (tmp) {
	//if its a comma, unfold the appended value. Otherwise unfold the value itself
	optree_nd *to_unfold = ((tmp->flags & ND_ISOP) && tmp->v.val.i == OPTR_APPND)? tmp->l : tmp;
	unfold_res tr = unfold_optree(vp, to_unfold, insts, n_insts, er);
	ret.n_written += tr.n_written;
	n_before = ret.n_written;
	//we have to manipulate the stack such that the value returned from tr is at the top of the stack and no other values were pushed
	if (tr.l == L_STK) {
	    debug_assert(tr.v.st >= 0);
	    if (tr.v.st+1 < tr.n_pushed) {
		//if other values were pushed, we need to make sure this value is the highest so we can pop everything below
		insts[ret.n_written++] = gen_op3(OP_MOV, L_STK, L_STK);
		insts[ret.n_written++] = tr.n_pushed-1;
		insts[ret.n_written++] = tr.l;
		if (tr.n_pushed > 1)
		    insts[ret.n_written++] = set_meta(gen_op1(OP_POPN), tr.n_pushed-1);
	    } else if (tr.v.st > tr.n_pushed) {
		//if the value is higher on the stack, then we need to push a duplicate 
		if (tr.n_pushed > 0)
		    insts[ret.n_written++] = set_meta(gen_op1(OP_POPN), tr.n_pushed);
		insts[ret.n_written++] = gen_op2(OP_PUSH, L_STK);
		insts[ret.n_written++] = tr.v.st;
	    }
	} else {
	    if (tr.n_pushed > 0)
		insts[ret.n_written++] = set_meta(gen_op1(OP_POPN), tr.n_pushed);
	    insts[ret.n_written++] = gen_op2(OP_PUSH, tr.l);
	    insts[ret.n_written++] = (tr.l == L_LIT)? tr.v.st : (usize)tr.v.hp;
	}
	//execute all instructions
	vproc_exec(vp, insts+n_before, ret.n_written-n_before);
	//check whether all the arguments were constants (in which case we can propogate)
	if (tmp->flags & ND_ISLF || ((tmp->flags & ND_ISOP) && (tmp->v.val.i != OPTR_APPND || !(tmp->l->flags & ND_ISCNST))))
	    allcnst = 0;
	++n_els;
	//if the unfolded operation matches the current operation, we've reached the end
	if (to_unfold == tmp)
	    break;
	tmp = tmp->r;
    }
    //lastly, we need to push the number of arguments and the call instruction
    n_before = ret.n_written;
    insts[ret.n_written++] = gen_op2(OP_PUSH, L_LIT);
    insts[ret.n_written++] = n_els;
    insts[ret.n_written++] = gen_op2(OP_CALL, fres.l);
    insts[ret.n_written++] = (fres.l == L_STK)? fres.v.st+n_els+1 : (fres.l == L_LIT)? fres.v.st : (usize)fres.v.hp;
    //execute the call, TODO: allow for tuple return
    vproc_exec(vp, insts+n_before, ret.n_written-n_before);
    ret.l = L_STK;
    ret.v.st = 0;
    ret.n_pushed = 0;
    //calls always push their return values to the stack
    //TODO: constant propagation, this is a little tricky since we first must ensure the function called has no side effects
}
/**
 * Helper for unfold_optree which turns a node into a constant and returns the result
 * nd: the node with the operation to be performed
 */
static inline unfold_res unfold_const(optree_nd *nd, vproc *vp) {
    //TODO: return literals for floating point values
    unfold_res ret = (unfold_res){0};
    ret.type = nd->v.type;
    if (nd->v.type == VAL_INT) {
	ret.l = L_LIT;
	ret.v.st = nd->v.val.i;
    } else {
	ret.l = L_HEP;
	spcl_val v = copy_spcl_val(nd->v, vp);
	//the memory stored in v will persist after the call, but not v itself. We must allocate
	ret.v.hp = xmalloc(sizeof(spcl_val), vp);
	*ret.v.hp = v;
    }
    return ret;
}
static inline unfold_res const_prop(unfold_res def, vproc *vp, optree_nd *nd) {
    //ternary operators are a special case since they use three values
    if (nd->v.val.i == OPTR_TRNQ && ndflag(nd->l, ND_ISCNST) && ndflag(nd->r->l, ND_ISCNST) && ndflag(nd->r->r, ND_ISCNST)) {
	nd->flags = ND_ISCNST;
	nd->v = vp->stack[vp->sp+def.v.st];
	vp->sp += def.n_pushed;
	return unfold_const(nd, vp);
    }
    if (ndflag(nd->l, ND_ISCNST) && ndflag(nd->r, ND_ISCNST) && nd->v.val.i <= OP_AND) {
	nd->flags = ND_ISCNST;
	nd->v = vp->stack[vp->sp+def.v.st];
	vp->sp += def.n_pushed;
	return unfold_const(nd, vp);
    }
    def.type = vp->stack[vp->sp+def.v.st].type;
    def.l = L_STK;
    return def;
}
/**
 * Unfold the optree with a root at nd
 * nd: the optree to unfold
 * insts: a buffer to write instructions to
 * n_insts: the length of the buffer. If there were insufficient elements, then -1 is returned.
 * returns: an integer value with the number of instructions written on success or an error
 */
unfold_res unfold_optree(vproc *vp, optree_nd *nd, usize *insts, usize n_insts, spcl_val *er) {
    if (!nd) {
	//*er = spcl_make_err(E_BAD_VALUE, vp, "not an operator");
	return (unfold_res){0};
    }
    unfold_res ret = (unfold_res){0};
    if (nd->flags & ND_ISCNST) {
	return unfold_const(nd, vp);
    } else if (nd->flags & ND_ISLF) {
	ret.l = L_STK;
	//TODO: this will break once we remove labels so it needs modification
	psize i;
	s8 str = (s8){nd->v.val.s, nd->v.n_els};
	/*if (find_ind(vp->c, str, &i))
	    ret.v.hp = &(vp->c->table[i].v);*/
	if (find_ind(vp->c, str, &i)) {
	    i = vp->c->table[i].v.val.i;
	    if (i < vp->sp) {
		*er = spcl_make_err(E_UNDEF, vp, "token \"%.*s\" used before assignment", str.n, str.s);
		return ret;
	    }
	    ret.type = vp->stack[i].type;
	    ret.v.st = i - vp->sp;
	} else {
	    *er = spcl_make_err(E_UNDEF, vp, "token \"%.*s\" not defined", str.n, str.s);
	}
    } else if (nd->flags & ND_ISOP) {
	optree_nd *trn_parent = NULL;
	//parentheses and list definitions are special cases
	if (nd->v.val.i == OPTR_PAREN) {
	    ret = unfold_optree(vp, nd->l, insts, n_insts, er);
	    if (ndflag(nd->l, ND_ISCNST))
		*nd = *nd->l;
	    return ret;
	} else if (nd->v.val.i == OPTR_CALL) {
	    return _call_unfold(vp, nd, insts, n_insts, er);
	} else if (nd->v.val.i == OPTR_LSTDF) {
	    return _list_def_unfold(vp, nd, insts, n_insts, er);
	} else if (nd->v.val.i == OPTR_NOT) {
	    //TODO: find a way to more elegantly merge this with other operators
	    ret = unfold_optree(vp, nd->r, insts, n_insts, er);
	    if (ret.l == L_STK && ret.v.st < ret.n_pushed) {
		insts[ret.n_written++] = gen_op2((OP_NOT | RELOP_BIT), L_STK);
		insts[ret.n_written++] = ret.v.st;
	    } else {
		insts[ret.n_written++] = gen_op2(OP_NOT, ret.l);
		insts[ret.n_written++] = (ret.l == L_STK || ret.l == L_LIT)? ret.v.st : (usize)ret.v.hp;
		ret.v.st = 0;
		++ret.n_pushed;
	    }
	    vproc_exec(vp, insts, ret.n_written);
	    return const_prop(ret, vp, nd);
	} else if (nd->v.val.i == OPTR_TRNQ) {
	    //switch to the colon since we'll need to unfold both of those operators
	    trn_parent = nd;
	    nd = nd->r;
	    if (!ndflag(nd, ND_ISOP) || nd->v.val.i != OPTR_TRNC) {
		*er = spcl_make_err(E_BAD_SYNTAX, vp, "expected \':\' in ternary");
		return ret;
	    }
	} else if (nd->v.val.i == OPTR_ASSGN) {
	    //if its an assignment, then we need to perform that action before there is any complaint about missing values
	    ret = unfold_optree(vp, nd->r, insts, n_insts, er);
	    //if unfolding the right side pushed to the stack, then we can just assign that value. Otherwise we have to push.
	    if (ret.l != L_STK) {
		insts[ret.n_written++] = gen_op2(OP_PUSH, ret.l);
		insts[ret.n_written++] = (ret.l == L_LIT)? ret.v.st : (usize)ret.v.hp;
		ret.v.st = 0;
	    }
	    vproc_exec(vp, insts, ret.n_written);
	    spcl_set_valn(vp->c, nd->l->v.val.s, nd->l->v.n_els, spcl_make_int(vp->sp), 0, vp);
	    return ret;
	}
	//otherwise, unfold both children and check for errors
	unfold_res lr = unfold_optree(vp, nd->l, insts, n_insts, er);
	if (er->type == VAL_ERR)
	    return ret;
	unfold_res rr = unfold_optree(vp, nd->r, insts, n_insts-lr.n_written, er);
	if (er->type == VAL_ERR)
	    return ret;
	
	//add up the number of written and pushed items
	ret.n_written += lr.n_written + rr.n_written;
	ret.n_pushed += lr.n_pushed + rr.n_pushed;
	//we need to keep track of the number of instructions before writing so that we can execute
	psize n_before = ret.n_written;
	//handle ternary operator conditions
	if (trn_parent) {
	    unfold_res cr = unfold_optree(vp, trn_parent->l, insts, n_insts-ret.n_written, er);
	    if (er->type == VAL_ERR)
		return cr;
	    //correct the return write and push numbers
	    ret.n_written += cr.n_written;
	    ret.n_pushed += cr.n_pushed;
	    n_before += cr.n_written;
	    //write the ternary expression
	    insts[ret.n_written++] = set_meta(gen_op3(OP_TRN, lr.l, rr.l), cr.l);
	    insts[ret.n_written++] = (lr.l == L_STK)?lr.v.st+rr.n_pushed+cr.n_pushed:(lr.l == L_LIT)?lr.v.st:(usize)lr.v.hp;
	    insts[ret.n_written++] = (rr.l == L_STK)?rr.v.st+cr.n_pushed:(rr.l == L_LIT)?rr.v.st:(usize)rr.v.hp;
	    insts[ret.n_written++] = (cr.l == L_STK)?cr.v.st:(cr.l == L_LIT)?cr.v.st:(usize)cr.v.hp;
	    ret.v.st = 0;
	    ++ret.n_pushed;
	    vproc_exec(vp, insts+n_before, ret.n_written-n_before);
	    return const_prop(ret, vp, nd);
	}
	//if the result of l was pushed onto the stack then we can modify it inplace and return the result, otherwise use a generic operator that pushes onto the stack
	if (lr.l == L_STK && lr.v.st < ret.n_pushed && nd->v.val.i <= OP_SHR) {
	    insts[ret.n_written++] = gen_op3((nd->v.val.i | RELOP_BIT), lr.l, rr.l);
	    ret.v.st = lr.v.st;
	} else {
	    insts[ret.n_written++] = gen_op3(nd->v.val.i, lr.l, rr.l);
	    ret.v.st = 0;
	    ++ret.n_pushed;
	}
	//we have to correct the left stack address to account for pushes by the right
	insts[ret.n_written++] = (lr.l == L_STK)? lr.v.st+rr.n_pushed : (lr.l == L_LIT)? lr.v.st : (usize)lr.v.hp;
	insts[ret.n_written++] = (rr.l == L_STK)? rr.v.st : (rr.l == L_LIT)? rr.v.st : (usize)rr.v.hp;
	//execute what we've written so far and propogate constants if possible
	vproc_exec(vp, insts+n_before, ret.n_written-n_before);
	return const_prop(ret, vp, nd);
	/*if (ndflag(nd->l, ND_ISCNST) && ndflag(nd->r, ND_ISCNST) && nd->v.val.i <= OP_AND) {
	    nd->flags = ND_ISCNST;
	    nd->v = vp->stack[vp->sp+ret.v.st];
	    vp->sp += ret.n_pushed;
	    return unfold_const(nd, vp);
	}
	ret.type = vp->stack[vp->sp+ret.v.st].type;
	ret.l = L_STK;*/
    }
    return ret;
}
//forward declare so that helpers can call
static inline spcl_val spcl_parse_line_rs(spcl_inst* c, read_state rs, psize* new_end, spcl_key start_key);
static inline spcl_val spcl_read_lines_block(vproc *vp, read_state rs) {
    //TODO: give this an actual buffer
    usize inst_buf[TMP_INST_SIZE];
    spcl_val tmp;
    while (rs.start < rs.end) {
	optree_nd *nd = make_nd(NULL, 0, &vp->a);
	read_state old_rs = rs;
	//parse the line
	rs.start = _tokenize(vp, rs, nd, SM_EX_LSTART, &tmp);
	if (tmp.type == VAL_ERR) {
	    _print_error(stderr, tmp, old_rs);
	    return tmp;
	}
	//execute it
	unfold_optree(vp, nd, inst_buf, TMP_INST_SIZE, &tmp);
	if (tmp.type == VAL_ERR)
	    _print_error(stderr, tmp, old_rs);
	    return tmp;
    }
}
spcl_val spcl_parse_line(vproc *vp, const char* str) {
    //setup a read state from the string
    read_state rs;
    psize n = strlen(str);
    rs.b = make_spcl_fstream_str(str, n);
    rs.start = 0;
    rs.end = n;
    //storage pointers
    spcl_val er = (spcl_val){0};
    optree_nd *nd = make_nd(NULL, 0, &vp->a);
    //finally we're ready to do the tokenizing
    _tokenize(vp, rs, nd, SM_EX_LSTART, &er);
    if (er.type == VAL_ERR)
	return er;
    //unfold the tree
    psize sp_before = vp->sp;
    usize inst_buf[TMP_INST_SIZE];
    unfold_res res = unfold_optree(vp, nd, inst_buf, TMP_INST_SIZE, &er);
    if (res.type == VAL_UNDEF || er.type == VAL_ERR)
	return er;
    //find the return value
    if (res.l == L_LIT)
	er = (res.type == VAL_NUM)? spcl_make_num(res.v.st) : spcl_make_int(res.v.st);
    else if (res.l == L_STK)
	er = vp->stack[vp->sp + res.v.st];
    else if (res.l == L_HEP)
	er = *res.v.hp;
    //deallocate memory and return
    destroy_spcl_fstream(rs.b);
    vp->sp = sp_before;
    reset(&vp->a);
    return er;
}
int spcl_test(vproc *vp, const char* str) {
    spcl_val v = spcl_parse_line(vp, str);
    return (v.type != VAL_INT || v.val.i != 0);
    //return (v.type != VAL_ERR && v.type != VAL_UNDEF && (v.type != VAL_INT || v.val.i == 0));
}

/**
 * A helper which accesses v[ind]. If assign is not NULL, then v[ind] = *assign.
 */
static inline spcl_val _spcl_index(spcl_val v, spcl_val ind, vproc *vp) {
    //check for invalid types
    if (ind.type != VAL_NUM)
	return spcl_make_err(E_BAD_TYPE, vp, "cannot index with type %s", valnames[ind.type]);
    if (-(ind.val.x) > v.n_els || ind.val.x >= v.n_els)
	return spcl_make_err(E_OUT_OF_RANGE, vp, "index %d out of bounds for list of size %lu", (int)ind.val.x, v.n_els);
    size_t i = (ind.val.x < 0)? v.n_els - (size_t)(-ind.val.x) : (size_t)ind.val.x;
    //create a new dummy value or return the element depending on type
    if (v.type == VAL_LIST || v.type == VAL_MAT) {
	return v.val.l[i];
    } else if (v.type == VAL_ARRAY) {
	//in principle this should be a reference, but numerics are trivially destructable so it doesn't matter
	return spcl_make_num(v.val.a[i]);
    }
    return spcl_make_err(E_BAD_TYPE, vp, "type %s is not indexable", valnames[v.type]);
}
/**
 * An alternative to lookup which only considers the first n bytes in str
 */
/*static inline spcl_val spcl_find_rs(spcl_inst* c, read_state rs) {
    psize dot_loc = strchr_block_rs(rs.b, rs.start, rs.end, '.');
    psize ref_loc = strchr_block_rs(rs.b, rs.start, rs.end, BEG_SQR);//]
    if (dot_loc == rs.end && ref_loc == rs.end) {
	//if there was neither a period or open brace, just lookup directly
	psize i;
	s8 str = trim_whitespace(fs_read(rs.b, rs.start, rs.end));
	while (c) {
	    if (find_ind(c, str, &i))
		return c->table[i].v;
	    //go up if we didn't find it
	    c = c->parent;
	}
	//reaching this point in execution means the matching entry wasn't found
	return spcl_make_none();
    } else if (dot_loc < ref_loc) {
	//if there was a dot, access spcl_inst members
	spcl_val sub_con = spcl_find_rs(c, make_read_state(rs.b, rs.start, dot_loc));
	if (sub_con.type != VAL_INST)
	    return spcl_make_err(E_BAD_TYPE, vp, "cannot access member from non-instance type %s", valnames[sub_con.type]);
	return spcl_find_rs(sub_con.val.c, make_read_state(rs.b, dot_loc+1, rs.end));
    } else {
	//access lists/arrays
	psize close_ind = strchr_block_rs(rs.b, ref_loc+1, rs.end, END_SQR);
	if (close_ind > rs.end)
	    return spcl_make_err(E_BAD_SYNTAX, vp, "expected %c", END_SQR);
	//read the list and the index
	spcl_val lst = spcl_find_rs(c, make_read_state(rs.b, rs.start, ref_loc));
	spcl_val index = spcl_parse_line_rs(c, make_read_state(rs.b, ref_loc+1, close_ind), NULL, KEY_NONE);
	return _spcl_index(lst, index, NULL);
    }
}*/
/**
 * similar to set_spcl_valn(), but read in place from a read state
 */
/*static inline spcl_val set_spcl_val_rs(struct spcl_inst* c, read_state rs, spcl_val p_val) {
    psize dot_loc = strchr_block_rs(rs.b, rs.start, rs.end, '.');
    psize ref_loc = strchr_block_rs(rs.b, rs.start, rs.end, BEG_SQR);//]
    //if there are no dereferences, just access the table directly
    if (dot_loc == rs.end && ref_loc == rs.end) {
	s8 str = trim_whitespace(fs_read(rs.b, rs.start, rs.end));
	spcl_set_valn(c, str.s, str.n, p_val, 0);
	return p_val;
    } else if (dot_loc < ref_loc) {
	//access spcl_inst members
	spcl_val sub_con = spcl_find_rs(c, make_read_state(rs.b, rs.start, dot_loc));
	if (sub_con.type != VAL_INST)
	    return spcl_make_err(E_BAD_TYPE, vp, "cannot access member from non instance type %s", valnames[sub_con.type]);
	return set_spcl_val_rs(sub_con.val.c, make_read_state(rs.b, dot_loc+1, rs.end), p_val);
    } else {
	//access lists/arrays
	psize close_ind = strchr_block_rs(rs.b, ref_loc+1, rs.end, END_SQR);
	if (close_ind > rs.end)
	    return spcl_make_err(E_BAD_SYNTAX, vp, "expected %c", END_SQR);
	//read the list and the index
	spcl_val lst = spcl_find_rs(c, make_read_state(rs.b, rs.start, ref_loc));
	spcl_val index = spcl_parse_line_rs(c, make_read_state(rs.b, ref_loc+1, close_ind), NULL, KEY_NONE);
	return _spcl_index(lst, index, &p_val);
    }
    return spcl_make_none();
}*/
//get the numer of instructions in a given opcode (including the opcode itself
static inline int _get_op_n_insts(usize inst) {
    switch (inst & CODE_MASK) {
    case OP_SRCH:	return 3;
    case OP_STNM:	return 3;
    case OP_REA:	return 3;
    case OP_WEA:	return 3;
    case OP_PUSH:	return 2;
    case OP_POP:	return 2;
    case OP_POPN:	return 1;
    case OP_MOV:	return 3;
    case OP_CPY:	return 3;
    case OP_SWAP:	return 3;
    case OP_CALL:	return 2;
			//regtest and jump instructions take three arguments
    case OP_JZR:	return 3;
    case OP_JNZR:	return 3;
    /*case OP_JGT:	return 3;
    case OP_JLT:	return 3;
    case OP_JGE:	return 3;
    case OP_JLE:	return 3;*/
			//compare and jump instructions take four arguments
			//increments take two arguments
    case OP_CONV:	return 2;
    case OP_NOT:	return 2;
			//other math operations take three
    case OP_ADD:	return 3;
    case OP_SUB:	return 3;
    case OP_MUL:	return 3;
    case OP_DIV:	return 3;
    case OP_MOD:	return 3;
    case OP_EXP:	return 3;
    case OP_INC:	return 2;
    case OP_DEC:	return 2;
    case OP_RD:		return 4;
    case OP_EQ:		return 3;
    case OP_NEQ:	return 3;
    case OP_GT:		return 3;
    case OP_LT:		return 3;
    case OP_GE:		return 3;
    case OP_LE:		return 3;
    default: return 1;
    }
}
spcl_local spcl_val* _val_from_inst(vproc *vp, short inst_loc, usize inst, spcl_val *sto) {
    spcl_val *ret;
    switch (inst_loc) {
    case L_LIT: *sto = spcl_make_int(inst);ret = sto;		break;
    case L_STK: ret = vp->stack+vp->sp+inst;			break;
    case L_HEP: ret = (spcl_val*)inst;				break;
    }
    return ret;
}
spcl_local usize _jmp(usize* insts, usize n_insts, usize pc, usize addr) {
    short meta = op_meta(insts[pc]);
    if ((meta & 0x3) == 0) {
	return addr;
    } else if ((meta & 0x3) == 1) {
	while (pc < n_insts) {
	    if (op_code(insts[pc]) == OP_LAB && op_meta(insts[pc]))
		break;
	    pc += _get_op_n_insts(insts[pc]);
	}
	return pc;
    } else if ((meta & 0x3) == 2) {
	return pc + addr;
    }
    return pc+1;
}
vproc *make_vproc() {
    vproc *vp = malloc(sizeof(vproc));
    vp->stack = mmap(NULL, sizeof(spcl_val)*STACK_PAGE_N*getpagesize(), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK, -1, 0);
    vp->heap = mmap(NULL, sizeof(spcl_val)*getpagesize(), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    vp->pc = 0;
    vp->sp = getpagesize();
    init_arena(&vp->a, VP_ARENA_N, vp);
    //TODO: move this to function definitions
    vp->c = make_spcl_inst(NULL, vp);
    setup_builtins(vp);
    return vp;
}
void destroy_vproc(vproc *vp) {
    if (!vp)
	return;
    destroy_spcl_inst(vp->c, vp);
    cleanup_arena(&vp->a, vp);
    munmap(vp->stack, sizeof(spcl_val)*getpagesize());
    munmap(vp->heap, sizeof(spcl_val)*getpagesize());
    free(vp);
}
static inline void _pshcpy(vproc *vp, spcl_val v) {
    vp->stack[--vp->sp] = copy_spcl_val(v, vp);
}
/**
 * execute the instructions insts
 * insts: an array with the code to execute 
 * n_insts: the length of insts
 */
spcl_val vproc_exec(vproc *vp, usize* insts, usize n_insts) {
    usize pc = 0;
    s8 *name;
    spcl_val tmpd, tmps, tmpa;
    spcl_val *dst, *src, *arg;
    short code, blen, meta;
    while (pc < n_insts) {
	code = op_code(insts[pc]);
	blen = op_blen(insts[pc]);
	//check that there are enough instructions
	//debug_assert(pc+OP_LENS[code] < n_insts+1);
	//TODO: come up with a less hacky way of doing this than passing pointers to tmpd and tmps
	dst = _val_from_inst(vp, op_dstl(insts[pc]), insts[pc+1], &tmpd);
	src = _val_from_inst(vp, op_srcl(insts[pc]), insts[pc+2], &tmps);
	switch (code) {
	case 0: ++pc;break; //noop
	/*case OP_SRCH:
	    assert(pc+2 < n_insts);
	    name = (s8*)insts[pc+1];
	    spcl_fstream* fs = make_spcl_fstream_str(name->s, name->n);
	    *dst = spcl_find_rs( vp->c, make_read_state(fs, 0, fs_end(fs)) );
	    destroy_spcl_fstream(fs);
	    pc += 3;
	break;
	case OP_STNM:
	    assert(pc+2 < n_insts);
	    spcl_set_valn(vp->c, dst->val.s, dst->n_els, *src, 1);
	    pc += 3;
	break;*/
	case OP_CALL:
	    if (dst->type != VAL_FN)
		return spcl_make_err(E_BAD_TYPE, vp, "cannot call non-function %s", valnames[dst->type]);
	    tmpd = vp->stack[vp->sp++];
	    if (tmpd.type != VAL_INT)
		return spcl_make_err(E_BAD_VALUE, vp, "corrupted stack");
	    //TODO: refactor spcl_fn_call out of existance
	    spcl_fn_call fc;
	    fc.n_args = tmpd.val.i;
	    for (psize i = fc.n_args-1; i > 0; --i)
		fc.args[i] = vp->stack[vp->sp++];
	    vp->stack[--vp->sp] = spcl_uf_eval(dst->val.f, fc, vp);
	    pc += 2;
	break;
	case OP_TRN:
	    arg = _val_from_inst(vp, op_meta(insts[pc]), insts[pc+3], &tmpa);
	    tmpa = *arg;
	    if (spcl_isfalse(tmpa))
		vp->stack[--vp->sp] = *src;
	    else
		vp->stack[--vp->sp] = *dst;
	    pc += 4;
	break;
	case OP_ALLOC:
	    if (dst->type != VAL_INT)
		return spcl_make_err(E_BAD_TYPE, vp, "list size must be an integer");
	    //TODO: garbage collection
	    if (op_meta(insts[pc]) == VAL_ARRAY) {
		vp->stack[--vp->sp] = spcl_make_array(NULL, dst->val.i, vp);
	    } else if (op_meta(insts[pc]) == VAL_LIST) {
		vp->stack[--vp->sp] = spcl_make_list(NULL, dst->val.i, vp);
	    } else if (op_meta(insts[pc]) == VAL_STR) {
		vp->stack[--vp->sp] = spcl_make_str(NULL, dst->val.i, vp);
	    }
	    pc += 2;
	break;
	case OP_REA:
	    if (src->type != VAL_INT)
		return spcl_make_err(E_BAD_TYPE, vp, "list index must be an integer");
	    if (dst->type == VAL_LIST)
		vp->stack[--vp->sp] = dst->val.l[src->val.i];
	    else if (dst->type == VAL_ARRAY)
		vp->stack[--vp->sp] = spcl_make_num(dst->val.a[src->val.i]);
	    pc += 3;
	break;
	case OP_WEA:
	    if (src->type != VAL_INT)
		return spcl_make_err(E_BAD_TYPE, vp, "list index must be an integer");
	    if (dst->type == VAL_LIST) {
		dst->val.l[src->val.i] = vp->stack[vp->sp++];
	    } else if (dst->type == VAL_ARRAY) {
		tmps = vp->stack[vp->sp++];
		dst->val.a[src->val.i] = tmps.val.x;
	    }
	    pc += 3;
	break;
	case OP_PUSH:
	    vp->stack[--vp->sp] = *dst;
	    pc += 2;
	break;
	case OP_PSHCPY:
	    _pshcpy(vp, *dst);
	    pc += 2;
	break;
	case OP_POP:
	    debug_assert(vp->sp < getpagesize());
	    *dst = vp->stack[vp->sp++];
	    pc += 2;
	break;
	case OP_POPN:
	    debug_assert(vp->sp + op_meta(insts[pc]) < getpagesize());
	    vp->sp -= op_meta(insts[pc]);
	    ++pc;
	break;
	case OP_MOV:
	    *dst = *src;
	    pc += 3;
	break;
	case OP_CPY:
	    *dst = copy_spcl_val(*src, vp);
	    pc += 3;
	break;
	case OP_SWAP:
	    tmpd = *dst;
	    *dst = *src;
	    *src = tmpd;
	    pc += 3;
	break;
	

	case OP_ADD: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_add(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_SUB: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_sub(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_MUL: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_mul(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_DIV: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_div(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_MOD: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_mod(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_EXP: vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);val_exp(vp->stack+vp->sp,*src,vp);pc += 3;break;
	case OP_EQ:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (spcl_isfalse(vp->stack[vp->sp]))? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_NEQ:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (spcl_isfalse(vp->stack[vp->sp]))? spcl_make_int(0) : spcl_make_int(1);
	    pc += 3;
	break;
	case OP_GT:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (vp->stack[vp->sp].val.i > 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_LT:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (vp->stack[vp->sp].val.i < 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_GE:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (vp->stack[vp->sp].val.i >= 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_LE:
	    vp->stack[--vp->sp] = spcl_valcmp(*dst,*src,vp);
	    if (vp->stack[vp->sp].type == VAL_ERR)
		return vp->stack[vp->sp++];
	    vp->stack[vp->sp] = (vp->stack[vp->sp].val.i <= 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_NOT:
	    vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);
	    tmpd = *dst;
	    vp->stack[vp->sp] = (spcl_isfalse(tmpd))? spcl_make_int(1) : spcl_make_int(0);
	    pc += 2;
	break;
	case OP_OR:
	    vp->stack[--vp->sp] = copy_spcl_val(*dst,vp);
	    tmpd = *dst;
	    tmps = *src;
	    if (spcl_istrue(tmpd) || spcl_istrue(tmps))
		vp->stack[vp->sp] = spcl_make_int(1);
	    else
		vp->stack[vp->sp] = spcl_make_int(0);
	    pc += 3;
	break;
	case OP_AND:
	    vp->stack[--vp->sp] = copy_spcl_val(*dst, vp);
	    tmpd = *dst;
	    tmps = *src;
	    if (spcl_istrue(tmpd) && spcl_istrue(tmps))
		vp->stack[vp->sp] = spcl_make_int(1);
	    else
		vp->stack[vp->sp] = spcl_make_int(0);
	    pc += 3;
	break;
	case OP_INC: val_add(dst, spcl_make_int(1), vp);pc += 2;break;
	case OP_DEC: val_sub(dst, spcl_make_int(1), vp);pc += 2;break;

	case OP_RADD: val_add(dst, *src, vp);pc += 3;break;
	case OP_RSUB: val_sub(dst, *src, vp);pc += 3;break;
	case OP_RMUL: val_mul(dst, *src, vp);pc += 3;break;
	case OP_RDIV: val_div(dst, *src, vp);pc += 3;break;
	case OP_RMOD: val_mod(dst, *src, vp);pc += 3;break;
	case OP_REXP: val_exp(dst, *src, vp);pc += 3;break;
	case OP_REQ:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (spcl_isfalse(tmpd))? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_RNEQ:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (spcl_isfalse(tmpd))? spcl_make_int(0) : spcl_make_int(1);
	    pc += 3;
	break;
	case OP_RGT:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (tmpd.val.i > 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_RLT:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (tmpd.val.i < 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_RGE:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (tmpd.val.i >= 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_RLE:
	    tmpd = spcl_valcmp(*dst,*src,vp);
	    if (tmpd.type == VAL_ERR)
		return tmpd;
	    *dst = (tmpd.val.i <= 0)? spcl_make_int(1) : spcl_make_int(0);
	    pc += 3;
	break;
	case OP_RNOT: tmpd = *dst;*dst = (spcl_isfalse(tmpd))? spcl_make_int(1) : spcl_make_int(0);pc += 2;break;
	default: ++pc;break; //noop
	}
    }
    return (spcl_val){0};
}

void spcl_set_valn(spcl_inst *c, char* p_name, size_t namelen, spcl_val p_val, int copy, vproc *vp) {
    //generate a fake name if none was provided
    if (!p_name || p_name[0] == 0) {
	char tmp[SPCL_STR_BSIZE];
	snprintf(tmp, SPCL_STR_BSIZE, "\e_%lu", c->n_memb);
	return spcl_set_valn(c, tmp, namelen, p_val, copy, vp);
    }
    s8 tmp_name = (s8){p_name, namelen};
    size_t ti = fnv_1(tmp_name, c->t_bits);
    if (!find_ind(c, tmp_name, &ti)) {
	//if there isn't already an element with that name we have to expand the table and add a member
	if (grow_inst(c, vp))
	    find_ind(c, tmp_name, &ti);
	c->table[ti].s.s = strndup(p_name, namelen);
	c->table[ti].s.n = namelen;
	c->table[ti].v = (copy)? copy_spcl_val(p_val, vp) : p_val;
	++c->n_memb;
    } else {
	//otherwise we need to cleanup the old spcl_val and add the new
	cleanup_spcl_val( &(c->table[ti].v), NULL );
	c->table[ti].v = (copy)? copy_spcl_val(p_val, vp) : p_val;
    }
}
spcl_val spcl_find(spcl_inst *c, s8 name) {
    size_t ti = fnv_1(name, c->t_bits);
    if (!find_ind(c, name, &ti)) {
	return (spcl_val){0};
    }
    return c->table[ti].v;
}
int spcl_find_object(vproc *vp, const char* str, const char* typename, spcl_inst** sto) {
    spcl_val vobj = spcl_parse_line(vp, str);
    if (vobj.type != VAL_INST)
	return -1;
    //now check that the type matches
    char *tstr = vp->a.head;
    for (psize i = 0; str[i]; ++i)
	aappend(&vp->a, str[i]);
    memcpy(vp->a.head, ".__type__", strlen(".__type__"));
    vp->a.head += strlen(".__type__");
    aappend(&vp->a, 0);
    spcl_val tmp = spcl_parse_line(vp, tstr);
    if (tmp.type != VAL_STR || strncmp(tmp.val.s, typename, tmp.n_els))
	return -2;
    if (sto) *sto = vobj.val.c;
    return 0;
}
int spcl_find_c_iarray(vproc *vp, const char* str, int* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_parse_line(vp, str);
    if (!tmp.type)
	return -1;
    //bounds check
    size_t n_write = (tmp.n_els > n) ? n : tmp.n_els;
    for (size_t i = 0; i < n_write; ++i) {
	spcl_val sub = _spcl_index(tmp, spcl_make_num(i), vp);
	sto[i] = (int)sub.val.x;
    }
    return (int)n_write;
}
int spcl_find_c_uarray(vproc *vp, const char* str, unsigned* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_parse_line(vp, str);
    if (!tmp.type)
	return -1;
    //bounds check
    size_t n_write = (tmp.n_els > n) ? n : tmp.n_els;
    for (size_t i = 0; i < n_write; ++i) {
	spcl_val sub = _spcl_index(tmp, spcl_make_num(i), vp);
	sto[i] = (unsigned)sub.val.x;
    }
    return (int)n_write;
}
int spcl_find_c_darray(vproc *vp, const char* str, double* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_parse_line(vp, str);
    if (!tmp.type)
	return -1;
    //bounds check
    size_t n_write = (tmp.n_els > n) ? n : tmp.n_els;
    if (tmp.type == VAL_ARRAY) {
	memcpy(sto, tmp.val.a, sizeof(double)*n_write);
	return (int)n_write;
    } else if (tmp.type == VAL_LIST) {
	for (size_t i = 0; i < n_write; ++i) {
	    if (tmp.val.l[i].type != VAL_NUM)
		return -3;
	    sto[i] = tmp.val.l[i].val.x;
	}
	return (int)n_write;
    }
    return -2;
}
int spcl_find_c_str(vproc *vp, const char* str, char* sto, size_t n) {
    //we can't save anything to an empty buffer so exit early
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_parse_line(vp, str);
    if (tmp.type != VAL_STR) {
	*sto = 0;//set to an empty string
	return -1;
    }
    //bounds check
    size_t n_write = (tmp.n_els > n-1) ? n-1 : tmp.n_els;
    memcpy(sto, tmp.val.s, sizeof(char)*n_write);
    sto[n_write] = 0;
    return (int)n_write;
}
int spcl_find_int(vproc *vp, const char* str, int* sto) {
    spcl_val tmp = spcl_parse_line(vp, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = (int)tmp.val.x;
    return 0;
}
int spcl_find_uint(vproc *vp, const char* str, unsigned* sto) {
    spcl_val tmp = spcl_parse_line(vp, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = (size_t)tmp.val.x;
    return 0;
}
int spcl_find_float(vproc *vp, const char* str, double* sto) {
    spcl_val tmp = spcl_parse_line(vp, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = tmp.val.x;
    return 0;
}
/**
 * For if, while, and for blocks, we need to find the enclosing block
 */
/*static inline spcl_val get_block(spcl_key k, read_state* rs, psize* state_end) {
    //store the final end so that we can easily reset rs
    psize open_ind, close_ind;
    char open_char = 0;
    psize op_loc;
    while (1) {
	spcl_val v = find_operator(*rs, &op_loc, &open_ind, &close_ind, &(rs->end));
	if (v.type == VAL_ERR)
	    return v;
	if (open_ind >= rs->end) {
	    //we only accept single line blocks if there was a perenthesis that produces a well defined end and the statement is not a function.
	    if (open_char != BEG_PAR || k == KEY_FN)
		return spcl_make_err(E_BAD_SYNTAX, vp, "expected a block enclosed by {...} after keyword %s", spcl_keywords[k]);
	    return spcl_make_num(0);
	}
	open_char = fs_get(rs->b, open_ind);
	if (open_char == BEG_CRL) {
	    *state_end = open_ind;
	    rs->start = open_ind+1;
	    rs->end = close_ind;
	    return spcl_make_num(1);
	}
	//move the block forward
	rs->end = fs_end(rs->b);
	rs->start = skip_ws(rs->b, close_ind, rs->end, 1);
	*state_end = close_ind;
    }
    return spcl_make_err(E_BAD_SYNTAX, vp, "something that should be impossible happened! congratulations!");
}
static inline spcl_val spcl_read_lines_block(struct spcl_inst* c, read_state block_rs) {
    spcl_val ret;
    psize end;
    read_state rs = make_read_state(block_rs.b, block_rs.start, block_rs.end);

    //iterate over each line in the file
    while (rs.start < block_rs.end) {
	rs.end = block_rs.end;

	//look for keywords at the start of a line. If fast-forwarding takes us to a newline, then this string was empty unless there was a keyword.
	spcl_key start_key = get_keyword(&rs);
	if (start_key == KEY_BREAK || start_key == KEY_CONT) {
	    //TODO: return break and continue statements should immediately exit
	    //return sto;
	} else if (start_key == KEY_IMPORT) {
	    rs.start = skip_ws(rs.b, rs.start, rs.end, 0);
	    //TODO: allow enclosed quotes for files with whitespace
	    s8 name = fs_read(rs.b, rs.start, rs.end);
	    name.n = (psize)(strchr(name.s, '\n') - name.s);
	    spcl_fstream* fs = make_spcl_fstreamn(name.s,  name.n);
	    if (!fs)
		return spcl_make_err(E_BAD_VALUE, "couldn't open file %.*s", name.n, name.s);
	    ret = spcl_read_lines(c, fs);
	    //set the end to the last character in the line
	    end = rs.start + name.n;
	}*//* else if (start_key == KEY_IF) {
	    read_state sub_rs = rs;
	    ret = get_block(start_key, &sub_rs, &rs.end);
	    //keep going through else statements until we find something true
	    while (ret.type != VAL_ERR) {
		if ( spcl_isfalse(spcl_parse_line_rs(c, rs, &end, KEY_NONE)) ) {
		    sub_rs.start = skip_ws(rs.b, sub_rs.start, sub_rs.end, 1);
		    start_key = get_keyword(&sub_rs);
		    if (start_key == KEY_ELSE) {
			//if this is an else if statement, we should check the conditional again
			if (get_keyword(&sub_rs) == KEY_IF) {
			    sub_rs.end = block_rs.end;
			    ret = get_block(start_key, &sub_rs, &rs.end);
			    continue;
			}
			//otherwise execute the else statement
			ret = get_block(start_key, &sub_rs, &rs.end);
		    }
		}
	    }
	}*//* else {
	    ret = spcl_parse_line_rs(c, rs, &end, start_key);
	}
	if (ret.type == VAL_ERR || start_key == KEY_RET) {
	    if (start_key != KEY_RET && ret.val.e) {
		s8 line = fs_read(rs.b, rs.start, fs_line_end(rs.b, rs.start));
		fprintf(stderr, "\e[1m\033[31mError\033[0m\e[1m %s on line %lu:\e[m %.*s\n\t%s\n", errnames[ret.val.e->c], fs_find_line(rs.b, rs.start)+1, (int)line.n, line.s, ret.val.e->msg);
		free(ret.val.e);
		ret.val.e = NULL;
	    }
	    return ret;
	}
	//TODO: Currently I'm using end=SIZE_MAX to signal that the line just read wanted to return a value and go back to the previous stack frame. We should decide if this is actually a good way of doing things or an ugly hack.
	if (end == SIZE_MAX)
	    return ret;
	//if its a comment we should skip this line
	if (fs_get(rs.b, end) == '#') {
	    rs.start = fs_line_end(rs.b, end);
	    ++rs.start;
	} else {
	    rs.start = end+1;
	}
    }
    return spcl_make_none();
}*/

spcl_val spcl_read_lines(vproc *vp, const spcl_fstream* b) {
    read_state rs = make_read_state(b, 0, b->flen);
    return spcl_read_lines_block(vp, rs);
}

vproc *spcl_read_file(const char* fname, int argc, const char** argv) {
    //read command line arguments
    spcl_val ret = {0};
    //create a new spcl_inst
    vproc *vp = make_vproc();
    //write the prefix to start a list
    const char *prefix = "sys.argv=[";
    char *argv_str = vp->a.head;
    memcpy(vp->a.head, prefix, strlen(prefix));
    vp->a.head += strlen(prefix);
    //read each of the arguments
    if (argc > 0 && argv) {
	for (int i = 0; i < argc; ++i) {
	    //store whether argv[i] is a flag like -r
	    int is_flag = 0;
	    //find the first non-dash character
	    int k = 0;
	    if (argv[i][0] == '-' && argv[i][1] != '-') {
		aappend(&vp->a, '\"');
		k = 1;
		is_flag = 1;
	    } else if (argv[i][0] == '-' && argv[i][1] == '-') {
		k = 2;
	    }
	    for (;; ++k) {
		//when we reach the end of an argument either add a comma or end brace
		if (argv[i][k] == 0) {
		    //we have to put the close quote around flags
		    if (is_flag)
			aappend(&vp->a, '\"');
		    //add a comma between elements but not after the last one
		    if (i+1 < argc)
			aappend(&vp->a, ',');
		    break;
		}
		aappend(&vp->a, argv[i][k]);
	    }
	}
    }
    aappend(&vp->a, ']');
    //now read the argv buffer and free memory
    spcl_val er = spcl_parse_line(vp, argv_str);
    if (er.type == VAL_ERR) {
	read_state rs;
	rs.start = 0;
	rs.end = (char*)vp->a.head - argv_str;
	rs.b = make_spcl_fstream_str(argv_str, rs.end);
	_print_error(stderr, er, rs);
	destroy_spcl_fstream(rs.b);
	destroy_vproc(vp);
	return NULL;
    }
    reset(&vp->a);

    //read the rest of the file and check to ensure the file was opened successfully
    spcl_fstream* fs = make_spcl_fstream(fname);
    if (fs) {
	er = spcl_read_lines(vp, fs);
	destroy_spcl_fstream(fs);
	if (!er.type) {
	    destroy_vproc(vp);
	    return NULL;
	}
    } else {
	fprintf(stderr, "couldn't open file %s", fname);
	destroy_vproc(vp);
	return NULL;
    }
    return vp;
}

/** ============================ spcl_uf ============================ **/

/**
 * create a new user function using a read state rs and a set of arguments
 * rs: the read state of the start of the function declaration
 * arg_inds: indices for each argument
 * n_args: the number of arguments. Note that arg_inds must have one more value allocated than n_args so that it can store the termination points for each stri
 * new_end: we must track the final location so that the caller fast-forwards to the end of the declaration
 * returns: a spcl_val with the function set
 */
/*static inline spcl_val spcl_make_fn_rs(struct spcl_inst* c, read_state rs, psize* arg_inds, size_t n_args, psize* new_end) {
    //ensure that we can store the end location
    if (!new_end)
	return spcl_make_err(E_BAD_SYNTAX, vp, "declared function without room to grow");
    //fast forward to the open curly brace
    psize args_end = arg_inds[n_args]+1;
    args_end = skip_ws(rs.b, args_end, rs.end, 0);
    if (fs_get(rs.b, args_end) != BEG_CRL)
	return spcl_make_err(E_BAD_SYNTAX, vp, "unexpected %c", fs_get(rs.b, args_end));
    psize op_loc, open_ind, close_ind;
    spcl_val sto = find_operator(make_read_state(rs.b, args_end, fs_end(rs.b)), &op_loc, &open_ind, &close_ind, new_end);
    //let errors fall through
    if (sto.type == VAL_ERR)
	return sto;
    //if we got here, then we can proceed without errors
    sto.type = VAL_FN;
    sto.val.f = xmalloc(sizeof(spcl_uf));
    sto.n_els = n_args;
    //setup the call signature
    sto.val.f->call_sig.name = (s8){0};
    sto.val.f->call_sig.n_args = n_args;
    //copy function argument names
    for (size_t i = 0; i < n_args && i+1 < SPCL_ARGS_BSIZE; ++i) {
	psize this_start = arg_inds[i]+1;
	//we have to fast forward until we're on the same line so that we can safely use fs_read
	s8 argname = trim_whitespace(fs_read(rs.b, this_start, arg_inds[i+1]));
	sto.val.f->call_sig.args[i] = spcl_make_str(argname.s, argname.n);
    }
    sto.val.f->code_lines = make_read_state(rs.b, open_ind+1, close_ind);
    sto.val.f->exec = NULL;
    //we change the parent in spcl_uf_eval. However, calling with NULL indicates no parent, so we must pass a dummy
    sto.val.f->fn_scope = make_spcl_inst(c);
    return sto;
}*/
spcl_uf* make_spcl_uf_ex(lib_call p_exec, vproc *vp) {
    spcl_uf* uf = xmalloc(sizeof(spcl_uf), vp);
    uf->code_lines = make_read_state(NULL, 0, 0);
    uf->call_sig.name = (s8){0};
    uf->call_sig.n_args = 0;
    uf->exec = p_exec;
    uf->fn_scope = NULL;
    return uf;
}
spcl_uf* copy_spcl_uf(const spcl_uf* o, vproc *vp) {
    spcl_uf* uf = xmalloc(sizeof(spcl_uf), vp);
    memcpy(uf, o, sizeof(spcl_uf));
    uf->call_sig.name = (s8){0};
    return uf;
}
//deallocation
void destroy_spcl_uf(spcl_uf* uf, vproc *vp) {
    cleanup_spcl_fn_call(&(uf->call_sig));
    if (uf->fn_scope)
	destroy_spcl_inst(uf->fn_scope, vp);
    xfree(uf, vp);
}
spcl_val spcl_uf_eval(spcl_uf* uf, spcl_fn_call call, vproc *vp) {
    if (uf->exec) {
	return (*uf->exec)(call, vp);
    } else if (uf->code_lines.b) {
	return (spcl_val){0};
	/*if (call.n_args != uf->call_sig.n_args)
	    return spcl_make_err(E_LACK_TOKENS, "%.*s() expected %lu arguments, got %lu", call.name.n, call.name.s, uf->call_sig.n_args, call.n_args);
	//setup a new scope with function arguments defined
	uf->fn_scope->parent = c;
	for (size_t i = 0; i < uf->call_sig.n_args; ++i) {
	    spcl_set_valn(uf->fn_scope, uf->call_sig.args[i].val.s, uf->call_sig.args[i].n_els, call.args[i], 0);
	}
	spcl_val ret = spcl_read_lines_block(uf->fn_scope, uf->code_lines);
	//function calls make shallow copies, so we need to reset memory to avoid double frees
	memset( uf->fn_scope->table, 0, sizeof(name_val_pair)*con_size(uf->fn_scope) );
	return ret;*/
    }
    //return spcl_make_err(E_BAD_VALUE, vp, "function not implemented");
}
