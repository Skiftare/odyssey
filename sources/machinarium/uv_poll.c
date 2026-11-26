#include <errno.h>
#include <string.h>

#include <uv.h>

#include <machinarium/machinarium.h>
#include <machinarium/memory.h>
#include <machinarium/uv_poll.h>

typedef struct mm_uv mm_uv_t;
typedef struct mm_uv_watcher mm_uv_watcher_t;

struct mm_uv {
	mm_poll_t poll;
	uv_loop_t loop;
	uv_timer_t timer;
};

struct mm_uv_watcher {
	uv_poll_t poll;
	mm_fd_t *fd;
	int interests;
};

static void mm_uv_timer_cb(uv_timer_t *timer)
{
	(void)timer;
}
static void mm_uv_poll_cb(uv_poll_t *handle, int status, int events)
{
	mm_uv_watcher_t *watcher = handle->data;
	mm_fd_t *fd = watcher->fd;

	if (fd == NULL) {
		return;
	}
	int delivered = events & (UV_READABLE | UV_WRITABLE | UV_DISCONNECT);
	int interests = watcher->interests & ~delivered;
	if (interests != watcher->interests) {
		int rc;

		if (interests == 0) {
			rc = uv_poll_stop(&watcher->poll);
		} else {
			rc = uv_poll_start(&watcher->poll, interests,
					   mm_uv_poll_cb);
		}

		if (rc != 0) {
			fd->last_event = MM_ERR;
			if (fd->on_err != NULL) {
				fd->on_err(fd);
			}
			return;
		}

		watcher->interests = interests;
	}
	fd->last_event = 0;

	if (status < 0 && (fd->mask & MM_ERR)) {
		fd->last_event |= MM_ERR;
		if (fd->on_err != NULL) {
			fd->on_err(fd);
		}
                return; //?
	}

	if ((events & UV_DISCONNECT) && (fd->mask & MM_CLOSE)) {
		fd->last_event |= MM_CLOSE;
		if (fd->on_close != NULL) {
			fd->on_close(fd);
		}
	}

	if ((events & UV_READABLE) && (fd->mask & MM_R)) {
		fd->last_event |= MM_R;
		if (fd->on_read != NULL) {
			fd->on_read(fd);
		}
	}

	if ((events & UV_WRITABLE) && (fd->mask & MM_W)) {
		fd->last_event |= MM_W;
		if (fd->on_write != NULL) {
			fd->on_write(fd);
		}
	}
}
static void mm_uv_close_cb(uv_handle_t *handle)
{
	mm_uv_watcher_t *watcher = handle->data;
	mm_free(watcher);
}
static void mm_uv_walk_close_cb(uv_handle_t *handle, void *arg)
{
	(void)arg;

	if (uv_is_closing(handle)) {
		return;
	}

	if (handle->type == UV_POLL) {
		mm_uv_watcher_t *watcher = handle->data;

		uv_poll_stop((uv_poll_t *)handle);

		if (watcher->fd != NULL) {
			watcher->fd->poll_handle = NULL;
			watcher->fd->mask = 0;
			watcher->fd = NULL;
		}

		uv_close(handle, mm_uv_close_cb);
		return;
	}

	/* В частности, встроенный timer закроем */
	uv_close(handle, NULL);
}

static mm_poll_t *mm_uv_create(void)
{
	mm_uv_t *uv;
	uv = mm_malloc(sizeof(mm_uv_t));
	if (uv == NULL) {
		return NULL;
	}
	uv->poll.iface = &mm_uv_if;

	int rc = uv_loop_init(&uv->loop);
	if (rc != 0) {
		mm_free(uv);
		return NULL;
	}
	rc = uv_timer_init(&uv->loop, &uv->timer);
	if (rc != 0) {
		uv_loop_close(&uv->loop);
		mm_free(uv);
		return NULL;
	}
	return &uv->poll;
}

static void mm_uv_free(mm_poll_t *poll)
{
	mm_uv_t *uv = (mm_uv_t *)poll;
	mm_free(uv);
}
static int mm_uv_shutdown(mm_poll_t *poll)
{
	mm_uv_t *uv = (mm_uv_t *)poll;

	uv_walk(&uv->loop, mm_uv_walk_close_cb, NULL);
	uv_run(&uv->loop, UV_RUN_DEFAULT);

	int rc = uv_loop_close(&uv->loop);
	if (rc != 0) {
		return -1;
	}

	return 0;
}
static int mm_uv_step(mm_poll_t *poll, int timeout_ms)
{
	mm_uv_t *uv = (mm_uv_t *)poll;
	if (timeout_ms == 0) {
		int count_active = uv_run(&uv->loop, UV_RUN_NOWAIT);
		return count_active;
	}
	uv_timer_start(&uv->timer, mm_uv_timer_cb, (uint64_t)timeout_ms, 0);
	int count_active = uv_run(&uv->loop, UV_RUN_ONCE);
	uv_timer_stop(&uv->timer);
	return count_active;
}

static inline int mm_uv_modify(mm_poll_t *poll, mm_fd_t *fd, int mask)
{
	if (fd->mask == mask) {
		return 0;
	}
	mm_uv_t *uv = (mm_uv_t *)poll;
	int uv_flags = 0;
	if (mask & MM_R) {
		uv_flags |= UV_READABLE;
	}
	if (mask & MM_W) {
		uv_flags |= UV_WRITABLE;
	}
	if (mask & MM_CLOSE) {
		uv_flags |= UV_DISCONNECT;
	}
	if (fd->mask == 0 && mask != 0) {
		mm_uv_watcher_t *watcher;
		watcher = mm_calloc(1, sizeof(*watcher));
		if (watcher == NULL) {
			return -1;
		}
		watcher->fd = fd;

		int rc = uv_poll_init_socket(&uv->loop, &watcher->poll,
					     (uv_os_sock_t)fd->fd);
		if (rc != 0) {
			mm_free(watcher);
			return -1;
		}
		watcher->poll.data = watcher;
		rc = uv_poll_start(&watcher->poll, uv_flags, mm_uv_poll_cb);
		if (rc != 0) {
			watcher->fd = NULL;
			uv_close((uv_handle_t *)&watcher->poll, mm_uv_close_cb);
			return -1;
		}
		watcher->interests = uv_flags;
		fd->poll_handle = watcher;
		fd->mask = mask;

	} else if (fd->mask != 0 && mask != 0) {
		mm_uv_watcher_t *watcher = (mm_uv_watcher_t *)fd->poll_handle;
		if (watcher == NULL) {
			return -1;
		}
		int rc = uv_poll_start(&watcher->poll, uv_flags, mm_uv_poll_cb);

		if (rc != 0) {
			return -1;
		}
		fd->mask = mask;
		watcher->interests = uv_flags;

	} else {
		mm_uv_watcher_t *watcher = (mm_uv_watcher_t *)fd->poll_handle;
		if (watcher == NULL) {
			return -1;
		}
		int rc = uv_poll_stop(&watcher->poll);
		if (rc != 0) {
			return -1;
		}
		fd->poll_handle = NULL;
		fd->mask = 0;
		watcher->fd = NULL;
		uv_close((uv_handle_t *)&watcher->poll, mm_uv_close_cb);
	}
	return 0;
}

static int mm_uv_add(mm_poll_t *poll, mm_fd_t *fd, mm_fd_callback_t on_read,
		     void *on_read_arg, mm_fd_callback_t on_write,
		     void *on_write_arg, mm_fd_callback_t on_err,
		     void *on_err_arg, mm_fd_callback_t on_close,
		     void *on_close_arg)
{
	fd->on_read = on_read;
	fd->on_read_arg = on_read_arg;
	fd->on_write = on_write;
	fd->on_write_arg = on_write_arg;
	fd->on_err = on_err;
	fd->on_err_arg = on_err_arg;
	fd->on_close = on_close;
	fd->on_close_arg = on_close_arg;

	int mask = 0;

	if (fd->on_read != NULL) {
		mask |= MM_R;
	}
	if (fd->on_write != NULL) {
		mask |= MM_W;
	}
	if (fd->on_err != NULL) {
		mask |= MM_ERR;
	}
	if (fd->on_close != NULL) {
		mask |= MM_CLOSE;
	}

	return mm_uv_modify(poll, fd, mask);
}
static int mm_uv_del(mm_poll_t *poll, mm_fd_t *fd)
{
	fd->on_read = NULL;
	fd->on_read_arg = NULL;
	fd->on_write = NULL;
	fd->on_write_arg = NULL;

	return mm_uv_modify(poll, fd, 0);
}
static int mm_uv_rearm(mm_poll_t *poll, mm_fd_t *fd, int mask)
{
	(void)poll;
	mm_uv_watcher_t *watcher = (mm_uv_watcher_t *)fd->poll_handle;
	if (watcher == NULL) {
		return -1;
	}
	int uv_flags = 0;
	if (mask & MM_R) {
		uv_flags |= UV_READABLE;
	}
	if (mask & MM_W) {
		uv_flags |= UV_WRITABLE;
	}
	if (mask & MM_CLOSE) {
		uv_flags |= UV_DISCONNECT;
	}
	int interests = watcher->interests | uv_flags;
	if (interests == watcher->interests) {
		return 0;
	}

	int rc = uv_poll_start(&watcher->poll, interests, mm_uv_poll_cb);
	if (rc != 0) {
		return -1;
	}
	watcher->interests = interests;
	return 0;
}

mm_pollif_t mm_uv_if = { .name = "libuv",
			 .create = mm_uv_create,
			 .free = mm_uv_free,
			 .shutdown = mm_uv_shutdown,
			 .step = mm_uv_step,
			 .add = mm_uv_add,
			 .del = mm_uv_del,
			 .rearm = mm_uv_rearm };
