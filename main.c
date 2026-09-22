#define _GNU_SOURCE

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

enum {
    INITIAL_WIDTH = 560,
    INITIAL_HEIGHT = 360,
    INPUT_X = 24,
    INPUT_Y = 24,
    INPUT_WIDTH = 200,
    INPUT_HEIGHT = 80,
    MAX_BUFFER_BYTES = 128 * 1024 * 1024,
};

enum input_mode { INPUT_RECT, INPUT_EMPTY, INPUT_FULL };

struct buffer {
    struct wl_buffer *proxy;
    void *pixels;
    size_t size;
    struct wl_list link;
};

struct app {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_list buffers;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    enum input_mode input_mode;
    unsigned clicks;
    int width;
    int height;
    int pending_width;
    int pending_height;
    int running;
    int failed;
};

static int buffer_size(int width, int height, size_t *size)
{
    if (width <= 0 || height <= 0 || width > INT_MAX / 4)
        return 0;
    size_t stride = (size_t)width * 4;
    if (stride > MAX_BUFFER_BYTES / (size_t)height)
        return 0;
    *size = stride * (size_t)height;
    return 1;
}

static void paint(uint32_t *pixels, int width, int height, enum input_mode mode)
{
    memset(pixels, 0, (size_t)width * height * 4);
    if (mode == INPUT_EMPTY)
        return;
    for (int y = INPUT_Y; y < INPUT_Y + INPUT_HEIGHT && y < height; y++) {
        for (int x = INPUT_X; x < INPUT_X + INPUT_WIDTH && x < width; x++)
            pixels[(size_t)y * width + x] = 0xff20c878;
    }
}

static int create_shm_file(size_t size)
{
    int fd = memfd_create("wayland-input-region-test", MFD_CLOEXEC);
    if (fd < 0) {
        perror("memfd_create");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        perror("ftruncate");
        close(fd);
        exit(EXIT_FAILURE);
    }
    return fd;
}

static void destroy_buffer(struct buffer *buffer)
{
    wl_list_remove(&buffer->link);
    wl_buffer_destroy(buffer->proxy);
    munmap(buffer->pixels, buffer->size);
    free(buffer);
}

static void buffer_release(void *data, struct wl_buffer *proxy)
{
    (void)proxy;
    destroy_buffer(data);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static void draw(struct app *app)
{
    size_t size;
    if (!buffer_size(app->width, app->height, &size)) {
        fprintf(stderr, "invalid or excessive buffer size: %dx%d\n", app->width, app->height);
        app->failed = 1;
        app->running = 0;
        return;
    }
    const int stride = app->width * 4;
    int fd = create_shm_file(size);
    struct wl_shm_pool *pool;
    struct wl_region *input;
    struct buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        perror("calloc");
        close(fd);
        exit(EXIT_FAILURE);
    }
    buffer->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->pixels == MAP_FAILED) {
        perror("mmap");
        close(fd);
        exit(EXIT_FAILURE);
    }
    buffer->size = size;
    paint(buffer->pixels, app->width, app->height, app->input_mode);

    pool = wl_shm_create_pool(app->shm, fd, (int)size);
    buffer->proxy = wl_shm_pool_create_buffer(
        pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buffer->proxy, &buffer_listener, buffer);
    wl_list_insert(&app->buffers, &buffer->link);
    wl_shm_pool_destroy(pool);
    close(fd);

    input = wl_compositor_create_region(app->compositor);
    if (app->input_mode == INPUT_RECT)
        wl_region_add(input, INPUT_X, INPUT_Y, INPUT_WIDTH, INPUT_HEIGHT);
    wl_surface_set_input_region(app->surface, app->input_mode == INPUT_FULL ? NULL : input);
    wl_region_destroy(input);

    wl_surface_attach(app->surface, buffer->proxy, 0, 0);
    if (wl_proxy_get_version((struct wl_proxy *)app->surface) >= 4)
        wl_surface_damage_buffer(app->surface, 0, 0, app->width, app->height);
    else
        wl_surface_damage(app->surface, 0, 0, app->width, app->height);
    wl_surface_commit(app->surface);
    fprintf(stderr, "configured %dx%d\n", app->width, app->height);
}

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
    fprintf(stderr, "pointer enter %.1f %.1f\n", wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
    fputs("pointer leave\n", stderr);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)pointer; (void)time; (void)x; (void)y;
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state)
{
    struct app *app = data;
    (void)pointer; (void)serial; (void)time;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    fprintf(stderr, "button %u, press %u\n", button, ++app->clicks);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    struct app *app = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
        wl_pointer_destroy(app->pointer);
        app->pointer = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    struct app *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                           version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0 && !app->seat) {
        // Version 1 is sufficient for button logging; no newer pointer events are needed.
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_remove,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    struct app *app = data;
    xdg_surface_ack_configure(surface, serial);
    if (app->pending_width > 0)
        app->width = app->pending_width;
    if (app->pending_height > 0)
        app->height = app->pending_height;
    draw(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states)
{
    struct app *app = data;
    (void)toplevel;
    (void)states;
    app->pending_width = width;
    app->pending_height = height;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;
    (void)toplevel;
    app->running = 0;
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                     struct wl_array *capabilities)
{
    (void)data;
    (void)toplevel;
    (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities = toplevel_wm_capabilities,
};

int main(int argc, char **argv)
{
    struct app app = {
        .width = INITIAL_WIDTH,
        .height = INITIAL_HEIGHT,
        .running = 1,
    };
    struct wl_registry *registry;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("Usage: wayland-input-region-test [--empty-input | --full-input]\n"
                 "Default: transparent 560x360 window with a green 200x80 input region at 24,24.\n"
                 "--empty-input  No client pixels accept input; no green rectangle.\n"
                 "--full-input   All client pixels accept input, including transparent pixels.\n"
                 "Configure and pointer events are logged to stderr. Close with your window manager or Ctrl+C.");
            return EXIT_SUCCESS;
        }
        if (app.input_mode == INPUT_RECT && strcmp(argv[i], "--empty-input") == 0)
            app.input_mode = INPUT_EMPTY;
        else if (app.input_mode == INPUT_RECT && strcmp(argv[i], "--full-input") == 0)
            app.input_mode = INPUT_FULL;
        else {
            fprintf(stderr, "unknown or conflicting option: %s (see --help)\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    wl_list_init(&app.buffers);

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "failed to connect to Wayland display\n");
        return EXIT_FAILURE;
    }
    registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registry_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) {
        fputs("Wayland registry roundtrip failed\n", stderr);
        wl_display_disconnect(app.display);
        return EXIT_FAILURE;
    }
    if (!app.compositor || !app.shm || !app.wm_base) {
        fprintf(stderr, "required Wayland globals are unavailable\n");
        wl_display_disconnect(app.display);
        return EXIT_FAILURE;
    }

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.toplevel, &toplevel_listener, &app);
    xdg_toplevel_set_title(app.toplevel, "Wayland input-region test");
    xdg_toplevel_set_app_id(app.toplevel, "wayland-input-region-test");
    xdg_toplevel_set_min_size(app.toplevel, 240, 160);
    xdg_toplevel_set_max_size(app.toplevel, 0, 0);
    wl_surface_commit(app.surface);

    while (app.running) {
        if (wl_display_dispatch(app.display) < 0) {
            fputs("Wayland connection failed\n", stderr);
            app.failed = 1;
            break;
        }
    }

    xdg_toplevel_destroy(app.toplevel);
    xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface);
    struct buffer *buffer, *next;
    wl_list_for_each_safe(buffer, next, &app.buffers, link)
        destroy_buffer(buffer);
    if (app.pointer)
        wl_pointer_destroy(app.pointer);
    if (app.seat)
        wl_seat_destroy(app.seat);
    xdg_wm_base_destroy(app.wm_base);
    wl_shm_destroy(app.shm);
    wl_compositor_destroy(app.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(app.display);
    return app.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
