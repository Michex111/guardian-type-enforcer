/*
 * access_control.c
 * ----------------
 * Frame-walking logic used to determine whether a mutation of a protected /
 * private attribute originates from inside the owning class/instance.
 *
 * Both `ShieldBase` (instance attributes) and `ShieldMeta` (class attributes)
 * rely on this single implementation.
 */
#include "guardian_common.h"

/*
 * Walk the current Python execution frames and check whether `self` (either
 * an instance or a class object) is reachable as `self` or `cls` in any
 * enclosing frame. If so, the access is considered internal.
 *
 * Returns 1 for internal access, 0 for external access.
 */
int check_internal_access(PyObject *self) {
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
            /* 1. Instance methods expose the receiver as `self`. */
            PyObject *frame_self = PyMapping_GetItemString(locals, "self");
            if (frame_self) {
                if (frame_self == self) is_internal = 1;
                Py_DECREF(frame_self);
            } else {
                PyErr_Clear();
            }

            /* 2. Class methods expose the class as `cls`. */
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
