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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "compositor.h"
#include "openwfd-capabilities.h"

static const char *
openwfd_completion_semantics_name(enum openwfd_completion_semantics semantics)
{
	switch (semantics) {
	case OPENWFD_COMPLETE_IS_LATCH_AND_RELEASE:
		return "latch-and-release";
	case OPENWFD_COMPLETE_IS_LATCH_ONLY:
		return "latch-only";
	case OPENWFD_COMPLETE_IS_API_ONLY:
		return "API-only";
	default:
		return "unknown";
	}
}

int
openwfd_probe_capabilities(WFDDevice dev, struct openwfd_capabilities *caps)
{
	enum { max_extensions_to_log = 256 };
	WFDint device_id;
	WFDint extension_count;
	WFDint extension_capacity;
	WFDint extension_result;
	WFDint i;
	WFDErrorCode error;
	const char **extensions;

	if (caps == NULL)
		return -1;

	memset(caps, 0, sizeof *caps);
	caps->completion_semantics = OPENWFD_COMPLETE_IS_API_ONLY;
	if (dev == WFD_INVALID_HANDLE) {
		weston_log("OpenWFD: capability probe received an invalid device\n");
		return -1;
	}

	device_id = wfdGetDeviceAttribi(dev, WFD_DEVICE_ID);
	error = wfdGetError(dev);
	if (error != WFD_ERROR_NONE) {
		weston_log("OpenWFD: querying device ID failed with error %d\n",
			   error);
		return -1;
	}

	extension_count = wfdGetStrings(dev, WFD_EXTENSIONS, NULL, 0);
	error = wfdGetError(dev);
	if (error != WFD_ERROR_NONE || extension_count < 0) {
		weston_log("OpenWFD: extension list unavailable "
			   "(error %d); assuming no extensions\n", error);
		extension_count = 0;
	} else if (extension_count > 0) {
		extension_capacity = extension_count;
		if (extension_capacity > max_extensions_to_log) {
			weston_log("OpenWFD: logging the first %d of %d "
				   "advertised extensions\n",
				   max_extensions_to_log, extension_capacity);
			extension_capacity = max_extensions_to_log;
		}
		extensions = calloc(extension_capacity, sizeof *extensions);
		if (extensions == NULL) {
			weston_log("OpenWFD: unable to allocate extension list\n");
			return -1;
		}

		extension_result = wfdGetStrings(dev, WFD_EXTENSIONS, extensions,
						 extension_capacity);
		error = wfdGetError(dev);
		if (error != WFD_ERROR_NONE || extension_result < 0) {
			weston_log("OpenWFD: reading extension list failed "
				   "with error %d\n", error);
			extension_count = 0;
		} else {
			extension_count = MIN(extension_result, extension_capacity);
			for (i = 0; i < extension_count; i++)
				weston_log("OpenWFD: advertised extension: %s "
					   "(not yet mapped to a capability)\n",
					   extensions[i] ? extensions[i] :
					   "(unnamed)");
		}

		free(extensions);
	}

	/*
	 * The core OpenWF-Display API does not expose the EGLImage binding,
	 * completion-event, or source-release guarantees required here. Leave
	 * those and optional capabilities disabled until a vendor probe exists.
	 */
	weston_log("OpenWFD: capability matrix (1=supported, 0=unknown/assumed no): "
		   "eglimage_source=%d at_vsync_transition=%d "
		   "bind_completion_events=%d reliable_source_release=%d "
		   "explicit_acquire_fence=%d explicit_release_fence=%d "
		   "overlays=%d source_alpha=%d global_alpha=%d scaling=%d "
		   "rotation=%d partial_refresh=%d protected_output=%d\n",
		   caps->eglimage_source, caps->at_vsync_transition,
		   caps->bind_completion_events, caps->reliable_source_release,
		   caps->explicit_acquire_fence, caps->explicit_release_fence,
		   caps->overlays, caps->source_alpha, caps->global_alpha,
		   caps->scaling, caps->rotation, caps->partial_refresh,
		   caps->protected_output);
	weston_log("OpenWFD: device ID %d; completion semantics %s "
		   "(conservative default; vendor confirmation required)\n",
		   device_id,
		   openwfd_completion_semantics_name(caps->completion_semantics));

	return 0;
}

int
openwfd_capabilities_satisfy_requirements(
	const struct openwfd_capabilities *caps)
{
	int valid = 1;

	if (caps == NULL)
		return 0;

	if (!caps->eglimage_source) {
		weston_log("OpenWFD: missing required capability eglimage_source\n");
		valid = 0;
	}
	if (!caps->at_vsync_transition) {
		weston_log("OpenWFD: missing required capability at_vsync_transition\n");
		valid = 0;
	}
	if (!caps->bind_completion_events) {
		weston_log("OpenWFD: missing required capability bind_completion_events\n");
		valid = 0;
	}
	if (!caps->reliable_source_release) {
		weston_log("OpenWFD: missing required capability reliable_source_release\n");
		valid = 0;
	}
	if (caps->completion_semantics < OPENWFD_COMPLETE_IS_LATCH_AND_RELEASE ||
	    caps->completion_semantics > OPENWFD_COMPLETE_IS_API_ONLY) {
		weston_log("OpenWFD: invalid completion semantics value %d\n",
			   caps->completion_semantics);
		valid = 0;
	}
	if (caps->completion_semantics == OPENWFD_COMPLETE_IS_API_ONLY &&
	    !caps->reliable_source_release) {
		weston_log("OpenWFD: API-only completion has no configured "
			   "alternative source-release guarantee\n");
		valid = 0;
	}

	if (!valid)
		weston_log("OpenWFD: required capability gate failed\n");
	else
		weston_log("OpenWFD: required capability gate passed\n");

	return valid;
}
