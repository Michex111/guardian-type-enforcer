/*
 * guards.c
 * --------
 * The vectorcall-backed wrapper objects:
 *   - GuardObject       : boundary type enforcement (@guard)
 *   - StrictGuardObject : deep, frame-traced enforcement (@deepguard)
 *
 * NOTE: These objects hold strong references to arbitrary Python callables.
 * A future enhancement could add the cyclic-GC protocol (tp_traverse/
 * tp_clear) so that reference cycles (e.g. a bound method stored back on an
 * instance) are collectable. This was prototyped but reverted because it
 * produced a shutdown-time crash that needs deeper debugging; see the notes.
 */
#include "guardian_common.h"

/* ------------------------------------------------------------------ */
/* GuardObject                                                         */
/* ------------------------------------------------------------------ */

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

static int Guard_clear(GuardObject *self) {
    Py_CLEAR(self->func);
    Py_CLEAR(self->pos_rules);
    Py_CLEAR(self->kw_rules);
    Py_CLEAR(self->ret_rule);
    Py_CLEAR(self->ret_name);
    return 0;
}

static void Guard_dealloc(GuardObject *self) {
    Guard_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/*
 * Descriptor protocol: binds the guard object to an instance, injecting
 * `self` into the argument vector (same semantics as a normal method).
 */
static PyObject *Guard_descr_get(PyObject *self, PyObject *obj, PyObject *type) {
    if (obj == NULL || obj == Py_None) {
        Py_INCREF(self);
        return self;
    }
    return PyMethod_New(self, obj);
}

static PyObject *Guard_getattro(PyObject *self, PyObject *name) {
    PyObject *res = PyObject_GenericGetAttr(self, name);
    if (res != NULL) return res;
    PyErr_Clear();
    return PyObject_GetAttr(((GuardObject *)self)->func, name);
}

static PyObject *Guard_vectorcall(PyObject *self_obj, PyObject *const *args,
                                  size_t nargsf, PyObject *kwnames) {
    GuardObject *self = (GuardObject *)self_obj;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

    /* Positional argument validation. */
    Py_ssize_t n_pos = PyTuple_GET_SIZE(self->pos_rules);
    for (Py_ssize_t i = 0; i < nargs && i < n_pos; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->pos_rules, i);
        if (rule_def != Py_None) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
            if (unlikely(!fast_check_type(args[i], rule))) {
                raise_type_error(PyTuple_GET_ITEM(rule_def, 0),
                                 PyTuple_GET_ITEM(rule_def, 1), args[i]);
                return NULL;
            }
        }
    }

    /* Keyword argument validation. */
    if (unlikely(kwnames != NULL)) {
        Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwnames);
        for (Py_ssize_t i = 0; i < nkwargs; i++) {
            PyObject *kw = PyTuple_GET_ITEM(kwnames, i);
            PyObject *val = args[nargs + i];

            PyObject *rule_def = PyDict_GetItemWithError(self->kw_rules, kw);
            if (rule_def == NULL) {
                if (PyErr_Occurred()) PyErr_Clear();
                continue;
            }
            if (unlikely(!fast_check_type(val, PyTuple_GET_ITEM(rule_def, 2)))) {
                raise_type_error(kw, PyTuple_GET_ITEM(rule_def, 1), val);
                return NULL;
            }
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);

    /* Return value validation. The name string is interned (immortal), so
     * the temporary reference is dropped without leaking a per-error object. */
    if (result && self->check_return && self->ret_rule != Py_None) {
        if (unlikely(!fast_check_type(result, self->ret_rule))) {
            raise_type_error(PyUnicode_InternFromString("return"), self->ret_name, result);
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyTypeObject GuardType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.Guard",
    .tp_basicsize = sizeof(GuardObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_dealloc = (destructor)Guard_dealloc,
    .tp_call = PyVectorcall_Call,
    .tp_getattro = Guard_getattro,
    .tp_descr_get = Guard_descr_get,
    .tp_vectorcall_offset = offsetof(GuardObject, vectorcall),
};

/* ------------------------------------------------------------------ */
/* StrictGuardObject                                                   */
/* ------------------------------------------------------------------ */

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

static int StrictGuard_clear(StrictGuardObject *self) {
    Py_CLEAR(self->func);
    Py_CLEAR(self->func_code);
    Py_CLEAR(self->pos_rules);
    Py_CLEAR(self->kw_rules);
    Py_CLEAR(self->ret_rule);
    Py_CLEAR(self->ret_name);
    return 0;
}

static void StrictGuard_dealloc(StrictGuardObject *self) {
    StrictGuard_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/*
 * Profiling hook installed via PyEval_SetProfile. On PyTrace_RETURN of the
 * wrapped function, verifies every declared local is initialized and of the
 * declared type. Returns 0 on success, -1 on failure (aborts the function).
 */
static int strict_trace_func(PyObject *obj, PyFrameObject *frame, int what, PyObject *arg) {
    if (what != PyTrace_RETURN) return 0;

    StrictGuardObject *self = (StrictGuardObject *)obj;
    PyCodeObject *f_code = PyFrame_GetCode(frame);
    int is_target = ((PyObject *)f_code == self->func_code);
    Py_DECREF(f_code);

    if (!is_target) return 0;

    PyObject *locals = PyFrame_GetLocals(frame);
    if (!locals) return 0;

    PyObject *key, *rule_def;
    Py_ssize_t pos = 0;

    while (PyDict_Next(self->kw_rules, &pos, &key, &rule_def)) {
        /* PyObject_GetItem respects modern Python proxy mappings while
         * retaining the O(1) string lookup performance win. */
        PyObject *val = PyObject_GetItem(locals, key);

        if (val) {
            PyObject *rule = PyTuple_GET_ITEM(rule_def, 2);
            if (unlikely(!fast_check_type(val, rule))) {
                raise_type_error(key, PyTuple_GET_ITEM(rule_def, 1), val);
                Py_DECREF(val);
                Py_DECREF(locals);
                return -1;
            }
            Py_DECREF(val);
        } else {
            /* Declared but never initialized before return. */
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

static PyObject *StrictGuard_getattro(PyObject *self, PyObject *name) {
    PyObject *res = PyObject_GenericGetAttr(self, name);
    if (res != NULL) return res;
    PyErr_Clear();
    return PyObject_GetAttr(((StrictGuardObject *)self)->func, name);
}

static PyObject *StrictGuard_vectorcall(PyObject *self_obj, PyObject *const *args,
                                        size_t nargsf, PyObject *kwnames) {
    StrictGuardObject *self = (StrictGuardObject *)self_obj;

    PyEval_SetProfile(strict_trace_func, self_obj);

    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t n_pos = PyTuple_GET_SIZE(self->pos_rules);
    for (Py_ssize_t i = 0; i < nargs && i < n_pos; i++) {
        PyObject *rule_def = PyTuple_GET_ITEM(self->pos_rules, i);
        if (rule_def != Py_None) {
            if (unlikely(!fast_check_type(args[i], PyTuple_GET_ITEM(rule_def, 2)))) {
                PyEval_SetProfile(NULL, NULL);
                raise_type_error(PyTuple_GET_ITEM(rule_def, 0),
                                 PyTuple_GET_ITEM(rule_def, 1), args[i]);
                return NULL;
            }
        }
    }

    if (unlikely(kwnames != NULL)) {
        Py_ssize_t nkwargs = PyTuple_GET_SIZE(kwnames);
        for (Py_ssize_t i = 0; i < nkwargs; i++) {
            PyObject *kw = PyTuple_GET_ITEM(kwnames, i);
            PyObject *rule_def = PyDict_GetItemWithError(self->kw_rules, kw);
            if (rule_def == NULL) {
                if (PyErr_Occurred()) PyErr_Clear();
                continue;
            }
            if (unlikely(!fast_check_type(args[nargs + i], PyTuple_GET_ITEM(rule_def, 2)))) {
                PyEval_SetProfile(NULL, NULL);
                raise_type_error(kw, PyTuple_GET_ITEM(rule_def, 1), args[nargs + i]);
                return NULL;
            }
        }
    }

    PyObject *result = PyObject_Vectorcall(self->func, args, nargsf, kwnames);
    PyEval_SetProfile(NULL, NULL);

    if (result && self->check_return && self->ret_rule != Py_None) {
        if (unlikely(!fast_check_type(result, self->ret_rule))) {
            raise_type_error(PyUnicode_InternFromString("return"), self->ret_name, result);
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyTypeObject StrictGuardType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.StrictGuard",
    .tp_basicsize = sizeof(StrictGuardObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
    .tp_dealloc = (destructor)StrictGuard_dealloc,
    .tp_call = PyVectorcall_Call,
    .tp_getattro = StrictGuard_getattro,
    .tp_descr_get = Guard_descr_get,
    .tp_vectorcall_offset = offsetof(StrictGuardObject, vectorcall),
};

/* ------------------------------------------------------------------ */
/* Factory functions                                                   */
/* ------------------------------------------------------------------ */

PyObject *make_guard(PyObject *module, PyObject *args) {
    PyObject *func, *pos_rules, *kw_rules, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &pos_rules, &kw_rules,
                          &ret_rule, &ret_name, &check_return)) return NULL;

    GuardObject *guard = PyObject_New(GuardObject, &GuardType);
    Py_INCREF(func); Py_INCREF(pos_rules); Py_INCREF(kw_rules);
    Py_INCREF(ret_rule); Py_INCREF(ret_name);
    guard->vectorcall = Guard_vectorcall;
    guard->func = func;
    guard->pos_rules = pos_rules;
    guard->kw_rules = kw_rules;
    guard->ret_rule = ret_rule;
    guard->ret_name = ret_name;
    guard->check_return = check_return;
    return (PyObject *)guard;
}

PyObject *make_strictguard(PyObject *module, PyObject *args) {
    PyObject *func, *pos_rules, *kw_rules, *ret_rule, *ret_name;
    int check_return;
    if (!PyArg_ParseTuple(args, "OOOOOp", &func, &pos_rules, &kw_rules,
                          &ret_rule, &ret_name, &check_return)) return NULL;

    StrictGuardObject *guard = PyObject_New(StrictGuardObject, &StrictGuardType);
    Py_INCREF(func); Py_INCREF(pos_rules); Py_INCREF(kw_rules);
    Py_INCREF(ret_rule); Py_INCREF(ret_name);
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
