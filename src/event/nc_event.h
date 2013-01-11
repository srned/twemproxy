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

#ifndef _NC_EVENT_H_
#define _NC_EVENT_H_

#include <nc_core.h>

/*
 * A hint to the kernel that is used to size the event backing store
 * of a given epoll instance
 */
#define EVENT_SIZE_HINT 1024
#define NC_EV_NONE         0
#define NC_EV_READABLE     1
#define NC_EV_WRITABLE     2
#define NC_EV_ERROR        4

typedef void nc_event_proc(void *, uint32_t);

struct nc_event {
    int           nevent;      /* # events */
    nc_event_proc *event_proc; /* Callback function to be processed */
    void          *event_data; /* This is used for multiplexing spec data */
};

struct nc_event *event_init(int size, nc_event_proc *event_proc);
void event_deinit(struct nc_event *event);

int event_add_out(struct nc_event *event, struct conn *c);
int event_del_out(struct nc_event *event, struct conn *c);
int event_add_conn(struct nc_event *event, struct conn *c);
int event_del_conn(struct nc_event *event, struct conn *c);
int event_wait(struct nc_event *event, int timeout);
int event_add_st(struct nc_event *event, int fd);

#endif
