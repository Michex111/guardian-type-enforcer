#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif

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
static PyObject *GuardianInitializationError;

static int fast_check_type(PyObject *obj, PyObject *rule) {
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
        case OP_INSTANCE:
            return PyObject_IsInstance(obj, arg);
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
            PyObject *item;
            while ((item = PyIter_Next(iter))) {
                int res = fast_check_type(item, arg);
                Py_DECREF(item);
                if (!res) {
                    Py_DECREF(iter);
                    return 0;
                }
            }
            Py_DECREF(iter);
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
            // 1. Check for standard 'self' (Instance methods)
            PyObject *frame_self = PyMapping_GetItemString(locals, "self");
            if (frame_self) {
                if (frame_self == self) is_internal = 1;
                Py_DECREF(frame_self);
            } else {
                PyErr_Clear();
            }

            // 2. Check for 'cls' (Class methods)
            if (!is_internal) {
                PyObject *frame_cls = PyMapping_GetItemString(locals, "cls");
                if (frame_cls) {
                    if (frame_cls == self) is_internal = 1;
                    Py_DECREF(frame_cls);
                } else {
                    PyErr_Clear();
                }
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
    if (unlikely(!PyUnicode_Check(name))) return PyObject_GenericSetAttr(self, name, value);

    const char *name_str = PyUnicode_AsUTF8(name);

    if (unlikely(name_str[0] == '_')) {
        size_t len = strlen(name_str);
        if (!(len >= 4 && name_str[1] == '_' && name_str[len-1] == '_' && name_str[len-2] == '_')) {
            if (!check_internal_access(self, name_str)) {
                PyErr_Format(GuardianAccessError, "External access denied: Cannot modify protected/private attribute '%s'.", name_str);
                return -1;
            }
        }
    }

    if (unlikely(value == NULL)) return PyObject_GenericSetAttr(self, name, value);

    PyTypeObject *type = Py_TYPE(self);
    PyObject *rules_dict = PyDict_GetItemString(type->tp_dict, "__shield_rules__");

    if (likely(rules_dict != NULL)) {
        PyObject *rule_def = PyDict_GetItemWithError(rules_dict, name);
        if (rule_def) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 0);
            if (unlikely(!fast_check_type(value, rule))) {
                PyObject *expected = PyTuple_GET_ITEM(rule_def, 1);
                raise_type_error(name, expected, value);
                return -1;
            }
        }
    } else {
        PyErr_Clear();
    }

    return PyObject_GenericSetAttr(self, name, value);
}


static PyTypeObject ShieldBaseType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.ShieldBase",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_getattro = PyObject_GenericGetAttr,  // <--- BYPASS CUSTOM LOGIC ENTIRELY
    .tp_setattro = shield_setattro,          // Keep the write protection
    .tp_new = PyType_GenericNew,
};

// --- CORE OBJECTS ---
typedef struct {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *func;
    PyObject *pos_rules;
    PyObject *kw_rules;
    PyObject *ret_rule;
    PyObject *ret_name;
    int check_return;
} GuardObject;

static void Guard_dealloc(GuardObject *self) {
    Py_XDECREF(self->func);
    Py_XDECREF(self->pos_rules);
    Py_XDECREF(self->kw_rules);
    Py_XDECREF(self->ret_rule);
    Py_XDECREF(self->ret_name);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

// THE FIX: Descriptor Protocol binds the C-object to instances (injects `self` into args)
static PyObject *Guard_descr_get(PyObject *self, PyObject *obj, PyObject *type) {
    if (obj == NULL || obj == Py_None) {
        Py_INCREF(self);
        return self;
    }
    return PyMethod_New(self, obj);
}

static PyObject* Guard_getattro(PyObject *self, PyObject *name) {
    PyObject *res = PyObject_GenericGetAttr(self, name);
    if (res != NULL) return res;
    PyErr_Clear();
    return PyObject_GetAttr(((GuardObject*)self)->func, name);
}

static PyObject *Guard_vectorcall(PyObject *self_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames) {
    GuardObject *self = (GuardObject *)self_obj;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

    Py_ssize_t n_pos = PyTuple_GET_SIZE(self->pos_rules);
    for (Py_ssize_t i = 0; i < nargs && i < n_pos; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->pos_rules, i);
        if (rule_def != Py_None) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
            if (unlikely(!fast_check_type(args[i], rule))) {
                raise_type_error(PyTuple_GET_ITEM(rule_def, 0), PyTuple_GET_ITEM(rule_def, 1), args[i]);
                return NULL;
            }
        }
    }

    if (unlikely(kwnames != NULL)) {
        Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwnames);
        for (Py_ssize_t i = 0; i < nkwargs; i++) {
            PyObject *kw = PyTuple_GET_ITEM(kwnames, i);
            PyObject *val = args[nargs + i];

            PyObject *rule_def = PyDict_GetItemWithError(self->kw_rules, kw);
            if (rule_def) {
                if (unlikely(!fast_check_type(val, PyTuple_GET_ITEM(rule_def, 2)))) {
                    raise_type_error(kw, PyTuple_GET_ITEM(rule_def, 1), val);
                    return NULL;
                }
            }
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);

    if (result && self->check_return && self->ret_rule != Py_None) {
        if (unlikely(!fast_check_type(result, self->ret_rule))) {
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
    .tp_getattro = Guard_getattro,
    .tp_descr_get = Guard_descr_get, // Binds method accurately
};

typedef struct {
    PyObject_HEAD
    vectorcallfunc vectorcall;
    PyObject *func;
    PyObject *func_code;
    PyObject *pos_rules;
    PyObject *kw_rules;
    PyObject *ret_rule;
    PyObject *ret_name;
    int check_return;
} StrictGuardObject;

static int strict_trace_func(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
    if (what != PyTrace_RETURN) return 0;

    StrictGuardObject *self = (StrictGuardObject *)obj;
    PyCodeObject *f_code = PyFrame_GetCode(frame);
    int is_target = ((PyObject *)f_code == self->func_code);
    Py_XDECREF(f_code);

    if (!is_target) return 0;

    PyObject *locals = PyFrame_GetLocals(frame);
    if (!locals) return 0;

    PyObject *key, *rule_def;
    Py_ssize_t pos = 0;
    
    // Modify strict_trace_func around line 273:

    while (PyDict_Next(self->kw_rules, &pos, &key, &rule_def)) {
        // THE FIX: PyObject_GetItem respects modern Python proxy mappings while 
        // retaining the O(1) string lookup performance win.
        PyObject *val = PyObject_GetItem(locals, key); 
        
        if (val) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
            if (unlikely(!fast_check_type(val, rule))) {
                raise_type_error(key, PyTuple_GET_ITEM(rule_def, 1), val);
                Py_DECREF(val);
                Py_DECREF(locals);
                return -1;
            }
            Py_DECREF(val); // Clean up the new reference
        } else {
            // STRICT ENFORCEMENT: The variable was declared but never initialized
            PyErr_Clear();
            PyErr_Format(GuardianInitializationError, 
                         "Variable '%U' was declared as %U but was never initialized before return.", 
                         key, PyTuple_GET_ITEM(rule_def, 1));
            Py_DECREF(locals);
            return -1;
        }
    }
    
    Py_DECREF(locals);
    return 0;
}

static PyObject* StrictGuard_getattro(PyObject *self, PyObject *name) {
    PyObject *res = PyObject_GenericGetAttr(self, name);
    if (res != NULL) return res;
    PyErr_Clear();
    return PyObject_GetAttr(((StrictGuardObject*)self)->func, name);
}

static PyObject *StrictGuard_vectorcall(PyObject *self_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames) {
    StrictGuardObject *self = (StrictGuardObject *)self_obj;

    PyEval_SetProfile(strict_trace_func, self_obj);

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t n_pos = PyTuple_GET_SIZE(self->pos_rules);
    for (Py_ssize_t i = 0; i < nargs && i < n_pos; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->pos_rules, i);
        if (rule_def != Py_None) {
            if (unlikely(!fast_check_type(args[i], PyTuple_GET_ITEM(rule_def, 2)))) {
                PyEval_SetProfile(NULL, NULL);
                raise_type_error(PyTuple_GET_ITEM(rule_def, 0), PyTuple_GET_ITEM(rule_def, 1), args[i]);
                return NULL;
            }
        }
    }

    if (unlikely(kwnames != NULL)) {
        Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwnames);
        for (Py_ssize_t i = 0; i < nkwargs; i++) {
            PyObject *kw = PyTuple_GET_ITEM(kwnames, i);
            PyObject *rule_def = PyDict_GetItemWithError(self->kw_rules, kw);
            if (rule_def) {
                if (unlikely(!fast_check_type(args[nargs + i], PyTuple_GET_ITEM(rule_def, 2)))) {
                    PyEval_SetProfile(NULL, NULL);
                    raise_type_error(kw, PyTuple_GET_ITEM(rule_def, 1), args[nargs + i]);
                    return NULL;
                }
            }
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);
    PyEval_SetProfile(NULL, NULL);

    if (result && self->check_return && self->ret_rule != Py_None) {
        if (unlikely(!fast_check_type(result, self->ret_rule))) {
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
    Py_XDECREF(self->pos_rules);
    Py_XDECREF(self->kw_rules);
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
    .tp_getattro = StrictGuard_getattro,
    .tp_descr_get = Guard_descr_get,
};

static PyObject* make_guard(PyObject *module, PyObject *args) {
    PyObject *func, *pos_rules, *kw_rules, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &pos_rules, &kw_rules, &ret_rule, &ret_name, &check_return)) return NULL;

    GuardObject *guard = PyObject_New(GuardObject, &GuardType);
    Py_INCREF(func); Py_INCREF(pos_rules); Py_INCREF(kw_rules); Py_INCREF(ret_rule); Py_INCREF(ret_name);
    guard->vectorcall = Guard_vectorcall;
    guard->func = func;
    guard->pos_rules = pos_rules;
    guard->kw_rules = kw_rules;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;

    return (PyObject *)guard;
}

static PyObject* make_strictguard(PyObject *module, PyObject *args) {
    PyObject *func, *pos_rules, *kw_rules, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &pos_rules, &kw_rules, &ret_rule, &ret_name, &check_return)) return NULL;

    StrictGuardObject *guard = PyObject_New(StrictGuardObject, &StrictGuardType);
    Py_INCREF(func); Py_INCREF(pos_rules); Py_INCREF(kw_rules); Py_INCREF(ret_rule); Py_INCREF(ret_name);
    guard->vectorcall = StrictGuard_vectorcall;
    guard->func = func;
    guard->func_code = PyObject_GetAttrString(func, "__code__");
    guard->pos_rules = pos_rules;
    guard->kw_rules = kw_rules;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;

    return (PyObject *)guard;
}

// --- C FIELD DESCRIPTOR FOR DATACLASSES ---

typedef struct {
    PyObject_HEAD
    PyObject *name;
    PyObject *private_name;
    PyObject *rule;
    PyObject *expected_name;
    PyObject *custom_validator;
} CFieldDescriptorObject;

static void CFieldDescriptor_dealloc(CFieldDescriptorObject *self) {
    Py_XDECREF(self->name);
    Py_XDECREF(self->private_name);
    Py_XDECREF(self->rule);
    Py_XDECREF(self->expected_name);
    Py_XDECREF(self->custom_validator);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *CFieldDescriptor_descr_get(PyObject *self, PyObject *obj, PyObject *type) {
    if (obj == NULL || obj == Py_None) {
        Py_INCREF(self);
        return self;
    }
    return PyObject_GenericGetAttr(obj, ((CFieldDescriptorObject *)self)->private_name);
}

static int CFieldDescriptor_descr_set(PyObject *self_obj, PyObject *obj, PyObject *value) {
    CFieldDescriptorObject *self = (CFieldDescriptorObject *)self_obj;

    if (unlikely(value == NULL)) {
        PyErr_Format(PyExc_AttributeError, "Cannot delete guarded dataclass attribute '%U'", self->name);
        return -1;
    }

    // 1. Fast Path Validation using existing C logic
    if (unlikely(!fast_check_type(value, self->rule))) {
        raise_type_error(self->name, self->expected_name, value);
        return -1;
    }

    // 2. Custom Validator Hook (if provided by user)
    PyObject *final_value = value;
    if (self->custom_validator != Py_None) {
        PyObject *cls = (PyObject *)Py_TYPE(obj);
        PyObject *args = PyTuple_Pack(2, cls, value);
        final_value = PyObject_CallObject(self->custom_validator, args);
        Py_DECREF(args);
        
        if (final_value == NULL) return -1; // Exception raised inside custom validator
    } else {
        Py_INCREF(final_value);
    }

    // 3. Set the attribute bypasses Python dict lookup
    int res = PyObject_GenericSetAttr(obj, self->private_name, final_value);
    Py_DECREF(final_value);
    return res;
}

static PyTypeObject CFieldDescriptorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.CFieldDescriptor",
    .tp_basicsize = sizeof(CFieldDescriptorObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)CFieldDescriptor_dealloc,
    .tp_descr_get = CFieldDescriptor_descr_get,
    .tp_descr_set = CFieldDescriptor_descr_set,
};

// Factory function to instantiate the descriptor from Python
static PyObject* make_c_descriptor(PyObject *module, PyObject *args) {
    PyObject *name, *private_name, *rule, *expected_name, *custom_val;
    if (!PyArg_ParseTuple(args, "OOOOO", &name, &private_name, &rule, &expected_name, &custom_val)) return NULL;

    CFieldDescriptorObject *desc = PyObject_New(CFieldDescriptorObject, &CFieldDescriptorType);
    
    Py_INCREF(name); Py_INCREF(private_name); Py_INCREF(rule); 
    Py_INCREF(expected_name); Py_INCREF(custom_val);
    
    desc->name = name;
    desc->private_name = private_name;
    desc->rule = rule;
    desc->expected_name = expected_name;
    desc->custom_validator = custom_val;
    
    return (PyObject *)desc;
}

// --- CLASS-LEVEL METACLASS PROTECTION ---

static int shield_meta_setattro(PyObject *cls, PyObject *name, PyObject *value) {
    if (unlikely(!PyUnicode_Check(name))) return PyType_Type.tp_setattro(cls, name, value);

    const char *name_str = PyUnicode_AsUTF8(name);

    if (unlikely(name_str[0] == '_')) {
        size_t len = strlen(name_str);
        // Ignore standard dunders (like __module__ or __shield_rules__)
        if (!(len >= 4 && name_str[1] == '_' && name_str[len-1] == '_' && name_str[len-2] == '_')) {
            // Perform the C-level internal frame check against the class object itself
            if (!check_internal_access(cls, name_str)) {
                PyErr_Format(GuardianAccessError, "External access denied: Cannot modify protected/private class attribute '%s'.", name_str);
                return -1;
            }
        }
    }

    // Delegate the actual assignment to Python's core type implementation
    return PyType_Type.tp_setattro(cls, name, value);
}

static PyTypeObject ShieldMetaType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.ShieldMeta",
    .tp_basicsize = sizeof(PyHeapTypeObject), // Required size for metaclasses in C
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_setattro = shield_meta_setattro,      // Intercepts MyClass._var = 5
};

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

    GuardianTypeError = PyErr_NewException("guardian.GuardianTypeError", PyExc_TypeError, NULL);
    Py_XINCREF(GuardianTypeError);
    PyModule_AddObject(m, "GuardianTypeError", GuardianTypeError);

    GuardianAccessError = PyErr_NewException("guardian.GuardianAccessError", PyExc_AttributeError, NULL);
    Py_XINCREF(GuardianAccessError);
    PyModule_AddObject(m, "GuardianAccessError", GuardianAccessError);

    GuardianInitializationError = PyErr_NewException("guardian.GuardianInitializationError", PyExc_UnboundLocalError, NULL);
    Py_XINCREF(GuardianInitializationError);
    PyModule_AddObject(m, "GuardianInitializationError", GuardianInitializationError);

    GuardType.tp_vectorcall_offset = offsetof(GuardObject, vectorcall);
    if (PyType_Ready(&GuardType) < 0) return NULL;

    StrictGuardType.tp_vectorcall_offset = offsetof(StrictGuardObject, vectorcall);
    if (PyType_Ready(&StrictGuardType) < 0) return NULL;

    if (PyType_Ready(&ShieldBaseType) < 0) return NULL;
    Py_INCREF(&ShieldBaseType);
    PyModule_AddObject(m, "ShieldBase", (PyObject *)&ShieldBaseType);

    if (PyType_Ready(&CFieldDescriptorType) < 0) return NULL;

    // Register ShieldBase
    if (PyType_Ready(&ShieldBaseType) < 0) return NULL;
    Py_INCREF(&ShieldBaseType);
    PyModule_AddObject(m, "ShieldBase", (PyObject *)&ShieldBaseType);

    // Register ShieldMeta (Inherits from type)
    ShieldMetaType.tp_base = &PyType_Type;
    if (PyType_Ready(&ShieldMetaType) < 0) return NULL;
    Py_INCREF(&ShieldMetaType);
    PyModule_AddObject(m, "ShieldMeta", (PyObject *)&ShieldMetaType);

    return m;
}