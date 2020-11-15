#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h> 
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

struct simple_client {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_buffer *buffer;
  struct wl_surface *surface;
  struct wl_shm *shm;
  struct wl_shell *shell;
  struct wl_shell_surface *shell_surface;
  void *data;
  int width, height;
};

void die(const char msg[]) {
  fprintf(stderr, "%s", msg);
  exit(EXIT_FAILURE);
}

static void handle_ping(void *data, struct wl_shell_surface *shell_surface,
                        uint32_t serial) {
  wl_shell_surface_pong(shell_surface, serial);
}

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  struct simple_client *client = data;
  printf("interface=%s name=%0x version=%d\n", interface, name, version);
  if (strcmp(interface, "wl_compositor") == 0)
    client->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 1);
  else if (strcmp(interface, "wl_shell") == 0)
    client->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
  else if (strcmp(interface, "wl_shm") == 0)
    client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
}

static inline int memfd_create(const char *name, unsigned int flags) {
  return syscall(__NR_memfd_create, name, flags);
}

static void create_shm_buffer(struct simple_client *client) {
  struct wl_shm_pool *pool;
  int fd, size, stride;

  stride = client->width * 4;
  size = stride * client->height;

  fd = memfd_create("wayland-shm", 0);
  if (fd < 0) {
    fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
    exit(1);
  }

  int ret = ftruncate(fd, size);
  if (ret < 0) {
    fprintf(stderr, "Truncating a buffer file for %d B failed: %m\n", size);
    exit(1);
  }

  client->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (client->data == MAP_FAILED) {
    fprintf(stderr, "mmap failed: %m\n");
    close(fd);
    exit(1);
  }

  pool = wl_shm_create_pool(client->shm, fd, size);
  client->buffer = wl_shm_pool_create_buffer(
      pool, 0, client->width, client->height, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);

  close(fd);
}

void draw_argb8888(void *d, uint8_t a, uint8_t r, uint8_t g, uint8_t b,
                   size_t count) {
  while (count-- > 0)
    *((uint32_t *)d + count) = ((a << 24) | (r << 16) | (g << 8) | b);
}

struct simple_client *simple_client_create() {
  static struct wl_registry_listener registry_listener = {
      registry_handle_global, NULL};
  struct simple_client *client = malloc(sizeof(struct simple_client));
  if (!client) die("Cannot allocate memory for simple_client\n");

  client->display = wl_display_connect(NULL);
  if (!client->display) die("Cannot connect to Wayland display\n");

  client->registry = wl_display_get_registry(client->display);
  if (!client->registry) die("Cannot get registry from Wayland display\n");
  wl_registry_add_listener(client->registry, &registry_listener, client);

  wl_display_roundtrip(client->display);
  wl_display_dispatch(client->display);

  client->width = 600;
  client->height = 500;
  client->surface = wl_compositor_create_surface(client->compositor);
  client->shell_surface =
      wl_shell_get_shell_surface(client->shell, client->surface);

  create_shm_buffer(client);

  if (client->shell_surface) {
    static const struct wl_shell_surface_listener shell_surface_listener = {
        handle_ping, NULL, NULL};
    wl_shell_surface_add_listener(client->shell_surface,
                                  &shell_surface_listener, client);
    wl_shell_surface_set_toplevel(client->shell_surface);
  }

  wl_surface_set_user_data(client->surface, client);
  wl_shell_surface_set_title(client->shell_surface, "simple-client");

  draw_argb8888(client->data, 0x00, 0x00, 0x00, 0xff,
                client->width * client->height);
  wl_surface_attach(client->surface, client->buffer, 0, 0);
  wl_surface_damage(client->surface, 0, 0, client->width, client->height);
  wl_surface_commit(client->surface);

  return client;
}

int main(int argc, char **argv) {
  struct simple_client *client = simple_client_create();
  while (wl_display_dispatch(client->display) != -1)
    ;
  free(client);
  exit(EXIT_SUCCESS);
}