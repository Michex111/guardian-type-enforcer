/*
 * shield_meta.c
 * -------------
 * The `ShieldMeta` metaclass (subclass of `type`). Its `tp_setattro` hook
 * guards the assignment of protected/private CLASS attributes, so that
 * external code cannot modify `MyClass._internal` etc. The class object
 * itself is the `self` passed to check_internal_access.
 */
#include "guardian_common.h"

static int shield_meta_setattro(PyObject *cls, PyObject *name, PyObject *value) {
    if (unlikely(!PyUnicode_Check(name))) return PyType_Type.tp_setattro(cls, name, value);

    const char *name_str = PyUnicode_AsUTF8(name);

    /* Guard _protected/_private class members (skip standard dunders like
     * __module__ or __shield_rules__). */
    if (unlikely(name_str[0] == '_')) {
        size_t len = strlen(name_str);
        if (!(len >= 4 && name_str[1] == '_' && name_str[len - 1] == '_' && name_str[len - 2] == '_')) {
            if (!check_internal_access(cls)) {
                PyErr_Format(GuardianAccessError,
                             "External access denied: Cannot modify protected/private class attribute '%s'.",
                             name_str);
                return -1;
            }
        }
    }

    /* Delegate the actual assignment to CPython's core type implementation. */
    return PyType_Type.tp_setattro(cls, name, value);
}

PyTypeObject ShieldMetaType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.ShieldMeta",
    /* Instances of this metaclass ARE heap type objects, so the allocation
     * size must be sizeof(PyHeapTypeObject), NOT sizeof(PyTypeObject), or
     * class objects are under-allocated and the heap-type fields overflow. */
    .tp_basicsize = sizeof(PyHeapTypeObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_setattro = shield_meta_setattro,  /* intercepts MyClass._var = 5 */
};
