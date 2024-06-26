#include "utils.h"
#include "speclang.h"

#if SPCL_DEBUG_LVL==0
typedef enum { KEY_NONE, KEY_IMPORT, KEY_CLASS, KEY_IF, KEY_FOR, KEY_ELSE, KEY_WHILE, KEY_BREAK, KEY_CONT, KEY_RET, KEY_FN, SPCL_N_KEYS } spcl_key;
#endif
static s8 spcl_keywords[SPCL_N_KEYS] = {s8(" "), s8("import"), s8("class"), s8("if"), s8("for"), s8("else"), s8("while"), s8("break"), s8("continue"), s8("return"), s8("fn")};
static const char* const errnames[N_ERRORS] =
{"SUCCESS", "NO_FILE", "LACK_TOKENS", "BAD_SYNTAX", "BAD_VALUE", "BAD_TYPE", "NOMEM", "NAN", "UNDEFINED_TOKEN", "OUT_OF_BOUNDS", "ASSERT"};
static const char* const valnames[N_VALTYPES] = {"none", "error", "numeric", "string", "array", "list", "fn", "obj"};

#define spcl_isfalse(v) (v.type == VAL_UNDEF || (v.type == VAL_NUM && v.val.x == 0) || v.n_els == 0)
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
static inline char fs_get(const spcl_fstream* fs, psize pos) {
    //TODO: this won't work correctly once we switch to actually streaming files
    if (pos >= fs->clen)
        return 0;
    return fs->cache[pos];
}

/** ======================================================== utility functions ======================================================== **/

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
	//now look for matches
	if (cur == c && blk_stk.ptr == 0)
	    return s;
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
    spcl_fstream* fs = xmalloc(sizeof(spcl_fstream));
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
spcl_fstream* make_spcl_fstream_str(const char* str, size_t n) {
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
spcl_fstream* make_spcl_fstreamn(const char* p_fname, size_t n) {
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
void destroy_spcl_fstream(spcl_fstream* fs) {
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
	    cleanup_spcl_val(f->args+i);
    }
}

/** ======================================================== name_val_pair ======================================================== **/

void cleanup_name_val_pair(name_val_pair nv) {
    if (nv.s.s)
	free(nv.s.s);
    cleanup_spcl_val(&nv.v);
}

/** ======================================================== builtin functions ======================================================== **/
spcl_val get_sigerr(spcl_fn_call f, size_t min_args, size_t max_args, const valtype* sig) {
    if (!sig || max_args < min_args)
	return spcl_make_none();
    if (f.n_args < min_args)
	return spcl_make_err(E_LACK_TOKENS, "%.*s expected %lu arguments, got %lu", f.name.n, f.name.s, min_args, f.n_args);
    if (f.n_args > max_args)
	return spcl_make_err(E_LACK_TOKENS, "%.*s with too many arguments, %lu", f.name.n, f.name.s, f.n_args);
    for (size_t i = 0; i < f.n_args; ++i) {
	//treat undefined as allowing for arbitrary type
	if (sig[i] && f.args[i].type != sig[i]) {
	    //if the type is an error, let it pass through
	    if (f.args[i].type == VAL_ERR)
		return f.args[i];
	    return spcl_make_err(E_BAD_TYPE, "%.*s expected args[%lu].type=%s, got %s", f.name.n, f.name.s, i, valnames[sig[i]], valnames[f.args[i].type]);
	}
	if (sig[i] > VAL_NUM && f.args[i].val.s == NULL)
	    return spcl_make_err(E_BAD_TYPE, "%.*s found empty %s at args[%lu]", f.name.n, f.name.s, valnames[sig[i]], i);
    }
    return spcl_make_none();
}
static const valtype ANY1_SIG[] = {VAL_UNDEF};
static const valtype NUM1_SIG[] = {VAL_NUM};
static const valtype ARR1_SIG[] = {VAL_ARRAY};
spcl_val spcl_assert(struct spcl_inst*c, spcl_fn_call f) {
    static const valtype ASSERT_SIG[] = {VAL_UNDEF, VAL_STR};
    spcl_sigcheck_opts(f, 1, ASSERT_SIG);
    if (f.args[0].val.x == 0)
	return (f.n_args == 1)? spcl_make_err(E_ASSERT, "") : spcl_make_err(E_ASSERT, "%s", f.args[1].val.s);
    return spcl_make_num(f.args[0].val.x);
}
spcl_val spcl_typeof(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, ANY1_SIG);
    spcl_val sto;
    sto.type = VAL_STR;
    //handle instances as a special case
    if (f.args[0].type == VAL_INST) {
	spcl_val t = spcl_find(f.args[0].val.c, "__type__");
	if (t.type == VAL_STR) {
	    sto.n_els = t.n_els;
	    sto.val.s = xmalloc(sizeof(char)*(sto.n_els+1));
	    for (size_t i = 0; i < sto.n_els; ++i)
		sto.val.s[i] = t.val.s[i];
	    sto.val.s[sto.n_els] = 0;
	} else {
	    return spcl_make_none();
	}
	return sto;
    }
    sto.n_els = strlen(valnames[f.args[0].type])+1;
    sto.val.s = strdup(valnames[f.args[0].type]);
    return sto;
}
spcl_val spcl_len(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, ANY1_SIG);
    return spcl_make_num(f.args[0].n_els);
}
//create a list with undefined elements
static const valtype LIST_SIG[] = {VAL_NUM};
spcl_val spcl_list(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, LIST_SIG);
    if (f.args[0].val.x < 0)
	return spcl_make_err(E_OUT_OF_RANGE, "cannot create list with negative number of elements");
    spcl_val ret;
    ret.type = VAL_LIST;
    ret.n_els = (size_t)(f.args[0].val.x);
    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els);
    memset(ret.val.l, 0, sizeof(spcl_val)*ret.n_els);
    return ret;
}
static const valtype RANGE_SIG[] = {VAL_NUM, VAL_NUM, VAL_NUM};
spcl_val spcl_range(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck_opts(f, 1, RANGE_SIG);
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
	return spcl_make_err(E_BAD_VALUE, "range(%f, %f, %f) with invalid increment", min, max, inc);
    spcl_val ret;
    ret.type = VAL_ARRAY;
    ret.n_els = (max - min) / inc;
    ret.val.a = xmalloc(sizeof(double)*ret.n_els);
    for (size_t i = 0; i < ret.n_els; ++i)
	ret.val.a[i] = i*inc + min;
    return ret;
}
static const valtype LINSPACE_SIG[] = {VAL_NUM, VAL_NUM, VAL_NUM};
spcl_val spcl_linspace(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, LINSPACE_SIG);
    spcl_val ret;
    ret.type = VAL_ARRAY;
    ret.n_els = (size_t)(f.args[2].val.x);
    //prevent divisions by zero
    if (ret.n_els < 2)
	return spcl_make_err(E_BAD_VALUE, "cannot make linspace with size %lu", ret.n_els);
    ret.val.a = xmalloc(sizeof(double)*ret.n_els);
    double step = (f.args[1].val.x - f.args[0].val.x)/(ret.n_els - 1);
    for (size_t i = 0; i < ret.n_els; ++i) {
	ret.val.a[i] = step*i + f.args[0].val.x;
    }
    return ret;
}
STACK_DEF(spcl_val,LST_MAX)
STACK_DEF(size_t,LST_MAX)
static const valtype FLATTEN_SIG[] = {VAL_LIST};
spcl_val spcl_flatten(struct spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, FLATTEN_SIG);
    spcl_val ret = spcl_make_none();
    spcl_val cur_list = f.args[0];
    //flattening an empty list is the identity op.
    if (cur_list.n_els == 0 || cur_list.val.l == NULL) {
	ret.type = VAL_LIST;
	return ret;
    }
    size_t cur_st = 0;
    //there may potentially be nested lists, we need to be able to find our way back to the parent and the index once we're done
    stack(spcl_val,LST_MAX) lists = make_stack(spcl_val,LST_MAX)();
    stack(size_t,LST_MAX) inds = make_stack(size_t,LST_MAX)();
    //just make sure that there's a root level on the stack to be popped out
    if ( push(spcl_val,LST_MAX)(&lists, cur_list) ) return ret;
    if ( push(size_t,LST_MAX)(&inds, 0) ) return ret;

    //this is used for estimating the size of the buffer we need. Take however many elements were needed for this list and assume each sub-list has the same number of elements
    size_t base_n_els = cur_list.n_els;
    //start with the number of elements in the lowest order of the list
    size_t buf_size = cur_list.n_els;
    ret.val.l = xmalloc(sizeof(spcl_val)*buf_size);
    size_t j = 0;
    do {
	size_t i = cur_st;
	size_t start_depth = inds.ptr;
	for (; i < cur_list.n_els; ++i) {
	    if (cur_list.val.l[i].type == VAL_LIST) {
		if ( push(spcl_val,LST_MAX)(&lists, cur_list) ) return ret;
		if ( push(size_t,LST_MAX)(&inds, i+1) ) return ret;//push + 1 so that we start at the next index instead of reading the list again
		cur_list = cur_list.val.l[i];
		cur_st = 0;
		break;
	    }
	    if (j >= buf_size) {
		//-1 since we already have at least one element. no base_n_els=0 check is needed since that case will ensure the for loop is never evaluated
		buf_size += (base_n_els-1)*(i+1);
		spcl_val* tmp_val = xrealloc(ret.val.l, sizeof(spcl_val)*buf_size);
		if (!tmp_val) {
		    xfree(ret.val.l);
		    cleanup_spcl_fn_call(&f);
		    destroy_stack(spcl_val,LST_MAX)(&lists, &cleanup_spcl_val);
		    return spcl_make_err(E_NOMEM, "");
		}
		ret.val.l = tmp_val;
	    }
	    ret.val.l[j++] = copy_spcl_val(cur_list.val.l[i]);
	}
	//if we reached the end of a list without any sublists then we should return back to the parent list
	if (inds.ptr <= start_depth) {
	    pop(size_t,LST_MAX)(&inds, &cur_st);
	    pop(spcl_val,LST_MAX)(&lists, &cur_list);
	}
    } while (lists.ptr);
    ret.type = VAL_LIST;
    ret.n_els = j;
    return ret;
}
spcl_val spcl_cat(struct spcl_inst* c, spcl_fn_call f) {
    spcl_val sto;
    if (f.n_args < 2)
	return spcl_make_err(E_LACK_TOKENS, "cat() expected 2 arguments but got %lu", f.n_args);
    spcl_val l = f.args[0];
    spcl_val r = f.args[1];
    size_t l1 = l.n_els;
    size_t l2 = (r.type == VAL_LIST || r.type == VAL_ARRAY)? r.n_els : 1;
    //special case for matrices, just append a new row
    if (l.type == VAL_MAT && r.type == VAL_ARRAY) {
	sto.type = VAL_MAT;
	sto.n_els = l1 + 1;
	sto.val.l = xmalloc(sizeof(spcl_val)*sto.n_els);
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(l.val.l[i]);
	sto.val.l[l1] = copy_spcl_val(r);
	return sto;
    }
    //otherwise we have to do something else
    if (l.type != VAL_LIST && r.type != VAL_ARRAY)
	return spcl_make_err(E_BAD_TYPE, "called cat() with types <%s> <%s>", valnames[l.type], valnames[r.type]);
    sto.type = l.type;
    sto.n_els = l1 + l2;
    //deep copy the first list/array
    if (sto.type == VAL_LIST) {
	sto.val.l = xmalloc(sizeof(spcl_val)*sto.n_els);
	if (!sto.val.l) return spcl_make_err(E_NOMEM, "");
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(l.val.l[i]);
	if (r.type == VAL_LIST) {
	    //list -> list
	    for (size_t i = 0; i < l2; ++i)
		sto.val.l[i+l1] = copy_spcl_val(r.val.l[i]);
	} else if (r.type == VAL_ARRAY) {
	    //array -> list
	    for (size_t i = 0; i < l2; ++i)
		sto.val.l[i+l1] = spcl_make_num(r.val.a[i]);
	} else {
	    //anything -> list
	    sto.val.l[l1] = copy_spcl_val(r);
	}
    } else {
	sto.val.a = xmalloc(sizeof(double)*sto.n_els);
	if (!sto.val.a) return spcl_make_err(E_NOMEM, "");
	for (size_t i = 0; i < l1; ++i)
	    sto.val.l[i] = copy_spcl_val(f.args[0].val.l[i]);
	if (r.type == VAL_LIST) {
	    //list -> array
	    for (size_t i = 0; i < l2; ++i) {
		if (r.val.l[i].type != VAL_NUM) {
		    xfree(sto.val.a);
		    return spcl_make_err(E_BAD_TYPE, "can only concatenate numeric lists to arrays");
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
	    return spcl_make_err(E_BAD_TYPE, "called cat() with types <%s> <%s>", valnames[l.type], valnames[r.type]);
	}
    }
    return sto;
}
/**
 * print the elements to the console
 */
spcl_val spcl_print(struct spcl_inst* inst, spcl_fn_call f) {
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
		    default: return spcl_make_err(E_BAD_SYNTAX, "unrecognized escape sequence %c%c", c[0], c[1]);
		}
		++i;
	    } else if (*c == '%') {
		if (j >= f.n_args)
		    return spcl_make_err(E_LACK_TOKENS, "too few tokens for format string");
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
spcl_val spcl_array(spcl_inst* c, spcl_fn_call f) {
    spcl_sigcheck(f, ARRAY_SIG);
    //treat matrices with one row as vectors
    if (f.n_args == 1) {
	if (f.args[0].val.l[0].type == VAL_LIST)
	    return spcl_cast(f.args[0], VAL_MAT);
	else
	    return spcl_cast(f.args[0], VAL_ARRAY);
    }
    spcl_val ret;
    //otherwise we need to do more work
    size_t n_cols = f.args[0].n_els;
    ret.type = VAL_MAT;
    ret.n_els = f.n_args;
    ret.val.l = xmalloc(sizeof(spcl_val)*f.n_args);
    //iterate through rows
    for (size_t i = 0; i < f.n_args; ++i) {
	if (f.args[i].type == VAL_LIST) {
	    xfree(ret.val.l);
	    return spcl_make_err(E_BAD_TYPE, "non list encountered in matrix");
	}
	if (f.args[i].n_els != n_cols) {
	    xfree(ret.val.l);
	    return spcl_make_err(E_BAD_VALUE, "can't create matrix from ragged array");
	}
	ret.val.l[i] = spcl_cast(f.args[i], VAL_ARRAY);
	//check for errors
	if (ret.val.l[i].type == VAL_ERR) {
	    xfree(ret.val.l);
	    ret = copy_spcl_val(ret.val.l[i]);
	    return ret;
	}
    }
    return ret;
}

spcl_val spcl_vec(spcl_inst* c, spcl_fn_call f) {
    spcl_val ret = spcl_make_none();
    //just copy the elements
    ret.type = VAL_ARRAY;
    ret.n_els = f.n_args;
    //skip copying an empty list
    if (ret.n_els == 0)
	return ret;
    ret.val.a = xmalloc(sizeof(double)*ret.n_els);
    for (size_t i = 0; i < f.n_args; ++i) {
	if (f.args[i].type != VAL_NUM) {
	    xfree(ret.val.a);
	    return spcl_make_err(E_BAD_TYPE, "cannot cast list with non-numeric types to array");
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
#define WRAP_MATH_FN(FN) spcl_val TYPED(spcl,FN)(struct spcl_inst* c, spcl_fn_call f) {	\
    spcl_val sto = get_sigerr(f, SIGLEN(NUM1_SIG), SIGLEN(NUM1_SIG), NUM1_SIG);		\
    if (sto.type == 0)									\
	return spcl_make_num( FN(f.args[0].val.x) );					\
    cleanup_spcl_val(&sto);								\
    sto = get_sigerr(f, SIGLEN(ARR1_SIG), SIGLEN(ARR1_SIG), ARR1_SIG);			\
    if (sto.type == 0) {								\
	sto.type = VAL_ARRAY;								\
	sto.n_els = f.args[0].n_els;							\
	sto.val.a = malloc(sizeof(double)*sto.n_els);					\
	if (!sto.val.a)									\
	    return spcl_make_err(E_NOMEM, "");						\
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
spcl_val spcl_strtod(spcl_inst* c, spcl_fn_call f) {
    static const valtype STRTOD_SIG[] = {VAL_STR, VAL_NUM};
    spcl_sigcheck_opts(f, 1, STRTOD_SIG);

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
	} else if (base > 10 && s[i]|0x20 >= 'a' && s[i]|0x20 <= 'a'+base-9) {
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
	    spcl_val exp = spcl_strtod(c, f);
	    if (exp.type != VAL_NUM)
		return exp;
	    return spcl_make_num( sgn*x*pow(base, exp.val.x) );
	} else {
	    return spcl_make_err(E_BAD_SYNTAX, "invalid numeric literal");
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

spcl_val spcl_make_err(parse_ercode code, const char* format, ...) {
    spcl_val ret;
    ret.type = VAL_ERR;
    //a nomemory error obviously won't be able to allocate any more memory
    if (code == E_NOMEM) {
	fprintf(stderr, "kernel panic: out of memory!\n");
	ret.val.e = NULL;
	return ret;
    }
    ret.val.e = xmalloc(sizeof(struct spcl_error));
    ret.val.e->c = code;
    va_list args;
    va_start(args, format);
    int tmp = vsnprintf(ret.val.e->msg, ERR_BSIZE, format, args);
    va_end(args);
    ret.n_els = (tmp < 0)? ERR_BSIZE : (size_t)tmp;
    return ret;
}

spcl_val spcl_make_num(double x) {
    spcl_val v;
    v.type = VAL_NUM;
    v.n_els = 1;
    v.val.x = x;
    return v;
}

spcl_val spcl_make_str(const char* s, size_t n) {
    spcl_val v;
    v.type = VAL_STR;
    v.n_els = n;
    //we allocate one more than the actual length to null terminate
    v.val.s = xmalloc(sizeof(char)*(v.n_els+1));
    memcpy(v.val.s, s, n);
    v.val.s[n] = 0;
    return v;
}
spcl_val spcl_make_array(double* vs, size_t n) {
    spcl_val v;
    v.type = VAL_ARRAY;
    v.n_els = n;
    v.val.a = xmalloc(sizeof(double)*v.n_els);
    memcpy(v.val.a, vs, sizeof(double)*n);
    return v;
}
spcl_val spcl_make_list(const spcl_val* vs, size_t n_vs) {
    spcl_val v;
    v.type = VAL_LIST;
    v.n_els = n_vs;
    v.val.l = xmalloc(sizeof(spcl_val)*v.n_els);
    for (size_t i = 0; i < v.n_els; ++i) v.val.l[i] = copy_spcl_val(vs[i]);
    return v;
}
spcl_val spcl_make_fn(const char* name, size_t n_args, spcl_val (*p_exec)(spcl_inst*, spcl_fn_call)) {
    spcl_val ret;
    ret.type = VAL_FN;
    ret.n_els = n_args;
    ret.val.f = make_spcl_uf_ex(p_exec);
    return ret;
}
spcl_val spcl_make_inst(spcl_inst* parent, const char* s) {
    spcl_val v;
    v.type = VAL_INST;
    v.val.c = make_spcl_inst(parent);
    if (s && s[0] != 0) {
	spcl_val tmp = spcl_make_str(s, strlen(s));
	spcl_set_val(v.val.c, "__type__", tmp, 0);
    }
    return v;
}
spcl_val spcl_valcmp(spcl_val a, spcl_val b) {
    if (a.type != b.type || a.type == VAL_ERR || b.type == VAL_ERR)
	return spcl_make_err(E_BAD_VALUE, "cannot compare types %s and %s", valnames[a.type], valnames[b.type]);
    if (a.type == VAL_NUM) {
	return spcl_make_num(a.val.x - b.val.x);
    } else if (a.type == VAL_STR) {
	return spcl_make_num(spcl_strcmp(a, b));
    } else if (a.type == VAL_LIST) {
	if (a.n_els != b.n_els)
	    return spcl_make_num(a.n_els - b.n_els);
	spcl_val tmp;
	for (size_t i = 0; i < a.n_els; ++i) {
	    tmp = spcl_valcmp(a.val.l[i], b.val.l[i]);
	    if (tmp.val.x)
		return tmp;
	}
	return spcl_make_num(0);
    } else if (a.type == VAL_ARRAY) {
	if (a.n_els != b.n_els)
	    return spcl_make_num(a.n_els - b.n_els);
	for (size_t i = 0; i < a.n_els; ++i) {
	    if (a.val.a[i] != b.val.a[i])
		return spcl_make_num(a.val.a[i] - b.val.a[i]);
	}
	return spcl_make_num(0);
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

spcl_val spcl_cast(spcl_val v, valtype t) {
    if (v.type == VAL_UNDEF)
	return spcl_make_err(E_BAD_TYPE, "cannot cast <undefined> to <%s>", valnames[t]);
    //trivial casts should just be copies
    if (v.type == t)
	return copy_spcl_val(v);
    spcl_val ret;
    ret.type = t;
    ret.n_els = v.n_els;
    if (t == VAL_LIST) {
	if (v.type == VAL_ARRAY) {
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els);
	    for (size_t i = 0; i < ret.n_els; ++i)
		ret.val.l[i] = spcl_make_num(v.val.a[i]);
	    return ret;
	} else if (v.type == VAL_INST) {
	    //instance -> list
	    ret.n_els = v.val.c->n_memb;
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els);
	    memset(ret.val.l, 0, sizeof(spcl_val)*ret.n_els);
	    for (size_t i = con_it_next(v.val.c, 0); i < con_size(v.val.c); i = con_it_next(v.val.c, i+1))
		ret.val.l[i] = copy_spcl_val(v.val.c->table[i].v);
	    ret.n_els = v.n_els;
	    return ret;
	} else if (v.type == VAL_MAT) {
	    //matrices are basically just an alias for lists
	    ret = copy_spcl_val(v);
	    ret.type = t;
	    return ret;
	}
    } else if (t == VAL_MAT) {
	if (v.type == VAL_LIST) {
	    ret.val.l = xmalloc(sizeof(spcl_val)*ret.n_els);
	    for (size_t i = 0; i < ret.n_els; ++i) {
		//first try making the element an array
		spcl_val tmp = spcl_cast(v.val.l[i], VAL_ARRAY);
		if (tmp.type == VAL_ERR) {
		    //if that doesn't work try making it a matrix
		    cleanup_spcl_val(&tmp);
		    tmp = spcl_cast(v.val.l[i], VAL_MAT);
		    if (tmp.type == VAL_ERR) {
			//if both of those failed, give up
			xfree(ret.val.l);
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
	    ret.val.a = xmalloc(sizeof(double)*ret.n_els);
	    for (size_t i = 0; i < ret.n_els; ++i) {
		if (v.val.l[i].type != VAL_NUM) {
		    xfree(ret.val.a);
		    return spcl_make_err(E_BAD_TYPE, "cannot cast list with non-numeric types to array");
		}
		ret.val.a[i] = v.val.l[i].val.x;
	    }
	    return ret;
	}
    } else if (t == VAL_STR) {
	//anything -> string
	ret.val.s = xmalloc(sizeof(char)*SPCL_STR_BSIZE);
	char* end = spcl_stringify(v, ret.val.s, SPCL_STR_BSIZE);
	ret.n_els = (size_t)(end-ret.val.s);
    }
    //if we reach this point in execution then there was an error
    ret.type = VAL_UNDEF;
    ret.n_els = 0;
    return ret;
}

void cleanup_spcl_val(spcl_val* v) {
    if (v->type == VAL_ERR) {
	xfree(v->val.e);
    } else if ((v->type == VAL_STR && v->val.s) || (v->type == VAL_ARRAY && v->val.a)) {
	xfree(v->val.s);
    } else if ((v->type == VAL_LIST || v->type == VAL_MAT) && v->val.l) {
	for (size_t i = 0; i < v->n_els; ++i)
	    cleanup_spcl_val(v->val.l + i);
	xfree(v->val.l);
    } else if (v->type == VAL_ARRAY && v->val.a) {
	xfree(v->val.a);
    } else if (v->type == VAL_INST && v->val.c) {
	destroy_spcl_inst(v->val.c);
    } else if (v->type == VAL_FN && v->val.f) {
	destroy_spcl_uf(v->val.f);
    }
    v->type = VAL_UNDEF;
    v->val.x = 0;
    v->n_els = 0;
}

spcl_val copy_spcl_val(const spcl_val o) {
    spcl_val ret;
    ret.type = o.type;
    ret.n_els = o.n_els;
    //strings or lists must be copied
    switch (o.type) {
	case VAL_ERR:	ret.val.e = xmalloc(sizeof(spcl_error)); memcpy(ret.val.e, o.val.e, sizeof(spcl_error)); break;
	//case VAL_STR:	ret.val.s = xmalloc(o.n_els); strncpy(ret.val.s, o.val.s, o.n_els); break;
	case VAL_STR:	ret.val.s = xmalloc(o.n_els); memcpy(ret.val.s, o.val.s, o.n_els); break;
	case VAL_ARRAY:	ret.val.a = xmalloc(sizeof(double)*o.n_els); memcpy(ret.val.a, o.val.a, sizeof(double)*o.n_els); break;
	case VAL_LIST:	ret.val.l = xmalloc(sizeof(spcl_val)*o.n_els);
			for (size_t i = 0; i < o.n_els; ++i) ret.val.l[i] = copy_spcl_val(o.val.l[i]);
			break;
	case VAL_MAT:	ret.val.l = xmalloc(sizeof(spcl_val)*o.n_els);
			for (size_t i = 0; i < o.n_els; ++i) ret.val.l[i] = copy_spcl_val(o.val.l[i]);
			break;
	case VAL_INST:	ret.val.c = copy_spcl_inst(o.val.c); break;
	case VAL_FN:	ret.val.f = copy_spcl_uf(o.val.f); break;
	default:	ret.val.x = o.val.x; break;
    }
    return ret;
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
static inline int matrix_err(spcl_val* l, size_t i) {
    if (i >= l->n_els || l->val.l[i].type == VAL_LIST || l->val.l[i].type == VAL_ARRAY)
	return 0;
    //handle incorrect types
    if (l->val.l[i].type != VAL_ERR) {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "matrix contains type <%s>", valnames[l->val.l[i].type]);
	return 1;
    }
    //move the error to overwrite the list
    spcl_val tmp = l->val.l[i];
    for (size_t j = 0; j < l->n_els; ++j) {
	if (i != j)
	    cleanup_spcl_val(l->val.l + j);
    }
    xfree(l->val.l);
    *l = tmp;
    return 1;
}
spcl_local void val_add(spcl_val* l, spcl_val r) {
    if (l->type == VAL_UNDEF && r.type == VAL_NUM) {
	*l = r;
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)+(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot add arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot add lists of length %lu and %lu", l->n_els, r.n_els);	
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_add(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l, i))
		return;
	}
    } else if (l->type == VAL_LIST) {
	++l->n_els;
	l->val.l = xrealloc(l->val.l, l->n_els);
	l->val.l[l->n_els-1] = copy_spcl_val(r);
    } else if (l->type == VAL_STR) {
	size_t l_len = l->n_els;
	size_t r_len = spcl_est_strlen(r);
	//create a new string and copy
	l->val.s = xrealloc(l->val.s, l_len+r_len+1); //+1 for null terminator
	char* tmp = spcl_stringify(r, l->val.s+l_len, r_len);
	tmp[0] = 0;
	//now set the spcl_val
	l->n_els = (size_t)(tmp - l->val.s);
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot add types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_sub(spcl_val* l, spcl_val r) {
    if (l->type == VAL_UNDEF && r.type == VAL_NUM) {
	*l = spcl_make_num(-r.val.x);
    } else if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)-(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot subtract arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot subtract lists of length %lu and %lu", l->n_els, r.n_els);	
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_sub(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l, i))
		return;
	}
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot subtract types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_mul(spcl_val* l, spcl_val r) {
    if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)*(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot multiply arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot multiply lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mul(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l,i))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mul(l->val.l+i, r);
	    if (matrix_err(l, i))
		return;
	}
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot multiply types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_div(spcl_val* l, spcl_val r) {
    if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( (l->val.x)/(r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot divide arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot divide lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_div(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l,i))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_div(l->val.l+i, r);
	    if (matrix_err(l, i))
		return;
	}
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot divide types %s and %s", valnames[l->type], valnames[r.type]);
    }
}
spcl_local void val_mod(spcl_val* l, spcl_val r) {
    if (l->type == VAL_NUM && r.type == VAL_NUM) {
	double div = l->val.x / r.val.x;
	l->val.x -= floor(div)*r.val.x;
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot divide arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot divide lists of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mod(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l,i))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_mod(l->val.l+i, r);
	    if (matrix_err(l, i))
		return;
	}
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot divide types %s and %s", valnames[l->type], valnames[r.type]);
    }
}

spcl_local void val_exp(spcl_val* l, spcl_val r) {
    if (l->type == VAL_NUM && r.type == VAL_NUM) {
	*l = spcl_make_num( pow(l->val.x, r.val.x) );
    } else if (l->type == VAL_ARRAY && r.type == VAL_ARRAY) {
	//add the two arrays together
	if (l->n_els != r.n_els) {
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot raise arrays of length %lu and %lu", l->n_els, r.n_els);
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
	    cleanup_spcl_val(l);
	    *l = spcl_make_err(E_OUT_OF_RANGE, "cannot raise matrices of length %lu and %lu", l->n_els, r.n_els);	
	    return;
	}
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_exp(l->val.l+i, r.val.l[i]);
	    if (matrix_err(l,i))
		return;
	}
    } else if (l->type == VAL_MAT && r.type == VAL_NUM) {
	//add a scalar to each element of the array
	for (size_t i = 0; i < l->n_els; ++i) {
	    val_exp(l->val.l+i, r);
	    if (matrix_err(l, i))
		return;
	}
    } else {
	cleanup_spcl_val(l);
	*l = spcl_make_err(E_BAD_TYPE, "cannot raise types %s and %s", valnames[l->type], valnames[r.type]);
    }
}

/** ============================ spcl_inst ============================ **/

//helper to convert possibly negative index spcl_vals to real C indices
static inline size_t index_to_abs(spcl_val* ind, size_t max_n) {
    if (-(ind->val.x) > max_n || ind->val.x >= max_n) {
	*ind = spcl_make_err(E_OUT_OF_RANGE, "index %d out of bounds for list of size %lu", (int)ind->val.x, max_n);
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
static inline int find_ind(const struct spcl_inst* c, s8 name, size_t* ind) {
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
static inline int grow_inst(struct spcl_inst* c) {
    if (c && c->n_memb*GROW_LOAD_DEN > con_size(c)*GROW_LOAD_NUM) {
	//create a new spcl_inst with twice as many elements
	struct spcl_inst nc;
	nc.parent = c->parent;
	nc.t_bits = c->t_bits + 1;
	nc.n_memb = 0;
	nc.table = xmalloc(sizeof(name_val_pair)*con_size(&nc));
	memset(nc.table, 0, sizeof(name_val_pair)*con_size(&nc));
	//we have to rehash every member in the old table
	for (size_t i = 0; i < con_size(c); ++i) {
	    if (c->table[i].s.n == 0)
		continue;
	    //only move non-null members
	    size_t new_ind;
	    if (!find_ind(&nc, c->table[i].s, &new_ind))
		++nc.n_memb;
	    nc.table[new_ind] = c->table[i];
	}
	//deallocate old table and replace it with the new one
	xfree(c->table);
	*c = nc;
	return 1;
    }
    return 0;
}
/**
 * include builtin functions
 * TODO: make this not dumb
 */
static inline void setup_builtins(struct spcl_inst* c) {
    //create builtins
    spcl_set_val(c, "false",	spcl_make_num(0), 0);
    spcl_set_val(c, "true",	spcl_make_num(1), 0);//create horrible (if amusing bugs when someone tries to assign to true or false
    spcl_add_fn(c, spcl_assert,		"assert");
    spcl_add_fn(c, spcl_typeof,		"typeof");
    spcl_add_fn(c, spcl_len,		"len");
    spcl_add_fn(c, spcl_list,		"list");
    spcl_add_fn(c, spcl_range,		"range");
    spcl_add_fn(c, spcl_linspace,	"linspace");
    spcl_add_fn(c, spcl_flatten,	"flatten");
    spcl_add_fn(c, spcl_array,		"array");
    spcl_add_fn(c, spcl_vec,		"vec");
    spcl_add_fn(c, spcl_cat,		"cat");
    spcl_add_fn(c, spcl_print,		"print");
    //TODO: this is a really dumb way of adding namespaces
    //math stuff
    spcl_val tmp = spcl_make_inst(c, "math");
    spcl_inst* math_c = tmp.val.c;
    spcl_set_val(math_c, "pi", 	spcl_make_num(M_PI), 0);
    spcl_set_val(math_c, "e", 	spcl_make_num(M_E), 0);
    spcl_add_fn(math_c, spcl_sin,	"sin");
    spcl_add_fn(math_c, spcl_cos,	"cos");
    spcl_add_fn(math_c, spcl_tan,	"tan");
    spcl_add_fn(math_c, spcl_asin,	"asin");
    spcl_add_fn(math_c, spcl_acos,	"acos");
    spcl_add_fn(math_c, spcl_atan,	"atan");
    spcl_add_fn(math_c, spcl_exp,	"exp");
    spcl_add_fn(math_c, spcl_log,	"log");
    spcl_add_fn(math_c, spcl_sqrt,	"sqrt");
    spcl_add_fn(math_c, spcl_floor,	"floor");
    spcl_add_fn(math_c, spcl_ceil,	"ceil");
    spcl_add_fn(math_c, spcl_fabs,	"abs");
    spcl_set_val(c, "math", tmp, 0);
    tmp = spcl_make_inst(c, "sys");
    spcl_set_val(c, "sys", tmp, 0);
}

struct spcl_inst* make_spcl_inst(spcl_inst* parent) {
    spcl_inst* c = xmalloc(sizeof(spcl_inst));
    c->parent = parent;
    c->n_memb = 0;
    c->t_bits = DEF_TAB_BITS;
    //double the allocated size for root insts (since they're likely to hold more stuff)
    if (!parent) c->t_bits++;
    c->table = xmalloc(sizeof(name_val_pair)*con_size(c));
    memset(c->table, 0, sizeof(name_val_pair)*con_size(c));
    if (!parent) {
	setup_builtins(c);
    }
    return c;
}

struct spcl_inst* copy_spcl_inst(const spcl_inst* o) {
    if (!o)
	return NULL;
    spcl_inst* c = xmalloc(sizeof(spcl_inst));
    c->table = xmalloc(sizeof(name_val_pair)*con_size(o));
    memset(c->table, 0, sizeof(name_val_pair)*con_size(o));
    c->parent = o->parent;
    c->n_memb = o->n_memb;
    c->t_bits = o->t_bits;
    for (size_t i = con_it_next(o, 0); i < con_size(o); i = con_it_next(o, i+1)) {
	c->table[i].s.s = strdup(o->table[i].s.s);
	c->table[i].s.n = o->table[i].s.n;
	c->table[i].v = copy_spcl_val(o->table[i].v);
    }
    return c;
}

void destroy_spcl_inst(struct spcl_inst* c) {
    if (!c)
	return;
    //erase the hash table
    for (size_t i = con_it_next(c, 0); i < con_size(c); i = con_it_next(c,i+1))
	cleanup_name_val_pair(c->table[i]);
    xfree(c->table);
    xfree(c);
}
#define MAX_ASCII 0x7f
#define MAX_OP_PREC  7
static const int OP1_PRECS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 3, 0, 0, 0, 0, 3, 4, 0, 4, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 5, 7, 5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int OP2_PRECS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 7, 7, 0, 7, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0};
/**
 * Find the length of an operator sequence e.g. '==', '=', '+=' etc.
 */
static inline int get_oplen(unsigned char op, unsigned char next) {
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
/**
 * Identify the keyword starting at rs->start up to rs->end. If a key is found, then rs->start is updated to the first character after the keyword.
 * returns: the spck_key code for the matched key.
 */
spcl_local spcl_key get_keyword(read_state* rs) {
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
 * Get the location of the first operator which is not enclosed in a block expression
 * op_loc: store the location of the operator
 * open_ind: store the location of the first enclosing block
 * close_ind: store the location of the last escaping block
 * returns: the index of the first expression or 0 in the event of an error (it is impossible for a valid expression to have an operator as the first character)
 */
spcl_local spcl_val find_operator(read_state rs, psize* op_loc, psize* open_ind, psize* close_ind, psize* new_end) {
    *op_loc = rs.end;
    *open_ind = rs.end;*close_ind = rs.end;
    //keeps track of open and close [], (), {}, and ""
    stack(char,BLK_MAX) blk_stk = make_stack(char,BLK_MAX)();
    //variable names are not allowed to start with '+', '-', or a digit and may not contain any '.' symbols. Use this to check whether the spcl_val is numeric
    u8 open_type, prev;
    u8 cur = fs_get(rs.b, rs.start);
    int is_num = ((cur >= '0' && cur <= '9') || cur == '.');

    //keep track of the precedence of the orders of operation (lower means executed later) ">,=,>=,==,<=,<"=4 "+,-"=3, "*,/"=2, "**"=1
    int op_prec = 0;
    for (; rs.start < rs.end || blk_stk.ptr; ++rs.start) {
	//make sure we don't read past the end of the file
	if (rs.start >= fs_end(rs.b))
	    break;
	prev = cur;
	cur = fs_get(rs.b, rs.start);
	char next = fs_get(rs.b, rs.start+1);
	if (cur == BEG_PAR || cur == BEG_CRL || cur == BEG_SQR) {
	    //if we've already found an entire block we can stop
	    if (blk_stk.ptr == 0 && *open_ind < rs.end) break;
	    push(char,BLK_MAX)(&blk_stk, cur);
	    //only set the open index if this is the first match
	    if (*open_ind == rs.end) *open_ind = rs.start;
	} else if (cur == END_SQR || cur == END_PAR || cur == END_CRL) {
	    if (pop(char,BLK_MAX)(&blk_stk, &open_type) || cur != get_match(open_type)) {
		destroy_stack(char,BLK_MAX)(&blk_stk, NULL);
		return spcl_make_err(E_BAD_SYNTAX, "unexpected %c", cur);
	    }
	    *close_ind = rs.start;
	} else if (cur == '\"' && (prev != '\\')) {
	    //quotes need to be handled in a special way since the open and close characters are identical
	    if (peek(char,BLK_MAX)(&blk_stk, 1, &open_type) || cur != get_match(open_type)) {
		//if we've already found an entire block we can stop
		if (blk_stk.ptr == 0 && *open_ind < rs.end) break;
		push(char,BLK_MAX)(&blk_stk, cur);
		//only set the open index if this is the first match
		if (*open_ind == rs.end) *open_ind = rs.start;
	    } else {
		pop(char,BLK_MAX)(&blk_stk, &open_type);
		*close_ind = rs.start;
	    }
	}

	if (blk_stk.ptr == 0) {
	    int oplen = get_oplen(cur, next);
	    //reset the enclosing indices when we find an operator
	    if (oplen) {
		*open_ind = rs.end;
		*close_ind = rs.end;
	    }
	    if (oplen >= 2) {
		if (op_prec < OP2_PRECS[(unsigned char)cur]) {
		    *op_loc = rs.start;
		    op_prec = OP2_PRECS[(unsigned char)cur];
		}
		rs.start += oplen-1;	
	    } else if (oplen == 1 && op_prec < OP1_PRECS[(unsigned char)cur]) {
		//avoid matches with numeric literals
		if (OP1_PRECS[(unsigned char)cur] == OP1_PRECS['-'] && is_num && (prev == 'e' || prev == 'E'))
		    continue;
		*op_loc = rs.start;
		op_prec = OP1_PRECS[(unsigned char)cur];
	    } else if (!cur || cur == ';' || cur == '\n' || cur == '#') {
		break;
	    }
	}
    }
    if (blk_stk.ptr > 0) {
	pop(char,BLK_MAX)(&blk_stk, &open_type);
	destroy_stack(char,BLK_MAX)(&blk_stk, NULL);
	return spcl_make_err(E_BAD_SYNTAX, "expected %c", get_match(open_type));
    }
    if (new_end) {
	//if we didn't find an operator then we have to move the location to the new end to signal that it wasn't found
	if (!op_prec)
	    *op_loc = rs.start;
	*new_end = rs.start;
    }
    destroy_stack(char,BLK_MAX)(&blk_stk, NULL);
    return spcl_make_none();
}
//forward declare so that helpers can call
static inline spcl_val spcl_parse_line_rs(spcl_inst* c, read_state rs, psize* new_end, spcl_key start_key);
static inline spcl_val spcl_read_lines_block(struct spcl_inst* c, read_state block_rs);
/**
 * A helper which accesses v[ind]. If assign is not NULL, then v[ind] = *assign.
 */
static inline spcl_val _spcl_index(spcl_val v, spcl_val ind, spcl_val* assign) {
    //check for invalid types
    if (ind.type != VAL_NUM)
	return spcl_make_err(E_BAD_TYPE, "cannot index with type %s", valnames[ind.type]);
    if (-(ind.val.x) > v.n_els || ind.val.x >= v.n_els)
	return spcl_make_err(E_OUT_OF_RANGE, "index %d out of bounds for list of size %lu", (int)ind.val.x, v.n_els);
    size_t i = (ind.val.x < 0)? v.n_els - (size_t)(-ind.val.x) : (size_t)ind.val.x;
    //create a new dummy value or return the element depending on type
    if (v.type == VAL_LIST || v.type == VAL_MAT) {
	if (assign)
	    v.val.l[i] = *assign;
	return v.val.l[i];
    } else if (v.type == VAL_ARRAY) {
	if (assign) {
	    if (assign->type != VAL_NUM)
		return spcl_make_err(E_BAD_TYPE, "cannot assign type %s to array", valnames[assign->type]);
	    v.val.a[i] = assign->val.x;
	}
	//in principle this should be a reference, but numerics are trivially destructable so it doesn't matter
	return spcl_make_num(v.val.a[i]);
    }
    return spcl_make_err(E_BAD_TYPE, "type %s is not indexable", valnames[v.type]);
}
/**
 * An alternative to lookup which only considers the first n bytes in str
 */
static inline spcl_val spcl_find_rs(const spcl_inst* c, read_state rs) {
    psize dot_loc = strchr_block_rs(rs.b, rs.start, rs.end, '.');
    psize ref_loc = strchr_block_rs(rs.b, rs.start, rs.end, BEG_SQR);//]
    if (dot_loc == rs.end && ref_loc == rs.end) {
	//if there was neither a period or open brace, just lookup directly
	size_t i, n;
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
	    return spcl_make_err(E_BAD_TYPE, "cannot access member from non-instance type %s", valnames[sub_con.type]);
	return spcl_find_rs(sub_con.val.c, make_read_state(rs.b, dot_loc+1, rs.end));
    } else {
	//access lists/arrays
	psize close_ind = strchr_block_rs(rs.b, ref_loc+1, rs.end, END_SQR);
	if (close_ind > rs.end)
	    return spcl_make_err(E_BAD_SYNTAX, "expected %c", END_SQR);
	//read the list and the index
	spcl_val lst = spcl_find_rs(c, make_read_state(rs.b, rs.start, ref_loc));
	spcl_val index = spcl_parse_line_rs(c, make_read_state(rs.b, ref_loc+1, close_ind), NULL, KEY_NONE);
	return _spcl_index(lst, index, NULL);
    }
}
/**
 * similar to set_spcl_valn(), but read in place from a read state
 */
static inline spcl_val set_spcl_val_rs(struct spcl_inst* c, read_state rs, spcl_val p_val) {
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
	    return spcl_make_err(E_BAD_TYPE, "cannot access member from non instance type %s", valnames[sub_con.type]);
	return set_spcl_val_rs(sub_con.val.c, make_read_state(rs.b, dot_loc+1, rs.end), p_val);
    } else {
	//access lists/arrays
	psize close_ind = strchr_block_rs(rs.b, ref_loc+1, rs.end, END_SQR);
	if (close_ind > rs.end)
	    return spcl_make_err(E_BAD_SYNTAX, "expected %c", END_SQR);
	//read the list and the index
	spcl_val lst = spcl_find_rs(c, make_read_state(rs.b, rs.start, ref_loc));
	spcl_val index = spcl_parse_line_rs(c, make_read_state(rs.b, ref_loc+1, close_ind), NULL, KEY_NONE);
	return _spcl_index(lst, index, &p_val);
    }
    return spcl_make_none();
}
//TODO: to inline or not to inline
spcl_local spcl_val do_op(spcl_inst* c, read_state rs, psize op_loc, psize* new_end, spcl_key key) {
    spcl_val sto = spcl_make_none();
    //some operators (==, >=, <=) take up more than one character, test for these
    char op = fs_get(rs.b, op_loc);
    char next = fs_get(rs.b, op_loc+1);
    int op_width = get_oplen(op, next);
    //set a read state before the operator and after the operator
    read_state rs_l = rs;
    read_state rs_r = rs;
    rs_l.end = op_loc;
    rs_r.start = op_loc+op_width;
    //fast-forward
    rs_l.start = skip_ws(rs_l.b, rs_l.start, rs_l.end, 0);
    rs_r.start = skip_ws(rs_r.b, rs_r.start, rs_r.end, 0);
    //handle special cases
    if (op == '?') {
	//ternary operators and dereferences are special cases
	//the colon must be present
	psize col_loc = strchr_block_rs(rs.b, op_loc, rs.end, ':');
	if (col_loc >= rs.end)
	    return spcl_make_err(E_BAD_SYNTAX, "expected ':' in ternary");

	spcl_val l = spcl_parse_line_rs(c, rs_l, NULL, key);
	if (l.type == VAL_ERR)
	    return l;
	//0 branch
	if (l.type == VAL_UNDEF || l.val.x == 0) {
	    rs_r.start = col_loc+1;
	    sto = spcl_parse_line_rs(c, rs_r, new_end, key);
	    return sto;
	} else {
	    //1 branch
	    rs_r.end = col_loc;
	    sto = spcl_parse_line_rs(c, rs_r, new_end, key);
	    return sto;
	}
    } else if (op == '=' && op_width == 1) {
	//assignments
	spcl_val tmp_val = spcl_parse_line_rs(c, rs_r, new_end, key);
	if (tmp_val.type == VAL_ERR)
	    return tmp_val;
	set_spcl_val_rs(c, rs_l, tmp_val);
	//this is a super ugly hack to make function names appear in debugging info
	//TODO: this entire thing needs to be refactored into a buffer of tokens
	if (tmp_val.type == VAL_FN) {
	    //tmp_val.val.f->call_sig.name = fs_read( rs.b, rs_l.start, rs_l.end );
	}
	return spcl_make_none();
    }
    //parse right and left spcl_vals. Note that we don't pass the key since we must do type checking after the operation completes
    spcl_val l = spcl_parse_line_rs(c, rs_l, NULL, KEY_NONE);
    if (l.type == VAL_ERR)
	return l;
    //logic for short circuiting && statements
    if (op == '&' && next == op && spcl_isfalse(l))
	return spcl_make_num(0);
    //logic for short circuiting || statements
    if (op == '|' && next == op && spcl_istrue(l))
	return spcl_make_num(1);
    spcl_val r = spcl_parse_line_rs(c, rs_r, new_end, KEY_NONE);
    if (r.type == VAL_ERR) {
	cleanup_spcl_val(&l);
	return r;
    }
    //handle equality comparisons
    if (op == '=' || (op_width == 2 && op == '!') || op == '>' || op == '<') {
	spcl_val cmp = spcl_valcmp(l,r);
	cleanup_spcl_val(&l);
	cleanup_spcl_val(&r);
	if (cmp.type == VAL_ERR)
	    return cmp;
	if (op == '=')
	    return spcl_make_num(!cmp.val.x);
	if (op == '!')
	    return cmp;
	if (op == '>') {
	    if (op_width == 2)
		return spcl_make_num(cmp.val.x >= 0);
	    return spcl_make_num(cmp.val.x > 0);
	} else {
	    if (op_width == 2)
		return spcl_make_num(cmp.val.x <= 0);
	    return spcl_make_num(cmp.val.x < 0);
	}
    } else if (op == '|' || op == '&') {
	if (next != op)
	    return spcl_make_err(E_BAD_SYNTAX, "invalid operation \'%c%c\'", op, next);
	if (l.type == VAL_NUM && r.type == VAL_NUM)
	    return (op == '|')? spcl_make_num(l.val.x || r.val.x) : spcl_make_num(l.val.x && r.val.x);
	//undefined == false
	if (l.type == VAL_UNDEF)
	    return (op == '|')? spcl_make_num(r.val.x) : spcl_make_num(0);
	if (r.type == VAL_UNDEF)
	    return (op == '|')? spcl_make_num(1) : spcl_make_num(0);
	return spcl_make_num(1);
    } else {
	//arithmetic is all relatively simple
	switch(op) {
	case '+': val_add(&l, r);break;
	case '-': val_sub(&l, r);break;
	case '*': val_mul(&l, r);break;
	case '/': val_div(&l, r);break;
	case '%': val_mod(&l, r);break;
	case '^': val_exp(&l, r);break;
	case '!': l = (r.type == VAL_UNDEF || r.val.x == 0)? spcl_make_num(1) : spcl_make_num(0);break;
	default: cleanup_spcl_val(&l);return spcl_make_err(E_BAD_SYNTAX, "unexpected %c", op);break;
	}
	//if this is a relative assignment, do that
	if (next == '=') {
	    set_spcl_val_rs(c, rs_l, l);
	    l = spcl_make_none();
	}
	cleanup_spcl_val(&r);
	return l;
    }
}
typedef struct for_state {
    read_state expr_name;
    psize for_start;
    psize in_start;
    spcl_val it_list;
    name_val_pair prev;
    size_t var_ind;
} for_state;
static inline for_state* make_for_state(spcl_inst* c, read_state rs, psize for_start, spcl_val* er) {
    for_state* fs = xmalloc(sizeof(for_state));
    psize after_for = for_start+strlen("for");
    //now look for a block labeled "in"
    fs->for_start = for_start;
    fs->in_start = token_block(rs.b, after_for, rs.end, "in", strlen("in"));
    if (fs->in_start == rs.end) {
	*er = spcl_make_err(E_BAD_SYNTAX, "expected keyword 'in'");
	return fs;
    }
    //the variable name is whatever is in between the "for" and the "in"
    after_for = skip_ws(rs.b, after_for, rs.end, 0);
    s8 var_name = s8dup( trim_whitespace(fs_read(rs.b, after_for, fs->in_start)) );
    //now parse the list we iterate over
    psize after_in = fs->in_start+strlen("in");
    fs->it_list = spcl_parse_line_rs(c, make_read_state(rs.b, after_in, rs.end), NULL, KEY_FOR);
    if (fs->it_list.type == VAL_ERR) {
	*er = spcl_make_err(E_BAD_SYNTAX, "in expression %s", fs_read(rs.b, after_in, rs.end));
	return fs;
    }
    if (fs->it_list.type != VAL_ARRAY && fs->it_list.type != VAL_LIST) {
	*er =  spcl_make_err(E_BAD_TYPE, "can't iterate over type %s", valnames[fs->it_list.type]);
	return fs;
    }
    fs->expr_name = make_read_state(rs.b, rs.start+1, fs->for_start);
    //we need to add a variable with the appropriate name to loop over. We write a spcl_val and save the spcl_val there before so we can remove it when we're done
    find_ind(c, var_name, &(fs->var_ind));
    fs->prev = c->table[fs->var_ind];
    c->table[fs->var_ind].s = var_name;
    *er = spcl_make_none();
    return fs;
}
static inline void destroy_for_state(for_state* fs, spcl_inst* c) {
    //we need to reset the table with the loop index before iteration
    cleanup_name_val_pair(c->table[fs->var_ind]);
    c->table[fs->var_ind] = fs->prev;
    //free the memory from the iteration list
    cleanup_spcl_val(&fs->it_list);
    xfree(fs);
}
//helper for spcl_parse_line to handle string literals
static inline spcl_val parse_literal_str(spcl_inst* c, read_state rs, psize open_ind, psize close_ind) {
    spcl_val v;
    v.type = VAL_STR;
    //set up a buffer with enough memory
    v.val.s = xmalloc(close_ind - open_ind + 1);
    v.n_els = 0;
    for (psize it = open_ind+1; it < close_ind; ++it) {
	char c = fs_get(rs.b, it);
	//check for escape sequences
	if (c == '\\') {
	    it = it+1;
	    c = fs_get(rs.b, it);
	    switch (c) {
		case 't': v.val.s[v.n_els++] = '\t';break;
		case 'n': v.val.s[v.n_els++] = '\n';break;
		case '\\': v.val.s[v.n_els++] = '\\';break;
		case '\"': v.val.s[v.n_els++] = '\"';break;
		case '\'': v.val.s[v.n_els++] = '\'';break;
		default: free(v.val.s);return spcl_make_err(E_BAD_SYNTAX, "unrecognized escape sequence \\%c", c);	
	    }
	} else {
	    v.val.s[v.n_els++] = c;
	}
    }
    //null terminate so that it plays nicely with c
    v.val.s[v.n_els] = 0;
    return v;
}
//helper for spcl_parse_line to hand list literals
static inline spcl_val parse_literal_list(struct spcl_inst* c, read_state rs, psize open_ind, psize close_ind) {
    rs.start = open_ind;
    rs.end = close_ind;
    //store the return value
    spcl_val sto;
    //read the coordinates separated by spaces
    spcl_val* lbuf;
    //check if this is a list interpretation
    psize for_start = token_block(rs.b, open_ind+1, close_ind, "for", strlen("for"));
    if (for_start < close_ind) {
	for_state* fs = make_for_state(c, rs, for_start, &sto);
	if (sto.type == VAL_ERR)
	    return sto;
	//setup a buffer to hold the list
	sto.n_els = fs->it_list.n_els;
	lbuf = xmalloc(sizeof(spcl_val)*sto.n_els);
	//we now iterate through the list specified, substituting VAL in the expression with the current spcl_val
	for (size_t i = 0; i < sto.n_els; ++i) {
	    if (fs->it_list.type == VAL_LIST)
		c->table[fs->var_ind].v = fs->it_list.val.l[i];
	    else if (fs->it_list.type == VAL_ARRAY)
		c->table[fs->var_ind].v = spcl_make_num(fs->it_list.val.a[i]);
	    lbuf[i] = spcl_parse_line_rs(c, fs->expr_name, NULL, KEY_NONE);
	    if (lbuf[i].type == VAL_ERR) {
		spcl_val ret = copy_spcl_val(lbuf[i]);
		for (size_t j = 0; j < i; ++j)
		    cleanup_spcl_val(lbuf+j);
		free(lbuf);
		destroy_for_state(fs, c);
		return ret;
	    }
	}
	destroy_for_state(fs, c);
    } else {
	//TODO: figure out how to avoid code duplication with parse_literal_fn
	size_t alloc_n = ALLOC_LST_N;
	lbuf = xmalloc(sizeof(spcl_val)*alloc_n);
	sto.n_els = 0;
	//start reading one character after the open brace
	++rs.start;
	while (rs.start < close_ind) {
	    //grow as necessary
	    if (sto.n_els == alloc_n) {
		alloc_n *= 2;
		lbuf = xrealloc(lbuf, sizeof(spcl_val)*alloc_n);
	    }
	    //move the start to the first character after the open paren or previous comma and the end to the next comma or close paren. 
	    rs.end = strchr_block_rs(rs.b, rs.start, close_ind, ',');
	    rs.start = skip_ws(rs.b, rs.start, rs.end, 0);
	    //read using the current rs
	    lbuf[sto.n_els] = spcl_parse_line_rs(c, rs, NULL, KEY_NONE);
	    if (lbuf[sto.n_els].type == VAL_ERR) {
		sto = copy_spcl_val(lbuf[sto.n_els]);
		free(lbuf);
		return sto;
	    }
	    //only include defined spcl_vals
	    if (lbuf[sto.n_els].type != VAL_UNDEF)
		sto.n_els++;
	    //start the next read one character after the terminating comma
	    rs.start = rs.end+1;
	}
    }
    //set number of elements and type
    sto.type = VAL_LIST;
    sto.val.l = lbuf;
    return sto;
}

/**
 * Given a read state, read a list of each occurrence of a comma separator between open_ind and close_ind.
 * returns: a list of the location of each comma and the open and close brace. If the returned spcl_val is called args, then the characters between (args[i], args[i+1]) (non-inclusive) give the ith string
 */
static inline psize* csv_to_inds(const spcl_fstream* fs, psize open_ind, psize close_ind, size_t* n_inds) {
    //get a list of each argument index plus an additional token at the end.
    size_t alloc_n = ALLOC_LST_N;
    psize* inds = xmalloc(sizeof(psize)*alloc_n);
    size_t i = 0;
    psize e = open_ind;
    psize s = e;
    while (s < close_ind) {
	s = e;
	e = strchr_block_rs(fs, s+1, close_ind, ',');
	if (i+1 == alloc_n) {
	    alloc_n *= 2;
	    inds = xrealloc(inds, sizeof(psize)*alloc_n);
	}
	inds[i++] = s;
    }
    if (i == 0) {
	free(inds);
	*n_inds = 0;
	return NULL;
    }
    *n_inds = i-1;
    return inds;
}
static inline spcl_val spcl_make_fn_rs(struct spcl_inst* c, read_state rs, psize* arg_inds, size_t n_args, psize* new_end);
//parse function definition/call statements
static inline spcl_val parse_literal_fn(struct spcl_inst* c, spcl_key key, read_state rs, psize open_ind, psize close_ind, psize* new_end) {
    //check if this is a parenthetical expression
    while ( is_whitespace(fs_get(rs.b, rs.start)) && rs.start != open_ind )
	++rs.start;
    if (key != KEY_FN && rs.start == open_ind) {
	rs.start = open_ind;
	rs.end = close_ind;
	rs.start = skip_ws(rs.b, rs.start, rs.end, 1);
	return spcl_parse_line_rs(c, rs, NULL, key);
    }
    spcl_val sto = spcl_make_none();
    rs.end = close_ind;

    //read the indices
    spcl_fn_call f;
    memset(f.args, 0, sizeof(f.args));
    psize* arg_inds = csv_to_inds(rs.b, open_ind, close_ind, &f.n_args);
    if (f.n_args == 0)
	return spcl_make_err(E_BAD_SYNTAX, "overlapping parentheses (something really weird happened)");
    if (f.n_args >= SPCL_ARGS_BSIZE)
	return spcl_make_err(E_OUT_OF_RANGE, "speclang only supports at most %lu arguments in functions", SPCL_ARGS_BSIZE-1);
    //set up a function and figure out the function name 
    psize s = find_token_before(rs.b, open_ind, rs.start);
    f.name = fs_read(rs.b, s, open_ind);
    //check if this is a declaration
    if (key == KEY_FN) {
	//parse the function and check for errors
	sto = spcl_make_fn_rs(c, rs, arg_inds, f.n_args, new_end);
	xfree(arg_inds);
	return sto;
    } else {
	//isdef is a special function, we implement it here to avoid errors about potentially undefined spcl_vals
	if (s8cmp(f.name, s8("isdef")) == 0) {
	    if (f.n_args == 0)
		return spcl_make_err(E_LACK_TOKENS, "isdef() expected 1 argument, got 0");
	    sto = spcl_find_rs( c, make_read_state(rs.b, arg_inds[0]+1, arg_inds[1]) );
	    xfree(arg_inds);
	    if (sto.type == VAL_UNDEF)
		return spcl_make_num(0);
	    else
		return spcl_make_num(1);
	}
	//handle function calls
	spcl_val func_val = spcl_find_rs(c, make_read_state(rs.b, s, open_ind));
	if (func_val.type != VAL_FN) {
	    cleanup_spcl_fn_call(&f);
	    xfree(arg_inds);
	    return spcl_make_err(E_LACK_TOKENS, "unrecognized function name %.*s\n", f.name.n, f.name.s);
	}
	//read the arguments
	for (size_t i = 0; i < f.n_args && i+1 < SPCL_ARGS_BSIZE; ++i) {
	    psize s = arg_inds[i]+1;
	    s = skip_ws(rs.b, s, arg_inds[i+1], 0);
	    //if we reached the end then that either indicates no arguments or invalid syntax
	    if (s == arg_inds[i+1]) {
		//that means that this is a () expression
		if (i == 0)
		    f.n_args = 0;
		else
		    return spcl_make_err(E_BAD_SYNTAX, "no expression between arguments");
		break;
	    }
	    f.args[i] = spcl_parse_line_rs(c, make_read_state(rs.b, s, arg_inds[i+1]), NULL, KEY_NONE);
	    //check for errors
	    if (f.args[i].type == VAL_ERR) {
		spcl_val er = copy_spcl_val(f.args[i]);
		cleanup_spcl_fn_call(&f);
		return er;
	    }
	}
	sto = spcl_uf_eval(func_val.val.f, c, f);
    }
    cleanup_spcl_fn_call(&f);
    xfree(arg_inds);
    return sto;
}

//parse table declaration statements
static inline spcl_val parse_literal_table(struct spcl_inst* c, read_state rs, psize open_ind, psize close_ind) {
    //create a new context and start reading
    spcl_val ret = spcl_make_inst(c, NULL);
    spcl_val er = spcl_read_lines_block( ret.val.c, make_read_state(rs.b, open_ind+1, close_ind) );
    //handle errors
    if (er.type == VAL_ERR) {
	cleanup_spcl_val(&ret);
	ret = er;
    }
    return ret;
}

/**
 * Parse a spcl_val from the line buffer rs.b
 * c: the spcl_inst to use for function calls and variables etc.
 * rs: the current state to read, includes the buffer and the start and end indices
 * start_key: the key that started this expression TODO: find a way to eliminate this?
 * new_end: if non-null, save the last character read when parsing this line
 */
static inline spcl_val spcl_parse_line_rs(struct spcl_inst* c, read_state rs, psize* new_end, spcl_key start_key) {
    rs.start = skip_ws(rs.b, rs.start, rs.end, 0);
    if (new_end)
	*new_end = rs.end;
    spcl_val sto = spcl_make_none();

    //store locations of the first instance of different operators. We do this so we can quickly look up new operators if we didn't find any other operators of a lower precedence (such operators are placed in the tree first).
    psize open_ind, close_ind, op_loc;
    sto = find_operator(rs, &op_loc, &open_ind, &close_ind, new_end);
    if (new_end) rs.end = *new_end;
    if (sto.type == VAL_ERR)
	return sto;

    //if the first non-whitespace character after a keyword is a letter, then interpret as a variable name. Note that _ through z includes all lowercase letters, _, and `. The backtick is kind of weird but i'm not using it for anything else...
    char thisc = fs_get(rs.b, rs.start);
    int is_var = (thisc > MAX_ASCII || (thisc >= 'A' && thisc <= 'Z') || (thisc >= '_' && thisc <= 'z'));

    //last try removing parenthesis 
    if (op_loc >= rs.end) {
	//if there isn't a valid parenthetical expression, then we should interpret this as a variable
	if (open_ind == rs.end || close_ind == rs.end) {
	    //ensure that empty strings return undefined
	    rs.start = skip_ws(rs.b, rs.start, rs.end, 0);
	    char cur = fs_get(rs.b, rs.start);
	    if (cur == 0 || rs.start == rs.end)
		return spcl_make_none(); 
	    if (is_var) {
		//spcl_find variables
		sto = copy_spcl_val( spcl_find_rs(c, make_read_state(rs.b, rs.start, rs.end)) );
		if (sto.type == VAL_UNDEF) {
		    s8 tmp = fs_read(rs.b, rs.start, rs.end);
		    return spcl_make_err(E_UNDEF, "token \"%.*s\" not defined", tmp.n, tmp.s);
		}
	    } else {
		//interpret number literals
		errno = 0;
		s8 tmp = fs_read(rs.b, rs.start, rs.end);
		char* str = strndup(tmp.s, tmp.n);
		sto.val.x = strtod(str, NULL);
		sto.n_els = 1;
		if (errno) {
		    sto = spcl_make_err(E_BAD_SYNTAX, "invalid numeric %s", str);
		    xfree(str);
		    return sto;
		}
		xfree(str);
		sto.type = VAL_NUM;
	    }
	} else {
	    //if there are enclosed blocks then we need to read those
	    switch (fs_get(rs.b, open_ind)) {
	    case '\"': sto = parse_literal_str(c, rs, open_ind, close_ind);break;
	    case BEG_SQR:  sto = (is_var)? copy_spcl_val(spcl_find_rs(c, rs)) : parse_literal_list(c, rs, open_ind, close_ind);break; //]
	    case BEG_CRL:  sto = parse_literal_table(c, rs, open_ind, close_ind);break; //}
	    case BEG_PAR:  sto = parse_literal_fn(c, start_key, rs, open_ind, close_ind, new_end);break; //)
	    }
	}
    } else {
	return do_op(c, rs, op_loc, new_end, start_key);
    }
    //TODO: the current implementation may leak memory if the user declares a variable without assigning it. I still haven't decided upon the most elegant solution
    return sto;
}
spcl_val spcl_parse_line(spcl_inst* c, const char* str) {
    //Setup a dummy line buffer. We're calling alloca with sizes known at compile-time, don't get mad at me.
    spcl_fstream* fs = make_spcl_fstream_str(str, strlen(str));
    if (!fs)
	return spcl_make_err(E_NOMEM, NULL);
    spcl_val v = spcl_parse_line_rs(c, make_read_state(fs, 0, fs_end(fs)), NULL, KEY_NONE);
    destroy_spcl_fstream(fs);
    return v;
}
int spcl_test(spcl_inst* c, const char* str) {
    spcl_fstream* fs = make_spcl_fstream_str(str, strlen(str));
    spcl_val v = spcl_parse_line_rs(c, make_read_state(fs, 0, fs_end(fs)), NULL, KEY_NONE);
    //check whether the statement v is true
    int ret = 1;
    if (v.type == VAL_ERR || v.type == VAL_UNDEF || (v.type == VAL_NUM && v.val.x == 0))
	ret = 0;
    //cleanup the temporary memory allocated
    cleanup_spcl_val(&v);
    destroy_spcl_fstream(fs);
    return ret;
}
spcl_val spcl_find(const struct spcl_inst* c, const char* str) {
    spcl_fstream* fs = make_spcl_fstream_str(str, strlen(str));
    spcl_val v = spcl_find_rs( c, make_read_state(fs, 0, fs_end(fs)) );
    destroy_spcl_fstream(fs);
    return v;
}
int spcl_find_object(const spcl_inst* c, const char* str, const char* typename, spcl_inst** sto) {
    spcl_val vobj = spcl_find(c, str);
    if (vobj.type != VAL_INST)
	return -1;
    spcl_val tmp = spcl_find(vobj.val.c, "__type__");
    if (tmp.type != VAL_STR || strncmp(tmp.val.s, typename, tmp.n_els))
	return -2;
    if (sto) *sto = vobj.val.c;
    return 0;
}
int spcl_find_c_iarray(const spcl_inst* c, const char* str, int* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_find(c, str);
    if (!tmp.type)
	return -1;
    //bounds check
    size_t n_write = (tmp.n_els > n) ? n : tmp.n_els;
    for (size_t i = 0; i < n_write; ++i) {
	spcl_val sub = _spcl_index(tmp, spcl_make_num(i), NULL);
	sto[i] = (int)sub.val.x;
    }
    return (int)n_write;
}
int spcl_find_c_uarray(const spcl_inst* c, const char* str, unsigned* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_find(c, str);
    if (!tmp.type)
	return -1;
    //bounds check
    size_t n_write = (tmp.n_els > n) ? n : tmp.n_els;
    for (size_t i = 0; i < n_write; ++i) {
	spcl_val sub = _spcl_index(tmp, spcl_make_num(i), NULL);
	sto[i] = (unsigned)sub.val.x;
    }
    return (int)n_write;
}
int spcl_find_c_darray(const spcl_inst* c, const char* str, double* sto, size_t n) {
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_find(c, str);
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
int spcl_find_c_str(const spcl_inst* c, const char* str, char* sto, size_t n) {
    //we can't save anything to an empty buffer so exit early
    if (sto == NULL || n == 0)
	return 0;
    spcl_val tmp = spcl_find(c, str);
    if (tmp.type != VAL_STR) {
	*sto = 0;//set to an empty string
	return -1;
    }
    //bounds check
    size_t n_write = (tmp.n_els > n-1) ? n : tmp.n_els;
    memcpy(sto, tmp.val.s, sizeof(char)*n_write);
    sto[n_write] = 0;
    return (int)n_write;
}
int spcl_find_int(const spcl_inst* c, const char* str, int* sto) {
    spcl_val tmp = spcl_find(c, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = (int)tmp.val.x;
    return 0;
}
int spcl_find_uint(const spcl_inst* c, const char* str, unsigned* sto) {
    spcl_val tmp = spcl_find(c, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = (size_t)tmp.val.x;
    return 0;
}
int spcl_find_float(const spcl_inst* c, const char* str, double* sto) {
    spcl_val tmp = spcl_find(c, str);
    if (tmp.type != VAL_NUM)
	return -1;
    if (sto) *sto = tmp.val.x;
    return 0;
}
void spcl_set_valn(struct spcl_inst* c, const char* p_name, size_t namelen, spcl_val p_val, int copy) {
    //generate a fake name if none was provided
    if (!p_name || p_name[0] == 0) {
	char tmp[SPCL_STR_BSIZE];
	snprintf(tmp, SPCL_STR_BSIZE, "\e_%lu", c->n_memb);
	return spcl_set_valn(c, tmp, namelen, p_val, copy);
    }
    s8 tmp_name = (s8){p_name, namelen};
    size_t ti = fnv_1(tmp_name, c->t_bits);
    if (!find_ind(c, tmp_name, &ti)) {
	//if there isn't already an element with that name we have to expand the table and add a member
	if (grow_inst(c))
	    find_ind(c, tmp_name, &ti);
	c->table[ti].s.s = strndup(p_name, namelen);
	c->table[ti].s.n = namelen;
	c->table[ti].v = (copy)? copy_spcl_val(p_val) : p_val;
	++c->n_memb;
    } else {
	//otherwise we need to cleanup the old spcl_val and add the new
	cleanup_spcl_val( &(c->table[ti].v) );
	c->table[ti].v = (copy)? copy_spcl_val(p_val) : p_val;
    }
}

/**
 * For if, while, and for blocks, we need to find the enclosing block
 */
static inline spcl_val get_block(spcl_key k, read_state* rs) {
    //store the final end so that we can easily reset rs
    psize end = fs_end(rs->b);
    psize open_ind, close_ind;
    char open_char = 0;
    psize op_loc;
    while (1) {//}
	spcl_val v = find_operator(*rs, &op_loc, &open_ind, &close_ind, &end);
	if (v.type == VAL_ERR)
	    return v;
	if (open_ind >= rs->end) {
	    //we only accept single line blocks if there was a perenthesis that produces a well defined end and the statement is not a function.
	    if (open_char != BEG_PAR || k == KEY_FN)//)
		return spcl_make_err(E_BAD_SYNTAX, "expected a block enclosed by {...} after keyword %s", spcl_keywords[k]);
	    return spcl_make_num(0);
	}
	open_char = fs_get(rs->b, open_ind);
	if (open_char == BEG_CRL) {
	    rs->start = open_ind+1;
	    rs->end = close_ind;
	    return spcl_make_num(1);
	}
	//move the block forward
	rs->start = close_ind+1;
	rs->end = fs_end(rs->b);
    }
    return spcl_make_err(E_BAD_SYNTAX, "something that should be impossible happened! congratulations!");
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
	/*TODO: if, else, for, and while
	 * } else if (start_key == KEY_IF) {
	    ret = get_block(start_key, &rs, &open_ind, &close_ind);*/
	} else {
	    ret = spcl_parse_line_rs(c, rs, &end, start_key);
	}
	if (ret.type == VAL_ERR || start_key == KEY_RET) {
	    if (start_key != KEY_RET && ret.val.e) {
		s8 line = fs_read(rs.b, rs.start, fs_line_end(rs.b, rs.start));
		fprintf(stderr, "\e[1m\033[31mError\033[0m\e[1m %s on line %lu:\e[m %.*s\n\t%s\n", errnames[ret.val.e->c], fs_find_line(rs.b, rs.start)+1, line.n, line.s, ret.val.e->msg);
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
}

spcl_val spcl_read_lines(struct spcl_inst* c, const spcl_fstream* b) {
    read_state rs = make_read_state(b, 0, b->flen);
    return spcl_read_lines_block(c, rs);
}

spcl_val spcl_inst_from_file(const char* fname, int argc, const char** argv) {
    //read command line arguments
    spcl_val ret = {0};
    //create a new spcl_inst
    spcl_inst* c = make_spcl_inst(NULL);
    //create a buffer that we'll populate with [argv[0], argv[1], ...]
    int tmp_n = SPCL_STR_BSIZE;
    char* argv_str = xmalloc(tmp_n);
    stpncpy(argv_str, "sys.argv=[", SPCL_STR_BSIZE);
    int j = (int)strlen("sys.argv=[");
    if (argc > 0 && argv) {
	for (int i = 0; i < argc; ++i) {
	    //store whether argv[i] is a flag like -r
	    int is_flag = 0;
	    //find the first non-dash character
	    int k = 0;
	    if (argv[i][0] == '-' && argv[i][1] != '-') {
		argv_str[j++] = '\"';
		k = 1;
		is_flag = 1;
	    } else if (argv[i][0] == '-' && argv[i][1] == '-') {
		k = 2;
	    }
	    for (;; ++k) {
		if (j+2 >= tmp_n) {
		    tmp_n *= 2;
		    argv_str = xrealloc(argv_str, tmp_n);
		}
		//when we reach the end of an argument either add a comma or end brace
		if (argv[i][k] == 0) {
		    //we have to put the close quote around flags
		    if (is_flag)
			argv_str[j++] = '\"';
		    //add a comma between elements but not after the last one
		    if (i+1 < argc)
			argv_str[j++] = ',';
		    break;
		}
		argv_str[j++] = argv[i][k];
	    }
	}
    }
    argv_str[j++] = ']';
    //now read the argv buffer and free memory
    ret = spcl_parse_line(c, argv_str);
    xfree(argv_str);
    if (ret.type == VAL_ERR) {
	destroy_spcl_inst(c);
	return ret;
    }
    //read the rest of the file and check to ensure the file was opened successfully
    spcl_fstream* fs = make_spcl_fstream(fname);
    if (fs) {
	ret = spcl_read_lines(c, fs);
	if (!ret.type) {
	    ret.type = VAL_INST;
	    ret.n_els = 1;
	    ret.val.c = c;
	}
	destroy_spcl_fstream(fs);
    } else {
	ret = spcl_make_err(E_BAD_VALUE, "couldn't open file %s", fname);
    }
    return ret;
}

/** ============================ spcl_uf ============================ **/

/**
 * create a new user function using a read state rs and a set of arguments
 * rs: the read state of the start of the function declaration
 * arg_inds: indices for each argument
 * n_args: the number of arguments. Note that arg_inds must have one more value allocated than n_args so that it can store the termination points for each string
 * new_end: we must track the final location so that the caller fast-forwards to the end of the declaration
 * returns: a spcl_val with the function set
 */
static inline spcl_val spcl_make_fn_rs(struct spcl_inst* c, read_state rs, psize* arg_inds, size_t n_args, psize* new_end) {
    //ensure that we can store the end location
    if (!new_end)
	return spcl_make_err(E_BAD_SYNTAX, "declared function without room to grow");
    //fast forward to the open curly brace
    psize args_end = arg_inds[n_args]+1;
    args_end = skip_ws(rs.b, args_end, rs.end, 0);
    if (fs_get(rs.b, args_end) != BEG_CRL)
	return spcl_make_err(E_BAD_SYNTAX, "unexpected %c", fs_get(rs.b, args_end));
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
}
spcl_uf* make_spcl_uf_ex(spcl_val (*p_exec)(spcl_inst*, spcl_fn_call)) {
    spcl_uf* uf = xmalloc(sizeof(spcl_uf));
    uf->code_lines = make_read_state(NULL, 0, 0);
    uf->call_sig.name = (s8){0};
    uf->call_sig.n_args = 0;
    uf->exec = p_exec;
    uf->fn_scope = NULL;
    return uf;
}
spcl_uf* copy_spcl_uf(const spcl_uf* o) {
    spcl_uf* uf = xmalloc(sizeof(spcl_uf));
    memcpy(uf, o, sizeof(spcl_uf));
    uf->call_sig.name = (s8){0};
    return uf;
}
//deallocation
void destroy_spcl_uf(spcl_uf* uf) {
    cleanup_spcl_fn_call(&(uf->call_sig));
    if (uf->fn_scope)
	destroy_spcl_inst(uf->fn_scope);
    xfree(uf);
}
spcl_val spcl_uf_eval(spcl_uf* uf, spcl_inst* c, spcl_fn_call call) {
    if (uf->exec) {
	return (*uf->exec)(c, call);
    } else if (uf->code_lines.b) {
	//TODO: handle script functions
	if (call.n_args != uf->call_sig.n_args)
	    return spcl_make_err(E_LACK_TOKENS, "%.*s() expected %lu arguments, got %lu", call.name.n, call.name.s, uf->call_sig.n_args, call.n_args);
	//setup a new scope with function arguments defined
	uf->fn_scope->parent = c;
	for (size_t i = 0; i < uf->call_sig.n_args; ++i) {
	    spcl_set_valn(uf->fn_scope, uf->call_sig.args[i].val.s, uf->call_sig.args[i].n_els, call.args[i], 0);
	}
	spcl_val ret = spcl_read_lines_block(uf->fn_scope, uf->code_lines);
	//function calls make shallow copies, so we need to reset memory to avoid double frees
	memset( uf->fn_scope->table, 0, sizeof(name_val_pair)*con_size(uf->fn_scope) );
	return ret;

    }
    return spcl_make_err(E_BAD_VALUE, "function not implemented");
}
