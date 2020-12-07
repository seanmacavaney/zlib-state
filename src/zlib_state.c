#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "structmember.h"

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include "C:/Program Files/zlib/include/zlib.h"
#else
#include <zlib.h>
#endif

#define BUFFER_SIZE (32*1024)


static void*
PyZlib_Malloc(voidpf ctx, uInt items, uInt size)
{
    if (size != 0 && items > (size_t)PY_SSIZE_T_MAX / size)
        return NULL;
    /* PyMem_Malloc() cannot be used: the GIL is not held when
       inflate() and deflate() are called */
    return PyMem_RawMalloc((size_t)items * (size_t)size);
}

static void
PyZlib_Free(voidpf ctx, void *ptr)
{
    PyMem_RawFree(ptr);
}

#define MY_MIN(a,b) a<b?a:b

typedef struct {
    PyObject_HEAD
    Bytef* inbuf;
    Bytef* outbuf;
    z_stream zst;
    int _eof;
    int out_pivot;
} DecompressorObject;

long needs_input(DecompressorObject *self) {
    if (self->_eof) {
        return 0;
    }
    long avail_in = self->zst.avail_in;
    if (avail_in == 0) {
        return BUFFER_SIZE;
    }
    return 0;
}

long block_boundary(DecompressorObject *self) {
    return self->zst.data_type & 128;
}

static void
Decompressor_dealloc(DecompressorObject *self)
{
    if (self->inbuf != NULL) {
        PyMem_RawFree(self->inbuf);
        self->inbuf = NULL;
    }
    if (self->outbuf != NULL) {
        PyMem_RawFree(self->outbuf);
        self->outbuf = NULL;
    }
    // TODO: anything to clean up with zst?
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject *
Decompressor_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    DecompressorObject *self;
    self = (DecompressorObject *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->inbuf = (Bytef *) malloc(BUFFER_SIZE);;
        self->outbuf = (Bytef *) malloc(BUFFER_SIZE);
        self->out_pivot = 0;
        self->_eof = 0;
        self->zst.opaque = NULL;
        self->zst.zalloc = PyZlib_Malloc;
        self->zst.zfree = PyZlib_Free;
        self->zst.next_in = self->inbuf;
        self->zst.avail_in = 0;
        self->zst.next_out = self->outbuf;
        self->zst.avail_out = BUFFER_SIZE;
    }
    return (PyObject *) self;
}

static int
Decompressor_init(DecompressorObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"wbits", NULL};
    int wbits = MAX_WBITS;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i", kwlist, &wbits))
        return -1;

    int err = inflateInit2(&self->zst, wbits);
    switch (err) {
    case Z_OK:
        return 0;
    case Z_STREAM_ERROR:
        Py_DECREF(self);
        PyErr_SetString(PyExc_ValueError, "Invalid initialization option");
        return -1;
    case Z_MEM_ERROR:
        Py_DECREF(self);
        PyErr_SetString(PyExc_MemoryError, "Can't allocate memory for decompression object");
        return -1;
    default:
        PyErr_SetString(PyExc_RuntimeError, "zlib error while creating decompression object");
        Py_DECREF(self);
        return -1;
    }

    return 0;
}


static PyObject *
Decompressor_feed_input(DecompressorObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"inbytes", NULL};
    PyObject *inbytes = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, &inbytes))
        return NULL;

    long max_size = needs_input(self);

    if (max_size == 0) {
        PyErr_SetString(PyExc_RuntimeError, "cannot set inbuff now; either EOF or must finish consuming data");
        return NULL;
    }

    Py_buffer inbytes_buf;
    if (PyObject_GetBuffer(inbytes, &inbytes_buf, PyBUF_SIMPLE) == -1) {
        PyErr_SetString(PyExc_ValueError, "inbytes must be buffer type");
        return NULL;
    }

    if (inbytes_buf.len > max_size) {
        PyErr_SetString(PyExc_OverflowError, "inbytes too long; check needs_input for max size");
        PyBuffer_Release(&inbytes_buf);
        return NULL;
    }

    memcpy(self->inbuf, inbytes_buf.buf, inbytes_buf.len);
    self->zst.next_in = self->inbuf;
    self->zst.avail_in = inbytes_buf.len;
    PyBuffer_Release(&inbytes_buf);

    return PyLong_FromLong(self->zst.avail_in);
}

static PyObject *
Decompressor_read(DecompressorObject *self, PyObject *args, PyObject *kwds) {
    PyObject *result = NULL;
    int err;
    static char *kwlist[] = {"oucount", "outbytes", NULL};
    PyObject *outbytes = NULL;
    int oucount = -1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iO", kwlist, &oucount, &outbytes))
        return NULL;

    int count;
    int start_avail;

    if (outbytes != NULL) {
        Py_buffer outbytes_buf;
        if (PyObject_GetBuffer(outbytes, &outbytes_buf, PyBUF_SIMPLE) == -1) {
            PyErr_SetString(PyExc_ValueError, "outbytes must be buffer type");
            return NULL;
        }
        self->zst.avail_out = MY_MIN(self->zst.avail_out, outbytes_buf.len);
        if (oucount > 0) {
            self->zst.avail_out = MY_MIN(self->zst.avail_out, oucount);
        }
        start_avail = self->zst.avail_out;
        // Py_BEGIN_ALLOW_THREADS
        err = inflate(&self->zst, Z_BLOCK);
        // Py_END_ALLOW_THREADS
        count = start_avail - self->zst.avail_out;
        memcpy(outbytes_buf.buf, self->outbuf + self->out_pivot, count);
        PyBuffer_Release(&outbytes_buf);
        result = PyLong_FromLong(count);
    }
    else {
        if (oucount > 0) {
            self->zst.avail_out = MY_MIN(self->zst.avail_out, oucount);
        }
        start_avail = self->zst.avail_out;
        // Py_BEGIN_ALLOW_THREADS
        err = inflate(&self->zst, Z_BLOCK);
        // Py_END_ALLOW_THREADS
        count = start_avail - self->zst.avail_out;
        result = PyBytes_FromStringAndSize((char*)self->outbuf + self->out_pivot, count);
    }

    self->out_pivot += count;
    if (self->out_pivot >= BUFFER_SIZE) {
        self->out_pivot = 0;
    }
    self->zst.next_out = self->outbuf + self->out_pivot;
    self->zst.avail_out = BUFFER_SIZE - self->out_pivot;

    switch (err) {
    case Z_NEED_DICT:
        PyErr_SetString(PyExc_RuntimeError, "zlib Z_NEED_DICT");
        return NULL;
    case Z_DATA_ERROR:
        PyErr_SetString(PyExc_RuntimeError, self->zst.msg);
        return NULL;
    case Z_STREAM_ERROR:
        PyErr_SetString(PyExc_RuntimeError, "zlib Z_STREAM_ERROR");
        return NULL;
    case Z_MEM_ERROR:
        PyErr_SetString(PyExc_RuntimeError, "zlib Z_MEM_ERROR");
        return NULL;
    case Z_BUF_ERROR:
        PyErr_SetString(PyExc_RuntimeError, "zlib Z_BUF_ERROR");
        return NULL;
    // case Z_OK:
    case Z_STREAM_END:
        self->_eof = 1;
    }

    return result;
}

static PyObject *
Decompressor_block_boundary(DecompressorObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(block_boundary(self));
}

static PyObject *
Decompressor_get_state(DecompressorObject *self, PyObject *Py_UNUSED(ignored))
{
    if (!block_boundary(self) || self->zst.avail_in == BUFFER_SIZE) {
        PyErr_SetString(PyExc_RuntimeError, "You can only get_state on block_boundary and before feed_input");
        return NULL;
    }

    PyObject *dict = PyBytes_FromStringAndSize((char*)self->outbuf + self->out_pivot, BUFFER_SIZE - self->out_pivot);
    PyObject *second_part = PyBytes_FromStringAndSize((char*)self->outbuf, self->out_pivot);

    PyBytes_ConcatAndDel(&dict, second_part);

    PyObject *bits = PyLong_FromLong(self->zst.data_type & 7);

    PyObject *last_byte = PyLong_FromLong(self->inbuf[BUFFER_SIZE - self->zst.avail_in - 1]);

    PyObject *result = PyTuple_Pack(3, dict, bits, last_byte);

    Py_DECREF(dict);
    Py_DECREF(bits);
    Py_DECREF(last_byte);

    return result;
}

static PyObject *
Decompressor_set_state(DecompressorObject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"zdict", "bits", "last_byte", NULL};
    PyObject *zdict = NULL;
    unsigned char bits, last_byte;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OBB", kwlist, &zdict, &bits, &last_byte)) {
        return NULL;
    }

    if (inflatePrime(&self->zst, bits, last_byte >> (8 - bits)) != Z_OK) {
        PyErr_SetString(PyExc_RuntimeError, "Priming error");
        return NULL;
    }

    Py_buffer zdict_buf;
    if (PyObject_GetBuffer(zdict, &zdict_buf, PyBUF_SIMPLE) == -1) {
        PyErr_SetString(PyExc_OverflowError,
                        "zdict must implement the buffer protocol");
        return NULL;
    }
    if ((size_t)zdict_buf.len > UINT_MAX) {
        PyErr_SetString(PyExc_OverflowError,
                        "zdict length does not fit in an unsigned int");
        PyBuffer_Release(&zdict_buf);
        return NULL;
    }
    int err;
    err = inflateSetDictionary(&self->zst,
                               zdict_buf.buf, (unsigned int)zdict_buf.len);
    PyBuffer_Release(&zdict_buf);
    switch (err) {
    case Z_STREAM_ERROR:
        PyErr_SetString(PyExc_RuntimeError, "Z_STREAM_ERROR");
        Py_DECREF(self);
        return NULL;
    case Z_DATA_ERROR:
        PyErr_SetString(PyExc_RuntimeError, "Z_DATA_ERROR");
        Py_DECREF(self);
        return NULL;
    }

    return PyBool_FromLong(1);
}

static PyObject *
Decompressor_total_in(DecompressorObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromLong(self->zst.total_in);
}

static PyObject *
Decompressor_eof(DecompressorObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyBool_FromLong(self->_eof);
}

static PyObject *
Decompressor_needs_input(DecompressorObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromLong(needs_input(self));
}

static PyMethodDef Decompressor_methods[] = {
    {"needs_input", (PyCFunction) Decompressor_needs_input, METH_NOARGS, "Does the decompressor need data? How much is recommended?"},
    {"feed_input", (PyCFunction) Decompressor_feed_input, METH_VARARGS | METH_KEYWORDS, "Feed input data"},
    {"read", (PyCFunction) Decompressor_read, METH_VARARGS | METH_KEYWORDS, "Read segment of data. Will always stop at block boundaries."},
    {"block_boundary", (PyCFunction) Decompressor_block_boundary, METH_NOARGS, "is it at the block boundary?"},
    {"get_state", (PyCFunction) Decompressor_get_state, METH_NOARGS, "gets the state of the compressor, i.e., all that it would need to resume the file"},
    {"set_state", (PyCFunction) Decompressor_set_state, METH_VARARGS | METH_KEYWORDS, "sets the state of the decompressor"},
    {"total_in", (PyCFunction) Decompressor_total_in, METH_NOARGS, "Total bytes read"},
    {"eof", (PyCFunction) Decompressor_eof, METH_NOARGS, "Has reached the end of the stream"},
    {NULL}  /* Sentinel */
};

static PyTypeObject DecompressorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_zlib_state.Decompressor",
    .tp_doc = "Decompressor",
    .tp_basicsize = sizeof(DecompressorObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Decompressor_new,
    .tp_init = (initproc) Decompressor_init,
    .tp_dealloc = (destructor) Decompressor_dealloc,
    .tp_methods = Decompressor_methods,
};

static PyModuleDef zlib_state_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_zlib_state",
    .m_doc = "c extensions for zlib_state",
    .m_size = -1,
};

PyMODINIT_FUNC
PyInit__zlib_state(void)
{
    PyObject *m;
    if (PyType_Ready(&DecompressorType) < 0)
        return NULL;

    m = PyModule_Create(&zlib_state_module);
    if (m == NULL)
        return NULL;

    Py_INCREF(&DecompressorType);
    if (PyModule_AddObject(m, "Decompressor", (PyObject *) &DecompressorType) < 0) {
        Py_DECREF(&DecompressorType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
