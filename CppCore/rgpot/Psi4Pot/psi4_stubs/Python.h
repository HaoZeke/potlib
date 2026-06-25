#pragma once
// Minimal Python.h stub for compile-only Psi4 headers (no interpreter).
// Engine never calls Python; headers may include Python.h transitively.

#ifndef Py_PYTHON_H
#define Py_PYTHON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _object PyObject;
typedef struct _typeobject PyTypeObject;
typedef ssize_t Py_ssize_t;

#define Py_None ((PyObject *)0)
#define Py_True ((PyObject *)1)
#define Py_False ((PyObject *)2)

#define Py_INCREF(op) ((void)(op))
#define Py_DECREF(op) ((void)(op))
#define Py_XINCREF(op) ((void)(op))
#define Py_XDECREF(op) ((void)(op))

#ifdef __cplusplus
}
#endif

#endif /* Py_PYTHON_H */
