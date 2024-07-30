#ifndef READ_H
#define READ_H

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include "s8.h"

//hints for dynamic buffer sizes
#define ERR_BSIZE		1024
#define SPCL_STR_BSIZE 		1024
#define SPCL_ARGS_BSIZE 	16
#define MAX_NUM_SIZE		10
#define LINE_SIZE 		128
#define ALLOC_LST_N		16
#define MAX_PRINT_ELS		8

//easily find signature lengths
#define SIGLEN(s)		(sizeof(s)/sizeof(valtype))

//hash params
#define DEF_TAB_BITS		4
#define FNV_PRIME		16777619
#define FNV_OFFSET		2166136261
//we grow the hash table when the load factor (number of occupied slots/total slots) is greater than GROW_LOAD_NUM/GROW_LOAD_DEN
#define GROW_LOAD_NUM		4
#define GROW_LOAD_DEN		5

#if SPCL_DEBUG_LVL<1
#define spcl_local static inline
#else
#define spcl_local
#endif

//forward declarations
struct spcl_val;
//struct spcl_inst;
struct spcl_uf;
struct spcl_fn_call;
struct _vproc;
typedef struct spcl_val spcl_val;

//constants
typedef struct spcl_val (*lib_call)(struct spcl_fn_call, struct _vproc *vp);

typedef enum { E_SUCCESS, E_NOFILE, E_LACK_TOKENS, E_BAD_SYNTAX, E_BAD_VALUE, E_BAD_TYPE, E_NOMEM, E_NAN, E_UNDEF, E_OUT_OF_RANGE, E_ASSERT, N_ERRORS } parse_ercode;
typedef enum {VAL_UNDEF, VAL_ERR, VAL_INT, VAL_NUM, VAL_STR, VAL_ARRAY, VAL_MAT, VAL_LIST, VAL_FN, VAL_INST, _VAL_STACKREF, _VAL_GLOBREF, N_VALTYPES} valtype;
//helper classes and things
typedef enum {BLK_UNDEF, BLK_MISC, BLK_INVERT, BLK_TRANSFORM, BLK_DATA, BLK_ROOT, BLK_COMPOSITE, BLK_FUNC_DEC, BLK_LITERAL, BLK_COMMENT, BLK_SQUARE, BLK_QUOTE, BLK_QUOTE_SING, BLK_PAREN, BLK_CURLY, N_BLK_TYPES} blk_type;

/**
 * Macro to set a value while automagically calculating the string length
 */
#define spcl_set_val(name,val,copy,vp) spcl_set_valn(vp, s8(name), val, copy)
#define spcl_set_sub_val(c,name,val,copy,vp) spcl_set_inst_valn(c, s8(name), val, copy, vp)
#define make_spcl_fstream(name) make_spcl_fstreamn(name, strlen(name))
/**
 * provides a handy macro which wraps get_sigerr and aborts execution of a function if an invalid signature was detected
 * FN_CALL: the name of the function
 * SIGNATURE: a constant array of valtypes. Each argument in the function FN_CALL is tested to make sure it is of the appropriate type. You may include VAL_UNDEF in this array as a wildcard
 */
#define spcl_sigcheck(FN_CALL, SIGNATURE, VP) \
    spcl_val er = get_sigerr(FN_CALL, SIGLEN(SIGNATURE), SIGLEN(SIGNATURE), SIGNATURE, VP); \
    if (er.type == VAL_ERR) return er
/**
 * Acts like SIGCHECK, but allows for optional arguments.
 * FN_CALL: the name of the function
 * MIN_ARGS: the minimum number of arguments that may be accepted
 * SIGNATURE: the signature of all arguments, including optional ones
 */
#define spcl_sigcheck_opts(FN_CALL, MIN_ARGS, SIGNATURE, VP) \
    spcl_val er = get_sigerr(FN_CALL, MIN_ARGS, SIGLEN(SIGNATURE), SIGNATURE, VP); \
    if (er.type == VAL_ERR) return er
/**
 * register a function FN_CALL to the spcl_inst CON with the name NAME
 * CON: the spcl_inst to add the function to
 * FN_CALL: the C function to add
 * NAME: the name of the function when calling from a spcl script
 */
#define spcl_add_fn(fn_call,name,vp) spcl_set_valn(vp, s8(name), spcl_make_fn(name,1,&fn_call,vp), 0);
#define spcl_add_inst_fn(c,fn_call,name,vp) spcl_set_inst_valn(c, s8(name), spcl_make_fn(name,1,&fn_call,vp), 0, vp);

/** ======================================================== utility functions ======================================================== **/

/**
 * Works similarly to strncmp, but ignores leading and tailing whitespace and returns a negative spcl_val if strlen(b)<n or a positive spcl_val if strlen(b)>n
 * a: the first string
 * b: the second string
 * n: match at most n characters
 */
int namecmp(const char* a, const char* b, size_t n);
/**
 * write at most n bytes of the double spcl_vald x to str
 */
int write_numeric(char* str, size_t n, double x);

/** ============================ arena allocator ============================ **/



//macro to allocate a single instance of the type T on the arena a
#define anew(a,T)	((T*)alloc(a, sizeof(T), 1, alignof(T)))
//macro to allocate an array of n instances of the type T on the arena a
#define anewa(a,T,n)	((T*)alloc(a, sizeof(T), n, alignof(T)))
//get the size needed to hold n bytes, under the constraint that the number of bytes allocated must be an integer multiple of b
#define holdsize(n,b)	(b*((n+b-1)/b))

struct _vproc;

typedef struct {
    void *beg;		//the beginning of allocated memory
    void *end;		//the end of allocated memory
    void *head;		//the current head to insert to
} arena;
/**
 * Create and return an empty arena and allocate len bytes
 */
void init_arena(arena *a, psize len, struct _vproc *vp);
/**
 * Destroy memory used for the arena a
 */
void cleanup_arena(arena *a, struct _vproc *vp);
/**
 * Allocate a count elements of length len (aligned to align) on the arena a. For instance, to allocate an array T arr[n] you would pass `T* arr = alloc(a, sizeof(T), n, alignof(T));`
 * a: the arena to allocate to
 * size: the size of each element.
 * count: the number of elements to allocate
 * align: the alignment in memory of the type you want (MUST be a power of two)
 * returns: an (uninitialized) pointer to the region of memory allocated. This is gauranteed to be aligned to have an address divisible by align bytes
 */
void *alloc(arena *a, psize size, psize count, psize align);
/**
 * Deallocate size bytes from the arena a
 */
void dealloc(arena *a, psize size);
/**
 * Reset the arena and deallocate all memory
 */
void reset(arena *a);

/** ============================ vproc ============================ **/

typedef struct {
    s8 name;
    psize ind;
} _hash_item;

typedef struct {
    _hash_item *table;
    usize n_memb;
    usize t_bits;
} spcl_dict;

//a virtual process
typedef struct _vproc {
    void *heap;
    spcl_val *stack;
    spcl_dict d;
    arena a;
    usize pc;		//program counter
    psize sp;		//stack pointer
} vproc;

/**
 * Initialize a vproc struct by reading from a file
 * fname: the name of the file to read
 * argc: the number arguments
 * argv: an array of arguments taken from the command-line. Note that callers should not directly pass argc,argv from int main(). Rather, argv should only include valid spclang commands. If you know that spclang commands start at the index i, then you should call spcl_inst_from_file(fname, argc-i, argv+(size_t)i).
 * returns: on success, a pointer to a vproc which should be destroyed with a call to destroy_vproc, otherwise NULL is returned and destroy_vproc() is still safe
 */
vproc *spcl_read_file(const char* fname, int argc, const char** argv);

/**
 * Setup a new virtual process along with its own callstack and registers.
 */
vproc *make_vproc();
/**
 * destroy the virtual process pointed to by vp and deallocate its memory
 */
void destroy_vproc(vproc *vp);
/**
 * Set the spcl_val with a name matching p_name to a copy of p_val.
 * name: the name of the variable to set
 * new_val: the spcl_val to set the variable to
 * copy: This is a boolean which, if true, performs a deep copy of new_val. Otherwise, only a shallow copy (move) is performed.
 * move_assign: If set to true, then the spcl_val is directly moved into the spcl_inst. This can save some time.
 */
void spcl_set_valn(vproc *vp, s8 name, spcl_val new_val, int copy);
/**
 * execute the instructions at insts[n_insts]
 */
spcl_val vproc_exec(vproc *vp, usize* insts, usize n_insts);
/**
 * Helper function which converts an instruction to a pointer to a spcl_val. This function does not perform any allocations, it simply looks up.
 * inst_loc: either L_LIT, L_REG, L_STK, or L_HEP to indicate which sector to look in
 * inst: the instruction to convert
 */
spcl_local spcl_val* _val_from_inst(vproc *vp, short inst_loc, usize inst, spcl_val *sto);
/**
 * Given a string str, return a spcl_val corresponding to the expression str
 * c: the spcl_inst to use when looking for variables and functions
 * str: the string expression to parse
 * returns: a spcl_val with the resultant expression
 */
spcl_val spcl_parse_line(vproc *vp, const char* str);
/**
 * Test whether the string str evaluates to true when using c.
 * c: the spcl_inst to use when looking for variables and functions
 * str: the string expression to parse
 * returns: 0 if str evaluated to false or an error occurred during parsing. Otherwise, 1 is returned.
 */
int spcl_test(vproc *vp, const char* str);

/** ============================ struct spcl_val ============================ **/

typedef struct spcl_error {
    parse_ercode c;
    char msg[ERR_BSIZE];
} spcl_error;

union V {
    spcl_error* e;
    char* s;
    int i;
    double x;
    double* a;
    struct spcl_val* l;
    struct spcl_uf* f;
    struct spcl_inst* c;
};

struct spcl_val {
    valtype type;
    union V val;
    size_t n_els; //only applicable for string and list types
};

/**
 * create an empty spcl_val
 */
spcl_val spcl_make_none();
/**
 * Create a new error with the specified code and format specifier
 * code: error code type
 * format: a format specifier (just like printf)
 * returns: a pointer to an error object with the specified, which should be deallocated with a call to free()
 */
spcl_val spcl_make_err(parse_ercode code, vproc *vp, const char* format, ...);
/**
 * create a spcl_val from an int
 */
spcl_val spcl_make_int(int x);
/**
 * create a spcl_val from a float
 */
spcl_val spcl_make_num(double x);
/**
 * create a spcl_val from a string
 */
spcl_val spcl_make_str(const char* s, psize n, vproc *vp);
/**
 * create a spcl_val from a c array of doubles
 */
spcl_val spcl_make_array(double* vs, size_t n, vproc *vp);
/**
 * create a spcl_val from a list
 */
spcl_val spcl_make_list(const spcl_val* vs, size_t n_vs, vproc *vp);
/**
 * Add a new callable function with the signature sig and function pointer corresponding to the executed code. This function must accept a function and a pointer to an error code and return a spcl_val.
 */
spcl_val spcl_make_fn(const char* name, psize n_ret, lib_call p_exec, vproc *vp);
/**
 * make an instance object with the given type
 * p: the parent of the current instance (i.e. its owner
 * s: the name of the type
 */
spcl_val spcl_make_inst(const char* s, vproc *vp);
/**
 * Compare two spcl_vals if appropriate, in a manner similar to strcmp.
 * returns: 0 if a==b, a positive spcl_val if a>b, and a negative spcl_val if a<b. If no comparison is possible, undefined is returned.
 */
spcl_val spcl_valcmp(spcl_val a, spcl_val b, vproc *vp);
/**
 * Convert a spcl_val to a string representation.
 * v: the spcl_val to convert to a string
 * buf: the buffer to write to
 * n: The number of bytes in buf which may safely be written
 * returns: a pointer to the null terminator written to buf
 */
char* spcl_stringify(spcl_val v, char* buf, size_t n);
/**
 * Perform a cast of the instance to the type t. An error is returned if a cast is impossible.
 * v: the spcl_val to cast
 * type: the type to cast v to
 * returns: a spcl_val with the specified type or an error spcl_val if the cast was impossible
 */
spcl_val spcl_cast(spcl_val v, valtype type, vproc *vp);
/**
 * check if the spcl_val has a type matching the typename str
 */
void cleanup_spcl_val(spcl_val* o, vproc *vp);
/**
 * create a new spcl_val which is a deep copy of o
 */
spcl_val copy_spcl_val(const spcl_val o, vproc *vp);
/**
 * This function behaves identically to strcmp, except it uses the internal string representatation for speclang. (strings are fat pointers as opposed to null terminated)
 */
int spcl_strcmp(spcl_val a, spcl_val b);

#if SPCL_DEBUG_LVL>0
/**
 * Recursively print out a spcl_val and the spcl_vals it contains. This is useful for debugging.
 */
void print_hierarchy(spcl_val v, FILE* f, size_t depth);
/**
 * Add two spcl_vals together, overwriting the result to l
 * num+num: arithmetic
 * array+array: piecewise addition
 * array+num: add number to each element
 * mat+mat: matrix addition
 * list+*: append to list
 * str+*: append the string representation of the type * to str
 */
// spcl_val operations
void val_add(spcl_val* l, spcl_val r, vproc *vp);
/**
 * Add two spcl_vals together, overwriting the result to l
 * num+num: arithmetic
 * array-array: piecewise subtraction
 * array-num: subtract number from each element
 * mat-mat: matrix subtraction
 */
void val_sub(spcl_val* l, spcl_val r, vproc *vp);
/**
 * Add two spcl_vals together, overwriting the result to l
 * num*num: arithmetic
 * array*array: piecewise multiplication
 * array*num: multiply each element by a number
 * mat*mat: piecewise matrix multiplication (NOT matrix multiplication)
 */
void val_mul(spcl_val* l, spcl_val r, vproc *vp);
/**
 * compute l/r
 * num/num: arithmetic
 * array/array: piecewise division
 * array/num: divide each element by a number
 * mat/mat: piecewise matrix multiplication
 */
void val_div(spcl_val* l, spcl_val r, vproc *vp);
/**
 * compute the remainder of l/r
 * num/num: arithmetic
 * array/array: piecewise division
 * array/num: divide each element by a number
 * mat/mat: piecewise matrix multiplication
 */
void val_mod(spcl_val* l, spcl_val r, vproc *vp);
/**
 * raise l^r
 * num/num: arithmetic
 * array/array: piecewise division
 * array/num: divide each element by a number
 * mat/mat: piecewise matrix multiplication
 */
void val_exp(spcl_val* l, spcl_val r, vproc *vp);
#endif

/** ============================ spcl_fn_call ============================ **/

/**
 * A class which stores a labeled spcl_val.
 */
typedef struct name_val_pair {
    s8 s;	//the name of the pair
    spcl_val v;	//the spcl_val
} name_val_pair;
struct name_val_pair make_name_val_pair(const char* p_name, spcl_val p_val);
void cleanup_name_val_pair(name_val_pair nv);

//TODO: refactor spcl_fn_call to accept psize's instead of names
typedef struct spcl_fn_call {
    s8 name;
    spcl_val args[SPCL_ARGS_BSIZE];
    size_t n_args;
} spcl_fn_call;

spcl_fn_call copy_spcl_fn_call(const spcl_fn_call o);
void cleanup_spcl_fn_call(spcl_fn_call* o);

/** ============================ spcl_inst ============================ **/

struct spcl_inst {
    spcl_dict *cls;
    spcl_val *vals;
};
typedef struct spcl_inst spcl_inst;

/**
 * make an empty spcl_inst. The result must be destroyed using destroy_inst().
 * parent: the parent of this spcl_inst so that we can look up in scope (i.e. a function can access global variables)
 */
struct spcl_inst *make_spcl_inst(vproc *vp);
/**
 * Create a deep copy of the spcl_inst o and return the result. The result must be destroyed using destroy_inst().
 */
struct spcl_inst *copy_spcl_inst(spcl_inst *o, vproc *vp);
/**
 * cleanup the spcl_inst c
 */
void destroy_spcl_inst(spcl_inst *c, vproc *vp);
/**
 * set the value of the instance c to a name p_name and value p_val
 * c: the instance to set
 * p_name: the name of the value in c to set
 * p_val: the value to assign to p_name
 * copy: whether or not the value should be copied
 * vp: the process on which c was allocated
 */
void spcl_set_inst_valn(spcl_inst *c, s8 p_name, spcl_val p_val, int copy, vproc *vp);
/**
 * Search the spcl_inst for the variable with the matching name.
 * name: the name of the variable to set
 * returns: the matching spcl_val, no deep copies are performed
 */
spcl_val spcl_find(spcl_inst *c, s8 name);
/**
 * Lookup the object named str in c and save the resulting spcl_inst to sto
 * c: the spcl_inst to search
 * str: the name to lookup
 * type: force the object to match the specified typename
 * sto: overwrite this information to save
 * returns: 0 on success or a negative spcl_val if an error occurred (-1 indicates no match, -2 indicates match of the wrong type)
 */
int spcl_find_object(vproc *vp, const char* str, const char* type, spcl_inst** sto);
/**
 * Lookup the spcl_val named str in c and write the first n elements of the resulting list/array to sto
 * c: the spcl_inst to search
 * str: the name to lookup
 * sto: the array to save to. At most n values are written. If the spcl_array found has m elements and m<n, then all values sto[i] with i>=m are not modified.
 * n: the length of sto
 * returns: the number of elements written on success or a negative spcl_val if an error occurred (-1 indicates no match, -2 indicates match of the wrong type, -3 indicates an invalid element)
 */
int spcl_find_c_iarray(vproc *vp, const char* str, int* sto, size_t n);
/**
 * Lookup the spcl_val named str in c and write the first n elements of the resulting list/array to sto
 * c: the spcl_inst to search
 * str: the name to lookup
 * sto: the array to save to. At most n values are written. If the spcl_array found has m elements and m<n, then all values sto[i] with i>=m are not modified.
 * n: the length of sto
 * returns: the number of elements written on success or a negative spcl_val if an error occurred (-1 indicates no match, -2 indicates match of the wrong type, -3 indicates an invalid element)
 */
int spcl_find_c_uarray(vproc *vp, const char* str, unsigned* sto, size_t n);
/**
 * Lookup the spcl_val named str in c and write the first n elements of the resulting list/array to sto
 * c: the spcl_inst to search
 * str: the name to lookup
 * sto: the array to save to. At most n values are written. If the spcl_array found has m elements and m<n, then all values sto[i] with i>=m are not modified.
 * n: the length of sto
 * returns: the number of elements written on success or a negative spcl_val if an error occurred (-1 indicates no match, -2 indicates match of the wrong type, -3 indicates an invalid element)
 */
int spcl_find_c_darray(vproc *vp, const char* str, double* sto, size_t n);
/**
 * Lookup the spcl_val named str in c and write the string sto
 * c: the spcl_inst to search
 * str: the name to lookup
 * sto: the array to save to (this is guaranteed to be null terminated after a call)
 * n: the length of sto
 * returns: the number of elements written on success or a negative spcl_val if an error occurred (-1 indicates no match, -2 indicates match of the wrong type, -3 indicates an invalid element)
 */
int spcl_find_c_str(vproc *vp, const char* str, char* sto, size_t n);
/**
 * lookup the integer spcl_val in c at str and save to sto.
 * returns: 0 on success or -1 if the name str couldn't be found
 */
int spcl_find_int(vproc *vp, const char* str, int* sto);
/**
 * lookup the unsigned integer spcl_val in c at str and save to sto.
 * returns: 0 on success or -1 if the name str couldn't be found
 */
int spcl_find_uint(vproc *vp, const char* str, unsigned* sto);
/**
 * lookup the floating point spcl_val in c at str and save to sto.
 * returns: 0 on success or -1 if the name str couldn't be found
 */
int spcl_find_float(vproc *vp, const char* str, double* sto);

#endif //READ_H
