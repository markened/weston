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

#include <errno.h>
#include <stdlib.h>

#include "openwfd-private.h"

static void
openwfd_restore(struct weston_compositor *compositor)
{
}

static void
openwfd_destroy(struct weston_compositor *compositor)
{
	struct openwfd_backend *backend =
		(struct openwfd_backend *) compositor;

	weston_compositor_shutdown(compositor);
	wfdDestroyDevice(backend->device);
	free(backend);
}

int
openwfd_head_create(struct openwfd_backend *backend)
{
	/* TODO: Discover WFD ports and construct heads in milestone 2. */
	errno = ENOSYS;
	return -1;
}

int
openwfd_output_create(struct openwfd_backend *backend,
		      struct openwfd_head *head)
{
	/* TODO: Allocate a rendering target and attach it to the WFD port. */
	errno = ENOSYS;
	return -1;
}

WL_EXPORT struct weston_compositor *
backend_init(struct wl_display *display, int *argc, char *argv[],
	     struct weston_config *config)
{
	struct openwfd_backend *backend;

	backend = zalloc(sizeof *backend);
	if (backend == NULL) {
		weston_log("OpenWFD: failed to allocate backend state\n");
		return NULL;
	}

	backend->device = wfdCreateDevice(WFD_DEFAULT_DEVICE_ID, NULL);
	if (backend->device == WFD_INVALID_HANDLE) {
		weston_log("OpenWFD: failed to open the default device\n");
		free(backend);
		return NULL;
	}

	wl_list_init(&backend->head_list);
	if (openwfd_probe_capabilities(backend->device,
				       &backend->capabilities) < 0 ||
	    !openwfd_capabilities_satisfy_requirements(
		    &backend->capabilities)) {
		wfdDestroyDevice(backend->device);
		free(backend);
		return NULL;
	}

	if (weston_compositor_init(&backend->base, display, argc, argv,
				   config) < 0) {
		weston_log("OpenWFD: failed to initialize Weston compositor state\n");
		wfdDestroyDevice(backend->device);
		free(backend);
		return NULL;
	}

	backend->base.destroy = openwfd_destroy;
	backend->base.restore = openwfd_restore;

	/* TODO: Initialize renderer and create heads/outputs in milestone 2. */
	return &backend->base;
}
