// =================================================================
// src/core/audio/PulseControlAbi.h  (NereusSDR)
// =================================================================
// Ported from PulseAudio v17.0 public API declarations:
//   src/pulse/{def,mainloop,context,operation,introspect}.h
//   https://github.com/pulseaudio/pulseaudio/tree/v17.0/src/pulse
// The minimal declarations below let the optional runtime control adapter
// resolve libpulse without requiring development headers or a link dependency.
// Original declarations and their comments are retained verbatim; unrelated
// APIs and preprocessor aliases are omitted. Opaque pointer declarations are
// forward-declared here because their layout is never inspected.
//
// Modification history (NereusSDR):
//   2026-10-14 - Selected public ABI declarations for the Linux VAX ownership
//                control adapter. J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// =================================================================

// --- From src/pulse/def.h [v17.0] ---
/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

// --- From src/pulse/mainloop.h [v17.0] ---
/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

// --- From src/pulse/context.h [v17.0] ---
/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

// --- From src/pulse/operation.h [v17.0] ---
/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

// --- From src/pulse/introspect.h [v17.0] ---
/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#pragma once

#include <cstdint>

namespace NereusSDR::PulseControlAbi {

extern "C" {

typedef struct pa_mainloop_api pa_mainloop_api;
typedef struct pa_proplist pa_proplist;
typedef struct pa_spawn_api pa_spawn_api;

// From PulseAudio src/pulse/def.h:36-45,92-102,114-124 [v17.0].
/** The state of a connection context */
typedef enum pa_context_state {
    PA_CONTEXT_UNCONNECTED,    /**< The context hasn't been connected yet */
    PA_CONTEXT_CONNECTING,     /**< A connection is being established */
    PA_CONTEXT_AUTHORIZING,    /**< The client is authorizing itself to the daemon */
    PA_CONTEXT_SETTING_NAME,   /**< The client is passing its application name to the daemon */
    PA_CONTEXT_READY,          /**< The connection is established, the context is ready to execute operations */
    PA_CONTEXT_FAILED,         /**< The connection failed or was disconnected */
    PA_CONTEXT_TERMINATED      /**< The connection was terminated cleanly */
} pa_context_state_t;

/** The state of an operation */
typedef enum pa_operation_state {
    PA_OPERATION_RUNNING,
    /**< The operation is still running */
    PA_OPERATION_DONE,
    /**< The operation has completed */
    PA_OPERATION_CANCELLED
    /**< The operation has been cancelled. Operations may get cancelled by the
     * application, or as a result of the context getting disconnected while the
     * operation is pending. */
} pa_operation_state_t;

/** Some special flags for contexts. */
typedef enum pa_context_flags {
    PA_CONTEXT_NOFLAGS = 0x0000U,
    /**< Flag to pass when no specific options are needed (used to avoid casting)  \since 0.9.19 */
    PA_CONTEXT_NOAUTOSPAWN = 0x0001U,
    /**< Disabled autospawning of the PulseAudio daemon if required */
    PA_CONTEXT_NOFAIL = 0x0002U
    /**< Don't fail if the daemon is not available when pa_context_connect() is
     * called, instead enter PA_CONTEXT_CONNECTING state and wait for the daemon
     * to appear.  \since 0.9.15 */
} pa_context_flags_t;

// From PulseAudio src/pulse/mainloop.h:77-96,114-118 [v17.0].
/** An opaque main loop object */
typedef struct pa_mainloop pa_mainloop;

/** Allocate a new main loop object. Free with pa_mainloop_free. */
pa_mainloop *pa_mainloop_new(void);

/** Free a main loop object */
void pa_mainloop_free(pa_mainloop* m);

/** Prepare for a single iteration of the main loop. Returns a negative value
on error or exit request. timeout specifies a maximum timeout for the subsequent
poll, or -1 for blocking behaviour. The timeout is specified in microseconds. */
int pa_mainloop_prepare(pa_mainloop *m, int timeout);

/** Execute the previously prepared poll. Returns a negative value on error.*/
int pa_mainloop_poll(pa_mainloop *m);

/** Dispatch timeout, io and deferred events from the previously executed poll. Returns
a negative value on error. On success returns the number of source dispatched. */
int pa_mainloop_dispatch(pa_mainloop *m);


/** Return the abstract main loop abstraction layer vtable for this
    main loop. No need to free the API as it is owned by the loop
    and is destroyed when the loop is freed. */
pa_mainloop_api* pa_mainloop_get_api(pa_mainloop*m);

// From PulseAudio src/pulse/context.h:153-160,169-172,179-180,198-213 [v17.0].
/** An opaque connection context to a daemon */
typedef struct pa_context pa_context;

/** Generic notification callback prototype */
typedef void (*pa_context_notify_cb_t)(pa_context *c, void *userdata);

/** A generic callback for operation completion */
typedef void (*pa_context_success_cb_t) (pa_context *c, int success, void *userdata);

/** Instantiate a new connection context with an abstract mainloop API
 * and an application name. It is recommended to use pa_context_new_with_proplist()
 * instead and specify some initial properties.*/
pa_context *pa_context_new(pa_mainloop_api *mainloop, const char *name);

/** Decrease the reference counter of the context by one */
void pa_context_unref(pa_context *c);

/** Return the current context status */
pa_context_state_t pa_context_get_state(const pa_context *c);

/** Connect the context to the specified server. If server is NULL,
 * connect to the default server. This routine may but will not always
 * return synchronously on error. Use pa_context_set_state_callback() to
 * be notified when the connection is established. If flags doesn't have
 * PA_CONTEXT_NOAUTOSPAWN set and no specific server is specified or
 * accessible a new daemon is spawned. If api is non-NULL, the functions
 * specified in the structure are used when forking a new child
 * process. Returns negative on certain errors such as invalid state
 * or parameters. */
int pa_context_connect(pa_context *c, const char *server, pa_context_flags_t flags, const pa_spawn_api *api);

/** Terminate the context connection immediately */
void pa_context_disconnect(pa_context *c);

// From PulseAudio src/pulse/operation.h:32-33,41-52 [v17.0].
/** An asynchronous operation object */
typedef struct pa_operation pa_operation;

/** Decrease the reference count by one */
void pa_operation_unref(pa_operation *o);

/** Cancel the operation. Beware! This will not necessarily cancel the
 * execution of the operation on the server side. However it will make
 * sure that the callback associated with this operation will not be
 * called anymore, effectively disabling the operation from the client
 * side's view. */
void pa_operation_cancel(pa_operation *o);

/** Return the current status of the operation */
pa_operation_state_t pa_operation_get_state(const pa_operation *o);

// From PulseAudio src/pulse/introspect.h:464-479,484-485,493-494 [v17.0].
/** Stores information about modules. Please note that this structure
 * can be extended as part of evolutionary API updates at any time in
 * any new release. */
typedef struct pa_module_info {
    uint32_t index;                     /**< Index of the module */
    const char*name,                    /**< Name of the module */
        *argument;                      /**< Argument string of the module */
    uint32_t n_used;                    /**< Usage counter or PA_INVALID_INDEX */
/** \cond fulldocs */
    int auto_unload;                    /**< \deprecated Non-zero if this is an autoloaded module. */
/** \endcond */
    pa_proplist *proplist;              /**< Property list \since 0.9.15 */
} pa_module_info;

/** Callback prototype for pa_context_get_module_info() and friends */
typedef void (*pa_module_info_cb_t) (pa_context *c, const pa_module_info*i, int eol, void *userdata);

/** Get the complete list of currently loaded modules */
pa_operation* pa_context_get_module_info_list(pa_context *c, pa_module_info_cb_t cb, void *userdata);

/** Unload a module. */
pa_operation* pa_context_unload_module(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void *userdata);

} // extern "C"

} // namespace NereusSDR::PulseControlAbi
