/*
 * c_descriptor.c
 * --------------
 * A C-level data descriptor used for guarded dataclass fields. It stores the
 * value under a private name (e.g. `_field`) while exposing the public name,
 * enforcing the compiled type rule and (optionally) a custom validator on
 * every write.
 */
#include "guardian_common.h"

typedef struct {
    PyObject_HEAD
    PyObject *name;
    PyObject *private_name;
    PyObject *rule;
    PyObject *expected_name;
    PyObject *custom_validator;
} CFieldDescriptorObject;

static void CFieldDescriptor_dealloc(CFieldDescriptorObject *self) {
    Py_CLEAR(self->name);
    Py_CLEAR(self->private_name);
    Py_CLEAR(self->rule);
    Py_CLEAR(self->expected_name);
    Py_CLEAR(self->custom_validator);
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
        PyErr_Format(PyExc_AttributeError,
                     "Cannot delete guarded dataclass attribute '%U'", self->name);
        return -1;
    }

    /* 1. Fast-path type validation. */
    if (unlikely(!fast_check_type(value, self->rule))) {
        raise_type_error(self->name, self->expected_name, value);
        return -1;
    }

    /* 2. Optional user-supplied validator hook. */
    PyObject *final_value = value;
    if (self->custom_validator != Py_None) {
        PyObject *cls = (PyObject *)Py_TYPE(obj);
        PyObject *args = PyTuple_Pack(2, cls, value);
        if (args == NULL) return -1;
        final_value = PyObject_CallObject(self->custom_validator, args);
        Py_DECREF(args);

        if (final_value == NULL) return -1; /* exception raised in validator */
    } else {
        Py_INCREF(final_value);
    }

    /* 3. Store under the private name, bypassing this descriptor. */
    int res = PyObject_GenericSetAttr(obj, self->private_name, final_value);
    Py_DECREF(final_value);
    return res;
}

PyTypeObject CFieldDescriptorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "guardian._guardian_core.CFieldDescriptor",
    .tp_basicsize = sizeof(CFieldDescriptorObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)CFieldDescriptor_dealloc,
    .tp_descr_get = CFieldDescriptor_descr_get,
    .tp_descr_set = CFieldDescriptor_descr_set,
};

/* Factory function: exposed to Python as _guardian_core.make_c_descriptor. */
PyObject *make_c_descriptor(PyObject *module, PyObject *args) {
    PyObject *name, *private_name, *rule, *expected_name, *custom_val;
    if (!PyArg_ParseTuple(args, "OOOOO", &name, &private_name, &rule,
                          &expected_name, &custom_val)) return NULL;

    CFieldDescriptorObject *desc = PyObject_New(CFieldDescriptorObject, &CFieldDescriptorType);

    Py_INCREF(name);
    Py_INCREF(private_name);
    Py_INCREF(rule);
    Py_INCREF(expected_name);
    Py_INCREF(custom_val);

    desc->name = name;
    desc->private_name = private_name;
    desc->rule = rule;
    desc->expected_name = expected_name;
    desc->custom_validator = custom_val;

    return (PyObject *)desc;
}
