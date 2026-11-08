/*
 * type_checker.c
 * --------------
 * The core validation engine. Contains the opcode-driven `fast_check_type`
 * validator and the `raise_type_error` formatting helper.
 *
 * This module is pure validation: it owns no state and exposes exactly two
 * functions used by every other translation unit.
 */
#include "guardian_common.h"

/*
 * Validate `obj` against a compiled rule tuple `(opcode, argument)`.
 * Returns 1 on success, 0 on failure.
 *
 * IMPORTANT: `PyObject_IsInstance` and `PySequence_Contains` can set an
 * exception and return -1. Because callers rely on the truthiness of this
 * return value, any -1 is treated as a mismatch and the exception is cleared
 * so it does not leak into unrelated code paths.
 */
int fast_check_type(PyObject *obj, PyObject *rule) {
    if (unlikely(rule == Py_None)) return 1;

    long op = PyLong_AS_LONG(PyTuple_GET_ITEM(rule, 0));
    PyObject *arg = PyTuple_GET_ITEM(rule, 1);

    switch (op) {
        case OP_ANY: return 1;
        case OP_EXACT: {
            if (arg == (PyObject *)&PyLong_Type) return PyLong_CheckExact(obj);
            if (arg == (PyObject *)&PyUnicode_Type) return PyUnicode_CheckExact(obj);
            if (arg == (PyObject *)&PyFloat_Type) return PyFloat_CheckExact(obj);
            if (arg == (PyObject *)&PyBool_Type) return PyBool_Check(obj);
            if (arg == (PyObject *)Py_TYPE(Py_None)) return obj == Py_None;
            return Py_TYPE(obj) == (PyTypeObject *)arg;
        }
        case OP_INSTANCE: {
            int res = PyObject_IsInstance(obj, arg);
            if (unlikely(res < 0)) { PyErr_Clear(); return 0; }
            return res;
        }
        case OP_UNION: {
            Py_ssize_t size = PyTuple_GET_SIZE(arg);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (fast_check_type(obj, PyTuple_GET_ITEM(arg, i))) return 1;
            }
            return 0;
        }
        case OP_LIST: {
            if (unlikely(!PyList_Check(obj))) return 0;
            if (arg == Py_None) return 1;
            Py_ssize_t size = PyList_GET_SIZE(obj);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!fast_check_type(PyList_GET_ITEM(obj, i), arg)) return 0;
            }
            return 1;
        }
        case OP_DICT: {
            if (unlikely(!PyDict_Check(obj))) return 0;
            if (arg == Py_None) return 1;
            PyObject *k_rule = PyTuple_GET_ITEM(arg, 0);
            PyObject *v_rule = PyTuple_GET_ITEM(arg, 1);
            PyObject *key, *value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(obj, &pos, &key, &value)) {
                if (!fast_check_type(key, k_rule) || !fast_check_type(value, v_rule)) return 0;
            }
            return 1;
        }
        case OP_TUPLE_VAR: {
            if (unlikely(!PyTuple_Check(obj))) return 0;
            Py_ssize_t size = PyTuple_GET_SIZE(obj);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!fast_check_type(PyTuple_GET_ITEM(obj, i), arg)) return 0;
            }
            return 1;
        }
        case OP_TUPLE_FIXED: {
            if (unlikely(!PyTuple_Check(obj))) return 0;
            Py_ssize_t size = PyTuple_GET_SIZE(arg);
            if (PyTuple_GET_SIZE(obj) != size) return 0;
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!fast_check_type(PyTuple_GET_ITEM(obj, i), PyTuple_GET_ITEM(arg, i))) return 0;
            }
            return 1;
        }
        case OP_SET: {
            if (unlikely(!PyAnySet_Check(obj))) return 0;
            if (arg == Py_None) return 1;
            PyObject *iter = PyObject_GetIter(obj);
            if (iter == NULL) return 0;
            PyObject *item;
            while ((item = PyIter_Next(iter))) {
                int res = fast_check_type(item, arg);
                Py_DECREF(item);
                if (!res) {
                    Py_DECREF(iter);
                    return 0;
                }
            }
            if (PyErr_Occurred()) PyErr_Clear(); /* tolerate broken iterators */
            Py_DECREF(iter);
            return 1;
        }
        case OP_LITERAL: {
            int contains = PySequence_Contains(arg, obj);
            if (unlikely(contains < 0)) { PyErr_Clear(); return 0; }
            return contains;
        }
    }
    return 0;
}

/*
 * Build and raise a GuardianTypeError with a readable message:
 *   Variable '<name>' expected <expected>, got <type> (<repr>)
 */
void raise_type_error(PyObject *param_name, PyObject *expected_name, PyObject *val) {
    PyObject *val_repr = PyObject_Repr(val);
    PyObject *val_type_name = PyObject_GetAttrString((PyObject *)Py_TYPE(val), "__name__");
    PyErr_Format(GuardianTypeError, "Variable '%U' expected %U, got %U (%U)",
                 param_name, expected_name, val_type_name, val_repr);
    Py_XDECREF(val_repr);
    Py_XDECREF(val_type_name);
}
