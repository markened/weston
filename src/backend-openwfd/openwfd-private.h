/*
 * Copyright © 2026 Weston contributors
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and the permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. The copyright holders make no
 * representations about the suitability of this software for any purpose.
 * It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef OPENWFD_PRIVATE_H
#define OPENWFD_PRIVATE_H

#include "compositor.h"
#include "openwfd-capabilities.h"
#include "gl-renderer.h"

struct openwfd_mode {
	struct weston_mode base;
	WFDPortMode mode;
};

struct openwfd_backend {
	struct weston_compositor base;
	WFDDevice device;
	WFDEvent event;
	struct wl_event_source *event_timer;
	struct gl_renderer_interface *gl_renderer;
	struct openwfd_capabilities capabilities;
	struct wl_list head_list;
	int initialized;
};

struct openwfd_head {
	struct openwfd_backend *backend;
	WFDPort port;
	struct weston_output *output;
	WFDint port_id;
	int pipeline_id;
	int x;
	struct wl_list link;
};

struct openwfd_output {
	struct weston_output base;
	struct openwfd_head *head;
	WFDPipeline pipeline;
	WFDSource frame_source[2];
	struct wl_event_source *frame_timer;
	int current_slot;
	int pending_slot;
	int x;
};

int
openwfd_head_create(struct openwfd_backend *backend, WFDint port_id, int x);

int
openwfd_output_create(struct openwfd_backend *backend,
		      struct openwfd_head *head);

void
openwfd_dispatch_events(struct openwfd_backend *backend);

#endif
