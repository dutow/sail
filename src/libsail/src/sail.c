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

#define _POSIX_C_SOURCE 200112L /* setenv */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SAIL_WIN32
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

/* sail-common */
#include "common.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "utils.h"

#include "context.h"
#include "plugin_info.h"
#include "plugin.h"
#include "string_node.h"
#include "sail.h"

/*
 * Private functions.
 */

static const char* plugins_path(void) {

    SAIL_THREAD_LOCAL static bool plugins_path_called = false;
    SAIL_THREAD_LOCAL static char *env = NULL;

    if (plugins_path_called) {
        return env;
    }

    plugins_path_called = true;

#ifdef SAIL_WIN32
    _dupenv_s(&env, NULL, "SAIL_PLUGINS_PATH");
#else
    env = getenv("SAIL_PLUGINS_PATH");
#endif

    if (env == NULL) {
        SAIL_LOG_DEBUG("SAIL_PLUGINS_PATH environment variable is not set. Loading plugins from %s", SAIL_PLUGINS_PATH);
        env = SAIL_PLUGINS_PATH;
    } else {
        SAIL_LOG_DEBUG("SAIL_PLUGINS_PATH environment variable is set. Loading plugins from %s", env);
    }

    return env;
}

/* Add "sail/plugins/lib" to the DLL/SO search path. */
static sail_error_t update_lib_path(void) {

    SAIL_THREAD_LOCAL static bool update_lib_path_called = false;

    if (update_lib_path_called) {
        return 0;
    }

    update_lib_path_called = true;

    /* Build a full path to the SAIL plugins path + "/lib". */
    const char *plugs_path = plugins_path();

#ifdef SAIL_WIN32
    char *full_path_to_lib;
    SAIL_TRY(sail_concat(&full_path_to_lib, 2, plugs_path, "\\lib"));

    SAIL_LOG_DEBUG("Set DLL directory to '%s'", full_path_to_lib);

    wchar_t *full_path_to_lib_w;
    SAIL_TRY_OR_CLEANUP(sail_to_wchar(full_path_to_lib, &full_path_to_lib_w),
                        free(full_path_to_lib));

    if (!AddDllDirectory(full_path_to_lib_w)) {
        SAIL_LOG_ERROR("Failed to update library search path. Error: %d", GetLastError());
        free(full_path_to_lib_w);
        free(full_path_to_lib);
        return SAIL_ENV_UPDATE_FAILED;
    }

    free(full_path_to_lib_w);
    free(full_path_to_lib);
#else
    char *full_path_to_lib;
    SAIL_TRY(sail_concat(&full_path_to_lib, 2, plugs_path, "/lib"));

    char *combined_ld_library_path;
    char *env = getenv("LD_LIBRARY_PATH");

    if (env == NULL) {
        SAIL_TRY_OR_CLEANUP(sail_strdup(full_path_to_lib, &combined_ld_library_path),
                            free(full_path_to_lib));
    } else {
        SAIL_TRY_OR_CLEANUP(sail_concat(&combined_ld_library_path, 3, env, ":", full_path_to_lib),
                            free(full_path_to_lib));
    }

    free(full_path_to_lib);
    SAIL_LOG_DEBUG("Set LD_LIBRARY_PATH to '%s'", combined_ld_library_path);

    if (setenv("LD_LIBRARY_PATH", combined_ld_library_path, true) != 0) {
        SAIL_LOG_ERROR("Failed to update library search path: %s", strerror(errno));
        free(combined_ld_library_path);
        return SAIL_ENV_UPDATE_FAILED;
    }

    free(combined_ld_library_path);
#endif

    return 0;
}

static sail_error_t build_full_path(const char *sail_plugins_path, const char *name, char **full_path) {

#ifdef SAIL_WIN32
    SAIL_TRY(sail_concat(full_path, 3, sail_plugins_path, "\\", name));
#else
    SAIL_TRY(sail_concat(full_path, 3, sail_plugins_path, "/", name));
#endif

    return 0;
}

static sail_error_t build_plugin_full_path(struct sail_context *context,
                                            struct sail_plugin_info_node **last_plugin_info_node,
                                            const char *plugin_info_full_path) {

    /* Build "/path/jpeg.so" from "/path/jpeg.plugin.info". */
    char *plugin_info_part = strstr(plugin_info_full_path, ".plugin.info");

    if (plugin_info_part == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    /* The length of "/path/jpeg". */
    size_t plugin_full_path_length = strlen(plugin_info_full_path) - strlen(plugin_info_part);
    char *plugin_full_path;

#ifdef SAIL_WIN32
    static const char * const LIB_SUFFIX = "dll";
#else
    static const char * const LIB_SUFFIX = "so";
#endif

    /* The resulting string will be "/path/jpeg.plu" (on Windows) or "/path/jpeg.pl". */
    SAIL_TRY(sail_strdup_length(plugin_info_full_path,
                                plugin_full_path_length + strlen(LIB_SUFFIX) + 1, &plugin_full_path));

#ifdef SAIL_WIN32
    /* Overwrite the end of the path with "dll". */
    strcpy_s(plugin_full_path + plugin_full_path_length + 1, strlen(LIB_SUFFIX) + 1, LIB_SUFFIX);
#else
    /* Overwrite the end of the path with "so". */
    strcpy(plugin_full_path + plugin_full_path_length + 1, LIB_SUFFIX);
#endif

    /* Parse plugin info. */
    struct sail_plugin_info_node *plugin_info_node;
    SAIL_TRY_OR_CLEANUP(sail_alloc_plugin_info_node(&plugin_info_node),
                        free(plugin_full_path));

    struct sail_plugin_info *plugin_info;
    SAIL_TRY_OR_CLEANUP(sail_plugin_read_info(plugin_info_full_path, &plugin_info),
                        sail_destroy_plugin_info_node(plugin_info_node),
                        free(plugin_full_path));

    /* Save the parsed plugin info into the SAIL context. */
    plugin_info_node->plugin_info = plugin_info;
    plugin_info->path = plugin_full_path;

    if (context->plugin_info_node == NULL) {
        context->plugin_info_node = *last_plugin_info_node = plugin_info_node;
    } else {
        (*last_plugin_info_node)->next = plugin_info_node;
        *last_plugin_info_node = plugin_info_node;
    }

    return 0;
}

/*
 * Public functions.
 */

sail_error_t sail_init(struct sail_context **context) {

    SAIL_CHECK_CONTEXT_PTR(context);

    *context = (struct sail_context *)malloc(sizeof(struct sail_context));

    if (*context == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    (*context)->plugin_info_node = NULL;

    SAIL_TRY(update_lib_path());

    struct sail_plugin_info_node *last_plugin_info_node;

#ifdef SAIL_WIN32
    const char *plugs_path = plugins_path();
    const char *plugs_info_mask = "\\*.plugin.info";

    size_t plugs_path_with_mask_length = strlen(plugs_path) + strlen(plugs_info_mask) + 1;
    char *plugs_path_with_mask = (char *)malloc(plugs_path_with_mask_length);

    if (plugs_path_with_mask == NULL) {
        return SAIL_MEMORY_ALLOCATION_FAILED;
    }

    strcpy_s(plugs_path_with_mask, plugs_path_with_mask_length, plugs_path);
    strcat_s(plugs_path_with_mask, plugs_path_with_mask_length, plugs_info_mask);

    WIN32_FIND_DATA data;
    HANDLE hFind = FindFirstFile(plugs_path_with_mask, &data);

    if (hFind == INVALID_HANDLE_VALUE) {
        SAIL_LOG_ERROR("Failed to list files in '%s'. Error: %d", plugs_path, GetLastError());
        free(plugs_path_with_mask);
        return SAIL_DIR_OPEN_ERROR;
    }

    do {
        /* Build a full path. */
        char *full_path;

        /* Ignore errors and try to load as much as possible. */
        if (build_full_path(plugs_path, data.cFileName, &full_path) != 0) {
            continue;
        }

        build_plugin_full_path(*context, &last_plugin_info_node, full_path);

        free(full_path);
    } while (FindNextFile(hFind, &data));

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        SAIL_LOG_ERROR("Failed to list files in '%s'. Error: %d. Some plugins may be ignored", plugs_path, GetLastError());
    }

    free(plugs_path_with_mask);
    FindClose(hFind);
#else
    const char *plugs_path = plugins_path();
    DIR *d = opendir(plugs_path);

    if (d == NULL) {
        SAIL_LOG_ERROR("Failed to list files in '%s': %s", plugs_path, strerror(errno));
        return SAIL_DIR_OPEN_ERROR;
    }

    struct dirent *dir;

    while ((dir = readdir(d)) != NULL) {

        /* Build a full path. */
        char *full_path;

        /* Ignore errors and try to load as much as possible. */
        if (build_full_path(plugs_path, dir->d_name, &full_path) != 0) {
            continue;
        }

        /* Handle files only. */
        struct stat full_path_stat;
        stat(full_path, &full_path_stat);
        bool is_file = S_ISREG(full_path_stat.st_mode);

        if (is_file) {
            build_plugin_full_path(*context, &last_plugin_info_node, full_path);
        }

        free(full_path);
    }

    closedir(d);
#endif

    return 0;
}

void sail_finish(struct sail_context *context) {

    if (context == NULL) {
        return;
    }

    sail_destroy_plugin_info_node_chain(context->plugin_info_node);

    free(context);
}

const struct sail_plugin_info_node* sail_plugin_info_list(const struct sail_context *context) {

    return context->plugin_info_node;
}

sail_error_t sail_plugin_info_by_extension(const struct sail_context *context, const char *extension, const struct sail_plugin_info **plugin_info) {

    SAIL_CHECK_CONTEXT_PTR(context);

    if (extension == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    char *extension_copy;

    SAIL_TRY(sail_strdup(extension, &extension_copy));

    /* Will compare in lower case. */
    sail_to_lower(extension_copy);

    struct sail_plugin_info_node *node = context->plugin_info_node;

    while (node != NULL) {
        struct sail_string_node *string_node = node->plugin_info->extension_node;

        while (string_node != NULL) {
            if (strcmp(string_node->value, extension_copy) == 0) {
                free(extension_copy);
                *plugin_info = node->plugin_info;
                return 0;
            }

            string_node = string_node->next;
        }

        node = node->next;
    }

    free(extension_copy);
    return SAIL_PLUGIN_NOT_FOUND;
}

sail_error_t sail_plugin_info_by_mime_type(const struct sail_context *context, const char *mime_type, const struct sail_plugin_info **plugin_info) {

    SAIL_CHECK_CONTEXT_PTR(context);

    if (mime_type == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    char *mime_type_copy;

    SAIL_TRY(sail_strdup(mime_type, &mime_type_copy));

    /* Will compare in lower case. */
    sail_to_lower(mime_type_copy);

    struct sail_plugin_info_node *node = context->plugin_info_node;

    while (node != NULL) {
        struct sail_string_node *string_node = node->plugin_info->mime_type_node;

        while (string_node != NULL) {
            if (strcmp(string_node->value, mime_type_copy) == 0) {
                free(mime_type_copy);
                *plugin_info = node->plugin_info;
                return 0;
            }

            string_node = string_node->next;
        }

        node = node->next;
    }

    free(mime_type_copy);
    return SAIL_PLUGIN_NOT_FOUND;
}

sail_error_t sail_load_plugin(struct sail_context *context, const struct sail_plugin_info *plugin_info, const struct sail_plugin **plugin) {

    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_PLUGIN_INFO_PTR(plugin_info);

    /* Find the plugin in the cache. */
    struct sail_plugin_info_node *node = context->plugin_info_node;

    while (node != NULL) {
        if (node->plugin_info == plugin_info) {
            if (node->plugin != NULL) {
                *plugin = node->plugin;
                return 0;
            }

            break;
        }

        node = node->next;
    }

    /* Something weird. The pointer to the plugin info is not found the cache. */
    if (node == NULL) {
        return SAIL_PLUGIN_NOT_FOUND;
    }

    struct sail_plugin *local_plugin;

    /* Plugin is not loaded. Let's load it. */
    SAIL_TRY(sail_alloc_plugin(plugin_info, &local_plugin));

    *plugin = local_plugin;
    node->plugin = local_plugin;

    return 0;
}

sail_error_t sail_unload_plugins(struct sail_context *context) {

    SAIL_CHECK_CONTEXT_PTR(context);

    SAIL_LOG_DEBUG("Unloading cached plugins");

    struct sail_plugin_info_node *node = context->plugin_info_node;
    int counter = 0;

    while (node != NULL) {
        if (node->plugin != NULL) {
            sail_destroy_plugin(node->plugin);
            counter++;
        }

        node->plugin = NULL;

        node = node->next;
    }

    SAIL_LOG_DEBUG("Unloaded plugins: %d", counter);

    return 0;
}

sail_error_t sail_plugin_read_features(const struct sail_plugin *plugin, struct sail_read_features **read_features) {

    SAIL_CHECK_PLUGIN_PTR(plugin);

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(plugin->v2->read_features_v2(read_features));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_plugin_write_features(const struct sail_plugin *plugin, struct sail_write_features **write_features) {

    SAIL_CHECK_PLUGIN_PTR(plugin);

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(plugin->v2->write_features_v2(write_features));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_probe_image(const char *path, struct sail_context *context, const struct sail_plugin_info **plugin_info, struct sail_image **image) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    const struct sail_plugin_info *plugin_info_noop;
    const struct sail_plugin_info **plugin_info_local = plugin_info == NULL ? &plugin_info_noop : plugin_info;

    SAIL_TRY(sail_plugin_info_by_extension(context, dot + 1, plugin_info_local));

    const struct sail_plugin *plugin;

    SAIL_TRY(sail_load_plugin(context, *plugin_info_local, &plugin));

    struct sail_file *file;

    SAIL_TRY(sail_alloc_file_for_reading(path, &file));

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY_OR_CLEANUP(plugin->v2->read_init_v2(file, NULL),
                            /* cleanup */ sail_destroy_file(file));
        SAIL_TRY_OR_CLEANUP(plugin->v2->read_seek_next_frame_v2(file, image),
                            /* cleanup */ sail_destroy_file(file));
        SAIL_TRY_OR_CLEANUP(plugin->v2->read_finish_v2(file),
                            /* cleanup */ sail_destroy_file(file));
    } else {
        sail_destroy_file(file);
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    sail_destroy_file(file);

    return 0;
}

sail_error_t sail_read(const char *path, struct sail_context *context, struct sail_image **image, void **image_bits,
                        const struct sail_plugin_info **plugin_info) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_IMAGE_PTR(image);
    SAIL_CHECK_PTR(image_bits);

    void *pimpl = NULL;
    *image_bits = NULL;

    SAIL_TRY_OR_CLEANUP(sail_start_reading(path, context, plugin_info, &pimpl),
                        sail_stop_reading(pimpl));
    SAIL_TRY_OR_CLEANUP(sail_read_next_frame(pimpl, image, image_bits),
                        free(image_bits),
                        sail_stop_reading(pimpl));
    SAIL_TRY_OR_CLEANUP(sail_stop_reading(pimpl),
                        free(image_bits));

    return 0;
}

sail_error_t sail_write(const char *path, struct sail_context *context, const struct sail_image *image, const void *image_bits,
                        const struct sail_plugin_info **plugin_info) {

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_IMAGE(image);
    SAIL_CHECK_PTR(image_bits);

    void *pimpl = NULL;

    SAIL_TRY_OR_CLEANUP(sail_start_writing(path, context, plugin_info, &pimpl),
                        sail_stop_writing(pimpl));
    SAIL_TRY_OR_CLEANUP(sail_write_next_frame(pimpl, image, image_bits),
                        sail_stop_writing(pimpl));
    SAIL_TRY(sail_stop_writing(pimpl));

    return 0;
}

struct hidden_pimpl {

    struct sail_file *file;
    const struct sail_plugin *plugin;
};

static void destroy_hidden_pimpl(struct hidden_pimpl *pimpl) {

    if (pimpl == NULL) {
        return;
    }

    sail_destroy_file(pimpl->file);

    free(pimpl);
}

sail_error_t sail_start_reading_with_plugin(const char *path, struct sail_context *context, const struct sail_plugin *plugin,
                                            const struct sail_read_options *read_options, void **pimpl) {
    SAIL_CHECK_PTR(pimpl);

    *pimpl = NULL;

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_PLUGIN_PTR(plugin);

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)malloc(sizeof(struct hidden_pimpl));

    SAIL_CHECK_PIMPL_PTR(pmpl);

    pmpl->file   = NULL;
    pmpl->plugin = plugin;

    *pimpl = pmpl;

    SAIL_TRY(sail_alloc_file_for_reading(path, &pmpl->file));

    if (pmpl->plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(pmpl->plugin->v2->read_init_v2(pmpl->file, read_options));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_start_reading(const char *path, struct sail_context *context, const struct sail_plugin_info **plugin_info, void **pimpl) {

    SAIL_CHECK_PTR(pimpl);

    *pimpl = NULL;

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)malloc(sizeof(struct hidden_pimpl));

    SAIL_CHECK_PIMPL_PTR(pmpl);

    pmpl->file   = NULL;
    pmpl->plugin = NULL;

    *pimpl = pmpl;

    const struct sail_plugin_info *plugin_info_noop;
    const struct sail_plugin_info **plugin_info_local = plugin_info == NULL ? &plugin_info_noop : plugin_info;

    SAIL_TRY(sail_plugin_info_by_extension(context, dot + 1, plugin_info_local));
    SAIL_TRY(sail_load_plugin(context, *plugin_info_local, &pmpl->plugin));
    SAIL_TRY(sail_alloc_file_for_reading(path, &pmpl->file));

    if (pmpl->plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(pmpl->plugin->v2->read_init_v2(pmpl->file, NULL));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_read_next_frame(void *pimpl, struct sail_image **image, void **image_bits) {

    SAIL_CHECK_PTR(pimpl);

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)pimpl;

    SAIL_CHECK_FILE(pmpl->file);
    SAIL_CHECK_PLUGIN_PTR(pmpl->plugin);

    struct sail_file *file = pmpl->file;
    const struct sail_plugin *plugin = pmpl->plugin;

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(plugin->v2->read_seek_next_frame_v2(file, image));

        *image_bits = malloc((*image)->bytes_per_line * (*image)->height);

        if (*image_bits == NULL) {
            return SAIL_MEMORY_ALLOCATION_FAILED;
        }

        for (int pass = 0; pass < (*image)->passes; pass++) {
            SAIL_TRY(plugin->v2->read_seek_next_pass_v2(file, *image));

            for (int j = 0; j < (*image)->height; j++) {
                SAIL_TRY(plugin->v2->read_scan_line_v2(file, *image, ((char *)*image_bits) + j * (*image)->bytes_per_line));
            }
        }
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_stop_reading(void *pimpl) {

    /* Not an error. */
    if (pimpl == NULL) {
        return 0;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)pimpl;

    struct sail_file *file = pmpl->file;
    const struct sail_plugin *plugin = pmpl->plugin;

    /* Not an error. */
    if (plugin == NULL) {
        destroy_hidden_pimpl(pmpl);
        return 0;
    }

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY_OR_CLEANUP(plugin->v2->read_finish_v2(file),
                            /* cleanup */ destroy_hidden_pimpl(pmpl));
    } else {
        destroy_hidden_pimpl(pmpl);
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    destroy_hidden_pimpl(pmpl);

    return 0;
}

sail_error_t sail_start_writing_with_plugin(const char *path, struct sail_context *context, const struct sail_plugin *plugin,
                                            const struct sail_write_options *write_options, void **pimpl) {
    SAIL_CHECK_PTR(pimpl);

    *pimpl = NULL;

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_PLUGIN_PTR(plugin);

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)malloc(sizeof(struct hidden_pimpl));

    SAIL_CHECK_PIMPL_PTR(pmpl);

    pmpl->file   = NULL;
    pmpl->plugin = plugin;

    *pimpl = pmpl;

    SAIL_TRY(sail_alloc_file_for_writing(path, &pmpl->file));

    if (pmpl->plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(pmpl->plugin->v2->write_init_v2(pmpl->file, write_options));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_start_writing(const char *path, struct sail_context *context, const struct sail_plugin_info **plugin_info, void **pimpl) {

    SAIL_CHECK_PTR(pimpl);

    *pimpl = NULL;

    SAIL_CHECK_PATH_PTR(path);
    SAIL_CHECK_CONTEXT_PTR(context);

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return SAIL_INVALID_ARGUMENT;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)malloc(sizeof(struct hidden_pimpl));

    SAIL_CHECK_PIMPL_PTR(pmpl);

    pmpl->file   = NULL;
    pmpl->plugin = NULL;

    *pimpl = pmpl;

    const struct sail_plugin_info *plugin_info_noop;
    const struct sail_plugin_info **plugin_info_local = plugin_info == NULL ? &plugin_info_noop : plugin_info;

    SAIL_TRY(sail_plugin_info_by_extension(context, dot + 1, plugin_info_local));
    SAIL_TRY(sail_load_plugin(context, *plugin_info_local, &pmpl->plugin));
    SAIL_TRY(sail_alloc_file_for_writing(path, &pmpl->file));

    if (pmpl->plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(pmpl->plugin->v2->write_init_v2(pmpl->file, NULL));
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_write_next_frame(void *pimpl, const struct sail_image *image, const void *image_bits) {

    SAIL_CHECK_PTR(pimpl);

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)pimpl;

    SAIL_CHECK_FILE(pmpl->file);
    SAIL_CHECK_PLUGIN_PTR(pmpl->plugin);

    struct sail_file *file = pmpl->file;
    const struct sail_plugin *plugin = pmpl->plugin;
    int passes;

    /* Detect the number of passes needed to write an interlaced image. */
    if (image->properties & SAIL_IMAGE_PROPERTY_INTERLACED) {
        struct sail_write_features *write_features;
        SAIL_TRY(sail_plugin_write_features(plugin, &write_features));
        passes = write_features->passes;
        sail_destroy_write_features(write_features);

        if (passes < 1) {
            return SAIL_INTERLACED_UNSUPPORTED;
        }
    } else {
        passes = 1;
    }

    const int bytes_per_line = sail_bytes_per_line(image->width, image->pixel_format);

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY(plugin->v2->write_seek_next_frame_v2(file, image));

        for (int pass = 0; pass < passes; pass++) {
            SAIL_TRY(plugin->v2->write_seek_next_pass_v2(file, image));

            for (int j = 0; j < image->height; j++) {
                SAIL_TRY(plugin->v2->write_scan_line_v2(file, image, ((char *)image_bits) + j * bytes_per_line));
            }
        }
    } else {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    return 0;
}

sail_error_t sail_stop_writing(void *pimpl) {

    /* Not an error. */
    if (pimpl == NULL) {
        return 0;
    }

    struct hidden_pimpl *pmpl = (struct hidden_pimpl *)pimpl;

    struct sail_file *file = pmpl->file;
    const struct sail_plugin *plugin = pmpl->plugin;

    /* Not an error. */
    if (plugin == NULL) {
        destroy_hidden_pimpl(pmpl);
        return 0;
    }

    if (plugin->layout == SAIL_PLUGIN_LAYOUT_V2) {
        SAIL_TRY_OR_CLEANUP(plugin->v2->write_finish_v2(file),
                            /* cleanup */ destroy_hidden_pimpl(pmpl));
    } else {
        destroy_hidden_pimpl(pmpl);
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    destroy_hidden_pimpl(pmpl);

    return 0;
}
