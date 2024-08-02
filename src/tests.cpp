#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

extern "C" {
#include "speclang.h"
#include "spcl_utils.h"
#include "exec.h"
#define TEST_FNAME	"/tmp/speclang_test.spcl"
}
#define STRIFY(S) #S

//this is equivalent to strncpy but safe. i.e. it is always gauranteed to have a null terminator
#define safecpy(dst,src,n) strncpy(dst, src, n);dst[n-1] = 0;

spcl_val cstr_to_spcl(const char* str) {
    spcl_val v;
    v.type = VAL_STR;
    v.n_els = strlen(str);
    v.val.s = const_cast<char*>(str);
    return v;
}

class s8cpp {
private:
    char* mem;
public:
    s8 str;
    s8cpp(std::string p_str) {
	str.n = p_str.size();
	mem = strdup(p_str.c_str());
	str.s = mem;
    }
    ~s8cpp() {
	free(mem);
	str.n = 0;
    }
};

void test_num(spcl_val v, double x) {
    int is_num = (v.type == VAL_NUM || v.type == VAL_INT);
    CHECK(is_num);
    CHECK(v.n_els == 1);
    if (v.type == VAL_NUM) 
	CHECK(v.val.x == x);
    else
	CHECK(v.val.i == (int)x);
}

/**
 * save the mean and variance of values in the flattened array x[n] to mean and var. Takes only points where the index i satisfies off <= i % (dim+space) < off+dim these samples are treated as a vector. For instance mean_var(x, 6, 2, 1, 0, &mean, &var) will take the samples i=(0,1),(3,4). mean_var(x, 6, 2, 0, 0, &mean, &var) will take the samples i=(0,1),(2,3),(4,5)
 * x: the list of points
 * dim: the dimension of each sample
 * space: the separation between each sample
 * mean: a pointer where the mean is stored
 * var: a pointer where the variance is stored
 */
void mean_var(const double* x, size_t n, double* mean, double* var, size_t dim=1, size_t space=0, size_t off=0) {
    if (n == 0) {
	*mean = 0;
	*var = 0;
	return;
    }
    *mean = 0;
    size_t width = dim+space;
    for (size_t i = 0; i < n; ++i) {
	double tmp = 0;
	for (size_t j = 0; j < dim; ++j)
	    tmp += x[i*width+j+off]*x[i*width+j+off];
	*mean += sqrt(tmp);
    }
    *mean = *mean/(double)n;
    *var = 0;
    //avoid division by zero
    if (n == 1)
	return;
    for (size_t i = 0; i < n; ++i) {
	double tmp = 0;
	for (size_t j = 0; j < dim; ++j)
	    tmp += x[i*width+j+off]*x[i*width+j+off];
	tmp = sqrt(tmp);
	*var += (tmp - *mean)*(tmp - *mean);
    }
    *var = *var/(double)(n-1);
}

TEST_CASE("namecmp") {
    s8cpp s8ta("ta ");
    s8cpp s8tan("tan");
    s8cpp s8foo("foo");
    s8cpp s8bar("bar");
    s8cpp s8foot("foot");
    s8cpp s8foop("foo+");
    s8cpp s8mfoop("-foo+");
    s8cpp s8foo_("foo ");
    s8cpp s8tfoo_("\tfoo ");

    CHECK(namecmp(s8foo.str.s, s8foo.str.s, 3) == 0);		// "foo"[:3] is "foo"
    CHECK(namecmp(s8foo.str.s, s8foot.str.s, 2) == 0);		// "foo"[:2] is "foo"
    CHECK(namecmp(s8foo.str.s, s8foot.str.s, 3) != 0);		// "foo"[:3] isn't "foot"
    CHECK(namecmp(s8foo.str.s, s8bar.str.s, 3) != 0);		// "foo"[:3] isn't "bar"
    CHECK(namecmp(s8foo_.str.s, s8foo.str.s, 3) == 0);		// "foo "[:3] is "bar"
    CHECK(namecmp(s8foo_.str.s, s8foo.str.s, SIZE_MAX) == 0);	// "foo "[:2^64-1] is "foo"
    CHECK(namecmp(s8foo_.str.s, s8foop.str.s, 3) == 0);		// "foo "[:3] is "foo+"
    CHECK(namecmp(s8tfoo_.str.s, s8foo.str.s, 3) == 0);		// "\tfoo"[:3] is "foo+"
    CHECK(namecmp(s8foo_.str.s, s8mfoop.str.s, 3) == 0);	// "foo"[:3] is "-foo+"
    CHECK(namecmp(s8ta.str.s, s8tan.str.s, 2) != 0);		// "ta"[:2] is "tan"
}

#define opis(nd, o) (nd && nd->flags == ND_ISOP && nd->v.val.i == o)
TEST_CASE("optree generation") {
    vproc *vp = make_vproc();
    s8cpp s8_assign("a = 1");
    s8cpp s8_op_order("a + b*c-3");
    s8cpp s8_paren("(2.0+3)*4");
    s8cpp s8_arr("[1e-1, 2, 3]");

    optree_nd *nd = tokenize_str(vp, s8_assign.str.s, s8_assign.str.n);
    CHECK(opis(nd, OPTR_ASSGN));
    CHECK(nd->l->flags == ND_ISLF);
    CHECK(nd->l->v.val.s[0] == 'a');
    CHECK(nd->l->v.n_els == 1);
    CHECK(nd->r->v.val.i == 1);
    //reset(vp.a);

    nd = tokenize_str(vp, s8_op_order.str.s, s8_op_order.str.n);
    CHECK(opis(nd, OPTR_SUB));
    CHECK(opis(nd->l, OPTR_ADD));
    CHECK(opis(nd->l->r, OPTR_MUL));
    CHECK(nd->r->flags == ND_ISCNST);
    CHECK(nd->r->v.val.i == 3);
    CHECK(nd->l->l->flags == ND_ISLF);
    //reset(vp.a);

    nd = tokenize_str(vp, s8_paren.str.s, s8_paren.str.n);
    CHECK(opis(nd, OPTR_MUL));
    CHECK(opis(nd->l, OPTR_PAREN));
    CHECK(opis(nd->l->l, OPTR_ADD));
    CHECK(nd->l->l->l->v.val.x == 2.0);
    CHECK(nd->l->l->r->v.val.i == 3);
    CHECK(nd->r->v.val.i == 4);
    /*CHECK(opis(nd, OPTR_MUL));
    CHECK(opis(nd->l, OPTR_ADD));
    CHECK(nd->l->l->v.val.x == 2.0);
    CHECK(nd->l->r->v.val.i == 3);
    CHECK(nd->r->v.val.i == 4);*/
    //reset(vp.a);

    nd = tokenize_str(vp, s8_arr.str.s, s8_arr.str.n);
    CHECK(opis(nd, OPTR_LSTDF));
    CHECK(opis(nd->l, OPTR_APPND));
    CHECK(nd->l->l->flags == ND_ISCNST);
    CHECK(nd->l->l->v.val.x == 0.1);
    CHECK(opis(nd->l->r, OPTR_APPND));
    CHECK(nd->l->r->l->flags == ND_ISCNST);
    CHECK(nd->l->r->l->v.val.i == 2);
    CHECK(nd->l->r->r->flags == ND_ISCNST);
    CHECK(nd->l->r->r->v.val.i == 3);
    destroy_vproc(vp);
}

#define INST_SIZE	256
TEST_CASE("bytecode") {
    vproc *vp = make_vproc();
    u64 inst_buf[INST_SIZE];
    SUBCASE("Arithmetic") {
	u64 i = 0;
	spcl_val a = spcl_make_num(1), b = spcl_make_num(2);
	//first check that we can push literals
	inst_buf[i++] = gen_op2(OP_PUSH, L_HEP);
	inst_buf[i++] = (u64)(&a);
	inst_buf[i++] = gen_op2(OP_PUSH, L_HEP);
	inst_buf[i++] = (u64)(&b);
	vproc_exec(vp, inst_buf, i);
	//check that the values are as expected
	test_num(vp->stack[vp->sp+1], 1);
	test_num(vp->stack[vp->sp+0], 2);
	//increment s[1]
	inst_buf[i++] = gen_op2(OP_INC, L_STK);
	inst_buf[i++] = 1;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 2);
	//decrement s[1]
	inst_buf[i++] = gen_op2(OP_DEC, L_STK);
	inst_buf[i++] = 1;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 1);
	//add
	inst_buf[i++] = gen_op3((OP_ADD | RELOP_BIT), L_STK, L_STK);
	inst_buf[i++] = 1;
	inst_buf[i++] = 0;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 3);
	//subtract
	inst_buf[i++] = gen_op3((OP_SUB | RELOP_BIT), L_STK, L_STK);
	inst_buf[i++] = 1;
	inst_buf[i++] = 0;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 1);
	//multiply
	inst_buf[i++] = gen_op3((OP_MUL | RELOP_BIT), L_STK, L_STK);
	inst_buf[i++] = 1;
	inst_buf[i++] = 0;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 2);
	//exponent
	inst_buf[i++] = gen_op3((OP_EXP | RELOP_BIT), L_STK, L_STK);
	inst_buf[i++] = 1;
	inst_buf[i++] = 0;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 4);
	//divide
	inst_buf[i++] = gen_op3((OP_DIV | RELOP_BIT), L_STK, L_STK);
	inst_buf[i++] = 1;
	inst_buf[i++] = 0;
	vproc_exec(vp, inst_buf, i);
	test_num(vp->stack[vp->sp+1], 2);
    }
}
TEST_CASE("optree unfolding") {
    vproc *vp = make_vproc(); 
    u64 inst_buf[INST_SIZE];
    spcl_val er;
    memset(&er, 0, sizeof(spcl_val));

    s8cpp s8_seta("a = 11");
    s8cpp s8_setb("b = 22");
    s8cpp s8_setc("c = 33");
    s8cpp s8_op_order("a + b*c-3");
    s8cpp s8_paren("(a+b)*4.0");
    s8cpp s8_const_prop("(2.0+3)*4");
    s8cpp s8_arr("lst = [1e-1, 2, 3]");

    optree_nd *nd = tokenize_str(vp, s8_seta.str.s, s8_seta.str.n);
    unfold_res res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    nd = tokenize_str(vp, s8_setb.str.s, s8_setb.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    nd = tokenize_str(vp, s8_setc.str.s, s8_setc.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    //reset(a);

    nd = tokenize_str(vp, s8_op_order.str.s, s8_op_order.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    CHECK(er.type != VAL_ERR);
    CHECK(op_code(inst_buf[0]) == OP_MUL);
    CHECK(op_dstl(inst_buf[0]) == L_STK);
    CHECK(op_srcl(inst_buf[0]) == L_STK);
    CHECK(op_code(inst_buf[3]) == OP_ADD);
    CHECK(op_dstl(inst_buf[3]) == L_STK);
    CHECK(op_srcl(inst_buf[3]) == L_STK);
    CHECK(op_code(inst_buf[6]) == OP_RSUB);
    CHECK(op_dstl(inst_buf[6]) == L_STK);
    CHECK(op_srcl(inst_buf[6]) == L_LIT);
    CHECK(inst_buf[7] == 0);
    CHECK(inst_buf[8] == 3);
    //check that the correct value was returned
    CHECK(res.l == L_STK);
    CHECK(vp->stack[vp->sp+res.v.st].val.i == 734);	//result of 11 + 22*33 - 3
    //reset(a);

    nd = tokenize_str(vp, s8_paren.str.s, s8_paren.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    CHECK(er.type != VAL_ERR);
    CHECK(op_code(inst_buf[0]) == OP_ADD);
    CHECK(op_dstl(inst_buf[0]) == L_STK);
    CHECK(op_srcl(inst_buf[0]) == L_STK);
    CHECK(op_code(inst_buf[3]) == OP_RMUL);
    CHECK(op_dstl(inst_buf[3]) == L_STK);
    CHECK(op_srcl(inst_buf[3]) == L_HEP);
    CHECK(inst_buf[4] == 0);
    //check that the correct value was returned
    CHECK(res.l == L_STK);
    CHECK(vp->stack[vp->sp+res.v.st].val.x == 132);	//result of (11+22)*4
    //reset(a);

    nd = tokenize_str(vp, s8_const_prop.str.s, s8_const_prop.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    CHECK(er.type != VAL_ERR);
    CHECK(op_code(inst_buf[0]) == OP_MUL);
    CHECK(op_dstl(inst_buf[0]) == L_HEP);
    CHECK(op_srcl(inst_buf[0]) == L_LIT);
    CHECK(inst_buf[2] == 4);
    //check that the correct value was returned
    CHECK(res.l == L_HEP);
    CHECK(res.v.hp->type == VAL_NUM);	//result of (11+22)*4
    CHECK(res.v.hp->val.x == 20);	//result of (11+22)*4
    //reset(a);

    nd = tokenize_str(vp, s8_arr.str.s, s8_arr.str.n);
    res = unfold_optree(vp, nd, inst_buf, INST_SIZE, &er);
    CHECK(er.type != VAL_ERR);
    //check that the correct value was returned
    spcl_val stack_top = vp->stack[vp->sp+res.v.st];
    CHECK(stack_top.type == VAL_LIST);
    CHECK(stack_top.n_els == 3);
    CHECK(stack_top.val.l[0].val.x == 0.1);
    CHECK(stack_top.val.l[1].val.i == 2);
    CHECK(stack_top.val.l[2].val.i == 3);
    
    destroy_vproc(vp);
}

TEST_CASE("spcl_val parsing") {
    char buf[SPCL_STR_BSIZE];
    vproc *vp = make_vproc();
    spcl_val tmp_val;

    SUBCASE("Reading numbers to spcl_vals works") {
	//test integers
	safecpy(buf, "1", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 1);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "12", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 12);
	cleanup_spcl_val(&tmp_val, vp);
	//test floats
	safecpy(buf, ".25", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, .25);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "+1.25", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 1.25);
	cleanup_spcl_val(&tmp_val, vp);
	//test scientific notation
	safecpy(buf, ".25e10", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 0.25e10);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "1.25e+10", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 1.25e10);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "-1.25e-10", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, -1.25e-10);
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("Reading strings to spcl_vals works") {
	//test a simple string
	safecpy(buf, "\"foo\"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("foo")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	//test a string with whitespace
	safecpy(buf, "\" foo bar \"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl(" foo bar ")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	//test a string with stuff inside it
	safecpy(buf, "\"foo(bar)\"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("foo(bar)")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	//test a string with an escaped string
	safecpy(buf, "\"foo\\\"bar\\\" \"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("foo\"bar\" ")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("Reading lists to spcl_vals works") {
	//test one element lists
	safecpy(buf, "[\"foo\"]", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_LIST);
	REQUIRE(tmp_val.val.l != NULL);
	CHECK(tmp_val.n_els == 1);
	CHECK(tmp_val.val.l[0].type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val.val.l[0], cstr_to_spcl("foo")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	//test two element lists
	safecpy(buf, "[\"foo\", 1]", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_LIST);
	REQUIRE(tmp_val.val.l != NULL);
	CHECK(tmp_val.n_els == 2);
	CHECK(tmp_val.val.l[0].type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val.val.l[0], cstr_to_spcl("foo")) == 0);
	CHECK(tmp_val.val.l[1].type == VAL_INT);
	CHECK(tmp_val.val.l[1].val.i == 1);
	cleanup_spcl_val(&tmp_val, vp);
	//test lists of lists
	safecpy(buf, "[[1,2,3], [\"4\", \"5\", \"6\"]]", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_LIST);
	REQUIRE(tmp_val.val.l != NULL);
	REQUIRE(tmp_val.n_els == 2);
	{
	    //check the first sublist
	    spcl_val element = tmp_val.val.l[0];
	    REQUIRE(element.type == VAL_LIST);
	    REQUIRE(element.n_els == 3);
	    REQUIRE(element.val.l != NULL);
	    CHECK(element.val.l[0].type == VAL_INT);
	    CHECK(element.val.l[0].val.i == 1);
	    CHECK(element.val.l[1].type == VAL_INT);
	    CHECK(element.val.l[1].val.i == 2);
	    CHECK(element.val.l[2].type == VAL_INT);
	    CHECK(element.val.l[2].val.i == 3);
	    //check the second sublist
	    element = tmp_val.val.l[1];
	    REQUIRE(element.type == VAL_LIST);
	    REQUIRE(element.n_els == 3);
	    REQUIRE(element.val.l != NULL);
	    CHECK(element.val.l[0].type == VAL_STR);
	    CHECK(spcl_strcmp(element.val.l[0], cstr_to_spcl("4")) == 0);
	    CHECK(element.val.l[1].type == VAL_STR);
	    CHECK(spcl_strcmp(element.val.l[1], cstr_to_spcl("5")) == 0);
	    CHECK(element.val.l[2].type == VAL_STR);
	    CHECK(spcl_strcmp(element.val.l[2], cstr_to_spcl("6")) == 0);
	}
	cleanup_spcl_val(&tmp_val, vp);
    } 
    destroy_vproc(vp);
}

TEST_CASE("string handling") {
    const size_t STR_SIZE = 64;
    vproc *vp = make_vproc();
    char buf[STR_SIZE];memset(buf, 0, STR_SIZE);
    //check that parsing an empty (or all whitespace) string gives VAL_UNDEF
    spcl_val tmp_val = spcl_parse_line(vp, buf);
    CHECK(tmp_val.type == VAL_UNDEF);
    CHECK(tmp_val.n_els == 0);
    cleanup_spcl_val(&tmp_val, vp);
    safecpy(buf, " ", STR_SIZE);
    tmp_val = spcl_parse_line(vp, buf);
    CHECK(tmp_val.type == VAL_UNDEF);
    CHECK(tmp_val.n_els == 0);
    cleanup_spcl_val(&tmp_val, vp);
    safecpy(buf, "\t  \t\n", STR_SIZE);
    tmp_val = spcl_parse_line(vp, buf);
    CHECK(tmp_val.type == VAL_UNDEF);
    CHECK(tmp_val.n_els == 0);
    cleanup_spcl_val(&tmp_val, vp);
    //now check that strings are the right length
    for (size_t i = 1; i < STR_SIZE-1; ++i) {
	for (size_t j = 0; j < i; ++j)
	    buf[j] = 'a';
	buf[0] = '\"';
	buf[i] = '\"';
	buf[i+1] = 0;
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.n_els == i-1);
	cleanup_spcl_val(&tmp_val, vp);
	CHECK(tmp_val.n_els == 0);
    }
    destroy_vproc(vp);
}

TEST_CASE("operations") {
    vproc *vp = make_vproc();
    char buf[SPCL_STR_BSIZE];
    SUBCASE("Arithmetic works") {
        //single operations
        safecpy(buf, "1+1.1", SPCL_STR_BSIZE);
        spcl_val tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 2.1);
        safecpy(buf, "2-1.25", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 0.75);
        safecpy(buf, "2*1.1", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 2.2);
        safecpy(buf, "2.2/2", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 1.1);
        //order of operations
        safecpy(buf, "2*2-1", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 3);
        safecpy(buf, "1+3/2", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 2.5);
        safecpy(buf, "(1+3)/2", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 2.0);
        safecpy(buf, "-(1+3)/2", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, -2.0);
        safecpy(buf, "2*9/4*3", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 12);
	safecpy(buf, "2.0**-4", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 0.0625);
	safecpy(buf, "-2*9**2/4*3", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, -120);
	safecpy(buf, "(-2*9)**2/4*3", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 243);
    }
    SUBCASE("Comparisons work") {
	//create a single true and false, this makes things easier
	CHECK(spcl_test(vp, "2 == 2") == 1);
	CHECK(spcl_test(vp, "1 == 2") == 0);
	CHECK(spcl_test(vp, "[2, 3] == [2, 3]") == 1);
	CHECK(spcl_test(vp, "[2, 3, 4] == [2, 3]") == 0);
	CHECK(spcl_test(vp, "[2, 3, 4] == [2, 3, 5]") == 0);
	CHECK(spcl_test(vp, "\"apple\" == \"apple\"") == 1);
	CHECK(spcl_test(vp, "\"apple\" == \"banana\"") == 0);

	CHECK(spcl_test(vp, "1 == 1") == 1);
	CHECK(spcl_test(vp, "1 == 2") == 0);
	CHECK(spcl_test(vp, "1 >= 1") == 1);
	CHECK(spcl_test(vp, "1 > 1") == 0);
	CHECK(spcl_test(vp, "-1 > 1") == 0);
	CHECK(spcl_test(vp, "1 <= 1") == 1);
	CHECK(spcl_test(vp, "1 < 2") == 1);
	CHECK(spcl_test(vp, "1 < 1") == 0);
	CHECK(spcl_test(vp, "1 < -1") == 0);
    }
    SUBCASE("String concatenation works") {
        //single operations
        safecpy(buf, "\"foo\"+\"bar\"", SPCL_STR_BSIZE);
        spcl_val tmp_val = spcl_parse_line(vp, buf);
        CHECK(tmp_val.type == VAL_STR);
        CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("foobar")) == 0);
	CHECK(tmp_val.n_els == 6);
        cleanup_spcl_val(&tmp_val, vp);
	CHECK(tmp_val.n_els == 0);
    }
    SUBCASE("boolean operations work") {
	//or
	CHECK(spcl_test(vp, "false || false") == 0);
	CHECK(spcl_test(vp, "true || false") == 1);
	CHECK(spcl_test(vp, "false || true") == 1);
	CHECK(spcl_test(vp, "true || true") == 1);
	CHECK(spcl_test(vp, "false && false") == 0);
	CHECK(spcl_test(vp, "true && false") == 0);
	CHECK(spcl_test(vp, "false && true") == 0);
	CHECK(spcl_test(vp, "true && true") == 1);
	CHECK(spcl_test(vp, "!false") == 1);
	CHECK(spcl_test(vp, "!true") == 0);
	//short circuiting && statements work (if the second branch is evaluated an error will occur)
	//TODO: come up with an error condition that will occur at runtime and not const-propagate
	/*safecpy(buf, "(\"foo\"-\"bar\" == 0)", SPCL_STR_BSIZE);
	spcl_val v = spcl_parse_line(vp, buf);
	CHECK(v.type == VAL_ERR);
	safecpy(buf, "(false && \"foo\"-\"bar\" == 0)", SPCL_STR_BSIZE);
	v = spcl_parse_line(vp, buf);
	test_num(v, 0);
	//short circuiting || statements works (if the second branch is evaluated an error will occur)
	safecpy(buf, "(true || \"foo\"-\"bar\" == 0)", SPCL_STR_BSIZE);
	v = spcl_parse_line(vp, buf);
	test_num(v, 1);*/
    }
    SUBCASE("Ternary operators work") {
	safecpy(buf, "(false) ? 100 : 200", SPCL_STR_BSIZE);
	spcl_val tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 200);
	safecpy(buf, "(true) ? 100 : 200", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 100);
	safecpy(buf, "(1 == 2) ? 100 : 200", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 200);
	safecpy(buf, "(2 == 2) ? 100 : 200", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 100);
	safecpy(buf, "(1 > 2) ? 100 : 200", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 200);
	safecpy(buf, "(1 < 2) ? 100 : 200", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 100);
#ifdef NEXP_TESTS
	safecpy(buf, "(1 < 2)? 0 : 1.2e-5", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 0);
#endif
	//check with pairs of strings
	safecpy(buf, "(1 < 2) ? \"100\" : \"200\"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("100")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "(1 > 2) ? \"100\" : \"200\"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_STR);
	CHECK(spcl_strcmp(tmp_val, cstr_to_spcl("200")) == 0);
	cleanup_spcl_val(&tmp_val, vp);
	//test graceful failure conditions
	safecpy(buf, "(1 < 2) ? 100", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected \':\' in ternary") == 0);
	cleanup_spcl_val(&tmp_val, vp);
    }
    destroy_vproc(vp);
}

TEST_CASE("builtin functions") {
    char buf[SPCL_STR_BSIZE];
    spcl_val tmp_val;
    vproc *vp = make_vproc();

    SUBCASE("range()") {
	safecpy(buf, "range(4)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_ARRAY);
	CHECK(tmp_val.n_els == 4);
	for (size_t i = 0; i < 4; ++i)
	    CHECK(tmp_val.val.a[i] == i);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "range(1,4)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_ARRAY);
	CHECK(tmp_val.n_els == 3);
	for (size_t i = 0; i < 3; ++i)
	    CHECK(tmp_val.val.a[i] == i+1);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "range(1,4,0.5)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_ARRAY);
	CHECK(tmp_val.n_els == 6);
	for (size_t i = 0; i < 6; ++i)
	    CHECK(tmp_val.val.a[i] == 0.5*i+1);
	cleanup_spcl_val(&tmp_val, vp);
        //graceful failure cases
        safecpy(buf, "range()", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	//check that divisions by zero are avoided
        safecpy(buf, "range(0,1,0)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_VALUE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "range(\"1\")", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "range(0.5,\"1\")", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "range(0.5,1,\"2\")", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("linspace()") {
        safecpy(buf, "linspace(1,2,5)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
        REQUIRE(tmp_val.type == VAL_ARRAY);
        REQUIRE(tmp_val.n_els == 5);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(tmp_val.val.a[i] == 1.0+0.25*i);
        }
        cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "linspace(2,1,5)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
        REQUIRE(tmp_val.type == VAL_ARRAY);
        REQUIRE(tmp_val.n_els == 5);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(tmp_val.val.a[i] == 2.0-0.25*i);
        }
        cleanup_spcl_val(&tmp_val, vp);
        //graceful failure cases
        safecpy(buf, "linspace(2,1)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "linspace(2,1,1)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_VALUE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "linspace(\"2\",1,1)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "linspace(2,\"1\",1)", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
        safecpy(buf, "linspace(2,1,\"1\")", SPCL_STR_BSIZE);
        tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
        WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("flatten()") {
	safecpy(buf, "flatten([])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_LIST);
	REQUIRE(tmp_val.n_els == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "flatten([1,2,3])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_LIST);
	REQUIRE(tmp_val.n_els == 3);
	for (size_t i = 0; i < 3; ++i) {
	    test_num(tmp_val.val.l[i], i+1);
	}
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "flatten([[1,2,3],[4,5],6])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_LIST);
	CHECK(tmp_val.n_els == 6);
	for (size_t i = 0; i < 6; ++i)
	    test_num(tmp_val.val.l[i], i+1);

	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("len") {
	safecpy(buf, "len(1)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 1);
	safecpy(buf, "len([1,2,3])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 3);
	safecpy(buf, "len(range(4))", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 4);
	safecpy(buf, "len(vec(1,2,3))", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	test_num(tmp_val, 3);
    }
    SUBCASE("math functions") {
	safecpy(buf, "math.sin(3.1415926535/2)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == doctest::Approx(1.0));
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.sin(3.1415926535/6)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == doctest::Approx(0.5));
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.cos(3.1415926535/2)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == doctest::Approx(0.0));
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.cos(3.1415926535/6)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.sqrt(3)/2", SPCL_STR_BSIZE);
	spcl_val sqrt_val = spcl_parse_line(vp, buf);
	CHECK(sqrt_val.type == VAL_NUM);
	CHECK(tmp_val.n_els == 1);
	CHECK(tmp_val.val.x == doctest::Approx(sqrt_val.val.x));
	safecpy(buf, "math.tan(3.141592653589793/4)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.n_els == 1);
	CHECK(tmp_val.val.x == doctest::Approx(1.0));
	safecpy(buf, "math.tan(0)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.n_els == 1);
	CHECK(tmp_val.val.x == doctest::Approx(0.0));
	safecpy(buf, "math.exp(0)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == doctest::Approx(1.0));
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.exp(1)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == doctest::Approx(2.718281828));
	CHECK(tmp_val.n_els == 1);
	safecpy(buf, "math.log(1)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	CHECK(tmp_val.type == VAL_NUM);
	CHECK(tmp_val.val.x == 0);
	CHECK(tmp_val.n_els == 1);
	//failure conditions
	safecpy(buf, "math.sin()", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.cos()", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.tan()", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.exp()", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.sqrt()", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_LACK_TOKENS);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.sin(\"a\")", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.cos(\"a\")", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.tan(\"a\")", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.exp(\"a\")", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "math.sqrt(\"a\")", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_TYPE);
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("assertions") {
	safecpy(buf, "assert(1)", SPCL_STR_BSIZE);
	spcl_val tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x == 1);
	CHECK(tmp.n_els == 1);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(true)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x == 1);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(false)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_ERR);
	WARN(tmp.val.e->c == E_ASSERT);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(1 <= 3)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x != 0);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(len([0]) == 1)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x != 0);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(1 > 3, \"1 is not greater than 3\")", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_ERR);
	WARN(tmp.val.e->c == E_ASSERT);
	WARN(strcmp(tmp.val.e->msg, "1 is not greater than 3") == 0);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "isdef(apple)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x == 0);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "isdef(math.pi)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_NUM);
	CHECK(tmp.val.x == 1);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(apple)", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_ERR);
	WARN(tmp.val.e->c == E_ASSERT);
	cleanup_spcl_val(&tmp, vp);
	safecpy(buf, "assert(isdef(apple))", SPCL_STR_BSIZE);
	tmp = spcl_parse_line(vp, buf);
	CHECK(tmp.type == VAL_ERR);
	WARN(tmp.val.e->c == E_ASSERT);
	cleanup_spcl_val(&tmp, vp);
    }
    SUBCASE("Reading vectors to spcl_vals works") {
	//test one element lists
	safecpy(buf, "vec(1.2, 3.4,56.7)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ARRAY);
	REQUIRE(tmp_val.val.a != NULL);
	REQUIRE(tmp_val.n_els == 3);
	CHECK(tmp_val.val.a[0] == doctest::Approx(1.2));
	CHECK(tmp_val.val.a[1] == doctest::Approx(3.4));
	CHECK(tmp_val.val.a[2] == doctest::Approx(56.7));
	cleanup_spcl_val(&tmp_val, vp);

	safecpy(buf, "array([1.2, 3.4,56.7])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ARRAY);
	REQUIRE(tmp_val.val.a != NULL);
	REQUIRE(tmp_val.n_els == 3);
	CHECK(tmp_val.val.a[0] == doctest::Approx(1.2));
	CHECK(tmp_val.val.a[1] == doctest::Approx(3.4));
	CHECK(tmp_val.val.a[2] == doctest::Approx(56.7));
	cleanup_spcl_val(&tmp_val, vp);

	safecpy(buf, "array([[0, 1, 2], [3, 4, 5], [6, 7, 8]])", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_MAT);
	REQUIRE(tmp_val.val.l != NULL);
	REQUIRE(tmp_val.n_els == 3);
	for (size_t i = 0; i < 3; ++i) {
	    REQUIRE(tmp_val.val.l[i].type == VAL_ARRAY);
	    REQUIRE(tmp_val.val.l[i].n_els == 3);
	    for (size_t j = 0; j < 3; ++j) {
		CHECK(tmp_val.val.l[i].val.a[j] == i*3 + j);
	    }
	}
	cleanup_spcl_val(&tmp_val, vp);
    }
    SUBCASE("Missing end graceful failure") {
	safecpy(buf, "[1,2", SPCL_STR_BSIZE);
	spcl_val tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected matching \']\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "a(1,2", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected matching \')\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "\"1,2", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected matching \'\"\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "1,2]", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected line end instead of \']\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "1,2)", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected line end instead of \')\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
	safecpy(buf, "1,2\"", SPCL_STR_BSIZE);
	tmp_val = spcl_parse_line(vp, buf);
	REQUIRE(tmp_val.type == VAL_ERR);
	WARN(tmp_val.val.e->c == E_BAD_SYNTAX);
	INFO("message=", tmp_val.val.e->msg);
	WARN(strcmp(tmp_val.val.e->msg, "expected line end instead of \'\"\'") == 0);
	cleanup_spcl_val(&tmp_val, vp);
    }
    destroy_vproc(vp);
}

TEST_CASE("list interpretations") {
    char buf[SPCL_STR_BSIZE];
    vproc *vp = make_vproc();
    //test lists interpretations
    safecpy(buf, "[[i*2 for i in range(2)], [i*2-1 for i in range(1,3)], [x for x in range(1,3,0.5)]]", SPCL_STR_BSIZE);
    spcl_val tmp_val = spcl_parse_line(vp, buf);
    CHECK(tmp_val.type == VAL_LIST);
    CHECK(tmp_val.val.l != NULL);
    CHECK(tmp_val.n_els == 3);
    {
	//check the first sublist
	spcl_val element = tmp_val.val.l[0];
	REQUIRE(element.type == VAL_LIST);
	REQUIRE(element.n_els == 2);
	REQUIRE(element.val.l != NULL);
	CHECK(element.val.l[0].type == VAL_NUM);
	CHECK(element.val.l[0].val.x == 0);
	CHECK(element.val.l[1].type == VAL_NUM);
	CHECK(element.val.l[1].val.x == 2);
	//check the second sublist
	element = tmp_val.val.l[1];
	REQUIRE(element.type == VAL_LIST);
	REQUIRE(element.n_els == 2);
	REQUIRE(element.val.l != NULL);
	CHECK(element.val.l[0].type == VAL_NUM);
	CHECK(element.val.l[0].val.x == 1);
	CHECK(element.val.l[1].type == VAL_NUM);
	CHECK(element.val.l[1].val.x == 3);
	//check the third sublist
	element = tmp_val.val.l[2];
	REQUIRE(element.type == VAL_LIST);
	REQUIRE(element.n_els == 4);
	REQUIRE(element.val.l != NULL);
	CHECK(element.val.l[0].type == VAL_NUM);
	CHECK(element.val.l[0].val.x == 1);
	CHECK(element.val.l[1].type == VAL_NUM);
	CHECK(element.val.l[1].val.x == 1.5);
	CHECK(element.val.l[2].type == VAL_NUM);
	CHECK(element.val.l[2].val.x == 2);
	CHECK(element.val.l[3].type == VAL_NUM);
	CHECK(element.val.l[3].val.x == 2.5);
    }
    cleanup_spcl_val(&tmp_val, vp);
    //test nested list interpretations
    safecpy(buf, "[[x*y for x in range(1,6)] for y in range(5)]", SPCL_STR_BSIZE);
    tmp_val = spcl_parse_line(vp, buf);
    REQUIRE(tmp_val.type == VAL_LIST);
    REQUIRE(tmp_val.val.l != NULL);
    REQUIRE(tmp_val.n_els == 5);
    for (size_t yy = 0; yy < tmp_val.n_els; ++yy) {
	CHECK(tmp_val.val.l[yy].type == VAL_LIST);
	CHECK(tmp_val.val.l[yy].n_els == 5);
	for (size_t xx = 0; xx < tmp_val.val.l[yy].n_els; ++xx) {
	    CHECK(tmp_val.val.l[yy].val.l[xx].type == VAL_NUM);
	    CHECK(tmp_val.val.l[yy].val.l[xx].val.x == (xx+1)*yy);
	}
    }
    cleanup_spcl_val(&tmp_val, vp);
}

void write_test_file(const char** lines, size_t n_lines, const char* fname) {
    FILE* f = fopen(fname, "w");
    for (size_t i = 0; i < n_lines; ++i)
	fprintf(f, "%s\n", lines[i]);
    fclose(f);
}

//this function has a bunch of stuff that's only marked visible in debug builds, so we just ommit it for release
#if SPCL_DEBUG_LVL>0
/*TEST_CASE("spcl_fstream navigation") {
    const char* fun_contents[] = {"{", "if a > 5 {", "return 1", "}", "return 0", ""};
    const char* if_contents[] = {"{", "return 1", ""};

    SUBCASE("open brace on a different line") {
	const char* lines[] = { "fn test_fun(a)", "{", "if a > 5 {", "return 1", "}", "return 0", "}" };
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	//check the lines (curly brace on new line)
	spcl_fstream* fs = make_spcl_fstream(TEST_FNAME);
	psize st_off = 0;
	for (size_t i = 0; i < n_lines; ++i) {
	    s8 line;
	    //duplicate to silence const char* to char* errors
	    line.s = strdup(lines[i]);
	    line.n = strlen(lines[i]);
	    s8 strval = fs_read( fs, st_off, st_off+line.n );
	    CHECK(s8cmp(line, strval) == 0);
	    free(line.s);
	    st_off += line.n+1;
	}
	psize cur = 0;
	psize lend = fs_line_end(fs, cur);
	CHECK(lend == strlen(lines[0]));
	//find the block that says fn
	read_state rs = make_read_state(fs, cur, fs->flen);
	spcl_key tkey = get_keyword(&rs);
	CHECK(tkey == KEY_FN);
	CHECK(fs_find_line(fs, rs.start) == 0);
	CHECK(rs.start == 3);
	//find the parenthesis
	psize op_loc, open_ind, close_ind, new_end;
	spcl_val er = find_operator(rs, &op_loc, &open_ind, &close_ind, &new_end);
	CHECK(er.type != VAL_ERR);
	CHECK(open_ind == strlen("fn test_fun"));
	CHECK(close_ind == strlen("fn test_fun")+2);
	CHECK(op_loc >= new_end);
	//test the braces around the function
	cur = lend+1;
	lend = fs_line_end(fs, cur);
	er = find_operator(make_read_state(fs, cur, fs_end(fs)), &op_loc, &open_ind, &close_ind, &new_end);
	CHECK(er.type != VAL_ERR);
	CHECK(fs_find_line(fs, cur) == 1);
	CHECK(open_ind == cur);
	CHECK(close_ind == fs->flen - 2);
	//check the braces around the if statement
	cur = lend+1;
	er = find_operator(make_read_state(fs, cur, close_ind), &op_loc, &open_ind, &close_ind, &new_end);
	CHECK(er.type != VAL_ERR);
	CHECK(fs_find_line(fs, open_ind) == 2);
	CHECK(open_ind == strlen(lines[0])+strlen(lines[1])+2+strlen("if a > 5 "));
	CHECK(close_ind == 37);
	destroy_spcl_fstream(fs);
    }
    SUBCASE("open brace on the same line") {
	const char* lines[] = { " fn  test_fun(a) {", "if a > 5 {return 1}", "return 0", "}" };
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	//check the lines (curly brace on new line)
	spcl_fstream* fs = make_spcl_fstream(TEST_FNAME);
	psize st_off = 0;
	for (size_t i = 0; i < n_lines; ++i) {
	    s8 line;
	    line.s = strdup(lines[i]);
	    line.n = strlen(lines[i]);
	    s8 strval = fs_read(fs, st_off, st_off+line.n);
	    CHECK(s8cmp(line, strval) == 0);
	    free(line.s);
	    st_off += line.n+1;
	}
	psize cur = 0;
	psize lend = fs_line_end(fs, cur);
	CHECK(lend == strlen(lines[0]));
	//find the block that says fn
	read_state rs = make_read_state(fs, cur, lend);
	spcl_key tkey = get_keyword(&rs);
	CHECK(tkey == KEY_FN);
	CHECK(rs.start == 5);
	//find the parenthesis
	psize op_loc, open_ind, close_ind, new_end;
	spcl_val er = find_operator(rs, &op_loc, &open_ind, &close_ind, &new_end);
	CHECK(er.type != VAL_ERR);
	CHECK(open_ind == strlen(" fn  test_fun"));
	CHECK(close_ind == 15);
	CHECK(op_loc >= new_end);
	//check the braces around the if statement
	cur = lend+1;
	er = find_operator(make_read_state(fs, cur, fs_end(fs)), &op_loc, &open_ind, &close_ind, &new_end);
	CHECK(er.type != VAL_ERR);
	CHECK(open_ind == 28);
	CHECK(close_ind == 37);
	//move to the next character after the open brace and get the contents
	open_ind += 1;
	destroy_spcl_fstream(fs);
    }
}*/
#endif

spcl_val test_fun_call(spcl_fn_call f, vproc *vp) {
    spcl_val ret;
    if (f.n_args < 1)
	return spcl_make_err(E_LACK_TOKENS, vp, "expected 1 argument");
    if (f.args[0].type != VAL_NUM)
	return spcl_make_err(E_LACK_TOKENS, vp, "only works with numbers");
    double a = f.args[0].val.x;
    if (a > 5) {
	ret.type = VAL_INST;
	ret.val.c = make_spcl_inst(vp);
	spcl_set_val("name", cstr_to_spcl("hi"), 1, vp);
	return ret;
    }
    return f.args[0];
}

spcl_val test_fun_gamma(spcl_fn_call f, vproc *vp) {
    if (f.n_args < 1)
	return spcl_make_err(E_LACK_TOKENS, vp, "expected 1 argument");
    if (f.args[0].type != VAL_NUM)
	return spcl_make_err(E_LACK_TOKENS, vp, "only works with numbers");
    double a = f.args[0].val.x;
    return spcl_make_num(sqrt(1 - a*a));
}

TEST_CASE("spcl_inst lookups") {
    const char* letters = "etaoin";
    size_t n_letters = strlen(letters);
    const size_t GEN_LEN = 4;
    char name[GEN_LEN+1];
    memset(name, 0, GEN_LEN+1);
    vproc *vp = make_vproc();
    spcl_set_val("tao", spcl_make_str("tao", 4, vp), 0, vp);
    size_t n_combs = 1;
    for (size_t i = 0; i < GEN_LEN; ++i)
	n_combs *= n_letters;

    //add a whole bunch of words using the most common letters
    spcl_val v;
    size_t before_size = vp->d.n_memb;
    auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n_combs; ++i) {
	size_t j = i;
	size_t k = 0;
	do {
	    name[k++] = letters[j % n_letters];
	    j /= n_letters;
	} while (j && k < GEN_LEN);
	spcl_set_val(name, spcl_make_num(i), 1, vp);
	v = spcl_parse_line(vp, name);
	test_num(v, i);
    }
    auto end = std::chrono::steady_clock::now();
    double time = std::chrono::duration <double, std::milli> (end-start).count();
    printf("took %f ms to set %lu elements\n", time, n_combs);
    //lookup something not in the spcl_inst, check that we only added n_combs-1 elements because we added one match explicitly before
    CHECK(vp->d.n_memb == n_combs+before_size-1);
    v = spcl_parse_line(vp, "vetaon");
    CHECK(v.type == VAL_UNDEF);
    CHECK(v.val.x == 0);
    CHECK(v.n_els == 0);
    destroy_vproc(vp);
}

TEST_CASE("spcl_inst parsing") {
    SUBCASE ("without nesting") {
	const char* lines[] = { "a1 = 1", "\"b\"", " c = [\"d\", \"e\"]" };
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	vproc *vp = spcl_read_file(TEST_FNAME, 0, NULL);
	REQUIRE(vp != NULL);
	//lookup the named spcl_vals
	spcl_val val_a = spcl_parse_line(vp, "a1");
	CHECK(val_a.type == VAL_NUM);
	CHECK(val_a.val.x == 1);
	spcl_val val_c = spcl_parse_line(vp, "c");
	CHECK(val_c.type == VAL_LIST);
	CHECK(val_c.n_els == 2);
	CHECK(val_c.val.l[0].type == VAL_STR);
	CHECK(val_c.val.l[1].type == VAL_STR);
	destroy_vproc(vp);
    }
    SUBCASE ("with nesting") {
	const char* lines[] = { "a = {name = \"apple\";", "spcl_vals = [20, 11]}", "b = a.spcl_vals[0]", "c = a.spcl_vals[1] + a.spcl_vals[0]+1" }; 
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	vproc *vp = spcl_read_file(TEST_FNAME, 0, NULL);
	REQUIRE(vp != NULL);
	//lookup the named spcl_vals
	spcl_val val_a = spcl_parse_line(vp, "a");
	CHECK(val_a.type == VAL_INST);
	spcl_val val_a_name = spcl_parse_line(vp, "a.name");
	CHECK(val_a_name.type == VAL_STR);
	CHECK(spcl_strcmp(val_a_name, cstr_to_spcl("apple")) == 0);
	spcl_val val_a_spcl_val = spcl_parse_line(vp, "a.spcl_vals");
	REQUIRE(val_a_spcl_val.type == VAL_LIST);
	REQUIRE(val_a_spcl_val.n_els == 2);
	CHECK(val_a_spcl_val.val.l[0].type == VAL_NUM);
	CHECK(val_a_spcl_val.val.l[1].type == VAL_NUM);
	CHECK(val_a_spcl_val.val.l[0].val.x == 20);
	CHECK(val_a_spcl_val.val.l[1].val.x == 11);
	spcl_val val_b = spcl_parse_line(vp, "b");
	CHECK(val_b.type == VAL_NUM);
	CHECK(val_b.val.x == 20);
	spcl_val val_c = spcl_parse_line(vp, "c");
	CHECK(val_c.type == VAL_NUM);
	CHECK(val_c.val.x == 32);
    }
    SUBCASE ("external user defined functions") {
	const char* fun_name = "test_fun";
	char* tmp_name = strdup(fun_name);

	const char* lines[] = { "a = test_fun(1);b=test_fun(10)" };
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	spcl_fstream* b_1 = make_spcl_fstream(TEST_FNAME);
	vproc *vp = make_vproc();
	spcl_add_fn(test_fun_call, "test_fun", vp);
	spcl_val er = spcl_read_lines(vp, b_1);
	REQUIRE(er.type != VAL_ERR);
	//make sure that the function is there
	spcl_val val_fun = spcl_parse_line(vp, "test_fun");
	CHECK(val_fun.type == VAL_FN);
	//make sure that the number spcl_val a is there
	spcl_val val_a = spcl_parse_line(vp, "a");
	CHECK(val_a.type == VAL_NUM);
	CHECK(val_a.val.x == 1);
	spcl_val val_b = spcl_parse_line(vp, "b");
	CHECK(val_b.type == VAL_INST);
	spcl_val val_b_name = spcl_parse_line(vp, "b.name");
	CHECK(val_b_name.type == VAL_STR);
	CHECK(spcl_strcmp(val_b_name, cstr_to_spcl("hi")) == 0);
	free(tmp_name);
	destroy_vproc(vp);
	destroy_spcl_fstream(b_1);
    }
    SUBCASE ("internal user defined functions") {
	const char* fun_name = "test_fun";
	char* tmp_name = strdup(fun_name);

	const char* lines[] = {
	    "fn test_fun = (i) {",
	    "return (i < 2)? \"a\" : \"b\"",
	    "}",
	    "fn inst_fn = (n) {",
	    "return {__type__ = \"test_inst\";num = n}",
	    "}",
	    "a = test_fun(1);",
	    "b=test_fun(10);c=inst_fn(2)" };
	size_t n_lines = sizeof(lines)/sizeof(char*);
	write_test_file(lines, n_lines, TEST_FNAME);
	//read the file and check for errors
	vproc *vp = spcl_read_file(TEST_FNAME, 0, NULL);
	REQUIRE(vp != NULL);
	//make sure that the function is there
	spcl_val val_fun = spcl_parse_line(vp, "test_fun");
	CHECK(val_fun.type == VAL_FN);
	//make sure that the number spcl_val a is there
	spcl_val val_a = spcl_parse_line(vp, "a");
	REQUIRE(val_a.type == VAL_STR);
	CHECK(spcl_strcmp(val_a, cstr_to_spcl("a")) == 0);
	spcl_val val_b = spcl_parse_line(vp, "b");
	REQUIRE(val_b.type == VAL_STR);
	CHECK(spcl_strcmp(val_b, cstr_to_spcl("b")) == 0);
	//look at the returned instance
	spcl_val val_c = spcl_parse_line(vp, "c");
	REQUIRE(val_c.type == VAL_INST);
	spcl_inst* sub_c = val_c.val.c;
	val_c = spcl_parse_line(vp, "c.num");
	test_num(val_c, 2);
	val_c = spcl_parse_line(vp, "c.__type__");
	REQUIRE(val_c.type == VAL_STR);
	CHECK(spcl_strcmp(val_c, cstr_to_spcl("test_inst")) == 0);
    }
    SUBCASE ("stress test") {
	//first we add a bunch of arbitrary variables to make searching harder for the parser
	const char* lines1[] = {
	    "Vodkis=1","Pagne=2","Meadaj=3","whis=4","nac4=5","RaKi=6","gyn=7","cid=8","Daiqui=9","Mooshi=10","Magnac=2","manChe=3","tes=4","Bourbu=5","magna=6","sak=7","Para=8","Keffi=9","Guino=10","Uuqax=11","Thraxeods=12","Trinzoins=13","gheds=14","theSoild=15","vengirs=16",
	    "y = 2.0",
	    "xs = linspace(0, y, 10000)",
	    "arr1 = [math.sin(6*x/y) for x in xs]",
	    "one = 1",
	    "two = 2",
	    "three=one+two" };
	size_t n_lines1 = sizeof(lines1)/sizeof(char*);
	write_test_file(lines1, n_lines1, TEST_FNAME);
	spcl_fstream* b_1 = make_spcl_fstream(TEST_FNAME);
	const char* lines2[] = { "arr2 = [gam(x/y) for x in xs]" };
	size_t n_lines2 = sizeof(lines2)/sizeof(char*);
	write_test_file(lines2, n_lines2, TEST_FNAME);
	spcl_fstream* b_2 = make_spcl_fstream(TEST_FNAME);
	vproc *vp = make_vproc();
	spcl_val er = spcl_read_lines(vp, b_1);
	CHECK(er.type != VAL_ERR);
	spcl_val tmp_f = spcl_make_fn("gam", 1, &test_fun_gamma, vp);
	spcl_set_val("gam", tmp_f, 1, vp);
	cleanup_spcl_val(&tmp_f, vp);
	er = spcl_read_lines(vp, b_2);
	CHECK(er.type != VAL_ERR);
	CHECK(spcl_test(vp, "three == 3"));
	destroy_vproc(vp);
	destroy_spcl_fstream(b_1);
	destroy_spcl_fstream(b_2);
    }
}

static const valtype SRC_SIG[] = {VAL_STR, VAL_NUM, VAL_NUM, VAL_NUM, VAL_NUM, VAL_NUM, VAL_NUM, VAL_INST};
spcl_val spcl_gen_gaussian_source(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck_opts(&f, 6, SRC_SIG, vp);
    spcl_val ret = spcl_make_inst("Gaussian_source", vp);
    spcl_set_sub_val(ret.val.c, "component", f.args[0], 1, vp);
    spcl_set_sub_val(ret.val.c, "wavelength", f.args[1], 0, vp);
    spcl_set_sub_val(ret.val.c, "amplitude", f.args[2], 0, vp);
    spcl_set_sub_val(ret.val.c, "width", f.args[3], 0, vp);
    spcl_set_sub_val(ret.val.c, "phase", f.args[4], 0, vp);
    //read additional parameters
    spcl_set_sub_val(ret.val.c, "cutoff", (f.n_args>6)? f.args[5]: spcl_make_num(5), 0, vp);
    spcl_set_sub_val(ret.val.c, "start_time", (f.n_args>7)? f.args[6]: spcl_make_num(5), 0, vp);
    spcl_set_sub_val(ret.val.c, "region", f.args[f.n_args-1], 1, vp);
    return ret;
}
static const valtype BOX_SIG[] = {VAL_ARRAY, VAL_ARRAY};
spcl_val spcl_gen_box(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(&f, BOX_SIG, vp);
    spcl_val ret = spcl_make_inst("Box", vp);
    spcl_set_sub_val(ret.val.c, "pt_1", f.args[0], 1, vp);
    spcl_set_sub_val(ret.val.c, "pt_2", f.args[1], 1, vp);
    return ret;
}
static const valtype QUAD_SIG[] = {VAL_NUM};
spcl_val spcl_quad_trap(spcl_fn_call f, vproc *vp) {
    spcl_sigcheck(&f, QUAD_SIG, vp);
    spcl_val ret = spcl_make_inst("quad_pot", vp);
    spcl_set_sub_val(ret.val.c, "k", f.args[0], 0, vp);
    return ret;
}
void setup_geometry_inst(vproc *vp) {
    //we have to set up the spcl_inst with all of our functions
    spcl_add_fn(spcl_gen_gaussian_source, "Gaussian_source", vp);
    spcl_add_fn(spcl_gen_box, "Box", vp);
    spcl_add_fn(spcl_quad_trap, "quad_pot", vp);
}

TEST_CASE("file parsing") {
    const size_t N_FLTS = 10;
    char buf[SPCL_STR_BSIZE];
    spcl_fstream* fs = make_spcl_fstream(TEST_GEOM_NAME);
    vproc *vp = make_vproc();
    setup_geometry_inst(vp);
    spcl_val er = spcl_read_lines(vp, fs);
    CHECK(er.type != VAL_ERR);
    spcl_val v = spcl_parse_line(vp, "offset");
    CHECK(v.type == VAL_NUM);CHECK(v.val.x == 0.2);
    v = spcl_parse_line(vp, "lst");
    CHECK(v.type == VAL_LIST);
    v = spcl_parse_line(vp, "sum_lst");
    CHECK(v.type == VAL_NUM);CHECK(v.val.x == 11);
    v = spcl_parse_line(vp, "prod_lst");
    CHECK(v.type == VAL_NUM);CHECK(v.val.x == 24.2);
    v = spcl_parse_line(vp, "acid_test");
    CHECK(v.type == VAL_NUM);CHECK(v.val.x == 16);
    CHECK(spcl_test(vp, "acid_res"));
    //lookup only does a shallow copy so we don't need to free
    CHECK(spcl_test(vp, "gs.__type__ == \"Gaussian_source\""));
    CHECK(spcl_test(vp, "gs.component == \"Ey\""));
    CHECK(spcl_test(vp, "gs.wavelength == 1.5"));
    CHECK(spcl_test(vp, "gs.amplitude == 7"));
    CHECK(spcl_test(vp, "gs.width == 3"));
    CHECK(spcl_test(vp, "gs.phase == 0.75"));
    CHECK(spcl_test(vp, "gs.cutoff == 6"));
    CHECK(spcl_test(vp, "gs.start_time == 5.2"));
    CHECK(spcl_test(vp, "gs.region.__type__ == \"Box\""));
    CHECK(spcl_test(vp, "gs.region.pt_1 == vec(0,0,.2)"));
    CHECK(spcl_test(vp, "gs.region.pt_2 == vec(.4, 0.4, .2)"));
    //now try using the builtin find functions
    int n;
    unsigned len;
    double flts[N_FLTS];
    spcl_inst* sub;
    REQUIRE(spcl_find_object(vp, "gs", "Gaussian_source", &sub) == 0);
    //string lookups
    CHECK(spcl_find_c_str(vp, "gs.component", buf, SPCL_STR_BSIZE) == 2);
    CHECK(strcmp(buf, "Ey") == 0);
    CHECK(spcl_find_float(vp, "gs.wavelength", flts) == 0);
    CHECK(flts[0] == 1.5);
    CHECK(spcl_find_int(vp, "gs.wavelength", &n) == 0);
    CHECK(n == 1);
    CHECK(spcl_find_uint(vp, "gs.amplitude", &len) == 0);
    CHECK(len == 7);
    //array lookups
    REQUIRE(spcl_find_object(vp, "region", "Box", &sub) == 0);
    REQUIRE(spcl_find_c_darray(vp, "region.pt_1", flts, N_FLTS) == 3);
    CHECK(flts[0] == 0);CHECK(flts[1] == 0);CHECK(flts[2] == 0.2);
    REQUIRE(spcl_find_c_darray(vp, "region.pt_2", flts, N_FLTS) == 3);
    CHECK(flts[0] == 0.4);CHECK(flts[1] == 0.4);CHECK(flts[2] == 0.2);
    REQUIRE(spcl_find_object(vp, "potential", "quad_pot", &sub) == 0);
    CHECK(spcl_find_float(vp, "potential.k", flts) == 0);
    CHECK(flts[0] == 4);
    //cleanup
    destroy_spcl_fstream(fs);
    destroy_vproc(vp);
}
TEST_CASE("file importing") {
    const char* targv[] = {"1", "--b1=0", "-r"};
    size_t targc = sizeof(targv)/sizeof(char*);
    const char* lines1[] = {
	"fn double = (n) {",
	    "mul = 2;",
	    "return mul*n;",
	"}" };
    size_t n_lines1 = sizeof(lines1)/sizeof(char*);
    write_test_file(lines1, n_lines1, "/tmp/lines1.spcl");
    const char* lines2[] = {
	"import /tmp/lines1.spcl",
	"a1 = double(1)",
	"a2 = double(2)",
	"a3 = double(3)",
	"a4 = double(sys.argv[0])",
	"print(\"in file importing\", a1, a2, a3, a4)" };
    size_t n_lines2 = sizeof(lines2)/sizeof(char*);
    write_test_file(lines2, n_lines2, "/tmp/lines2.spcl");
    spcl_val er;
    vproc *vp = spcl_read_file("/tmp/lines2.spcl", targc, targv);
    REQUIRE(vp != NULL);
    int tmp;
    CHECK(spcl_find_int(vp, "a1", &tmp) >= 0);
    CHECK(tmp == 2);
    CHECK(spcl_find_int(vp, "a2", &tmp) >= 0);
    CHECK(tmp == 4);
    CHECK(spcl_find_int(vp, "a3", &tmp) >= 0);
    CHECK(tmp == 6);
    CHECK(spcl_find_int(vp, "a4", &tmp) >= 0);
    CHECK(tmp == 2);
    CHECK(spcl_find_int(vp, "b1", &tmp) >= 0);
    CHECK(tmp == 0);
    /*TODO: fix these cases
    CHECK(spcl_test(v.val.c, "len(sys.argv) == 2"));
    CHECK(spcl_test(v.val.c, "sys.argv[1] == \"r\""));*/
    destroy_vproc(vp);
}

/*TODO: fix these
TEST_CASE("assertions") {
    spcl_val v = spcl_inst_from_file(TEST_ASSERT_NAME, 0, NULL);
    CHECK(v.type != VAL_ERR);
#ifdef NEXP_TESTS
    CHECK(spcl_test(v.val.c, "k1 == 0.000012"));
#endif
    CHECK(spcl_test(v.val.c, "box_len == 1"));
    CHECK(spcl_test(v.val.c, "hs_rad == 0.0157"));
    cleanup_spcl_val(&v, vp);
    const char* targv_free[] = {"-f", "--box_len=2", "--hs_rad=2"};
    size_t targc_free = sizeof(targv_free)/sizeof(char*);
    v = spcl_inst_from_file(TEST_ASSERT_NAME, targc_free, targv_free);
    CHECK(v.type != VAL_ERR);
    CHECK(spcl_test(v.val.c, "len(sys.argv) == 1"));
    CHECK(spcl_test(v.val.c, "sys.argv[0] == \"f\""));
#ifdef NEXP_TESTS
    CHECK(spcl_test(v.val.c, "k1 == 0"));
#endif
    CHECK(spcl_test(v.val.c, "box_len == 2"));
    CHECK(spcl_test(v.val.c, "hs_rad == 2"));
    cleanup_spcl_val(&v, vp);
}

TEST_CASE("benchmarks") {
    const size_t N_RUNS = 100;
    double times[N_RUNS];
    double mean, var;
    //run assertions
    for (size_t i = 0; i < N_RUNS; ++i) {
	auto start = std::chrono::steady_clock::now();
	spcl_val v = spcl_inst_from_file(TEST_BENCH_NAME, 0, NULL);
	auto end = std::chrono::steady_clock::now();
	times[i] = std::chrono::duration <double, std::milli> (end-start).count();
	if (i == 0)
	    CHECK(v.type == VAL_INST);
	cleanup_spcl_val(&v, vp);
    }
    mean_var(times, N_RUNS, &mean, &var);
    printf("benchmarks.spcl evaluated in %f\xc2\xb1%f ms\n", mean, sqrt(var));
    //run benchmarks
    for (size_t i = 0; i < N_RUNS; ++i) {
	auto start = std::chrono::steady_clock::now();
	spcl_val v = spcl_inst_from_file(POT_TEST_NAME, 0, NULL);
	auto end = std::chrono::steady_clock::now();
	times[i] = std::chrono::duration <double, std::milli> (end-start).count();
	cleanup_spcl_val(&v, vp);
    }
    mean_var(times, N_RUNS, &mean, &var);
    printf("pot_test.spcl evaluated in %f\xc2\xb1%f ms\n", mean, sqrt(var));
}*/
