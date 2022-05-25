#include <Python.h>
#include <structmember.h>
#include "DynamsoftCommon.h"
#include "DynamsoftBarcodeReader.h"
#include "barcode_result.h"

#define DEBUG 0

typedef struct
{
    PyObject_HEAD
    // Barcode reader handler
    void *hBarcode;
    // Callback function for video mode
    PyObject *py_cb_textResult;
    PyObject *py_cb_intermediateResult;
    PyObject *py_cb_errorCode;
    PyObject *py_UserData;
    IntermediateResultArray * pInnerIntermediateResults;
} DynamsoftBarcodeReader;

static int DynamsoftBarcodeReader_clear(DynamsoftBarcodeReader *self)
{
    if (self->hBarcode) Py_XDECREF(self->py_cb_errorCode);
    if (self->py_cb_intermediateResult) Py_XDECREF(self->py_cb_intermediateResult);
    if (self->py_cb_textResult) Py_XDECREF(self->py_cb_textResult);
    if (self->pInnerIntermediateResults) DBR_FreeIntermediateResults(&self->pInnerIntermediateResults);
    if(self->hBarcode) {
		DBR_DestroyInstance(self->hBarcode);
    	self->hBarcode = NULL;
	}
    return 0;
}

static void DynamsoftBarcodeReader_dealloc(DynamsoftBarcodeReader *self)
{
	DynamsoftBarcodeReader_clear(self);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *DynamsoftBarcodeReader_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    DynamsoftBarcodeReader *self;

    self = (DynamsoftBarcodeReader *)type->tp_alloc(type, 0);
    if (self != NULL)
    {
       	self->hBarcode = DBR_CreateInstance();
        self->pInnerIntermediateResults = NULL;
        self->py_cb_errorCode = NULL;
        self->py_cb_intermediateResult = NULL;
        self->py_cb_textResult = NULL;
    }

    return (PyObject *)self;
}

static PyObject *createPyResults(TextResultArray *pResults)
{
    if (!pResults)
    {
        printf("No barcode detected\n");
        return NULL;
    }
    // Get barcode results
    int count = pResults->resultsCount;

    // Create a Python object to store results
    PyObject *list = PyList_New(count);
    // printf("count: %d\n", count);
    PyObject *result = NULL;
    int i = 0;
	PyObject *pyObject = NULL;
    for (; i < count; i++)
    {
        LocalizationResult *pLocalizationResult = pResults->results[i]->localizationResult;
        int x1 = pLocalizationResult->x1;
        int y1 = pLocalizationResult->y1;
        int x2 = pLocalizationResult->x2;
        int y2 = pLocalizationResult->y2;
        int x3 = pLocalizationResult->x3;
        int y3 = pLocalizationResult->y3;
        int x4 = pLocalizationResult->x4;
        int y4 = pLocalizationResult->y4;
		BarcodeResult* result = PyObject_New(BarcodeResult, &BarcodeResultType);
		result->format = PyUnicode_FromString(pResults->results[i]->barcodeFormatString);
		result->text = PyUnicode_FromString(pResults->results[i]->barcodeText);
		result->x1 = Py_BuildValue("i", x1);
		result->y1 = Py_BuildValue("i", y1);
		result->x2 = Py_BuildValue("i", x2);
		result->y2 = Py_BuildValue("i", y2);
		result->x3 = Py_BuildValue("i", x3);
		result->y3 = Py_BuildValue("i", y3);
		result->x4 = Py_BuildValue("i", x4);
		result->y4 = Py_BuildValue("i", y4);

		// convert BarcodeResult to PyObject
		// pyObject = PyCapsule_New(result, NULL, NULL);
		// pyObject = PyObject_CallObject((PyObject*)&BarcodeResultType, result);
        PyList_SetItem(list, i, (PyObject *)result);

        // Print out PyObject if needed
        if (DEBUG)
        {
			PyObject *objectsRepresentation = PyObject_Repr(list);
            const char *s = PyUnicode_AsUTF8(objectsRepresentation);
            printf("Results: %s\n", s);
        }
    }

    // Release memory
    DBR_FreeTextResults(&pResults);

    return list;
}


/**
 * Decode barcode and QR code from image files. 
 * 
 * @param file name
 * 
 * @return BarcodeResult list
 */
static PyObject *decodeFile(PyObject *obj, PyObject *args)
{
    DynamsoftBarcodeReader *self = (DynamsoftBarcodeReader *)obj;

    char *pFileName; // File name
    char *encoding = NULL;
    if (!PyArg_ParseTuple(args, "s", &pFileName))
    {
        return NULL;
    }

    TextResultArray *pResults = NULL;

    // Barcode detection
    int ret = DBR_DecodeFile(self->hBarcode, pFileName, "");
    if (ret)
    {
        printf("Detection error: %s\n", DBR_GetErrorString(ret));
    }
    DBR_GetAllTextResults(self->hBarcode, &pResults);

    // Wrap results
    PyObject *list = createPyResults(pResults);
    return list;
}

/**
 * Decode barcode and QR code from OpenCV Mat. 
 * 
 * @param Mat image
 * 
 * @return BarcodeResult list
 */
static PyObject *decodeMat(PyObject *obj, PyObject *args)
{
    DynamsoftBarcodeReader *self = (DynamsoftBarcodeReader *)obj;

    PyObject *o;
    int iFormat;
    char *templateName = NULL;
    char *encoding = NULL;
    if (!PyArg_ParseTuple(args, "O", &o))
        return NULL;

    Py_buffer *view;
    int nd;
    PyObject *memoryview = PyMemoryView_FromObject(o);
    if (memoryview == NULL)
    {
        PyErr_Clear();
        return NULL;
    }

    view = PyMemoryView_GET_BUFFER(memoryview);
    char *buffer = (char *)view->buf;
    nd = view->ndim;
    int len = view->len;
    int stride = view->strides[0];
    int width = view->strides[0] / view->strides[1];
    int height = len / stride;
    Py_DECREF(memoryview);

    // Detect barcodes
    ImagePixelFormat format = IPF_RGB_888;

    if (width == stride)
    {
        format = IPF_GRAYSCALED;
    }
    else if (width * 3 == stride)
    {
        format = IPF_RGB_888;
    }
    else if (width * 4 == stride)
    {
        format = IPF_ARGB_8888;
    }

    PyObject *list = NULL;
    int ret = DBR_DecodeBuffer(self->hBarcode, (const unsigned char*)buffer, width, height, stride, format, "");
    if (ret)
    {
        printf("Detection error: %s\n", DBR_GetErrorString(ret));
    }
    // Wrap results
    TextResultArray *pResults = NULL;
    DBR_GetAllTextResults(self->hBarcode, &pResults);
    list = createPyResults(pResults);

    Py_DECREF(memoryview);

    return list;
}

/**
 * Get runtime settings.
 *
 * @return Return stringified JSON object.
 */
static PyObject *getParameters(PyObject *obj, PyObject *args)
{
    DynamsoftBarcodeReader *self = (DynamsoftBarcodeReader *)obj;

    char * pContent = NULL;

    int ret = DBR_OutputSettingsToStringPtr(self->hBarcode, &pContent, "CurrentRuntimeSettings");
    PyObject * content = Py_BuildValue("s", pContent);
    DBR_FreeSettingsString(&pContent);

    return content;
}

/**
 * Set runtime settings with JSON object.
 *
 * @param json string: the stringified JSON object.
 * 
 * @return Return 0 if the function operates successfully.
 */
static PyObject *setParameters(PyObject *obj, PyObject *args)
{
    DynamsoftBarcodeReader *self = (DynamsoftBarcodeReader *)obj;

    char *json;
    if (!PyArg_ParseTuple(args, "s", &json))
    {
        Py_RETURN_NONE;
    }

    char errorMessage[512];
    int ret = DBR_InitRuntimeSettingsWithString(self->hBarcode, json, CM_OVERWRITE, errorMessage, 256);
    if (ret) 
    {
        printf("Returned value: %d, error message: %s\n", ret, errorMessage);
        PyErr_SetString(PyExc_TypeError, "DBR_InitRuntimeSettingsWithString() failed");
    }
    return Py_BuildValue("i", ret);
}

static PyMethodDef instance_methods[] = {
  {"decodeFile", decodeFile, METH_VARARGS, NULL},
  {"decodeMat", decodeMat, METH_VARARGS, NULL},
  {"setParameters", setParameters, METH_VARARGS, NULL},
  {"getParameters", getParameters, METH_VARARGS, NULL},
  {NULL, NULL, 0, NULL}       
};

static PyTypeObject DynamsoftBarcodeReaderType = {
    PyVarObject_HEAD_INIT(NULL, 0) "barcodeQrSDK.DynamsoftBarcodeReader", /* tp_name */
    sizeof(DynamsoftBarcodeReader),                              /* tp_basicsize */
    0,                                                           /* tp_itemsize */
    (destructor)DynamsoftBarcodeReader_dealloc,                  /* tp_dealloc */
    0,                                                           /* tp_print */
    0,                                                           /* tp_getattr */
    0,                                                           /* tp_setattr */
    0,                                                           /* tp_reserved */
    0,                                                           /* tp_repr */
    0,                                                           /* tp_as_number */
    0,                                                           /* tp_as_sequence */
    0,                                                           /* tp_as_mapping */
    0,                                                           /* tp_hash  */
    0,                                                           /* tp_call */
    0,                                                           /* tp_str */
    0,                                                           /* tp_getattro */
    0,                                                           /* tp_setattro */
    0,                                                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,                    /*tp_flags*/
    "DynamsoftBarcodeReader",                          /* tp_doc */
    0,                                                           /* tp_traverse */
    0,                                                           /* tp_clear */
    0,                                                           /* tp_richcompare */
    0,                                                           /* tp_weaklistoffset */
    0,                                                           /* tp_iter */
    0,                                                           /* tp_iternext */
    instance_methods,                                                 /* tp_methods */
    0,                                                 /* tp_members */
    0,                                                           /* tp_getset */
    0,                                                           /* tp_base */
    0,                                                           /* tp_dict */
    0,                                                           /* tp_descr_get */
    0,                                                           /* tp_descr_set */
    0,                                                           /* tp_dictoffset */
    0,                       /* tp_init */
    0,                                                           /* tp_alloc */
    DynamsoftBarcodeReader_new,                                  /* tp_new */
};