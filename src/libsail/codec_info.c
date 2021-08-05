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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sail-common.h"
#include "sail.h"

sail_status_t sail_codec_info_from_path(const char *path, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    const char *dot = strrchr(path, '.');

    if (dot == NULL || *dot == '\0' || *(dot+1) == '\0') {
        SAIL_LOG_AND_RETURN(SAIL_ERROR_INVALID_ARGUMENT);
    }

    SAIL_LOG_DEBUG("Finding codec info for path '%s'", path);

    SAIL_TRY(sail_codec_info_from_extension(dot+1, codec_info));

    return SAIL_OK;
}

sail_status_t sail_codec_info_by_magic_number_from_path(const char *path, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    struct sail_io *io;
    SAIL_TRY(alloc_io_read_file(path, &io));

    SAIL_TRY_OR_CLEANUP(sail_codec_info_by_magic_number_from_io(io, codec_info),
                        /* cleanup */ sail_destroy_io(io));

    sail_destroy_io(io);

    return SAIL_OK;
}

sail_status_t sail_codec_info_by_magic_number_from_mem(const void *buffer, size_t buffer_length, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_BUFFER_PTR(buffer);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    struct sail_io *io;
    SAIL_TRY(alloc_io_read_mem(buffer, buffer_length, &io));

    SAIL_TRY_OR_CLEANUP(sail_codec_info_by_magic_number_from_io(io, codec_info),
                        /* cleanup */ sail_destroy_io(io));

    sail_destroy_io(io);

    return SAIL_OK;
}

sail_status_t sail_codec_info_by_magic_number_from_io(struct sail_io *io, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_IO_PTR(io);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    struct sail_context *context;
    SAIL_TRY(current_tls_context(&context));

    /* Read the image magic. */
    unsigned char buffer[SAIL_MAGIC_BUFFER_SIZE];
    SAIL_TRY(io->strict_read(io->stream, buffer, sizeof(buffer)));

    /* Seek back. */
    SAIL_TRY(io->seek(io->stream, 0, SEEK_SET));

    /* Debug print. */
    {
        /* \xFF\xDD => "FF DD" + string terminator. */
        char hex_numbers[sizeof(buffer) * 3 + 1];
        char *hex_numbers_ptr = hex_numbers;

        for (size_t i = 0; i < sizeof(buffer); i++, hex_numbers_ptr += 3) {
#ifdef _MSC_VER
            sprintf_s(hex_numbers_ptr, 4, "%02x ", buffer[i]);
#else
            sprintf(hex_numbers_ptr, "%02x ", buffer[i]);
#endif
        }

        *(hex_numbers_ptr-1) = '\0';

        SAIL_LOG_DEBUG("Read magic number: '%s'", hex_numbers);
    }

    /* Find the codec info. */
    const struct sail_codec_info_node *codec_info_node = context->codec_info_node;

    while (codec_info_node != NULL) {
        const struct sail_string_node *magic_number_node = codec_info_node->codec_info->magic_number_node;

        /*
         * Split "ab cd" into bytes and compare individual bytes against the read magic number.
         * Additionally, we support "??" pattern matching any byte. For example, "?? ?? 66 74"
         * matches both "00 20 66 74" and "20 30 66 74".
         */
        while (magic_number_node != NULL) {
            size_t buffer_index = 0;
            const char *magic = magic_number_node->value;
            char hex_byte[3];
            int bytes_consumed = 0;
            bool mismatch = false;

            SAIL_LOG_TRACE("Check against %s magic '%s'", codec_info_node->codec_info->name, magic);

#ifdef _MSC_VER
            while (buffer_index < sizeof(buffer) && sscanf_s(magic, "%2s%n", hex_byte, (unsigned)sizeof(hex_byte), &bytes_consumed) == 1) {
#else
            while (buffer_index < sizeof(buffer) && sscanf(magic, "%2s%n", hex_byte, &bytes_consumed) == 1) {
#endif
                if (hex_byte[0] == '?') {
                    SAIL_LOG_TRACE("Skipping ? character");
                } else {
                    unsigned byte = 0;

#ifdef _MSC_VER
                    if (sscanf_s(hex_byte, "%02x", &byte) != 1 || byte != buffer[buffer_index]) {
#else
                    if (sscanf(hex_byte, "%02x", &byte) != 1 || byte != buffer[buffer_index]) {
#endif
                        SAIL_LOG_TRACE("Character mismatch %02x != %02x", buffer[buffer_index], byte);
                        mismatch = true;
                        break;
                    }
                }

                magic += bytes_consumed;
                buffer_index++;
            }

            if (mismatch) {
                magic_number_node = magic_number_node->next;
            } else {
                *codec_info = codec_info_node->codec_info;
                SAIL_LOG_DEBUG("Found codec info: %s", (*codec_info)->name);
                return SAIL_OK;
            }
        }

        codec_info_node = codec_info_node->next;
    }

    SAIL_LOG_AND_RETURN(SAIL_ERROR_CODEC_NOT_FOUND);
}

sail_status_t sail_codec_info_from_extension(const char *extension, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_EXTENSION_PTR(extension);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    SAIL_LOG_DEBUG("Finding codec info for extension '%s'", extension);

    struct sail_context *context;
    SAIL_TRY(current_tls_context(&context));

    char *extension_copy;
    SAIL_TRY(sail_strdup(extension, &extension_copy));

    /* Will compare in lower case. */
    sail_to_lower(extension_copy);

    const struct sail_codec_info_node *codec_info_node = context->codec_info_node;

    while (codec_info_node != NULL) {
        const struct sail_string_node *extension_node = codec_info_node->codec_info->extension_node;

        while (extension_node != NULL) {
            SAIL_LOG_TRACE("Check against %s extension '%s'", codec_info_node->codec_info->name, extension_node->value);

            if (strcmp(extension_node->value, extension_copy) == 0) {
                sail_free(extension_copy);
                *codec_info = codec_info_node->codec_info;
                SAIL_LOG_DEBUG("Found codec info: %s", (*codec_info)->name);
                return SAIL_OK;
            } else {
                SAIL_LOG_TRACE("Extension mismatch '%s' != '%s'", extension_copy, extension_node->value);
            }

            extension_node = extension_node->next;
        }

        codec_info_node = codec_info_node->next;
    }

    sail_free(extension_copy);
    SAIL_LOG_AND_RETURN(SAIL_ERROR_CODEC_NOT_FOUND);
}

sail_status_t sail_codec_info_from_mime_type(const char *mime_type, const struct sail_codec_info **codec_info) {

    SAIL_CHECK_STRING_PTR(mime_type);
    SAIL_CHECK_CODEC_INFO_PTR(codec_info);

    SAIL_LOG_DEBUG("Finding codec info for mime type '%s'", mime_type);

    struct sail_context *context;
    SAIL_TRY(current_tls_context(&context));

    char *mime_type_copy;
    SAIL_TRY(sail_strdup(mime_type, &mime_type_copy));

    /* Will compare in lower case. */
    sail_to_lower(mime_type_copy);

    const struct sail_codec_info_node *codec_info_node = context->codec_info_node;

    while (codec_info_node != NULL) {
        const struct sail_string_node *mime_type_node = codec_info_node->codec_info->mime_type_node;

        while (mime_type_node != NULL) {
            SAIL_LOG_TRACE("Check against %s MIME type '%s'", codec_info_node->codec_info->name, mime_type_node->value);

            if (strcmp(mime_type_node->value, mime_type_copy) == 0) {
                sail_free(mime_type_copy);
                *codec_info = codec_info_node->codec_info;
                SAIL_LOG_DEBUG("Found codec info: %s", (*codec_info)->name);
                return SAIL_OK;
            } else {
                SAIL_LOG_TRACE("MIME type mismatch '%s' != '%s'", mime_type_copy, mime_type_node->value);
            }

            mime_type_node = mime_type_node->next;
        }

        codec_info_node = codec_info_node->next;
    }

    sail_free(mime_type_copy);
    SAIL_LOG_AND_RETURN(SAIL_ERROR_CODEC_NOT_FOUND);
}
