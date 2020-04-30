/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

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

#include <stdlib.h>

#include "sail-common.h"
#include "sail.h"

sail_error_t start_reading_io_with_options(struct sail_io *io, bool own_io, struct sail_context *context,
                                           const struct sail_plugin_info *plugin_info,
                                           const struct sail_read_options *read_options, void **state) {

    SAIL_CHECK_STATE_PTR(state);
    *state = NULL;

    SAIL_CHECK_IO_PTR(io);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_PLUGIN_INFO_PTR(plugin_info);

    struct hidden_state *state_of_mind = (struct hidden_state *)malloc(sizeof(struct hidden_state));
    SAIL_CHECK_STATE_PTR(state_of_mind);

    state_of_mind->io          = io;
    state_of_mind->own_io      = own_io;
    state_of_mind->state       = NULL;
    state_of_mind->plugin_info = plugin_info;
    state_of_mind->plugin      = NULL;

    *state = state_of_mind;

    SAIL_TRY(load_plugin_by_plugin_info(context, state_of_mind->plugin_info, &state_of_mind->plugin));

    if (state_of_mind->plugin->layout != SAIL_PLUGIN_LAYOUT_V2) {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    if (read_options == NULL) {
        struct sail_read_options *read_options_local = NULL;

        SAIL_TRY_OR_CLEANUP(sail_alloc_read_options_from_features(state_of_mind->plugin_info->read_features, &read_options_local),
                            /* cleanup */ sail_destroy_read_options(read_options_local));
        SAIL_TRY_OR_CLEANUP(state_of_mind->plugin->v2->read_init_v2(state_of_mind->io, read_options_local, &state_of_mind->state),
                            /* cleanup */ sail_destroy_read_options(read_options_local));
        sail_destroy_read_options(read_options_local);
    } else {
        SAIL_TRY(state_of_mind->plugin->v2->read_init_v2(state_of_mind->io, read_options, &state_of_mind->state));
    }

    return 0;
}

sail_error_t start_writing_io_with_options(struct sail_io *io, bool own_io, struct sail_context *context,
                                           const struct sail_plugin_info *plugin_info,
                                           const struct sail_write_options *write_options, void **state) {

    SAIL_CHECK_STATE_PTR(state);
    *state = NULL;

    SAIL_CHECK_IO_PTR(io);
    SAIL_CHECK_CONTEXT_PTR(context);
    SAIL_CHECK_PLUGIN_INFO_PTR(plugin_info);

    struct hidden_state *state_of_mind = (struct hidden_state *)malloc(sizeof(struct hidden_state));
    SAIL_CHECK_STATE_PTR(state_of_mind);

    state_of_mind->io          = io;
    state_of_mind->own_io      = own_io;
    state_of_mind->state       = NULL;
    state_of_mind->plugin_info = plugin_info;
    state_of_mind->plugin      = NULL;

    *state = state_of_mind;

    SAIL_TRY(load_plugin_by_plugin_info(context, state_of_mind->plugin_info, &state_of_mind->plugin));

    if (state_of_mind->plugin->layout != SAIL_PLUGIN_LAYOUT_V2) {
        return SAIL_UNSUPPORTED_PLUGIN_LAYOUT;
    }

    if (write_options == NULL) {
        struct sail_write_options *write_options_local = NULL;

        SAIL_TRY_OR_CLEANUP(sail_alloc_write_options_from_features(state_of_mind->plugin_info->write_features, &write_options_local),
                            /* cleanup */ sail_destroy_write_options(write_options_local));
        SAIL_TRY_OR_CLEANUP(state_of_mind->plugin->v2->write_init_v2(state_of_mind->io, write_options_local, &state_of_mind->state),
                            /* cleanup */ sail_destroy_write_options(write_options_local));
        sail_destroy_write_options(write_options_local);
    } else {
        SAIL_TRY(state_of_mind->plugin->v2->write_init_v2(state_of_mind->io, write_options, &state_of_mind->state));
    }

    return 0;
}