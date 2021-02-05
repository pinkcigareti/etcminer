/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <libdev/Log.h>

#if defined(_WIN32)
#include <windows.h>
void* wrap_dlopen(const char* filename);
void* wrap_dlsym(void* h, const char* sym);
int wrap_dlclose(void* h);
#else
/* assume we can use dlopen itself... */
#include <dlfcn.h>
void* wrap_dlopen(const char* filename);
void* wrap_dlsym(void* h, const char* sym);
int wrap_dlclose(void* h);
#endif
