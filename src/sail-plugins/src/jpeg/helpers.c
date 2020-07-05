/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2020 Dmitry Baryshev

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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sail-common.h"

#include "helpers.h"

void my_output_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);

    SAIL_LOG_ERROR("JPEG: %s", buffer);
}

void my_error_exit(j_common_ptr cinfo) {
    struct my_error_context *myerr = (struct my_error_context *)cinfo->err;

    (*cinfo->err->output_message)(cinfo);

    longjmp(myerr->setjmp_buffer, 1);
}

enum SailPixelFormat color_space_to_pixel_format(J_COLOR_SPACE color_space) {
    switch (color_space) {
        case JCS_GRAYSCALE: return SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE;

        case JCS_RGB565:    return SAIL_PIXEL_FORMAT_BPP16_RGB565;

        case JCS_EXT_RGB:
        case JCS_RGB:       return SAIL_PIXEL_FORMAT_BPP24_RGB;
        case JCS_EXT_BGR:   return SAIL_PIXEL_FORMAT_BPP24_BGR;

        case JCS_EXT_RGBA:  return SAIL_PIXEL_FORMAT_BPP32_RGBA;
        case JCS_EXT_BGRA:  return SAIL_PIXEL_FORMAT_BPP32_BGRA;
        case JCS_EXT_ABGR:  return SAIL_PIXEL_FORMAT_BPP32_ABGR;
        case JCS_EXT_ARGB:  return SAIL_PIXEL_FORMAT_BPP32_ARGB;

        case JCS_YCbCr:     return SAIL_PIXEL_FORMAT_BPP24_YCBCR;
        case JCS_CMYK:      return SAIL_PIXEL_FORMAT_BPP32_CMYK;
        case JCS_YCCK:      return SAIL_PIXEL_FORMAT_BPP32_YCCK;

        default:            return SAIL_PIXEL_FORMAT_UNKNOWN;
    }
}

J_COLOR_SPACE pixel_format_to_color_space(int pixel_format) {
    switch (pixel_format) {
        case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE:  return JCS_GRAYSCALE;

        case SAIL_PIXEL_FORMAT_BPP16_RGB565:    return JCS_RGB565;

        case SAIL_PIXEL_FORMAT_BPP24_RGB:       return JCS_RGB;
        case SAIL_PIXEL_FORMAT_BPP24_BGR:       return JCS_EXT_BGR;

        case SAIL_PIXEL_FORMAT_BPP32_RGBA:      return JCS_EXT_RGBA;
        case SAIL_PIXEL_FORMAT_BPP32_BGRA:      return JCS_EXT_BGRA;
        case SAIL_PIXEL_FORMAT_BPP32_ABGR:      return JCS_EXT_ABGR;
        case SAIL_PIXEL_FORMAT_BPP32_ARGB:      return JCS_EXT_ARGB;

        case SAIL_PIXEL_FORMAT_BPP24_YCBCR:     return JCS_YCbCr;
        case SAIL_PIXEL_FORMAT_BPP32_CMYK:      return JCS_CMYK;
        case SAIL_PIXEL_FORMAT_BPP32_YCCK:      return JCS_YCCK;

        default:                                return JCS_UNKNOWN;
    }
}

bool jpeg_supported_pixel_format(int pixel_format) {
    switch (pixel_format) {
        case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE:
        case SAIL_PIXEL_FORMAT_BPP24_RGB:
        case SAIL_PIXEL_FORMAT_BPP24_YCBCR:
        case SAIL_PIXEL_FORMAT_BPP32_CMYK:
        case SAIL_PIXEL_FORMAT_BPP32_YCCK: return true;

        default:                           return false;
    }
}

sail_error_t convert_cmyk(unsigned char *bits_source, unsigned char *bits_target, unsigned width, int target_pixel_format) {
    unsigned char C, M, Y, K;

    if (target_pixel_format == SAIL_PIXEL_FORMAT_BPP24_RGB) {
        for (unsigned i = 0; i < width; i++) {
            C = (unsigned char)(*bits_source++ / 100.0);
            M = (unsigned char)(*bits_source++ / 100.0);
            Y = (unsigned char)(*bits_source++ / 100.0);
            K = (unsigned char)(*bits_source++ / 100.0);

            *bits_target++ = 255 * (1-C) * (1-K);
            *bits_target++ = 255 * (1-M) * (1-K);
            *bits_target++ = 255 * (1-Y) * (1-K);
        }
    } else if (target_pixel_format == SAIL_PIXEL_FORMAT_BPP32_RGBA) {
        for (unsigned i = 0; i < width; i++) {
            C = (unsigned char)(*bits_source++ / 100.0);
            M = (unsigned char)(*bits_source++ / 100.0);
            Y = (unsigned char)(*bits_source++ / 100.0);
            K = (unsigned char)(*bits_source++ / 100.0);

            *bits_target++ = 255 * (1-C) * (1-K);
            *bits_target++ = 255 * (1-M) * (1-K);
            *bits_target++ = 255 * (1-Y) * (1-K);
            *bits_target++ = 255;
        }
    } else {
        return SAIL_UNSUPPORTED_PIXEL_FORMAT;
    }

    return 0;
}
