#ifndef breeze_compiler_h
#define breeze_compiler_h

#include "object.h"
#include "common.h"

ObjFunction * compile(const char* source );
void mark_compiler_roots();


#endif // !breeze_compiler_h
