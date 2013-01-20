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

#include <unistd.h>

#include <event/nc_event.h>

#ifdef NC_HAVE_EPOLL
#include "nc_epoll.c"
#else
#include "nc_kqueue.c"
#endif

struct nc_event *
event_init(int size, nc_event_proc *event_proc)
{
    struct nc_event *event;
    int status;

    event = nc_alloc(sizeof(*event));
    if (event == NULL) {
        log_error("create event alloc of size %zu failed: %s", sizeof(*event), 
                strerror(errno));
        return NULL;
    }
    event->nevent     = size;
    event->event_proc = event_proc; 

    status = nc_event_init(event);
    if (status == -1) {
        nc_free(event);
        log_error("event create of size %d failed: %s", size, strerror(errno));
        return NULL;
    }
    return event;
}

void
event_deinit(struct nc_event *event)
{
    ASSERT(event != NULL);

    nc_event_deinit(event);
    nc_free(event);
}

int
event_add_out(struct nc_event *event, struct conn *c)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(c != NULL);
    ASSERT(c->sd > 0);
    ASSERT(c->recv_active);

    if (c->send_active) {
        return 0;
    }

    status = nc_event_add(event, c->sd, NC_EV_READABLE | NC_EV_WRITABLE, 
            !NC_EV_NONE, 1, c);
    if (status == 0) {
        c->send_active = 1;
    }

    return status;
}

int
event_del_out(struct nc_event *event, struct conn *c)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(c != NULL);
    ASSERT(c->sd > 0);
    ASSERT(c->recv_active);

    if (!c->send_active) {
        return 0;
    }

    status = nc_event_del(event, c->sd, NC_EV_WRITABLE, !NC_EV_NONE, c);
    if (status == 0) {
        c->send_active = 0;
    }

    return status;
}

int
event_add_conn(struct nc_event *event, struct conn *c)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(c != NULL);
    ASSERT(c->sd > 0);

    status = nc_event_add(event, c->sd, NC_EV_READABLE|NC_EV_WRITABLE, 
            NC_EV_NONE, 1, c);
    if (status == 0) {
        c->send_active = 1;
        c->recv_active = 1;
    }

    return status;
}

int
event_del_conn(struct nc_event *event, struct conn *c)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(c != NULL);
    ASSERT(c->sd > 0);

    status = nc_event_del(event, c->sd, NC_EV_READABLE|NC_EV_WRITABLE, 
            NC_EV_NONE, c);
    if (status == 0) {
        c->recv_active = 0;
        c->send_active = 0;
    }

    return status;
}

int
event_wait(struct nc_event *event, int timeout)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(event->nevent > 0);

    status = nc_event_wait(event, timeout);

    return status;
}

int
event_add_st(struct nc_event *event, int fd)
{
    int status;

    ASSERT(event != NULL);
    ASSERT(fd > 0);

    status = nc_event_add(event, fd, NC_EV_READABLE, NC_EV_NONE, 0, NULL);

    return status;
}

