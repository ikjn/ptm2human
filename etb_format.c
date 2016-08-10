/*
 * etb_format.c: Decode ETB
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "tracer.h"
#include "stream.h"
#include "log.h"
#include "output.h"

#define ETB_PACKET_SIZE 16
#define NULL_TRACE_SOURCE 0

static struct stream* alloc_stream(struct stream *parent)
{
    struct stream *stream = malloc(sizeof(*stream));
    
    if (!stream)
        return NULL;

    memcpy(stream, parent, sizeof(struct stream));

    stream->buff_len  = 0;

    stream->buff = malloc(parent->buff_len);
    if (!stream->buff)
        return NULL;
    memset((void *)stream->buff, 0, parent->buff_len);

    stream->offsets = malloc(parent->buff_len * sizeof(stream->offsets[0]));
    if (!stream->buff) {
        free(stream->buff);
        stream->buff = NULL;
        return NULL;
    }

    return stream;
}
static void free_stream(struct stream *stream)
{
    if (stream->buff)
        free(stream->buff);
    if (stream->offsets)
        free(stream->offsets);
    free(stream);
}

#define ARRAY_SIZE(x)   (sizeof(x) / sizeof(x[0]))
#define MAX_STREAMS     (128)
int decode_etb_stream(struct stream *etb_stream)
{
    struct stream *streams[MAX_STREAMS];
    int pkt_idx, byte_idx;
    int id, nr_new, i, trace_stop = 0;
    int cur_id_next;
    unsigned char c, aux, tmp;

    int cur_id = -1;
    int next_id = -1;
    
    if (!etb_stream) {
        LOGE("Invalid stream pointer\n");
        return -1;
    }

    memset(streams, 0, sizeof(streams));

    for (pkt_idx = 0; pkt_idx < etb_stream->buff_len; pkt_idx += ETB_PACKET_SIZE) {
        if (trace_stop) {
            break;
        }
        
        for (byte_idx = 0; byte_idx < (ETB_PACKET_SIZE - 1); byte_idx++) {
            c = etb_stream->buff[pkt_idx + byte_idx];

            if ((byte_idx & 1) == 0)
                aux = !!( etb_stream->buff[pkt_idx + ETB_PACKET_SIZE - 1] & (1 << (byte_idx / 2)) );

            if ((byte_idx & 1) || !(c & 1)) {  /* DATA byte */
                struct stream *stream;

                if (cur_id < 0) /* drop the byte since there is no ID byte yet */
                    continue;
                
                stream = streams[cur_id];
                if (!stream) {
                    LOGE("\n\nFatal!\n");
                    return -1;
                }
            
                if (!(byte_idx & 1) && !(c & 1))
                    c |= aux;

                stream_push(stream, c, pkt_idx + byte_idx);
                
                cur_id = next_id;   /* catch up */
            } else {                            /* ID byte */
                id = (c >> 1) & 0x7f;
                if (id == NULL_TRACE_SOURCE) {
                    trace_stop = 1;
                    break;
                }
                next_id = id - 1;

                LOGD("found new source ID %d(aux=%d) @0x%x\n", next_id, aux, pkt_idx + byte_idx);
                
                if (!streams[next_id]) {
                    /* create new streams */
                    struct stream *stream = alloc_stream(etb_stream);
                    if (!stream) {
                        LOGE("Fail to allocate child stream (%s)\n", strerror(errno));
                        return -1;
                    }
                    streams[next_id] = stream;
                }
                if (!aux) {
                   /* new ID effect immediately */
                    cur_id = next_id;
                }
            }
        }
    }

    for (i = 0; i < ARRAY_SIZE(streams); i++) {
        struct stream *stream = streams[i];
        if (!stream)
            continue;

        if (stream->buff_len > 0) {
                OUTPUT("Decode trace stream of ID %d\n", i);
                LOGD("There are %d bytes in the stream %d\n", stream->buff_len, i);
                decode_stream(stream);
        }
        free_stream(stream);
        streams[i] = NULL;
    }

    return 0;
}
