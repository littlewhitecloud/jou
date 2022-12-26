#include <assert.h>
#include "jou_compiler.h"
#include "util.h"


struct LocalVariable {
    char name[100];
    struct Type type;
};

struct State {
    List(struct AstFunctionSignature) functions;

    // func_ = information about the function containing the code that is checked
    const struct AstFunctionSignature *func_signature;
    List(struct LocalVariable) func_locals;
};

static const struct AstFunctionSignature *find_function(const struct State *st, const char *name)
{
    for (struct AstFunctionSignature *func = st->functions.ptr; func < End(st->functions); func++)
        if (!strcmp(func->funcname, name))
            return func;
    return NULL;
}

static const struct LocalVariable *find_local_variable(const struct State *st, const char *name)
{
    for (struct LocalVariable *v = st->func_locals.ptr; v < End(st->func_locals); v++)
        if (!strcmp(v->name, name))
            return v;
    return NULL;
}


static void fill_types_expression(const struct State *st, struct AstExpression *expr);

// Does not include the return type
static char *signature_to_string(const struct AstFunctionSignature *sig)
{
    List(char) result = {0};
    AppendStr(&result, sig->funcname);
    Append(&result, '(');

    for (int i = 0; i < sig->nargs; i++) {
        if(i)
            AppendStr(&result, ", ");
        AppendStr(&result, sig->argnames[i]);
        AppendStr(&result, ": ");
        AppendStr(&result, sig->argtypes[i].name);
    }

    Append(&result, ')');
    Append(&result, '\0');
    return result.ptr;
}

// Returns the return type of the function, NULL if the function does not return a value.
static const struct Type *fill_types_call(const struct State *st, struct AstCall *call, struct Location location)
{
    const struct AstFunctionSignature *sig = find_function(st, call->funcname);
    if (!sig)
        fail_with_error(location, "function \"%s\" not found", call->funcname);

    if (sig->nargs != call->nargs) {
        fail_with_error(
            location,
            "function %s takes %d argument%s, but it was called with %d argument%s",
            signature_to_string(sig),
            sig->nargs,
            sig->nargs==1?"":"s",
            call->nargs,
            call->nargs==1?"":"s"
        );
    }

    bool match = true;
    for (int i = 0; i < call->nargs; i++) {
        fill_types_expression(st, &call->args[i]);
        if (!can_implicitly_convert(&call->args[i].type, &sig->argtypes[i]))
            match = false;
    }

    if (!match) {
        // This is a common error, so worth spending some effort to get a good error message.
        List(char) passed_str = {0};
        for (int i = 0; i < sig->nargs; i++) {
            if(i) AppendStr(&passed_str, ", ");
            AppendStr(&passed_str, call->args[i].type.name);
        }
        Append(&passed_str, '\0');

        fail_with_error(
            location,
            "function %s was called with wrong argument types: %s",
            signature_to_string(sig), passed_str.ptr);
    }

    return sig->returntype;
}

static void fill_types_expression(const struct State *st, struct AstExpression *expr)
{
    switch(expr->kind) {
        case AST_EXPR_GET_VARIABLE:
        {
            const struct LocalVariable *v = find_local_variable(st, expr->data.varname);
            if (!v)
                fail_with_error(expr->location, "no local variable named '%s'", expr->data.varname);
            expr->type = v->type;
            break;
        }

        case AST_EXPR_ADDRESS_OF_VARIABLE:
        {
            const struct LocalVariable *v = find_local_variable(st, expr->data.varname);
            if (!v)
                fail_with_error(expr->location, "no local variable named '%s'", expr->data.varname);
            // TODO: free the allocation in create_pointer_type()
            expr->type = create_pointer_type(&v->type, expr->location);
            break;
        }

        case AST_EXPR_CALL:
        {
            const struct Type *t = fill_types_call(st, &expr->data.call, expr->location);
            if (!t)
                fail_with_error(expr->location, "function '%s' does not return a value", expr->data.call.funcname);
            expr->type = *t;
            break;
        }

        case AST_EXPR_DEREFERENCE:
        {
            fill_types_expression(st, expr->data.pointerexpr);
            const struct Type ptrtype = expr->data.pointerexpr->type;
            if (ptrtype.kind != TYPE_POINTER)
                fail_with_error(expr->location, "the dereference operator '*' is only for pointers, not for '%s'", ptrtype.name);
            expr->type = *ptrtype.data.valuetype;
            break;
        }

        case AST_EXPR_TRUE:
        case AST_EXPR_FALSE:
            expr->type = bool_type;
            break;
        case AST_EXPR_INT_CONSTANT:
            expr->type = int_type;
            break;
        case AST_EXPR_CHAR_CONSTANT:
            expr->type = byte_type;
            break;
        case AST_EXPR_STRING_CONSTANT:
            expr->type = string_type;
            break;
    }
}

static void fill_types_body(const struct State *st, const struct AstBody *body);

static void fill_types_statement(const struct State *st, struct AstStatement *stmt)
{
    switch(stmt->kind) {
    case AST_STMT_CALL:
        fill_types_call(st, &stmt->data.call, stmt->location);
        break;

    case AST_STMT_IF:
        fill_types_expression(st, &stmt->data.ifstatement.condition);
        if (stmt->data.ifstatement.condition.type.kind != TYPE_BOOL) {
            fail_with_error(
                stmt->data.ifstatement.condition.location,
                "'if' condition must be a boolean, not %s", stmt->data.ifstatement.condition.type.name);
        }
        fill_types_body(st, &stmt->data.ifstatement.body);
        break;

    case AST_STMT_RETURN_VALUE:
        if (st->func_signature->returntype == NULL) {
            fail_with_error(
                stmt->location,
                "function '%s' cannot return a value because it was defined with '-> void'",
                st->func_signature->funcname);
        }
        fill_types_expression(st, &stmt->data.returnvalue);
        if (!can_implicitly_convert(&stmt->data.returnvalue.type, st->func_signature->returntype)) {
            fail_with_error(
                stmt->location,
                "attempting to return a value of type '%s' from function '%s' defined with '-> %s'",
                stmt->data.returnvalue.type.name,
                st->func_signature->funcname,
                st->func_signature->returntype->name);
        }
        break;

    case AST_STMT_RETURN_WITHOUT_VALUE:
        if (st->func_signature->returntype != NULL) {
            fail_with_error(
                stmt->location,
                "a return value is needed, because the return type of function '%s' is '%s'",
                st->func_signature->funcname,
                st->func_signature->returntype->name);
        }
        break;
    }
}

static void fill_types_body(const struct State *st, const struct AstBody *body)
{
    for (int i = 0; i < body->nstatements; i++)
        fill_types_statement(st, &body->statements[i]);
}

static void handle_signature(struct State *st, const struct AstFunctionSignature *sig)
{
    if (find_function(st, sig->funcname))
        fail_with_error(sig->location, "a function named \"%s\" already exists", sig->funcname);

    struct Type inttype = {.name="int",.kind=TYPE_SIGNED_INTEGER,.data.width_in_bits=32};
    if (!strcmp(sig->funcname, "main") &&
        (sig->returntype == NULL || !same_type(sig->returntype, &inttype)))
    {
        fail_with_error(sig->location, "the main() function must return int");
    }

    Append(&st->functions, *sig);
}

void fill_types(const struct AstToplevelNode *ast)
{
    struct State st = {0};

    for (; ast->kind != AST_TOPLEVEL_END_OF_FILE; ast++) {
        switch(ast->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            handle_signature(&st, &ast->data.decl_signature);
            break;

        case AST_TOPLEVEL_DEFINE_FUNCTION:
            handle_signature(&st, &ast->data.funcdef.signature);

            // TODO: Error for duplicate names of local variables.
            for (int i = 0; i < ast->data.funcdef.signature.nargs; i++) {
                struct LocalVariable local = {.type=ast->data.funcdef.signature.argtypes[i]};
                safe_strcpy(local.name, ast->data.funcdef.signature.argnames[i]);
                Append(&st.func_locals, local);
            }

            st.func_signature = &ast->data.funcdef.signature;
            fill_types_body(&st, &ast->data.funcdef.body);
            st.func_signature = NULL;
            st.func_locals.len = 0;
            break;

        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
}