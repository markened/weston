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

#ifndef OPENWFD_CAPABILITIES_H
#define OPENWFD_CAPABILITIES_H

#include <WF/wfd.h>

enum openwfd_completion_semantics {
	OPENWFD_COMPLETE_IS_LATCH_AND_RELEASE,
	OPENWFD_COMPLETE_IS_LATCH_ONLY,
	OPENWFD_COMPLETE_IS_API_ONLY,
};

struct openwfd_capabilities {
	int eglimage_source;
	int at_vsync_transition;
	int bind_completion_events;
	int reliable_source_release;

	int explicit_acquire_fence;
	int explicit_release_fence;

	int overlays;
	int source_alpha;
	int global_alpha;
	int scaling;
	int rotation;
	int partial_refresh;
	int protected_output;

	enum openwfd_completion_semantics completion_semantics;
};

int
openwfd_probe_capabilities(WFDDevice dev,
			   struct openwfd_capabilities *caps);

int
openwfd_capabilities_satisfy_requirements(
	const struct openwfd_capabilities *caps);

#endif
