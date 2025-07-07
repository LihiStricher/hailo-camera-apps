/*
 * Copyright (c) 2017-2025 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#ifdef HAVE_PERFETTO

#include <hailo_perfetto.h>

#define REFERENCE_CAMERA_CATEGORY "reference_camera"

PERFETTO_DEFINE_CATEGORIES(perfetto::Category(REFERENCE_CAMERA_CATEGORY)
                               .SetTags("hailo")
                               .SetDescription("Events from reference camera infrastructure"));

#define REFERENCE_CAMERA_TRACE_EVENT(...) TRACE_EVENT(REFERENCE_CAMERA_CATEGORY, ##__VA_ARGS__)

#define REFERENCE_CAMERA_TRACE_COUNTER(...) TRACE_COUNTER(REFERENCE_CAMERA_CATEGORY, ##__VA_ARGS__)

#else // no HAVE_PERFETTO

/* assert either HAVE_PERFETTO or PERFETTO_NOT_FOUND is defined to avoid meson bugs */
#ifndef PERFETTO_NOT_FOUND
#warning "Perfetto define not found - probably meson target is missing common_args"
#endif // no PERFETTO_NOT_FOUND

/* no perfetto - empty macros */
#define REFERENCE_CAMERA_TRACE_EVENT(name, ...)

#define REFERENCE_CAMERA_TRACE_COUNTER(...)

#endif // HAVE_PERFETTO
