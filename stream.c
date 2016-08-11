/*
 * stream.c: synchronize trace stream and decode it
 * Copyright (C) 2013  Chih-Chyuan Hwang (hwangcc@csie.nctu.edu.tw)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <stdio.h>
#include "log.h"
#include "tracer.h"
#include "stream.h"
#include "pktproto.h"

struct tracepkt **tracepkts;
sync_func synchronization;

static struct tracepkt* pick_traceptk(const pkt_header header)
{
    int i;
    for (i = 0; tracepkts[i]; i++) {
        struct tracepkt *pkt = tracepkts[i];

        if (pkt->type == PKTDEF_MASK) {
            if ((header & tracepkts[i]->mask) == tracepkts[i]->val) {
                return pkt;
            }
        } else if (pkt->type == PKTDEF_RANGE) {
            if ( header >= tracepkts[i]->mask && header <= tracepkts[i]->val) {
                return pkt;
            }
        }
    }
    return NULL;
}

int decode_stream(struct stream *stream)
{
    int cur, i, ret;

    if (!stream) {
        LOGE("Invalid struct stream pointer\n");
        return -1;
    }
    if (stream->state == READING) {
        /* READING -> SYNCING */
        stream->state++;
    } else {
        LOGE("Stream state is not correct\n");
        return -1;
    }

    LOGV("Syncing the trace stream...\n");
    cur = synchronization(stream);
    if (cur < 0) {
        LOGE("Cannot find any synchronization packet\n");
        return -1;
    } else {
        LOGD("Trace starts from offset 0x%x\n", cur);
    }

    LOGV("Decoding the trace stream...\n");
    /* INSYNC -> DECODING */
    stream->state++;
    for (; cur < stream->buff_len; ) {
        unsigned char c = stream->buff[cur];
        struct tracepkt *pkt;

        LOGD("Got a packet header 0x%02x at offset 0x%x/0x%x\n", c, cur, stream->offsets[cur]);

        pkt = pick_traceptk(c);
        if (!pkt) {
            LOGE("Cannot recognize a packet header 0x%02x\n", c);
            LOGE("Proceed on guesswork\n");
            cur++;
            continue;
        }
        LOGD("Get a packet of type %s\n", pkt->name);

        ret = pkt->decode((const unsigned char *)&(stream->buff[cur]), stream);
        if (ret <= 0) {
            LOGE("Cannot decode a packet of type %s at offset 0x%x/0x%x = 0x%02x\n",
                    pkt->name, cur, stream->offsets[cur], (unsigned char)stream->buff[cur]);
            LOGE("Proceed on guesswork\n");
            cur++;
        } else {
            cur += ret;
        }
    }

    LOGV("Complete decode of the trace stream\n");

    return 0;
}
