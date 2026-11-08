/*
 * _guardian_core.c
 * ----------------
 * Module entry point for `guardian._guardian_core`. This file owns:
 *   - the exception type globals,
 *   - the module method table,
 *   - the PyInit__guardian_core registration logic.
 *
 * All other concerns live in dedicated translation units:
 *   type_checker.c    -> fast_check_type / raise_type_error
 *   access_control.c  -> check_internal_access (frame walking)
 *   guards.c          -> GuardObject / StrictGuardObject
 *   shield_base.c     -> ShieldBaseType
 *   shield_meta.c     -> ShieldMetaType
 *   c_descriptor.c    -> CFieldDescriptorType
 */
#include "guardian_common.h"

/* Owned references to the exception types, shared across all units. */
PyObject *GuardianTypeError;
PyObject *GuardianAccessError;
PyObject *GuardianInitializationError;

static PyMethodDef GuardianMethods[] = {
    {"make_guard", make_guard, METH_VARARGS, "Create a C-level guard wrapper"},
    {"make_strictguard", make_strictguard, METH_VARARGS, "Create a C-level strictguard wrapper"},
    {"make_c_descriptor", make_c_descriptor, METH_VARARGS, "Create a C-level dataclass descriptor"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef guardianmodule = {
    PyModuleDef_HEAD_INIT, "_guardian_core", "C core for guardian type enforcement", -1, GuardianMethods
};

PyMODINIT_FUNC PyInit__guardian_core(void) {
    PyObject *m = PyModule_Create(&guardianmodule);
    if (m == NULL) return NULL;

#ifdef Py_GIL_DISABLED
    PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED);
#endif

    /* Exceptions. PyModule_AddObject steals a reference; the extra INCREF
     * keeps a stable owned reference for the C globals (prevents a dangling
     * pointer on module reload). */
    GuardianTypeError = PyErr_NewException("guardian.GuardianTypeError", PyExc_TypeError, NULL);
    if (GuardianTypeError == NULL) goto error;
    if (PyModule_AddObject(m, "GuardianTypeError", GuardianTypeError) < 0) goto error;
    Py_INCREF(GuardianTypeError);

    GuardianAccessError = PyErr_NewException("guardian.GuardianAccessError", PyExc_AttributeError, NULL);
    if (GuardianAccessError == NULL) goto error;
    if (PyModule_AddObject(m, "GuardianAccessError", GuardianAccessError) < 0) goto error;
    Py_INCREF(GuardianAccessError);

    GuardianInitializationError = PyErr_NewException(
        "guardian.GuardianInitializationError", PyExc_UnboundLocalError, NULL);
    if (GuardianInitializationError == NULL) goto error;
    if (PyModule_AddObject(m, "GuardianInitializationError", GuardianInitializationError) < 0) goto error;
    Py_INCREF(GuardianInitializationError);

    /* Register the (vectorcall) wrapper types. */
    if (PyType_Ready(&GuardType) < 0) goto error;
    if (PyType_Ready(&StrictGuardType) < 0) goto error;

    /* Register the Shield base type. */
    if (PyType_Ready(&ShieldBaseType) < 0) goto error;
    Py_INCREF(&ShieldBaseType);
    if (PyModule_AddObject(m, "ShieldBase", (PyObject *)&ShieldBaseType) < 0) goto error;

    /* The dataclass descriptor is only reachable via make_c_descriptor; it
     * still needs PyType_Ready so instances can be allocated. */
    if (PyType_Ready(&CFieldDescriptorType) < 0) goto error;

    /* Register the Shield metaclass (inherits from `type`). */
    ShieldMetaType.tp_base = &PyType_Type;
    if (PyType_Ready(&ShieldMetaType) < 0) goto error;
    Py_INCREF(&ShieldMetaType);
    if (PyModule_AddObject(m, "ShieldMeta", (PyObject *)&ShieldMetaType) < 0) goto error;

    return m;

error:
    Py_DECREF(m);
    return NULL;
}
