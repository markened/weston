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

#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openwfd-private.h"

static const EGLint openwfd_egl_config[] = {
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_NONE
};

static void
openwfd_restore(struct weston_compositor *compositor)
{
}

static int
openwfd_has_pending_frame(struct openwfd_backend *backend)
{
	struct openwfd_output *output;

	wl_list_for_each(output, &backend->base.output_list, base.link)
		if (output->pending_slot >= 0)
			return 1;

	return 0;
}

static int
openwfd_event_timer_handler(void *data)
{
	struct openwfd_backend *backend = data;

	openwfd_dispatch_events(backend);
	wl_event_source_timer_update(backend->event_timer,
				     openwfd_has_pending_frame(backend) ? 1 : 1000);

	return 0;
}

static int
openwfd_frame_timer_handler(void *data)
{
	struct openwfd_output *output = data;

	weston_output_finish_frame(&output->base, weston_compositor_get_time());

	return 0;
}

static void
openwfd_output_start_repaint_loop(struct weston_output *base)
{
	struct openwfd_output *output =
		(struct openwfd_output *) base;

	if (output->pending_slot < 0)
		wl_event_source_timer_update(output->frame_timer, 16);
}

static int
openwfd_output_repaint(struct weston_output *base,
		       pixman_region32_t *damage)
{
	struct openwfd_output *output = (struct openwfd_output *) base;
	struct openwfd_backend *backend =
		(struct openwfd_backend *) base->compositor;
	struct openwfd_head *head = output->head;
	int slot;

	if (output->pending_slot >= 0)
		return 0;

	for (slot = 0; slot < 2; slot++)
		if (slot != output->current_slot)
			break;
	if (slot == 2)
		return -1;

	backend->gl_renderer->output_set_target(base, slot);
	backend->base.renderer->repaint_output(base, damage);
	backend->gl_renderer->output_sync(base);

	if (output->frame_source[slot] == WFD_INVALID_HANDLE) {
		EGLImageKHR image =
			backend->gl_renderer->output_get_image(base, slot);

		output->frame_source[slot] =
			wfdCreateSourceFromImage(backend->device, output->pipeline,
						 (WFDEGLImage) image, NULL);
		if (output->frame_source[slot] == WFD_INVALID_HANDLE ||
		    wfdGetError(backend->device) != WFD_ERROR_NONE) {
			weston_log("OpenWFD: failed to recreate EGLImage source\n");
			return -1;
		}
	}

	wfdBindSourceToPipeline(backend->device, output->pipeline,
				output->frame_source[slot],
				WFD_TRANSITION_AT_VSYNC, NULL);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE) {
		weston_log("OpenWFD: failed to bind source on port %d\n",
			   head->port_id);
		return -1;
	}

	wfdDeviceCommit(backend->device, WFD_COMMIT_PIPELINE,
			output->pipeline);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE) {
		weston_log("OpenWFD: failed to commit pipeline on port %d\n",
			   head->port_id);
		return -1;
	}

	output->pending_slot = slot;
	backend->capabilities.at_vsync_transition = 1;
	wl_event_source_timer_update(backend->event_timer, 1);

	pixman_region32_subtract(&backend->base.primary_plane.damage,
				 &backend->base.primary_plane.damage, damage);

	return 0;
}

static void
openwfd_output_destroy(struct weston_output *base)
{
	struct openwfd_output *output = (struct openwfd_output *) base;
	struct openwfd_backend *backend =
		(struct openwfd_backend *) base->compositor;
	struct openwfd_head *head = output->head;
	struct weston_mode *mode, *next;

	if (output->frame_timer)
		wl_event_source_remove(output->frame_timer);
	if (backend->device != WFD_INVALID_HANDLE) {
		int i;

		for (i = 0; i < 2; i++)
			if (output->frame_source[i] != WFD_INVALID_HANDLE)
				wfdDestroySource(backend->device,
						 output->frame_source[i]);
		wfdDestroyPipeline(backend->device, output->pipeline);
		wfdDestroyPort(backend->device, head->port);
	}

	if (base->renderer_state)
		backend->gl_renderer->output_destroy(base);

	wl_list_for_each_safe(mode, next, &base->mode_list, link) {
		struct openwfd_mode *wfd_mode =
			container_of(mode, struct openwfd_mode, base);

		wl_list_remove(&mode->link);
		free(wfd_mode);
	}

	wl_list_remove(&head->link);
	weston_output_destroy(base);
	free(output);
	free(head);
}

static int
openwfd_get_mode_size(WFDDevice device, WFDPort port, WFDPortMode mode,
		      int *width, int *height, uint32_t *refresh)
{
	WFDfloat rate;

	*width = wfdGetPortModeAttribi(device, port, mode,
				       WFD_PORT_MODE_WIDTH);
	if (wfdGetError(device) != WFD_ERROR_NONE)
		return -1;
	*height = wfdGetPortModeAttribi(device, port, mode,
					WFD_PORT_MODE_HEIGHT);
	if (wfdGetError(device) != WFD_ERROR_NONE)
		return -1;
	rate = wfdGetPortModeAttribf(device, port, mode,
				     WFD_PORT_MODE_REFRESH_RATE);
	if (wfdGetError(device) != WFD_ERROR_NONE)
		return -1;
	if (*width <= 0 || *height <= 0 || rate <= 0.0f)
		return -1;

	*refresh = rate * 1000.0f;
	return 0;
}

static void
openwfd_destroy_unregistered_head(struct openwfd_backend *backend,
				 struct openwfd_head *head,
				 WFDPipeline pipeline)
{
	if (pipeline != WFD_INVALID_HANDLE)
		wfdDestroyPipeline(backend->device, pipeline);
	if (head && head->port != WFD_INVALID_HANDLE)
		wfdDestroyPort(backend->device, head->port);
	free(head);
}

int
openwfd_output_create(struct openwfd_backend *backend,
		      struct openwfd_head *head)
{
	struct openwfd_output *output;
	struct openwfd_mode *mode, *current = NULL;
	WFDPortMode *port_modes;
	WFDint mode_count, pipeline_count, i;
	WFDint *pipeline_ids;
	WFDPortMode current_mode;
	WFDint rect[4];
	WFDint pipeline_attribs[] = { WFD_NONE };
	WFDint width, height;
	uint32_t refresh;
	char name[32];

	mode_count = wfdGetPortModes(backend->device, head->port, NULL, 0);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE || mode_count <= 0) {
		weston_log("OpenWFD: port %d has no available modes\n",
			   head->port_id);
		return -1;
	}
	port_modes = calloc(mode_count, sizeof *port_modes);
	if (!port_modes)
		return -1;
	mode_count = wfdGetPortModes(backend->device, head->port,
				     port_modes, mode_count);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE || mode_count <= 0) {
		free(port_modes);
		return -1;
	}

	current_mode = wfdGetCurrentPortMode(backend->device, head->port);
	wfdGetError(backend->device);
	output = zalloc(sizeof *output);
	if (!output) {
		free(port_modes);
		return -1;
	}
	output->head = head;
	output->current_slot = -1;
	output->pending_slot = -1;
	output->frame_source[0] = WFD_INVALID_HANDLE;
	output->frame_source[1] = WFD_INVALID_HANDLE;
	wl_list_init(&output->base.mode_list);

	for (i = 0; i < mode_count; i++) {
		mode = zalloc(sizeof *mode);
		if (!mode)
			goto err_output;
		mode->mode = port_modes[i];
		if (openwfd_get_mode_size(backend->device, head->port,
					  mode->mode, &width, &height,
					  &refresh) < 0) {
			free(mode);
			continue;
		}
		mode->base.width = width;
		mode->base.height = height;
		mode->base.refresh = refresh;
		if (mode->mode == current_mode) {
			if (current)
				current->base.flags &= ~WL_OUTPUT_MODE_CURRENT;
			mode->base.flags |= WL_OUTPUT_MODE_CURRENT |
					    WL_OUTPUT_MODE_PREFERRED;
			current = mode;
		} else if (current == NULL) {
			mode->base.flags |= WL_OUTPUT_MODE_CURRENT |
					    WL_OUTPUT_MODE_PREFERRED;
			current = mode;
		}
		wl_list_insert(output->base.mode_list.prev, &mode->base.link);
	}
	free(port_modes);
	port_modes = NULL;
	if (current == NULL)
		goto err_output;
	output->base.current_mode = &current->base;
	output->base.native_mode = &current->base;
	output->base.original_mode = &current->base;

	pipeline_count = wfdGetPortAttribi(backend->device, head->port,
					   WFD_PORT_PIPELINE_ID_COUNT);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE ||
	    pipeline_count <= 0) {
		weston_log("OpenWFD: port %d has no bindable pipeline\n",
			   head->port_id);
		goto err_output;
	}
	pipeline_ids = calloc(pipeline_count, sizeof *pipeline_ids);
	if (!pipeline_ids)
		goto err_output;
	wfdGetPortAttribiv(backend->device, head->port,
			   WFD_PORT_BINDABLE_PIPELINE_IDS,
			   pipeline_count, pipeline_ids);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE) {
		free(pipeline_ids);
		goto err_output;
	}
	head->pipeline_id = pipeline_ids[0];
	free(pipeline_ids);

	output->pipeline = wfdCreatePipeline(backend->device,
					     head->pipeline_id,
					     pipeline_attribs);
	if (output->pipeline == WFD_INVALID_HANDLE) {
		weston_log("OpenWFD: failed to create pipeline %d\n",
			   head->pipeline_id);
		goto err_output;
	}
	wfdBindPipelineToPort(backend->device, head->port, output->pipeline);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;

	wfdSetPortMode(backend->device, head->port, current->mode);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;
	wfdDeviceCommit(backend->device, WFD_COMMIT_ENTIRE_PORT, head->port);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;

	rect[0] = 0;
	rect[1] = 0;
	rect[2] = current->base.width;
	rect[3] = current->base.height;
	wfdSetPipelineAttribiv(backend->device, output->pipeline,
			       WFD_PIPELINE_DESTINATION_RECTANGLE, 4, rect);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;
	wfdSetPipelineAttribiv(backend->device, output->pipeline,
			       WFD_PIPELINE_SOURCE_RECTANGLE, 4, rect);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;
	wfdDeviceCommit(backend->device, WFD_COMMIT_PIPELINE,
			output->pipeline);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE)
		goto err_pipeline;

	weston_output_init(&output->base, &backend->base, head->x, 0,
			   0, 0,
			   WL_OUTPUT_TRANSFORM_NORMAL, 1);
	wl_list_insert(backend->base.output_list.prev, &output->base.link);
	snprintf(name, sizeof name, "WFD-%d", head->port_id);
	output->base.name = strdup(name);
	if (!output->base.name)
		goto err_weston_output;
	output->base.make = "OpenWF";
	output->base.model = "Display";
	output->base.start_repaint_loop = openwfd_output_start_repaint_loop;
	output->base.repaint = openwfd_output_repaint;
	output->base.destroy = openwfd_output_destroy;
	output->base.assign_planes = NULL;
	output->base.set_backlight = NULL;
	output->base.set_dpms = NULL;
	output->base.switch_mode = NULL;
	output->frame_timer = wl_event_loop_add_timer(
		wl_display_get_event_loop(backend->base.wl_display),
		openwfd_frame_timer_handler, output);
	if (!output->frame_timer)
		goto err_weston_output;

	if (backend->gl_renderer->output_create_offscreen(&output->base) < 0) {
		weston_log("OpenWFD: failed to create offscreen renderer for port %d\n",
			   head->port_id);
		goto err_frame_timer;
	}

	for (i = 0; i < 2; i++) {
		EGLImageKHR image =
			backend->gl_renderer->output_get_image(&output->base, i);

		output->frame_source[i] =
			wfdCreateSourceFromImage(backend->device, output->pipeline,
						 (WFDEGLImage) image, NULL);
		if (output->frame_source[i] == WFD_INVALID_HANDLE ||
		    wfdGetError(backend->device) != WFD_ERROR_NONE) {
			weston_log("OpenWFD: failed to create EGLImage source "
				   "for port %d\n", head->port_id);
			goto err_frame_timer;
		}
	}

	openwfd_probe_port_pipeline(backend->device, head->port,
				    output->pipeline,
				    &backend->capabilities);
	backend->capabilities.eglimage_source = 1;
	head->output = &output->base;
	wl_list_insert(backend->head_list.prev, &head->link);
	weston_log("OpenWFD: output %s at %dx%d@%u\n", output->base.name,
		   current->base.width, current->base.height,
		   current->base.refresh);

	return 0;

err_frame_timer:
	if (output->frame_timer)
		wl_event_source_remove(output->frame_timer);
	for (i = 0; i < 2; i++) {
		if (output->frame_source[i] != WFD_INVALID_HANDLE) {
			wfdDestroySource(backend->device,
					 output->frame_source[i]);
			wfdGetError(backend->device);
			output->frame_source[i] = WFD_INVALID_HANDLE;
		}
	}
	if (output->base.renderer_state)
		backend->gl_renderer->output_destroy(&output->base);
err_weston_output:
	weston_output_destroy(&output->base);
err_pipeline:
	if (output->pipeline != WFD_INVALID_HANDLE)
		wfdDestroyPipeline(backend->device, output->pipeline);
err_output:
	if (port_modes)
		free(port_modes);
	{
		struct weston_mode *weston_mode, *next;

		wl_list_for_each_safe(weston_mode, next,
				      &output->base.mode_list, link) {
			struct openwfd_mode *wfd_mode =
				container_of(weston_mode, struct openwfd_mode, base);

			wl_list_remove(&weston_mode->link);
			free(wfd_mode);
		}
	}
	free(output);
	return -1;
}

int
openwfd_head_create(struct openwfd_backend *backend, WFDint port_id, int x)
{
	struct openwfd_head *head;
	WFDint attached;
	WFDint pipeline_count;

	head = zalloc(sizeof *head);
	if (!head)
		return -1;
	head->backend = backend;
	head->port_id = port_id;
	head->x = x;
	head->port = wfdCreatePort(backend->device, port_id, NULL);
	if (head->port == WFD_INVALID_HANDLE) {
		free(head);
		return -1;
	}

	attached = wfdGetPortAttribi(backend->device, head->port,
				     WFD_PORT_ATTACHED);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE ||
	    attached == WFD_FALSE) {
		wfdDestroyPort(backend->device, head->port);
		free(head);
		return 0;
	}
	pipeline_count = wfdGetPortAttribi(backend->device, head->port,
					   WFD_PORT_PIPELINE_ID_COUNT);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE ||
	    pipeline_count <= 0) {
		wfdDestroyPort(backend->device, head->port);
		free(head);
		return -1;
	}
	if (pipeline_count > 1)
		backend->capabilities.overlays = 1;

	if (openwfd_output_create(backend, head) < 0) {
		openwfd_destroy_unregistered_head(backend, head,
						  WFD_INVALID_HANDLE);
		return -1;
	}

	return 0;
}

static int
openwfd_enumerate_heads(struct openwfd_backend *backend)
{
	WFDint *port_ids;
	WFDint port_count;
	WFDint i;
	int x = 0;

	port_count = wfdEnumeratePorts(backend->device, NULL, 0, NULL);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE || port_count <= 0) {
		weston_log("OpenWFD: device has no discoverable ports\n");
		return -1;
	}
	port_ids = calloc(port_count, sizeof *port_ids);
	if (!port_ids)
		return -1;
	port_count = wfdEnumeratePorts(backend->device, port_ids,
				       port_count, NULL);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE ||
	    port_count <= 0) {
		free(port_ids);
		return -1;
	}

	for (i = 0; i < port_count; i++) {
		struct openwfd_head *head;

		if (openwfd_head_create(backend, port_ids[i], x) < 0) {
			free(port_ids);
			return -1;
		}
		wl_list_for_each(head, &backend->head_list, link)
			if (head->port_id == port_ids[i] && head->output)
				x += head->output->width;
	}
	free(port_ids);

	if (wl_list_empty(&backend->head_list)) {
		weston_log("OpenWFD: no attached outputs found\n");
		return -1;
	}

	return 0;
}

static void
openwfd_event_handle(struct openwfd_backend *backend)
{
	WFDint pipeline_id;
	WFDint source;
	WFDErrorCode error;
	struct openwfd_output *output;

	pipeline_id = wfdGetEventAttribi(backend->device, backend->event,
					 WFD_EVENT_PIPELINE_BIND_PIPELINE_ID);
	error = wfdGetError(backend->device);
	if (error != WFD_ERROR_NONE)
		return;
	source = wfdGetEventAttribi(backend->device, backend->event,
				    WFD_EVENT_PIPELINE_BIND_SOURCE);
	error = wfdGetError(backend->device);
	if (error != WFD_ERROR_NONE)
		return;
	if (wfdGetEventAttribi(backend->device, backend->event,
			       WFD_EVENT_PIPELINE_BIND_QUEUE_OVERFLOW) != 0)
		weston_log("OpenWFD: pipeline bind event queue overflow\n");
	wfdGetError(backend->device);

	wl_list_for_each(output, &backend->base.output_list, base.link) {
		struct openwfd_head *head = output->head;
		int old_slot = output->current_slot;

		if (head->pipeline_id != pipeline_id ||
		    output->pending_slot < 0 ||
		    output->frame_source[output->pending_slot] !=
			    (WFDSource) source)
			continue;

		if (old_slot >= 0) {
			wfdDestroySource(backend->device,
					 output->frame_source[old_slot]);
			if (wfdGetError(backend->device) == WFD_ERROR_NONE)
				output->frame_source[old_slot] =
					WFD_INVALID_HANDLE;
			else
				weston_log("OpenWFD: failed to release prior source\n");
		}
		output->current_slot = output->pending_slot;
		output->pending_slot = -1;
		backend->capabilities.reliable_source_release = 1;
		backend->capabilities.completion_semantics =
			OPENWFD_COMPLETE_IS_LATCH_AND_RELEASE;

		weston_output_finish_frame(&output->base,
					   weston_compositor_get_time());
		return;
	}
}

void
openwfd_dispatch_events(struct openwfd_backend *backend)
{
	WFDEventType type;
	int event_count = 0;

	if (backend->event == WFD_INVALID_HANDLE)
		return;

	while (event_count++ < 32) {
		type = wfdDeviceEventWait(backend->device, backend->event, 0);
		if (type == WFD_EVENT_NONE) {
			wfdGetError(backend->device);
			break;
		}
		if (wfdGetError(backend->device) != WFD_ERROR_NONE) {
			weston_log("OpenWFD: waiting for device events failed\n");
			break;
		}
		if (type == WFD_EVENT_PIPELINE_BIND_SOURCE_COMPLETE)
			openwfd_event_handle(backend);
		else
			weston_log("OpenWFD: unexpected device event %d\n", type);
	}
}

static int
openwfd_create_event(struct openwfd_backend *backend)
{
	static const WFDint attribs[] = {
		WFD_EVENT_PIPELINE_BIND_QUEUE_SIZE, 64,
		WFD_NONE
	};
	static const WFDEventType filter[] = {
		WFD_EVENT_PIPELINE_BIND_SOURCE_COMPLETE,
		WFD_EVENT_NONE
	};

	backend->event = wfdCreateEvent(backend->device, attribs);
	if (backend->event == WFD_INVALID_HANDLE) {
		weston_log("OpenWFD: failed to create pipeline completion event\n");
		return -1;
	}
	wfdDeviceEventFilter(backend->device, backend->event, filter);
	if (wfdGetError(backend->device) != WFD_ERROR_NONE) {
		weston_log("OpenWFD: failed to filter pipeline completion event\n");
		wfdDestroyEvent(backend->device, backend->event);
		backend->event = WFD_INVALID_HANDLE;
		return -1;
	}

	backend->capabilities.bind_completion_events = 1;
	backend->capabilities.reliable_source_release = 1;
	backend->capabilities.at_vsync_transition = 1;
	backend->capabilities.completion_semantics =
		OPENWFD_COMPLETE_IS_LATCH_AND_RELEASE;

	return 0;
}

static void
openwfd_destroy(struct weston_compositor *compositor)
{
	struct openwfd_backend *backend =
		(struct openwfd_backend *) compositor;

	if (backend->event_timer)
		wl_event_source_remove(backend->event_timer);
	if (backend->device != WFD_INVALID_HANDLE) {
		wfdDestroyDevice(backend->device);
		backend->device = WFD_INVALID_HANDLE;
	}
	if (backend->initialized)
		weston_compositor_shutdown(compositor);
	free(backend);
}

WL_EXPORT struct weston_compositor *
backend_init(struct wl_display *display, int *argc, char *argv[],
	     struct weston_config *config)
{
	struct openwfd_backend *backend;
	struct wl_event_loop *loop;
	WFDint count;
	WFDint device_id;

	backend = zalloc(sizeof *backend);
	if (backend == NULL) {
		weston_log("OpenWFD: failed to allocate backend state\n");
		return NULL;
	}
	backend->device = WFD_INVALID_HANDLE;
	backend->event = WFD_INVALID_HANDLE;
	wl_list_init(&backend->head_list);

	count = wfdEnumerateDevices(NULL, 0, NULL);
	if (count <= 0) {
		weston_log("OpenWFD: no devices enumerated\n");
		goto err_backend;
	}
	{
		WFDint *device_ids = calloc(count, sizeof *device_ids);

		if (!device_ids)
			goto err_backend;
		count = wfdEnumerateDevices(device_ids, count, NULL);
		if (count <= 0) {
			free(device_ids);
			weston_log("OpenWFD: failed to enumerate device IDs\n");
			goto err_backend;
		}
		device_id = device_ids[0];
		free(device_ids);
	}

	backend->device = wfdCreateDevice(device_id, NULL);
	if (backend->device == WFD_INVALID_HANDLE) {
		weston_log("OpenWFD: failed to open device %d\n", device_id);
		goto err_backend;
	}
	if (openwfd_probe_capabilities(backend->device,
				       &backend->capabilities) < 0)
		goto err_device;
	if (openwfd_create_event(backend) < 0)
		goto err_device;

	if (weston_compositor_init(&backend->base, display, argc, argv,
				   config) < 0) {
		weston_log("OpenWFD: failed to initialize Weston compositor state\n");
		goto err_device;
	}
	backend->initialized = 1;
	backend->base.destroy = openwfd_destroy;
	backend->base.restore = openwfd_restore;

	backend->gl_renderer = weston_load_module("gl-renderer.so",
						  "gl_renderer_interface");
	if (!backend->gl_renderer) {
		weston_log("OpenWFD: failed to load the GL renderer\n");
		goto err_initialized;
	}
	if (backend->gl_renderer->create(&backend->base, EGL_DEFAULT_DISPLAY,
					openwfd_egl_config, NULL) < 0) {
		weston_log("OpenWFD: failed to initialize the GL renderer\n");
		goto err_initialized;
	}

	loop = wl_display_get_event_loop(display);
	backend->event_timer =
		wl_event_loop_add_timer(loop, openwfd_event_timer_handler, backend);
	if (!backend->event_timer) {
		weston_log("OpenWFD: failed to create event timer\n");
		goto err_initialized;
	}
	wl_event_source_timer_update(backend->event_timer, 1000);

	if (openwfd_enumerate_heads(backend) < 0)
		goto err_initialized;
	openwfd_log_capabilities(&backend->capabilities);
	if (!openwfd_capabilities_satisfy_requirements(
		    &backend->capabilities))
		goto err_initialized;

	{
		struct openwfd_output *output;

		wl_list_for_each(output, &backend->base.output_list, base.link)
		weston_output_schedule_repaint(&output->base);
	}

	return &backend->base;

err_initialized:
	if (backend->event_timer) {
		wl_event_source_remove(backend->event_timer);
		backend->event_timer = NULL;
	}
	wfdDestroyDevice(backend->device);
	backend->device = WFD_INVALID_HANDLE;
	weston_compositor_shutdown(&backend->base);
	free(backend);
	return NULL;

err_device:
	if (backend->event != WFD_INVALID_HANDLE)
		wfdDestroyEvent(backend->device, backend->event);
	if (backend->device != WFD_INVALID_HANDLE)
		wfdDestroyDevice(backend->device);
err_backend:
	free(backend);
	return NULL;
}
