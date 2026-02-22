#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#include <stddef.h>

#if PY_VERSION_HEX < 0x030B0000
static inline PyObject* PyFrame_GetLocals(PyFrameObject *frame) {
    PyFrame_FastToLocals(frame);
    PyObject *locals = frame->f_locals;
    Py_XINCREF(locals);
    return locals;
}
#endif

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
static PyObject *GuardianAccessError;

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

static int check_internal_access(PyObject *self, const char *name_str) {
    if (!name_str || name_str[0] != '_') return 1;

    size_t len = strlen(name_str);
    if (len >= 4 && name_str[0] == '_' && name_str[1] == '_' && name_str[len-1] == '_' && name_str[len-2] == '_') {
        return 1;
    }

    int is_internal = 0;
#if PY_VERSION_HEX >= 0x030B0000
    PyFrameObject *frame = PyThreadState_GetFrame(PyThreadState_Get());
#else
    PyFrameObject *frame = PyEval_GetFrame();
    Py_XINCREF(frame);
#endif

    PyFrameObject *f = frame;
    while (f) {
        PyObject *locals = PyFrame_GetLocals(f);
        if (locals) {
            PyObject *frame_self = PyMapping_GetItemString(locals, "self");
            if (frame_self) {
                if (frame_self == self) is_internal = 1;
                Py_DECREF(frame_self);
            } else {
                PyErr_Clear();
            }
            Py_DECREF(locals);
        }
        if (is_internal) break;

#if PY_VERSION_HEX >= 0x03090000
        PyFrameObject *back = PyFrame_GetBack(f);
#else
        PyFrameObject *back = f->f_back;
        Py_XINCREF(back);
#endif
        Py_DECREF(f);
        f = back;
    }
    if (f) Py_DECREF(f);

    return is_internal;
}

static int shield_setattro(PyObject *self, PyObject *name, PyObject *value) {
    if (!PyUnicode_Check(name)) return PyObject_GenericSetAttr(self, name, value);

    const char *name_str = PyUnicode_AsUTF8(name);

    if (!check_internal_access(self, name_str)) {
        PyErr_Format(GuardianAccessError, "External access denied: Cannot modify protected/private attribute '%s'.", name_str);
        return -1;
    }

    if (value == NULL) return PyObject_GenericSetAttr(self, name, value);

    PyTypeObject *type = Py_TYPE(self);
    PyObject *rules = PyObject_GetAttrString((PyObject *)type, "__shield_rules__");

    if (rules && PyDict_Check(rules)) {
        PyObject *rule_def = PyDict_GetItem(rules, name);
        if (rule_def) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 0);
            if (!check_type(value, rule)) {
                PyObject *expected = PyTuple_GET_ITEM(rule_def, 1);
                raise_type_error(name, expected, value);
                Py_DECREF(rules);
                return -1;
            }
        }
    }
    if (rules) Py_DECREF(rules); else PyErr_Clear();

    return PyObject_GenericSetAttr(self, name, value);
}

static PyObject* shield_getattro(PyObject *self, PyObject *name) {
    if (!PyUnicode_Check(name)) return PyObject_GenericGetAttr(self, name);

    const char *name_str = PyUnicode_AsUTF8(name);
    if (!check_internal_access(self, name_str)) {
        PyErr_Format(GuardianAccessError, "External access denied: Cannot read protected/private attribute '%s'.", name_str);
        return NULL;
    }

    return PyObject_GenericGetAttr(self, name);
}

static PyTypeObject ShieldBaseType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.ShieldBase",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getattro = shield_getattro,
    .tp_setattro = shield_setattro,
    .tp_new = PyType_GenericNew,
};

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

// -------------------------------------------------------------
// STRICT GUARD FIX: Removed clunky local_types dictionary
// -------------------------------------------------------------
typedef struct {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *func;
    PyObject *func_code;
    PyObject *rules;
    PyObject *kw_names;
    PyObject *ret_rule;
    PyObject *ret_name;
    int check_return;
} StrictGuardObject;

static int strict_trace_func(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
    // 1. FAST EXIT: Only inspect locals when the function is finished and returning
    if (what != PyTrace_RETURN) return 0;

    StrictGuardObject *self = (StrictGuardObject *)obj;
    PyCodeObject *f_code = PyFrame_GetCode(frame);
    int is_target = ((PyObject *)f_code == self->func_code);
    Py_XDECREF(f_code);

    if (!is_target) return 0;

    PyObject *locals = PyFrame_GetLocals(frame);
    if (!locals) return 0;

    // 2. O(1) LOOKUP: Pluck the exact variables we need straight from the Proxy
    Py_ssize_t nrules = PyTuple_GET_SIZE(self->rules);
    for (Py_ssize_t i = 0; i < nrules; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->rules, i);
        PyObject *key = PyTuple_GET_ITEM(rule_def, 0);
        PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);

        const char *key_str = PyUnicode_AsUTF8(key);
        PyObject *val = PyMapping_GetItemString(locals, key_str);

        if (val) {
            if (!check_type(val, rule)) {
                raise_type_error(key, PyTuple_GET_ITEM(rule_def, 1), val);
                Py_DECREF(val);
                Py_DECREF(locals);
                return -1; // Abort and raise
            }
            Py_DECREF(val);
        } else {
            PyErr_Clear(); // Variable might not be initialized in this logic path, ignore.
        }
    }

    Py_DECREF(locals);
    return 0;
}

static PyObject *StrictGuard_vectorcall(PyObject *self_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames) {
    StrictGuardObject *self = (StrictGuardObject *)self_obj;

    // 3. PROFILER FIX: SetProfile instead of SetTrace drops O(N) line-checking completely
    PyEval_SetProfile(strict_trace_func, self_obj);

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t nrules = PyTuple_GET_SIZE(self->rules);
    for (Py_ssize_t i = 0; i < nargs && i < nrules; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->rules, i);
        if (!check_type(args[i], PyTuple_GET_ITEM(rule_def, 2))) {
            PyEval_SetProfile(NULL, NULL);
            raise_type_error(PyTuple_GET_ITEM(rule_def, 0), PyTuple_GET_ITEM(rule_def, 1), args[i]);
            return NULL;
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);

    // Clear profiler immediately
    PyEval_SetProfile(NULL, NULL);

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

    guard->func_code = PyObject_GetAttrString(func, "__code__");
    if (!guard->func_code) PyErr_Clear();

    guard->rules = rules;
    guard->kw_names = kw_names;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;

    return (PyObject *)guard;
}

static PyMethodDef GuardianMethods[] = {
    {"make_guard", make_guard, METH_VARARGS, "Create a C-level guard wrapper"},
    {"make_strictguard", make_strictguard, METH_VARARGS, "Create a C-level strictguard wrapper"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef guardianmodule = {
    PyModuleDef_HEAD_INIT,
    "_guardian_core",
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

    GuardianAccessError = PyErr_NewException("guardian.GuardianAccessError", PyExc_AttributeError, NULL);
    Py_XINCREF(GuardianAccessError);
    PyModule_AddObject(m, "GuardianAccessError", GuardianAccessError);

    GuardType.tp_vectorcall_offset = offsetof(GuardObject, vectorcall);
    if (PyType_Ready(&GuardType) < 0) return NULL;

    StrictGuardType.tp_vectorcall_offset = offsetof(StrictGuardObject, vectorcall);
    if (PyType_Ready(&StrictGuardType) < 0) return NULL;

    if (PyType_Ready(&ShieldBaseType) < 0) return NULL;
    Py_INCREF(&ShieldBaseType);
    PyModule_AddObject(m, "ShieldBase", (PyObject *)&ShieldBaseType);

    return m;
}