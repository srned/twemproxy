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

#include <sys/epoll.h>

struct nc_event_state {
    int ep;
    struct epoll_event *events;
};

static int
nc_event_init(struct nc_event *event)
{
    struct nc_event_state *e_state;

    e_state = nc_alloc(sizeof(*e_state));
    if (e_state == NULL) {
        return -1;
    }
    e_state->events = nc_calloc(event->nevent, sizeof(struct epoll_event));
    if (e_state->events == NULL) {
        nc_free(e_state);
        return -1;
    }
    e_state->ep = epoll_create(event->nevent);
    if (e_state->ep < 0) {
        nc_free(e_state->events);
        nc_free(e_state);
        log_error("epoll create of size %d failed: %s", event->nevent, 
                strerror(errno));
        return -1;
    }
    log_debug(LOG_INFO, "e %d with nevent %d", e_state->ep, event->nevent);
    event->event_data = e_state;
    return 0;
}

static void
nc_event_deinit(struct nc_event *event)
{
    int status;
    struct nc_event_state *e_state = event->event_data;

    ASSERT(e_state->ep >= 0);

    status = close(e_state->ep);
    if (status < 0) {
        log_error("close e %d failed, ignored: %s", e_state->ep, 
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
    struct epoll_event ee;
    int op, status;

    op = (new == NC_EV_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD);

    if (edge_t) { 
        ee.events = (uint32_t)(EPOLLET);
    }
    else {
        ee.events = 0;
    }
    if (mask & NC_EV_READABLE) {
        ee.events |= EPOLLIN;
    }
    if (mask & NC_EV_WRITABLE) {
        ee.events |= EPOLLOUT;
    }
    ee.data.ptr = data;

    status = epoll_ctl(e_state->ep, op, fd, &ee);
    if (status < 0) {
        log_error("epoll ctl on e %d sd %d failed: %s", e_state->ep, fd,
                strerror(errno));
        return -1;
    }
    return 0;
}

static int
nc_event_del(struct nc_event *event, int fd, int mask, 
        int new, void *data)
{
    struct nc_event_state *e_state = event->event_data;
    struct epoll_event ee;
    int op, status;

    if (new == NC_EV_NONE) { 
        op = EPOLL_CTL_DEL; 
    }
    else { 
        op = EPOLL_CTL_MOD;
        ee.events = (uint32_t)(EPOLLET);
        if (mask & NC_EV_READABLE) {
            ee.events |= EPOLLIN;
        }
        if (mask & NC_EV_WRITABLE) {
            ee.events |= EPOLLOUT;
        }
        ee.data.ptr = data;
    }

    status = epoll_ctl(e_state->ep, op, fd, &ee);
    if (status < 0) {
        log_error("epoll ctl on e %d sd %d failed: %s", e_state->ep, fd,
                strerror(errno));
        return -1;
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
        nsd = epoll_wait(e_state->ep, e_state->events, event->nevent, timeout);
        if (nsd > 0) {
            for (i = 0; i < nsd; i++) {
                struct epoll_event *e = e_state->events+i;
                mask = 0;

                if (e->events & EPOLLERR)
                    mask |= NC_EV_ERROR;

                if ((e->events & EPOLLIN) || (e->events & EPOLLHUP))
                    mask |= NC_EV_READABLE;

                if (e->events & EPOLLOUT)
                    mask |= NC_EV_WRITABLE;

                if (event->event_proc != NULL)
                    event->event_proc(e->data.ptr, mask);
            }
            return nsd;
        }

        if (nsd == 0) {
            if (timeout == -1) {
               log_error("epoll wait on e %d with %d events and %d timeout "
                         "returned no events", e_state->ep, event->nevent, 
                         timeout);
                return -1;
            }

            return 0;
        }

        if (errno == EINTR) {
            continue;
        }

        log_error("epoll wait on e %d with %d events failed: %s", e_state->ep, 
                event->nevent, strerror(errno));

        return -1;
    }
    NOT_REACHED();
}
