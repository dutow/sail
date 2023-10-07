/*  This file is part of SAIL (https://github.com/HappySeaFox/sail)

    Copyright (c) 2023 Dmitry Baryshev

    The MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef SAIL_JPEGXL_HELPERS_H
#define SAIL_JPEGXL_HELPERS_H

#include <stdint.h>

#include <jxl/types.h>

#include "common.h"
#include "error.h"
#include "export.h"

SAIL_HIDDEN enum SailPixelFormat jpegxl_private_sail_pixel_format(uint32_t bits_per_sample, uint32_t num_color_channels, uint32_t alpha_bits);

SAIL_HIDDEN unsigned jpegxl_private_sail_pixel_format_to_num_channels(enum SailPixelFormat pixel_format);

SAIL_HIDDEN JxlDataType jpegxl_private_sail_pixel_format_to_jxl_data_type(enum SailPixelFormat pixel_format);

#endif
