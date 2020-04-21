/*  This file is part of SAIL (https://github.com/sailor-keg/sail)

    Copyright (c) 2020 Dmitry Baryshev <dmitrymq@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library. If not, see <https://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libsail-common */
#include "common.h"
#include "error.h"
#include "log.h"
#include "utils.h"

#include "ini.h"
#include "plugin_info_private.h"
#include "plugin_info.h"
#include "plugin.h"
#include "string_node.h"

/*
 * Private functions.
 */

static sail_error_t alloc_string_node(struct sail_string_node **string_node) {

    *string_node = (struct sail_string_node *)malloc(sizeof(struct sail_string_node));

    if (*string_node == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*string_node)->value = NULL;
    (*string_node)->next  = NULL;

    return 0;
}

static void destroy_string_node(struct sail_string_node *string_node) {

    if (string_node == NULL) {
        return;
    }

    free(string_node->value);
    free(string_node);
}

static void destroy_string_node_chain(struct sail_string_node *string_node) {

    while (string_node != NULL) {
        struct sail_string_node *string_node_next = string_node->next;

        destroy_string_node(string_node);

        string_node = string_node_next;
    }
}

static sail_error_t split_into_string_node_chain(const char *value, struct sail_string_node **target_string_node) {

    struct sail_string_node *last_string_node = NULL;

    while (*(value += strspn(value, ";")) != '\0') {
        size_t length = strcspn(value, ";");

        struct sail_string_node *string_node;

        SAIL_TRY(alloc_string_node(&string_node));

        SAIL_TRY_OR_CLEANUP(sail_strdup_length(value, length, &string_node->value),
                            /* cleanup */ destroy_string_node(string_node));

        if (*target_string_node == NULL) {
            *target_string_node = last_string_node = string_node;
        } else {
            last_string_node->next = string_node;
            last_string_node = string_node;
        }

        value += length;
    }

    return 0;
}

static sail_error_t parse_serialized_ints(const char *value, int **target, int *length, int (*converter)(const char *)) {

    SAIL_CHECK_PTR(value);
    SAIL_CHECK_PTR(target);
    SAIL_CHECK_PTR(length);

    struct sail_string_node *string_node = NULL;

    SAIL_TRY_OR_CLEANUP(split_into_string_node_chain(value, &string_node),
                        /* cleanup */ destroy_string_node_chain(string_node));

    *length = 0;
    struct sail_string_node *node = string_node;

    while (node != NULL) {
        (*length)++;
        node = node->next;
    }

    if (*length > 0) {
        *target = (int *)malloc(*length * sizeof(int));

        if (*target == NULL) {
            SAIL_LOG_ERROR("Failed to allocate %d integers", *length);
            return SAIL_MEMORY_ALLOCATION_FAILED;
        }

        node = string_node;
        int i = 0;

        while (node != NULL) {
            (*target)[i++] = converter(node->value);
            node = node->next;
        }
    }

    destroy_string_node_chain(string_node);

    return 0;
}

static sail_error_t parse_flags(const char *value, int *features, int (*converter)(const char *)) {

    SAIL_CHECK_PTR(value);
    SAIL_CHECK_PTR(features);

    struct sail_string_node *string_node = NULL;

    SAIL_TRY_OR_CLEANUP(split_into_string_node_chain(value, &string_node),
                        /* cleanup */ destroy_string_node_chain(string_node));

    *features = 0;

    struct sail_string_node *node = string_node;

    while (node != NULL) {
        *features |= converter(node->value);
        node = node->next;
    }

    destroy_string_node_chain(string_node);

    return 0;
}

static int inih_handler(void *data, const char *section, const char *name, const char *value) {

    struct sail_plugin_info *plugin_info = (struct sail_plugin_info *)data;

    int res;

    if (strcmp(section, "plugin") == 0) {
        if (strcmp(name, "layout") == 0) {
            plugin_info->layout = atoi(value);
        } else if (strcmp(name, "version") == 0) {
            if ((res = sail_strdup(value, &plugin_info->version)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "name") == 0) {
            if ((res = sail_strdup(value, &plugin_info->name)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "description") == 0) {
            if ((res = sail_strdup(value, &plugin_info->description)) != 0) {
                return 0;
            }
        } else if (strcmp(name, "extensions") == 0) {
            if (split_into_string_node_chain(value, &plugin_info->extension_node) != 0) {
                return 0;
            }

            struct sail_string_node *node = plugin_info->extension_node;

            while (node != NULL) {
                sail_to_lower(node->value);
                node = node->next;
            }
        } else if (strcmp(name, "mime-types") == 0) {
            if (split_into_string_node_chain(value, &plugin_info->mime_type_node) != 0) {
                return 0;
            }

            struct sail_string_node *node = plugin_info->mime_type_node;

            while (node != NULL) {
                sail_to_lower(node->value);
                node = node->next;
            }
        } else {
            SAIL_LOG_ERROR("Unsupported plugin info key '%s' in [%s]", name, section);
            return 0; /* error */
        }
    } else if (strcmp(section, "read-features") == 0) {
        if (strcmp(name, "input-pixel-formats") == 0) {
            if (parse_serialized_ints(value, &plugin_info->read_features->input_pixel_formats,
                                      &plugin_info->read_features->input_pixel_formats_length,
                                      sail_pixel_format_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "output-pixel-formats") == 0) {
            if (parse_serialized_ints(value, &plugin_info->read_features->output_pixel_formats,
                                      &plugin_info->read_features->output_pixel_formats_length,
                                      sail_pixel_format_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "preferred-output-pixel-format") == 0) {
            plugin_info->read_features->preferred_output_pixel_format = sail_pixel_format_from_string(value);
        } else if (strcmp(name, "features") == 0) {
            if (parse_flags(value, &plugin_info->read_features->features, sail_plugin_feature_from_string) != 0) {
                return 0;
            }
        } else {
            SAIL_LOG_ERROR("Unsupported plugin info key '%s' in [%s]", name, section);
            return 0;
        }
    } else if (strcmp(section, "write-features") == 0) {
        if (strcmp(name, "input-pixel-formats") == 0) {
            if (parse_serialized_ints(value, &plugin_info->write_features->input_pixel_formats,
                                      &plugin_info->write_features->input_pixel_formats_length,
                                      sail_pixel_format_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "output-pixel-formats") == 0) {
            if (parse_serialized_ints(value, &plugin_info->write_features->output_pixel_formats,
                                      &plugin_info->write_features->output_pixel_formats_length,
                                      sail_pixel_format_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "preferred-output-pixel-format") == 0) {
            plugin_info->write_features->preferred_output_pixel_format = sail_pixel_format_from_string(value);
        } else if (strcmp(name, "features") == 0) {
            if (parse_flags(value, &plugin_info->write_features->features, sail_plugin_feature_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "properties") == 0) {
            if (parse_flags(value, &plugin_info->write_features->properties, sail_image_property_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "passes") == 0) {
            plugin_info->write_features->passes = atoi(value);
        } else if (strcmp(name, "compression-types") == 0) {
            if (parse_serialized_ints(value, &plugin_info->write_features->compression_types,
                                      &plugin_info->write_features->compression_types_length,
                                      sail_compression_type_from_string) != 0) {
                return 0;
            }
        } else if (strcmp(name, "preferred-compression-type") == 0) {
            plugin_info->write_features->preferred_compression_type = sail_compression_type_from_string(value);
        } else if (strcmp(name, "compression-min") == 0) {
            plugin_info->write_features->compression_min = atoi(value);
        } else if (strcmp(name, "compression-max") == 0) {
            plugin_info->write_features->compression_max = atoi(value);
        } else if (strcmp(name, "compression-default") == 0) {
            plugin_info->write_features->compression_default = atoi(value);
        } else {
            SAIL_LOG_ERROR("Unsupported plugin info key '%s' in [%s]", name, section);
            return 0;
        }
    } else {
        SAIL_LOG_ERROR("Unsupported plugin info section '%s'", section);
        return 0;
    }

    return 1;
}

/*
 * Public functions.
 */

int sail_alloc_plugin_info(struct sail_plugin_info **plugin_info) {

    *plugin_info = (struct sail_plugin_info *)malloc(sizeof(struct sail_plugin_info));

    if (*plugin_info == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*plugin_info)->layout          = 0;
    (*plugin_info)->version         = NULL;
    (*plugin_info)->name            = NULL;
    (*plugin_info)->description     = NULL;
    (*plugin_info)->extension_node  = NULL;
    (*plugin_info)->mime_type_node  = NULL;
    (*plugin_info)->path            = NULL;
    (*plugin_info)->read_features   = NULL;
    (*plugin_info)->write_features  = NULL;

    return 0;
}

void sail_destroy_plugin_info(struct sail_plugin_info *plugin_info) {

    if (plugin_info == NULL) {
        return;
    }

    free(plugin_info->version);
    free(plugin_info->name);
    free(plugin_info->description);

    destroy_string_node_chain(plugin_info->extension_node);
    destroy_string_node_chain(plugin_info->mime_type_node);

    free(plugin_info->path);

    sail_destroy_read_features(plugin_info->read_features);
    sail_destroy_write_features(plugin_info->write_features);

    free(plugin_info);
}

sail_error_t sail_alloc_plugin_info_node(struct sail_plugin_info_node **plugin_info_node) {

    *plugin_info_node = (struct sail_plugin_info_node *)malloc(sizeof(struct sail_plugin_info_node));

    if (*plugin_info_node == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*plugin_info_node)->plugin_info = NULL;
    (*plugin_info_node)->plugin      = NULL;
    (*plugin_info_node)->next        = NULL;

    return 0;
}

void sail_destroy_plugin_info_node(struct sail_plugin_info_node *plugin_info_node) {

    if (plugin_info_node == NULL) {
        return;
    }

    sail_destroy_plugin_info(plugin_info_node->plugin_info);
    sail_destroy_plugin(plugin_info_node->plugin);

    free(plugin_info_node);
}

void sail_destroy_plugin_info_node_chain(struct sail_plugin_info_node *plugin_info_node) {

    while (plugin_info_node != NULL) {
        struct sail_plugin_info_node *plugin_info_node_next = plugin_info_node->next;

        sail_destroy_plugin_info_node(plugin_info_node);

        plugin_info_node = plugin_info_node_next;
    }
}

static sail_error_t check_plugin_info(const char *path, const struct sail_plugin_info *plugin_info) {

    const struct sail_read_features *read_features = plugin_info->read_features;
    const struct sail_write_features *write_features = plugin_info->write_features;

    /* Check read features. */
    if (read_features->input_pixel_formats_length == 0 && read_features->output_pixel_formats_length != 0) {
        SAIL_LOG_ERROR("The plugin '%s' is not able to read anything, but output pixel formats are specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    if (read_features->input_pixel_formats_length != 0 && read_features->output_pixel_formats_length == 0) {
        SAIL_LOG_ERROR("The plugin '%s' is able to read images, but output pixel formats are not specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    if ((read_features->features & SAIL_PLUGIN_FEATURE_STATIC ||
            read_features->features & SAIL_PLUGIN_FEATURE_ANIMATED ||
            read_features->features & SAIL_PLUGIN_FEATURE_MULTIPAGED) && read_features->input_pixel_formats_length == 0) {
        SAIL_LOG_ERROR("The plugin '%s' is able to read images, but input pixel formats are not specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    /* Check write features. */
    if (write_features->input_pixel_formats_length == 0 && write_features->output_pixel_formats_length != 0) {
        SAIL_LOG_ERROR("The plugin '%s' is not able to write anything, but output pixel formats are specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    if (write_features->input_pixel_formats_length != 0 && write_features->output_pixel_formats_length == 0) {
        SAIL_LOG_ERROR("The plugin '%s' is able to write images, but output pixel formats are not specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    if ((write_features->features & SAIL_PLUGIN_FEATURE_STATIC ||
            write_features->features & SAIL_PLUGIN_FEATURE_ANIMATED ||
            write_features->features & SAIL_PLUGIN_FEATURE_MULTIPAGED) && write_features->output_pixel_formats_length == 0) {
        SAIL_LOG_ERROR("The plugin '%s' is able to write images, but output pixel formats are not specified", path);
        return SAIL_INCOMPLETE_PLUGIN_INFO;
    }

    return 0;
}

int sail_plugin_read_info(const char *path, struct sail_plugin_info **plugin_info) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_PLUGIN_INFO_PTR(plugin_info);

    SAIL_TRY(sail_alloc_plugin_info(plugin_info));
    SAIL_TRY_OR_CLEANUP(sail_alloc_read_features(&(*plugin_info)->read_features),
                        sail_destroy_plugin_info(*plugin_info));
    SAIL_TRY_OR_CLEANUP(sail_alloc_write_features(&(*plugin_info)->write_features),
                        sail_destroy_plugin_info(*plugin_info));

    /*
     * Returns:
     *  - 0 on success
     *  - line number of first error on parse error
     *  - -1 on file open error
     *  - -2 on memory allocation error (only when INI_USE_STACK is zero).
     */
    const int code = ini_parse(path, inih_handler, *plugin_info);

    if ((*plugin_info)->layout != SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_LOG_ERROR("Unsupported plugin layout version %d in '%s'", (*plugin_info)->layout, path);
        sail_destroy_plugin_info(*plugin_info);
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    /* Paranoid error checks. */
    SAIL_TRY_OR_CLEANUP(check_plugin_info(path, *plugin_info),
                        /* cleanup */ sail_destroy_plugin_info(*plugin_info));

    /* Success. */
    if (code == 0) {
        return 0;
    }

    /* Error. */
    sail_destroy_plugin_info(*plugin_info);

    switch (code) {
        case -1: return SAIL_FILE_OPEN_ERROR;
        case -2: return SAIL_MEMORY_ALLOCATION_FAILED;

        default: return SAIL_FILE_PARSE_ERROR;
    }
}