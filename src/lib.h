/*  vim: set sts=2 sw=2 et :
 *
 *  Copyright (C) 2015 Centricular Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __ONE_VIDEO_LIB_H__
#define __ONE_VIDEO_LIB_H__

#include <stdlib.h>
#include <gst/gst.h>
#include <gio/gio.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (onevideo_debug);
#define GST_CAT_DEFAULT onevideo_debug

typedef struct _OneVideoLocalPeer OneVideoLocalPeer;
typedef struct _OneVideoLocalPeerPriv OneVideoLocalPeerPriv;
typedef enum _OneVideoLocalPeerState OneVideoLocalPeerState;

typedef struct _OneVideoRemotePeer OneVideoRemotePeer;
typedef struct _OneVideoRemotePeerPriv OneVideoRemotePeerPriv;

enum _OneVideoLocalPeerState {
  ONE_VIDEO_STATE_NULL,
  ONE_VIDEO_STATE_READY,
  ONE_VIDEO_STATE_PLAYING,
};

/* Represents us; the library and the client implementing this local */
struct _OneVideoLocalPeer {
  /* Transmit pipeline */
  GstElement *transmit;
  /* Playback pipeline */
  GstElement *playback;
  /* Address we're listening on */
  GInetAddress *addr;
  /* Array of GInetSocketAddresses: available remote peers */
  GPtrArray *remotes;

  OneVideoLocalPeerState state;

  OneVideoLocalPeerPriv *priv;
};

/* Represents a remote local */
struct _OneVideoRemotePeer {
  OneVideoLocalPeer *local;

  gchar *name;
  /* Receive pipeline */
  GstElement *receive;
  /* Address of remote peer */
  GInetSocketAddress *addr;

  OneVideoRemotePeerPriv *priv;
};

OneVideoLocalPeer*  one_video_local_peer_new            (GInetAddress *addr);
void                one_video_local_peer_free           (OneVideoLocalPeer *local);
void                one_video_local_peer_stop           (OneVideoLocalPeer *local);
OneVideoRemotePeer* one_video_remote_peer_new           (OneVideoLocalPeer *local,
                                                         GInetSocketAddress *addr);
void                one_video_remote_peer_free          (OneVideoRemotePeer *remote);
void                one_video_remote_peer_stop          (OneVideoRemotePeer *remote);

gboolean            one_video_local_peer_find_remotes   (OneVideoLocalPeer *local);

gboolean            one_video_local_peer_begin_transmit (OneVideoLocalPeer *local);
gboolean            one_video_local_peer_setup_receive  (OneVideoLocalPeer *local,
                                                         OneVideoRemotePeer *remote);
gboolean            one_video_local_peer_setup_playback (OneVideoLocalPeer *local,
                                                         OneVideoRemotePeer *remote);
gboolean            one_video_local_peer_start_playback (OneVideoLocalPeer * local);

G_END_DECLS

#endif /* __ONE_VIDEO_LIB_H__ */
