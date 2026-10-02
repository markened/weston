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

struct openwfd_backend {
	struct weston_compositor base;
	WFDDevice device;
	struct openwfd_capabilities capabilities;
	struct wl_list head_list;
};

struct openwfd_head {
	WFDPort port;
	struct wl_list link;
};

struct openwfd_output {
	struct weston_output base;
	struct openwfd_head *head;
};

int
openwfd_head_create(struct openwfd_backend *backend);

int
openwfd_output_create(struct openwfd_backend *backend,
		      struct openwfd_head *head);

#endif
