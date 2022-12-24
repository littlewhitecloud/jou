/*
This file contains boring boilerplate code to free up data structures
used in compilation.
*/

#include "jou_compiler.h"
#include <assert.h>
#include <stdlib.h>

void free_tokens(struct Token *tokenlist)
{
    // Currently individual tokens don't need freeing.
    // TODO: This will change once we have strings, because a string token
    //       will contain an arbitrary amount of data.
    free(tokenlist);
}

static void free_body(const struct AstBody *body)
{
    // Currently individual statements don't need freeing.
    free(body->statements);
}

void free_ast(struct AstToplevelNode *topnodelist)
{
    for (struct AstToplevelNode *t = topnodelist; t->kind != AST_TOPLEVEL_END_OF_FILE; t++) {
        switch(t->kind) {
        case AST_TOPLEVEL_CDECL_FUNCTION:
            break;
        case AST_TOPLEVEL_DEFINE_FUNCTION:
            free_body(&t->data.funcdef.body);
            break;
        case AST_TOPLEVEL_END_OF_FILE:
            assert(0);
        }
    }
    free(topnodelist);
}