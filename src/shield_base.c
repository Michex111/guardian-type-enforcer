/*
 * shield_base.c
 * -------------
 * The `ShieldBase` type and its `tp_setattro` hook, which enforces:
 *   1. Write protection for protected/private attributes from outside the
 *      owning class (using check_internal_access).
 *   2. Runtime type validation of declared attributes against __shield_rules__.
 */
#include "guardian_common.h"

static int shield_setattro(PyObject *self, PyObject *name, PyObject *value) {
    if (unlikely(!PyUnicode_Check(name))) return PyObject_GenericSetAttr(self, name, value);

    const char *name_str = PyUnicode_AsUTF8(name);

    /* Write-protect _private and _protected members (skip standard dunders). */
    if (unlikely(name_str[0] == '_')) {
        size_t len = strlen(name_str);
        if (!(len >= 4 && name_str[1] == '_' && name_str[len - 1] == '_' && name_str[len - 2] == '_')) {
            if (!check_internal_access(self)) {
                PyErr_Format(GuardianAccessError,
                             "External access denied: Cannot modify protected/private attribute '%s'.",
                             name_str);
                return -1;
            }
        }
    }

    /* Attribute deletion is delegated directly. */
    if (unlikely(value == NULL)) return PyObject_GenericSetAttr(self, name, value);

    /* Type-check against the compiled shield rules, if any. */
    PyTypeObject *type = Py_TYPE(self);
    PyObject *rules_dict = PyDict_GetItemString(type->tp_dict, "__shield_rules__");

    if (likely(rules_dict != NULL)) {
        PyObject *rule_def = PyDict_GetItemWithError(rules_dict, name);
        if (rule_def != NULL) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 0);
            if (unlikely(!fast_check_type(value, rule))) {
                PyObject *expected = PyTuple_GET_ITEM(rule_def, 1);
                raise_type_error(name, expected, value);
                return -1;
            }
        } else if (PyErr_Occurred()) {
            PyErr_Clear();
        }
    } else {
        PyErr_Clear();
    }

    return PyObject_GenericSetAttr(self, name, value);
}

PyTypeObject ShieldBaseType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.ShieldBase",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getattro = PyObject_GenericGetAttr,  /* reads stay unguarded for speed */
    .tp_setattro = shield_setattro,          /* writes are protected          */
    .tp_new = PyType_GenericNew,
};
