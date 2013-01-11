/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

struct nc_event_state {
    int kq;
    struct kevent *events;
};

static int
nc_event_init(struct nc_event *event)
{
    struct nc_event_state *e_state;

    e_state = nc_alloc(sizeof(*e_state));
    if (e_state == NULL) {
        return -1;
    }
    e_state->events = nc_calloc(event->nevent, sizeof(struct kevent));
    if (e_state->events == NULL) {
        nc_free(e_state);
        return -1;
    }
    e_state->kq = kqueue();
    if (e_state->kq == -1) {
        nc_free(e_state->events);
        nc_free(e_state);
        log_error("kqueue create of size %d failed: %s", event->nevent,
                strerror(errno));
        return -1;
    }
    log_debug(LOG_INFO, "e %d with nevent %d", e_state->kq, event->nevent);
    event->event_data = e_state;
    return 0;
}

static void
nc_event_deinit(struct nc_event *event)
{
    struct nc_event_state *e_state = event->event_data;

    ASSERT(e_state->kq >= 0);

    status = close(e_state->kq);
    if (status < 0) {
        log_error("close e %d failed, ignored: %s", e_state->kq,
                strerror(errno));
    }
    nc_free(e_state->events);
    nc_free(e_state);
}

static int
nc_event_add(struct nc_event *event, int fd, int mask, int new, 
        int edge_t, void *data)
{
    struct nc_event_state *e_state = event->event_data;
    struct kevent ke;
    int status;

    ke.udata = data;
    if (mask & NC_EV_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        status = kevent(e_state->kq, &ke, 1, NULL, 0, NULL);
        if (status == -1) {
            log_error("kqueue ctl on e %d sd %d failed: %s", e_state->kq, 
                    fd,strerror(errno));
            return -1;
        }
    }
    if (mask & NC_EV_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        status = kevent(e_state->kq, &ke, 1, NULL, 0, NULL);
        if (status == -1) {
            log_error("kqueue ctl on e %d sd %d failed: %s", e_state->kq,
                    fd,strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int
nc_event_del(struct nc_event *event, int fd, int mask, int new, void *data)
{
    struct nc_event_state *e_state = event->event_data;
    struct kevent ke;
    int status;

    ke.udata = data;
    if (mask & NC_EV_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        status = kevent(e_state->kq, &ke, 1, NULL, 0, NULL);
        if (status == -1) {
            log_error("kqueue ctl on e %d sd %d failed: %s", e_state->kq, 
                    fd,strerror(errno));
            return -1;
        }
    }
    if (mask & NC_EV_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        status = kevent(e_state->kq, &ke, 1, NULL, 0, NULL);
        if (status == -1) {
            log_error("kqueue ctl on e %d sd %d failed: %s", e_state->kq,
                    fd,strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int
nc_event_wait(struct nc_event *event, int timeout)
{
    struct nc_event_state *e_state;
    int nsd, i;
    uint32_t mask = 0;

    ASSERT(event->event_data != NULL);
    ASSERT(event->event_data->ep > 0);

    e_state = event->event_data;

    for (;;) {
        if (timeout != -1) {
            struct timespec ts = nc_ms_to_timespec(timeout);
            nsd = kevent(e_state->kq, NULL, 0, e_state->events, 
                    event->nevent, &ts);
        }
        else {
            nsd = kevent(e_state->kq, NULL, 0, e_state->events, 
                    event->nevent, NULL);
        }
        if (nsd > 0) {
            for (i = 0; i < nsd; i++) {
                struct kevent *e = e_state->events+j;
                mask = 0;

                if (e->flags == EV_ERROR) {
                   /*
                    * Error messages that can happen, when a delete fails.
                    *   EBADF happens when the file descriptor has been
                    *   closed,
                    *   ENOENT when the file descriptor was closed and       
                    *   then reopened.
                    *   EINVAL for some reasons not understood; EINVAL
                    *   should not be returned ever; but FreeBSD does :-\
                    * An error is also indicated when a callback deletes
                    * an event we are still processing.  In that case
                    * the data field is set to ENOENT.
                    */
                    if (e->data == EBADF ||
                        e->data == EINVAL ||
                        e->data == ENOENT)
                        continue;
                    mask |= NC_ERROR;
                }

                if (e->filter == EVFILT_READ)
                    mask |= NC_EV_READABLE;

                if (e->filter == EVFILT_WRITE)
                    mask |= NC_EV_WRITABLE;

                if (event->event_proc != NULL)
                    event->event_proc(e->udata, mask);
            }
            return nsd;
        }

        if (nsd == 0) {
            if (timeout == -1) {
               log_error("kqueue on kq %d with %d events and %d timeout "
                         "returned no events", e_state->kq, event->nevent, 
                         timeout);
                return -1;
            }

            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        log_error("kevent on kq %d with %d events failed: %s", e_state->kq, 
                event->nevent, strerror(errno));

        return -1;
    }
    NOT_REACHED();
}
