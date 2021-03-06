/*
 * Copyright (C) 2015 Centricular Ltd.
 *   Author: Sebastian Dröge <sebastian@centricular.com>
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

/**
 * SECTION:element-proxysrc
 *
 * Proxysrc is a source element that proxies events, queries, and buffers from
 * another pipeline that contains a matching proxysink element. The purpose is
 * to allow two decoupled pipelines to function as though they are one without
 * having to manually shuttle buffers, events, queries, etc between the two.
 *
 * The element queues buffers from the matching proxysink to an internal queue,
 * so everything downstream is properly decoupled from the upstream pipeline.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstproxysrc.h"
#include "gstproxysink.h"
#include "gstproxysink-priv.h"

#define GST_CAT_DEFAULT gst_proxy_src_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);

enum
{
  PROP_0,
  PROP_PROXYSINK,
};

struct _GstProxySrcPrivate
{
  /* Queue to hold buffers from proxysink */
  GstElement *queue;
  /* Source pad of the above queue and the proxysrc element itself */
  GstPad *srcpad;
  /* Our internal srcpad that proxysink pushes buffers/events/queries into */
  GstPad *internal_srcpad;
  /* An unlinked dummy sinkpad; see gst_proxy_src_init() */
  GstPad *dummy_sinkpad;
  /* The matching proxysink; queries and events are sent to its sinkpad */
  GWeakRef proxysink;
};

/* We're not subclassing from basesrc because we don't want any of the special
 * handling it has for events/queries/etc. We just pass-through everything. */

/* Our parent type is a GstBin instead of GstElement because we contain a queue
 * element */
#define parent_class gst_proxy_src_parent_class
G_DEFINE_TYPE (GstProxySrc, gst_proxy_src, GST_TYPE_BIN);

static gboolean gst_proxy_src_internal_src_query (GstPad *pad, GstObject *parent,
  GstQuery *query);
static gboolean gst_proxy_src_internal_src_event (GstPad *pad, GstObject *parent,
  GstEvent *event);

static GstStateChangeReturn gst_proxy_src_change_state (GstElement *element, GstStateChange transition);
static void gst_proxy_src_dispose (GObject *object);

static void
gst_proxy_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * spec)
{
  GstProxySrc *self = GST_PROXY_SRC (object);

  switch (prop_id) {
    case PROP_PROXYSINK:
      g_value_take_object (value, g_weak_ref_get (&self->priv->proxysink));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}

static void
gst_proxy_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * spec)
{
  GstProxySrc *self = GST_PROXY_SRC (object);
  GstProxySink *sink;

  switch (prop_id) {
    case PROP_PROXYSINK:
      sink = g_value_dup_object (value);
      if (sink == NULL) {
        /* Unset proxysrc property on the existing proxysink to break the
         * connection in that direction */
        GstProxySink *old_sink = g_weak_ref_get (&self->priv->proxysink);
        if (old_sink) {
          gst_proxy_sink_set_proxysrc (old_sink, NULL);
          g_object_unref (old_sink);
        }
        g_weak_ref_set (&self->priv->proxysink, NULL);
      } else {
        /* Set proxysrc property on the new proxysink to point to us */
        gst_proxy_sink_set_proxysrc (sink, self);
        g_weak_ref_set (&self->priv->proxysink, sink);
        g_object_unref (sink);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
  }
}

static void
gst_proxy_src_class_init (GstProxySrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_proxy_src_debug, "proxysrc", 0, "proxy sink");

  g_type_class_add_private (klass, sizeof (GstProxySrcPrivate));

  gobject_class->dispose = gst_proxy_src_dispose;

  gobject_class->get_property = gst_proxy_src_get_property;
  gobject_class->set_property = gst_proxy_src_set_property;

  g_object_class_install_property (gobject_class, PROP_PROXYSINK,
      g_param_spec_object ("proxysink", "Proxysink", "Matching proxysink",
        GST_TYPE_PROXY_SINK, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_proxy_src_change_state;
  gst_element_class_add_pad_template (gstelement_class,
    gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (gstelement_class, "Proxy source",
      "Source", "Proxy source for internal process communication",
      "Sebastian Dröge <sebastian@centricular.com>");
}

static void
gst_proxy_src_init (GstProxySrc * self)
{
  GstPad *srcpad, *sinkpad;
  GstPadTemplate *templ;

  GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_SOURCE);

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_PROXY_SRC,
      GstProxySrcPrivate);

  /* We feed incoming buffers into a queue to decouple the downstream pipeline
   * from the upstream pipeline */
  self->priv->queue = gst_element_factory_make ("queue", NULL);
  gst_bin_add (GST_BIN (self), self->priv->queue);

  srcpad = gst_element_get_static_pad (self->priv->queue, "src");
  templ = gst_static_pad_template_get (&src_template);
  self->priv->srcpad = gst_ghost_pad_new_from_template ("src", srcpad, templ);
  gst_object_unref (templ);
  gst_object_unref (srcpad);

  gst_element_add_pad (GST_ELEMENT (self), self->priv->srcpad);

  /* A dummy sinkpad that's not actually used anywhere
   * Explanation for why this is needed is below */
  self->priv->dummy_sinkpad = gst_pad_new ("dummy_sinkpad", GST_PAD_SINK);
  gst_object_set_parent (GST_OBJECT (self->priv->dummy_sinkpad), GST_OBJECT (self));

  self->priv->internal_srcpad = gst_pad_new ("internal_src", GST_PAD_SRC);
  gst_object_set_parent (GST_OBJECT (self->priv->internal_srcpad),
      GST_OBJECT (self->priv->dummy_sinkpad));
  gst_pad_set_event_function (self->priv->internal_srcpad,
      gst_proxy_src_internal_src_event);
  gst_pad_set_query_function (self->priv->internal_srcpad,
      gst_proxy_src_internal_src_query);

  /* We need to link internal_srcpad from proxysink to the sinkpad of our
   * queue. However, two pads can only be linked if they share a common parent.
   * Above, we set the parent of the dummy_sinkpad as proxysrc, and then we set
   * the parent of internal_srcpad as dummy_sinkpad. This causes both these pads
   * to share a parent allowing us to link them.
   * Yes, this is a hack/workaround. */
  sinkpad = gst_element_get_static_pad (self->priv->queue, "sink");
  gst_pad_link (self->priv->internal_srcpad, sinkpad);
  gst_object_unref (sinkpad);
}

static void
gst_proxy_src_dispose (GObject * object)
{
  GstProxySrc *self = GST_PROXY_SRC (object);

  gst_object_unparent (GST_OBJECT (self->priv->dummy_sinkpad));
  self->priv->dummy_sinkpad = NULL;

  gst_object_unparent (GST_OBJECT (self->priv->internal_srcpad));
  self->priv->internal_srcpad = NULL;

  g_weak_ref_set (&self->priv->proxysink, NULL);

  G_OBJECT_CLASS (gst_proxy_src_parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_proxy_src_change_state (GstElement * element, GstStateChange transition)
{
  GstElementClass *gstelement_class =
    GST_ELEMENT_CLASS (gst_proxy_src_parent_class);
  GstProxySrc *self = GST_PROXY_SRC (element);
  GstStateChangeReturn ret;

  ret = gstelement_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    ret = GST_STATE_CHANGE_NO_PREROLL;
    gst_pad_set_active (self->priv->internal_srcpad, TRUE);
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gst_pad_set_active (self->priv->internal_srcpad, FALSE);
    break;
  default:
    break;
  }

  return ret;
}

static gboolean
gst_proxy_src_internal_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstProxySrc *self = GST_PROXY_SRC (gst_object_get_parent (parent));
  GstProxySink *sink;
  gboolean ret = FALSE;

  if (!self)
    return ret;

  GST_LOG_OBJECT (pad, "Handling query of type '%s'",
    gst_query_type_get_name (GST_QUERY_TYPE (query)));

  sink = g_weak_ref_get (&self->priv->proxysink);
  if (sink) {
    GstPad *sinkpad;
    sinkpad = gst_proxy_sink_get_internal_sinkpad (sink);

    ret = gst_pad_peer_query (sinkpad, query);
    gst_object_unref (sinkpad);
    gst_object_unref (sink);
  }

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_proxy_src_internal_src_event (GstPad * pad, GstObject * parent,
  GstEvent * event)
{
  GstProxySrc *self = GST_PROXY_SRC (gst_object_get_parent (parent));
  GstProxySink *sink;
  gboolean ret = FALSE;

  if (!self)
    return ret;

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  sink = g_weak_ref_get (&self->priv->proxysink);
  if (sink) {
    GstPad *sinkpad;
    sinkpad = gst_proxy_sink_get_internal_sinkpad (sink);

    ret = gst_pad_push_event (sinkpad, event);
    gst_object_unref (sinkpad);
    gst_object_unref (sink);
  } else
    gst_event_unref (event);


  gst_object_unref (self);

  return ret;
}

/* Wrapper function for accessing private member */
GstPad *
gst_proxy_src_get_internal_srcpad (GstProxySrc * self)
{
  return gst_object_ref (self->priv->internal_srcpad);
}
