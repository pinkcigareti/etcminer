/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include "wraphelper.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Ugly hacks to avoid dependencies on the real nvml.h until it starts
 * getting included with the CUDA toolkit or a GDK that's got a known
 * install location, etc.
 */
typedef enum wrap_nvmlReturn_enum { WRAPNVML_SUCCESS = 0 } wrap_nvmlReturn_t;

typedef void* wrap_nvmlDevice_t;

/* our own version of the PCI info struct */
typedef struct {
    char bus_id_str[16]; /* string form of bus info */
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pci_device_id; /* combined device and vendor id */
    unsigned int pci_subsystem_id;
    unsigned int res0; /* NVML internal use only */
    unsigned int res1;
    unsigned int res2;
    unsigned int res3;
} wrap_nvmlPciInfo_t;

typedef enum {
    NVML_VALUE_TYPE_DOUBLE = 0,
    NVML_VALUE_TYPE_UNSIGNED_INT = 1,
    NVML_VALUE_TYPE_UNSIGNED_LONG = 2,
    NVML_VALUE_TYPE_UNSIGNED_LONG_LONG = 3,
    NVML_VALUE_TYPE_SIGNED_LONG_LONG = 4,
    NVML_VALUE_TYPE_COUNT
} wrap_nvmlValueType;

typedef union {
    double dVal;
    unsigned int uiVal;
    unsigned long ulVal;
    unsigned long long ullVal;
    signed long long sllVal;
} wrap_nvmlValue;

typedef struct {
    unsigned int fieldId;
    unsigned int scopeId;
    long long timestamp;
    long long latencyUsec;
    wrap_nvmlValueType valueType;
    int nvmlReturn;
    wrap_nvmlValue value;
} wrap_nvmlFieldValue;

/*
 * Handle to hold the function pointers for the entry points we need,
 * and the shared library itself.
 */
typedef struct {
    void* nvml_dll;
    int nvml_gpucount;
    unsigned int* nvml_pci_domain_id;
    unsigned int* nvml_pci_bus_id;
    unsigned int* nvml_pci_device_id;
    wrap_nvmlDevice_t* devs;
    wrap_nvmlReturn_t (*nvmlInit)(void);
    wrap_nvmlReturn_t (*nvmlDeviceGetCount)(int*);
    wrap_nvmlReturn_t (*nvmlDeviceGetHandleByIndex)(int, wrap_nvmlDevice_t*);
    wrap_nvmlReturn_t (*nvmlDeviceGetPciInfo)(wrap_nvmlDevice_t, wrap_nvmlPciInfo_t*);
    wrap_nvmlReturn_t (*nvmlDeviceGetName)(wrap_nvmlDevice_t, char*, int);
    wrap_nvmlReturn_t (*nvmlDeviceGetTemperature)(wrap_nvmlDevice_t, int, unsigned int*);
    wrap_nvmlReturn_t (*nvmlDeviceGetFanSpeed)(wrap_nvmlDevice_t, unsigned int*);
    wrap_nvmlReturn_t (*nvmlDeviceGetPowerUsage)(wrap_nvmlDevice_t, unsigned int*);
    wrap_nvmlReturn_t (*nvmlShutdown)(void);
    wrap_nvmlReturn_t (*nvmlDeviceGetFieldValues)(wrap_nvmlDevice_t, int, wrap_nvmlFieldValue*);
} wrap_nvml_handle;

wrap_nvml_handle* wrap_nvml_create();
int wrap_nvml_destroy(wrap_nvml_handle* nvmlh);

/*
 * Query the number of GPUs seen by NVML
 */
int wrap_nvml_get_gpucount(wrap_nvml_handle* nvmlh, int* gpucount);

/*
 * query the name of the GPU model from the CUDA device ID
 *
 */
int wrap_nvml_get_gpu_name(wrap_nvml_handle* nvmlh, int gpuindex, char* namebuf, int bufsize);

/*
 * Query the current GPU temperature (Celsius), from the CUDA device ID
 */
int wrap_nvml_get_tempC(wrap_nvml_handle* nvmlh, int gpuindex, unsigned int* tempC);

int wrap_nvml_get_mem_tempC(wrap_nvml_handle* nvmlh, int gpuindex, unsigned int* tempC);

/*
 * Query the current GPU fan speed (percent) from the CUDA device ID
 */
int wrap_nvml_get_fanpcnt(wrap_nvml_handle* nvmlh, int gpuindex, unsigned int* fanpcnt);

/*
 * Query the current GPU power usage in milliwatts from the CUDA device ID
 *
 * This feature is only available on recent GPU generations and may be
 * limited in some cases only to Tesla series GPUs.
 * If the query is run on an unsupported GPU, this routine will return -1.
 */
int wrap_nvml_get_power_usage(wrap_nvml_handle* nvmlh, int gpuindex, unsigned int* milliwatts);

#if defined(__cplusplus)
}
#endif
