#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>


#if PY_VERSION_HEX < 0x030B0000
static inline PyObject* PyFrame_GetLocals(PyFrameObject *frame) {
    PyFrame_FastToLocals(frame);
    PyObject *locals = frame->f_locals;
    Py_XINCREF(locals);
    return locals;
}
#endif
// ----------------------

#define OP_ANY 0
#define OP_INSTANCE 1
#define OP_EXACT 2
#define OP_UNION 3
#define OP_LIST 4
#define OP_DICT 5
#define OP_TUPLE_VAR 6
#define OP_TUPLE_FIXED 7
#define OP_SET 8
#define OP_LITERAL 9

static PyObject *GuardianTypeError;

static int check_type(PyObject *obj, PyObject *rule) {
    if (rule == Py_None) return 1;

    long op = PyLong_AsLong(PyTuple_GET_ITEM(rule, 0));
    PyObject *arg = PyTuple_GET_ITEM(rule, 1);

    switch (op) {
        case OP_ANY: return 1;
        case OP_EXACT: return Py_TYPE(obj) == (PyTypeObject *)arg;
        case OP_INSTANCE: return PyObject_IsInstance(obj, arg);
        case OP_UNION: {
            Py_ssize_t size = PyTuple_GET_SIZE(arg);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (check_type(obj, PyTuple_GET_ITEM(arg, i))) return 1;
            }
            return 0;
        }
        case OP_LIST: {
            if (!PyList_Check(obj)) return 0;
            if (arg == Py_None) return 1;
            Py_ssize_t size = PyList_GET_SIZE(obj);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!check_type(PyList_GET_ITEM(obj, i), arg)) return 0;
            }
            return 1;
        }
        case OP_DICT: {
            if (!PyDict_Check(obj)) return 0;
            if (arg == Py_None) return 1;
            PyObject *k_rule = PyTuple_GET_ITEM(arg, 0);
            PyObject *v_rule = PyTuple_GET_ITEM(arg, 1);
            PyObject *key, *value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(obj, &pos, &key, &value)) {
                if (!check_type(key, k_rule) || !check_type(value, v_rule)) return 0;
            }
            return 1;
        }
        case OP_TUPLE_VAR: {
            if (!PyTuple_Check(obj)) return 0;
            Py_ssize_t size = PyTuple_GET_SIZE(obj);
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!check_type(PyTuple_GET_ITEM(obj, i), arg)) return 0;
            }
            return 1;
        }
        case OP_TUPLE_FIXED: {
            if (!PyTuple_Check(obj)) return 0;
            Py_ssize_t size = PyTuple_GET_SIZE(arg);
            if (PyTuple_GET_SIZE(obj) != size) return 0;
            for (Py_ssize_t i = 0; i < size; i++) {
                if (!check_type(PyTuple_GET_ITEM(obj, i), PyTuple_GET_ITEM(arg, i))) return 0;
            }
            return 1;
        }
        case OP_SET: {
            if (!PyAnySet_Check(obj)) return 0;
            if (arg == Py_None) return 1;
            PyObject *iterator = PyObject_GetIter(obj);
            PyObject *item;
            while ((item = PyIter_Next(iterator))) {
                int res = check_type(item, arg);
                Py_DECREF(item);
                if (!res) {
                    Py_DECREF(iterator);
                    return 0;
                }
            }
            Py_DECREF(iterator);
            return 1;
        }
        case OP_LITERAL: {
            return PySequence_Contains(arg, obj) == 1;
        }
    }
    return 0;
}

static void raise_type_error(PyObject *param_name, PyObject *expected_name, PyObject *val) {
    PyObject *val_repr = PyObject_Repr(val);
    PyObject *val_type_name = PyObject_GetAttrString((PyObject *)Py_TYPE(val), "__name__");
    PyErr_Format(GuardianTypeError, "Variable '%U' expected %U, got %U (%U)",
                 param_name, expected_name, val_type_name, val_repr);
    Py_XDECREF(val_repr);
    Py_XDECREF(val_type_name);
}

typedef struct {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *func;
    PyObject *rules;
    PyObject *kw_names;
    PyObject *ret_rule;
    PyObject *ret_name;
    int check_return;
} GuardObject;

static void Guard_dealloc(GuardObject *self) {
    Py_XDECREF(self->func);
    Py_XDECREF(self->rules);
    Py_XDECREF(self->kw_names);
    Py_XDECREF(self->ret_rule);
    Py_XDECREF(self->ret_name);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *Guard_vectorcall(PyObject *self_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames) {
    GuardObject *self = (GuardObject *)self_obj;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t nrules = PyTuple_GET_SIZE(self->rules);

    for (Py_ssize_t i = 0; i < nargs && i < nrules; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->rules, i);
        PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
        if (!check_type(args[i], rule)) {
            raise_type_error(PyTuple_GET_ITEM(rule_def, 0), PyTuple_GET_ITEM(rule_def, 1), args[i]);
            return NULL;
        }
    }

    if (kwnames != NULL) {
        Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwnames);
        for (Py_ssize_t i = 0; i < nkwargs; i++) {
            PyObject *kw = PyTuple_GET_ITEM(kwnames, i);
            PyObject *val = args[nargs + i];
            for (Py_ssize_t j = 0; j < nrules; j++) {
                PyObject *rule_def = PyTuple_GET_ITEM(self->rules, j);
                if (PyUnicode_Compare(kw, PyTuple_GET_ITEM(rule_def, 0)) == 0) {
                    if (!check_type(val, PyTuple_GET_ITEM(rule_def, 2))) {
                        raise_type_error(kw, PyTuple_GET_ITEM(rule_def, 1), val);
                        return NULL;
                    }
                    break;
                }
            }
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);

    if (result && self->check_return && self->ret_rule != Py_None) {
        if (!check_type(result, self->ret_rule)) {
            raise_type_error(PyUnicode_FromString("return"), self->ret_name, result);
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

static PyTypeObject GuardType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.Guard",
    .tp_basicsize = sizeof(GuardObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_dealloc = (destructor)Guard_dealloc,
    .tp_call = PyVectorcall_Call,
};

typedef struct {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *func;
    PyObject *func_code; // Fast-path Cache
    PyObject *rules;
    PyObject *kw_names;
    PyObject *ret_rule;
    PyObject *ret_name;
    PyObject *local_types;
    int check_return;
} StrictGuardObject;

static int strict_trace_func(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
    if (what != PyTrace_LINE && what != PyTrace_RETURN) return 0;

    StrictGuardObject *self = (StrictGuardObject *)obj;

    // Fast pointer comparison using our cached __code__
    PyCodeObject *f_code = PyFrame_GetCode(frame);
    int is_target = ((PyObject *)f_code == self->func_code);
    Py_XDECREF(f_code);

    if (!is_target) return 0;

    PyObject *locals = PyFrame_GetLocals(frame);
    if (!locals) return 0;

    // Python 3.13 Fix: Dump the FrameLocalsProxy into a real dict for fast iteration
    PyObject *locals_dict = PyDict_New();
    if (!locals_dict) {
        Py_DECREF(locals);
        return -1;
    }
    if (PyDict_Update(locals_dict, locals) < 0) {
        PyErr_Clear();
    }

    PyObject *frame_id = PyLong_FromVoidPtr(frame);
    PyObject *frame_locals_meta = PyDict_GetItem(self->local_types, frame_id);

    if (!frame_locals_meta) {
        frame_locals_meta = PyDict_New();
        PyDict_SetItem(self->local_types, frame_id, frame_locals_meta);
        Py_DECREF(frame_locals_meta);

        Py_ssize_t nrules = PyTuple_GET_SIZE(self->rules);
        for (Py_ssize_t i = 0; i < nrules; i++) {
            PyObject *rule_def = PyTuple_GET_ITEM(self->rules, i);
            PyDict_SetItem(frame_locals_meta, PyTuple_GET_ITEM(rule_def, 0), rule_def);
        }
    }

    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(locals_dict, &pos, &key, &value)) {
        PyObject *rule_def = PyDict_GetItem(frame_locals_meta, key);
        if (rule_def) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
            if (!check_type(value, rule)) {
                raise_type_error(key, PyTuple_GET_ITEM(rule_def, 1), value);
                Py_DECREF(locals_dict);
                Py_DECREF(locals);
                Py_DECREF(frame_id);
                return -1;
            }
        } else {
            PyObject *type_rule = PyTuple_Pack(2, PyLong_FromLong(OP_EXACT), (PyObject *)Py_TYPE(value));
            PyObject *type_name = PyObject_GetAttrString((PyObject *)Py_TYPE(value), "__name__");
            PyObject *new_rule_def = PyTuple_Pack(3, key, type_name, type_rule);
            PyDict_SetItem(frame_locals_meta, key, new_rule_def);
            Py_DECREF(type_rule);
            Py_DECREF(type_name);
            Py_DECREF(new_rule_def);
        }
    }

    if (what == PyTrace_RETURN) {
        if (PyDict_Contains(self->local_types, frame_id)) {
            PyDict_DelItem(self->local_types, frame_id);
        }
    }

    Py_DECREF(locals_dict);
    Py_DECREF(locals);
    Py_DECREF(frame_id);
    return 0;
}

static PyObject *StrictGuard_vectorcall(PyObject *self_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames) {
    StrictGuardObject *self = (StrictGuardObject *)self_obj;

    // Python 3.11+ Fix: Do not poke into PyThreadState directly.
    // We just set our trace function using the public API.
    PyEval_SetTrace(strict_trace_func, self_obj);

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t nrules = PyTuple_GET_SIZE(self->rules);
    for (Py_ssize_t i = 0; i < nargs && i < nrules; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->rules, i);
        if (!check_type(args[i], PyTuple_GET_ITEM(rule_def, 2))) {
            PyEval_SetTrace(NULL, NULL); // Clear trace on early exit
            raise_type_error(PyTuple_GET_ITEM(rule_def, 0), PyTuple_GET_ITEM(rule_def, 1), args[i]);
            return NULL;
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);

    // Python 3.11+ Fix: Clear the trace function using public API
    PyEval_SetTrace(NULL, NULL);

    if (result && self->check_return && self->ret_rule != Py_None) {
        if (!check_type(result, self->ret_rule)) {
            raise_type_error(PyUnicode_FromString("return"), self->ret_name, result);
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

static void StrictGuard_dealloc(StrictGuardObject *self) {
    Py_XDECREF(self->func);
    Py_XDECREF(self->func_code);
    Py_XDECREF(self->rules);
    Py_XDECREF(self->kw_names);
    Py_XDECREF(self->ret_rule);
    Py_XDECREF(self->ret_name);
    Py_XDECREF(self->local_types);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyTypeObject StrictGuardType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.StrictGuard",
    .tp_basicsize = sizeof(StrictGuardObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_dealloc = (destructor)StrictGuard_dealloc,
    .tp_call = PyVectorcall_Call,
};

static PyObject* make_guard(PyObject *module, PyObject *args) {
    PyObject *func, *rules, *kw_names, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &rules, &kw_names, &ret_rule, &ret_name, &check_return)) return NULL;

    GuardObject *guard = PyObject_New(GuardObject, &GuardType);
    Py_INCREF(func); Py_INCREF(rules); Py_INCREF(kw_names); Py_INCREF(ret_rule); Py_INCREF(ret_name);
    guard->vectorcall = Guard_vectorcall;
    guard->func = func;
    guard->rules = rules;
    guard->kw_names = kw_names;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;

    return (PyObject *)guard;
}

static PyObject* make_strictguard(PyObject *module, PyObject *args) {
    PyObject *func, *rules, *kw_names, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &rules, &kw_names, &ret_rule, &ret_name, &check_return)) return NULL;

    StrictGuardObject *guard = PyObject_New(StrictGuardObject, &StrictGuardType);
    Py_INCREF(func); Py_INCREF(rules); Py_INCREF(kw_names); Py_INCREF(ret_rule); Py_INCREF(ret_name);
    guard->vectorcall = StrictGuard_vectorcall;
    guard->func = func;

    // Fast-path Cache target code object
    guard->func_code = PyObject_GetAttrString(func, "__code__");
    if (!guard->func_code) PyErr_Clear();

    guard->rules = rules;
    guard->kw_names = kw_names;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;
    guard->local_types = PyDict_New();

    return (PyObject *)guard;
}

static PyMethodDef GuardianMethods[] = {
    {"make_guard", make_guard, METH_VARARGS, "Create a C-level guard wrapper"},
    {"make_strictguard", make_strictguard, METH_VARARGS, "Create a C-level strictguard wrapper"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef guardianmodule = {
    PyModuleDef_HEAD_INIT,
    "_guardian_core", // Ensure this matches the Extension name in setup.py
    "C core for guardian type enforcement",
    -1,
    GuardianMethods
};

PyMODINIT_FUNC PyInit__guardian_core(void) {
    PyObject *m = PyModule_Create(&guardianmodule);
    if (m == NULL) return NULL;

    GuardianTypeError = PyErr_NewException("guardian.GuardianTypeError", PyExc_TypeError, NULL);
    Py_XINCREF(GuardianTypeError);
    PyModule_AddObject(m, "GuardianTypeError", GuardianTypeError);

    GuardType.tp_vectorcall_offset = offsetof(GuardObject, vectorcall);
    if (PyType_Ready(&GuardType) < 0) return NULL;

    StrictGuardType.tp_vectorcall_offset = offsetof(StrictGuardObject, vectorcall);
    if (PyType_Ready(&StrictGuardType) < 0) return NULL;

    return m;
}