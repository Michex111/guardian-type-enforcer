/*
 * guardian_common.h
 * ------------------
 * Shared declarations for the _guardian_core extension.
 *
 * This header is included by every translation unit that makes up the
 * single compiled module `guardian._guardian_core`. It defines:
 *   - Portability macros (likely/unlikely, PyFrame_GetLocals fallback)
 *   - The opcode table shared between the Python compiler and the C validator
 *   - Extern declarations for shared globals (exceptions) and type objects
 *   - Prototypes for shared functions / module entry points
 *
 * NOTE: All types/functions that need to be visible across translation units
 * are deliberately NON-static here. Internal helpers (dealloc, getattro, the
 * per-type setattro hooks) remain `static` inside their owning .c file.
 */
#ifndef GUARDIAN_COMMON_H
#define GUARDIAN_COMMON_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#include <stddef.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif

/* PyFrame_GetLocals does not exist before Python 3.11; provide a fallback. */
#if PY_VERSION_HEX < 0x030B0000
static inline PyObject* PyFrame_GetLocals(PyFrameObject *frame) {
    PyFrame_FastToLocals(frame);
    PyObject *locals = frame->f_locals;
    Py_XINCREF(locals);
    return locals;
}
#endif

/*
 * Opcode table. Values are ABI-stable and mirrored by guardian/_compiler.py.
 * Keep the two sides in sync whenever a new opcode is introduced.
 */
enum {
    OP_ANY = 0,
    OP_INSTANCE = 1,
    OP_EXACT = 2,
    OP_UNION = 3,
    OP_LIST = 4,
    OP_DICT = 5,
    OP_TUPLE_VAR = 6,
    OP_TUPLE_FIXED = 7,
    OP_SET = 8,
    OP_LITERAL = 9
};

/* ---- Shared globals (owned references, defined in _guardian_core.c) ---- */
extern PyObject *GuardianTypeError;
extern PyObject *GuardianAccessError;
extern PyObject *GuardianInitializationError;

/* ---- Shared type objects (defined in their own .c files) ---- */
extern PyTypeObject GuardType;          /* guards.c       */
extern PyTypeObject StrictGuardType;    /* guards.c       */
extern PyTypeObject ShieldBaseType;     /* shield_base.c  */
extern PyTypeObject ShieldMetaType;     /* shield_meta.c  */
extern PyTypeObject CFieldDescriptorType; /* c_descriptor.c */

/* ---- Shared validation helpers (type_checker.c) ---- */
int fast_check_type(PyObject *obj, PyObject *rule);
void raise_type_error(PyObject *param_name, PyObject *expected_name, PyObject *val);

/* ---- Frame-walking access control (access_control.c) ---- */
int check_internal_access(PyObject *self);

/* ---- Module method-table entry points ---- */
PyObject *make_guard(PyObject *module, PyObject *args);       /* guards.c        */
PyObject *make_strictguard(PyObject *module, PyObject *args); /* guards.c        */
PyObject *make_c_descriptor(PyObject *module, PyObject *args);/* c_descriptor.c  */

#endif /* GUARDIAN_COMMON_H */
