/*
 * Regression test for rtt_AddLine/rtt_AddLineNoFace with GEOS >= 3.15.
 *
 * GEOS 3.15 returns line-merged overlay output, which hid the junction
 * between the part of a new line shared with an existing edge and the
 * rest of the line.  librttopo then tried to insert a single edge that
 * overlapped the existing one and failed with
 * "Spatial exception - geometry intersects edge 1".
 *
 * The test uses a minimal in-memory backend.  Starting from the edge
 * (0 0, 10 0), it adds the line (5 5, 5 0, 8 0), which must be split
 * into two edges: (5 5, 5 0) and (5 0, 8 0), the latter reusing a
 * portion of the existing edge.  The final topology has 5 nodes and
 * 4 edges, independently of the GEOS version.
 */
#include <librttopo_geom.h>
#include <librttopo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RTT_BE_DATA_T
{
  const RTCTX *ctx;
  char errmsg[1024];
};

struct RTT_BE_TOPOLOGY_T
{
  struct RTT_BE_DATA_T *be;
  RTT_ISO_NODE *nodes;
  int nnodes;
  RTT_ISO_EDGE *edges;
  int nedges;
  RTT_ISO_FACE *faces;
  int nfaces;
  RTT_ELEMID next_node_id;
  RTT_ELEMID next_edge_id;
  RTT_ELEMID next_face_id;
};

static struct RTT_BE_DATA_T be_data;
static struct RTT_BE_TOPOLOGY_T be_topo;
static char last_error[1024];

static void
error_logger(const char *fmt, va_list ap, void *arg)
{
  (void)arg;
  vsnprintf(last_error, sizeof(last_error), fmt, ap);
  fprintf(stderr, "librttopo error: %s\n", last_error);
}

static void
notice_logger(const char *fmt, va_list ap, void *arg)
{
  (void)fmt;
  (void)ap;
  (void)arg;
}

/* ---- helpers ---- */

static const RTCTX *
CTX(const RTT_BE_TOPOLOGY *topo)
{
  return topo->be->ctx;
}

static RTT_ISO_NODE *
find_node(RTT_BE_TOPOLOGY *topo, RTT_ELEMID id)
{
  int i;
  for (i = 0; i < topo->nnodes; ++i)
    if (topo->nodes[i].node_id == id) return &topo->nodes[i];
  return NULL;
}

static RTT_ISO_EDGE *
find_edge(RTT_BE_TOPOLOGY *topo, RTT_ELEMID id)
{
  int i;
  for (i = 0; i < topo->nedges; ++i)
    if (topo->edges[i].edge_id == id) return &topo->edges[i];
  return NULL;
}

static RTT_ISO_FACE *
find_face(RTT_BE_TOPOLOGY *topo, RTT_ELEMID id)
{
  int i;
  for (i = 0; i < topo->nfaces; ++i)
    if (topo->faces[i].face_id == id) return &topo->faces[i];
  return NULL;
}

static void
copy_node(const RTCTX *ctx, RTT_ISO_NODE *dst, const RTT_ISO_NODE *src)
{
  *dst = *src;
  dst->geom = src->geom ? rtgeom_as_rtpoint(ctx,
      rtgeom_clone_deep(ctx, rtpoint_as_rtgeom(ctx, src->geom))) : NULL;
}

static void
copy_edge(const RTCTX *ctx, RTT_ISO_EDGE *dst, const RTT_ISO_EDGE *src)
{
  *dst = *src;
  dst->geom = src->geom ? rtgeom_as_rtline(ctx,
      rtgeom_clone_deep(ctx, rtline_as_rtgeom(ctx, src->geom))) : NULL;
}

static void
copy_face(const RTCTX *ctx, RTT_ISO_FACE *dst, const RTT_ISO_FACE *src)
{
  *dst = *src;
  if (src->mbr)
  {
    dst->mbr = rtalloc(ctx, sizeof(RTGBOX));
    *dst->mbr = *src->mbr;
  }
}

static int
node_matches(const RTT_ISO_NODE *n, const RTT_ISO_NODE *sel, int fields)
{
  if ((fields & RTT_COL_NODE_NODE_ID) && n->node_id != sel->node_id) return 0;
  if ((fields & RTT_COL_NODE_CONTAINING_FACE) &&
      n->containing_face != sel->containing_face) return 0;
  return 1;
}

static int
edge_matches(const RTT_ISO_EDGE *e, const RTT_ISO_EDGE *sel, int fields)
{
  if ((fields & RTT_COL_EDGE_EDGE_ID) && e->edge_id != sel->edge_id) return 0;
  if ((fields & RTT_COL_EDGE_START_NODE) && e->start_node != sel->start_node) return 0;
  if ((fields & RTT_COL_EDGE_END_NODE) && e->end_node != sel->end_node) return 0;
  if ((fields & RTT_COL_EDGE_FACE_LEFT) && e->face_left != sel->face_left) return 0;
  if ((fields & RTT_COL_EDGE_FACE_RIGHT) && e->face_right != sel->face_right) return 0;
  if ((fields & RTT_COL_EDGE_NEXT_LEFT) && e->next_left != sel->next_left) return 0;
  if ((fields & RTT_COL_EDGE_NEXT_RIGHT) && e->next_right != sel->next_right) return 0;
  return 1;
}

static void
update_node(const RTCTX *ctx, RTT_ISO_NODE *n, const RTT_ISO_NODE *upd, int fields)
{
  if (fields & RTT_COL_NODE_NODE_ID) n->node_id = upd->node_id;
  if (fields & RTT_COL_NODE_CONTAINING_FACE) n->containing_face = upd->containing_face;
  if (fields & RTT_COL_NODE_GEOM)
  {
    if (n->geom) rtpoint_free(ctx, n->geom);
    n->geom = rtgeom_as_rtpoint(ctx,
        rtgeom_clone_deep(ctx, rtpoint_as_rtgeom(ctx, upd->geom)));
  }
}

static void
update_edge(const RTCTX *ctx, RTT_ISO_EDGE *e, const RTT_ISO_EDGE *upd, int fields)
{
  if (fields & RTT_COL_EDGE_EDGE_ID) e->edge_id = upd->edge_id;
  if (fields & RTT_COL_EDGE_START_NODE) e->start_node = upd->start_node;
  if (fields & RTT_COL_EDGE_END_NODE) e->end_node = upd->end_node;
  if (fields & RTT_COL_EDGE_FACE_LEFT) e->face_left = upd->face_left;
  if (fields & RTT_COL_EDGE_FACE_RIGHT) e->face_right = upd->face_right;
  if (fields & RTT_COL_EDGE_NEXT_LEFT) e->next_left = upd->next_left;
  if (fields & RTT_COL_EDGE_NEXT_RIGHT) e->next_right = upd->next_right;
  if (fields & RTT_COL_EDGE_GEOM)
  {
    if (e->geom) rtline_free(ctx, e->geom);
    e->geom = rtgeom_as_rtline(ctx,
        rtgeom_clone_deep(ctx, rtline_as_rtgeom(ctx, upd->geom)));
  }
}

static int
box_overlaps(const RTCTX *ctx, const RTGEOM *g, const RTGBOX *box)
{
  RTGBOX gbox;
  if (!box) return 1;
  if (rtgeom_calculate_gbox(ctx, g, &gbox) != RT_SUCCESS) return 0;
  return gbox.xmin <= box->xmax && gbox.xmax >= box->xmin &&
         gbox.ymin <= box->ymax && gbox.ymax >= box->ymin;
}

/* Finish a query returning nodes, honouring the "limit" convention */
static RTT_ISO_NODE *
node_result(RTT_BE_TOPOLOGY *topo, int *matches, int nmatches,
            int *numelems, int limit)
{
  const RTCTX *ctx = CTX(topo);
  RTT_ISO_NODE *out;
  int i;
  if (limit == -1)
  {
    *numelems = nmatches ? 1 : 0;
    return NULL;
  }
  if (limit > 0 && nmatches > limit) nmatches = limit;
  *numelems = nmatches;
  if (!nmatches) return NULL;
  out = rtalloc(ctx, sizeof(RTT_ISO_NODE) * nmatches);
  for (i = 0; i < nmatches; ++i) copy_node(ctx, &out[i], &topo->nodes[matches[i]]);
  return out;
}

static RTT_ISO_EDGE *
edge_result(RTT_BE_TOPOLOGY *topo, int *matches, int nmatches,
            int *numelems, int limit)
{
  const RTCTX *ctx = CTX(topo);
  RTT_ISO_EDGE *out;
  int i;
  if (limit == -1)
  {
    *numelems = nmatches ? 1 : 0;
    return NULL;
  }
  if (limit > 0 && nmatches > limit) nmatches = limit;
  *numelems = nmatches;
  if (!nmatches) return NULL;
  out = rtalloc(ctx, sizeof(RTT_ISO_EDGE) * nmatches);
  for (i = 0; i < nmatches; ++i) copy_edge(ctx, &out[i], &topo->edges[matches[i]]);
  return out;
}

static int *
match_buffer(int n)
{
  return malloc(sizeof(int) * (n ? n : 1));
}

/* ---- callbacks ---- */

static const char *
cb_lastErrorMessage(const RTT_BE_DATA *be)
{
  return be->errmsg;
}

static RTT_BE_TOPOLOGY *
cb_loadTopologyByName(const RTT_BE_DATA *be, const char *name)
{
  (void)be;
  (void)name;
  return &be_topo;
}

static int
cb_freeTopology(RTT_BE_TOPOLOGY *topo)
{
  (void)topo;
  return 1;
}

static RTT_ISO_NODE *
cb_getNodeById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ELEMID *ids,
               int *numelems, int fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int *m = match_buffer(topo->nnodes);
  int i, j, n = 0;
  RTT_ISO_NODE *out;
  (void)fields;
  for (i = 0; i < topo->nnodes; ++i)
    for (j = 0; j < *numelems; ++j)
      if (topo->nodes[i].node_id == ids[j]) { m[n++] = i; break; }
  out = node_result(topo, m, n, numelems, 0);
  free(m);
  return out;
}

static RTT_ISO_NODE *
cb_getNodeWithinDistance2D(const RTT_BE_TOPOLOGY *ctopo, const RTPOINT *pt,
                           double dist, int *numelems, int fields, int limit)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int *m = match_buffer(topo->nnodes);
  int i, n = 0;
  RTT_ISO_NODE *out;
  (void)fields;
  for (i = 0; i < topo->nnodes; ++i)
  {
    double d = rtgeom_mindistance2d(ctx, rtpoint_as_rtgeom(ctx, topo->nodes[i].geom),
                                    rtpoint_as_rtgeom(ctx, pt));
    if (d <= dist) m[n++] = i;
  }
  out = node_result(topo, m, n, numelems, limit);
  free(m);
  return out;
}

static int
cb_insertNodes(const RTT_BE_TOPOLOGY *ctopo, RTT_ISO_NODE *nodes, int numelems)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int i;
  topo->nodes = realloc(topo->nodes, sizeof(RTT_ISO_NODE) * (topo->nnodes + numelems));
  for (i = 0; i < numelems; ++i)
  {
    if (nodes[i].node_id == -1) nodes[i].node_id = topo->next_node_id++;
    copy_node(ctx, &topo->nodes[topo->nnodes++], &nodes[i]);
  }
  return 1;
}

static RTT_ISO_EDGE *
cb_getEdgeById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ELEMID *ids,
               int *numelems, int fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int *m = match_buffer(topo->nedges);
  int i, j, n = 0;
  RTT_ISO_EDGE *out;
  (void)fields;
  for (i = 0; i < topo->nedges; ++i)
    for (j = 0; j < *numelems; ++j)
      if (topo->edges[i].edge_id == ids[j]) { m[n++] = i; break; }
  out = edge_result(topo, m, n, numelems, 0);
  free(m);
  return out;
}

static RTT_ISO_EDGE *
cb_getEdgeWithinDistance2D(const RTT_BE_TOPOLOGY *ctopo, const RTPOINT *pt,
                           double dist, int *numelems, int fields, int limit)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int *m = match_buffer(topo->nedges);
  int i, n = 0;
  RTT_ISO_EDGE *out;
  (void)fields;
  for (i = 0; i < topo->nedges; ++i)
  {
    double d = rtgeom_mindistance2d(ctx, rtline_as_rtgeom(ctx, topo->edges[i].geom),
                                    rtpoint_as_rtgeom(ctx, pt));
    if (d <= dist) m[n++] = i;
  }
  out = edge_result(topo, m, n, numelems, limit);
  free(m);
  return out;
}

static RTT_ELEMID
cb_getNextEdgeId(const RTT_BE_TOPOLOGY *ctopo)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  return topo->next_edge_id++;
}

static int
cb_insertEdges(const RTT_BE_TOPOLOGY *ctopo, RTT_ISO_EDGE *edges, int numelems)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int i;
  topo->edges = realloc(topo->edges, sizeof(RTT_ISO_EDGE) * (topo->nedges + numelems));
  for (i = 0; i < numelems; ++i)
  {
    if (edges[i].edge_id == -1) edges[i].edge_id = topo->next_edge_id++;
    copy_edge(ctx, &topo->edges[topo->nedges++], &edges[i]);
  }
  return numelems;
}

static int
cb_updateEdges(const RTT_BE_TOPOLOGY *ctopo,
               const RTT_ISO_EDGE *sel_edge, int sel_fields,
               const RTT_ISO_EDGE *upd_edge, int upd_fields,
               const RTT_ISO_EDGE *exc_edge, int exc_fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i, n = 0;
  for (i = 0; i < topo->nedges; ++i)
  {
    RTT_ISO_EDGE *e = &topo->edges[i];
    if (!edge_matches(e, sel_edge, sel_fields)) continue;
    if (exc_edge && exc_fields && edge_matches(e, exc_edge, exc_fields)) continue;
    update_edge(CTX(topo), e, upd_edge, upd_fields);
    ++n;
  }
  return n;
}

static RTT_ISO_FACE *
cb_getFaceById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ELEMID *ids,
               int *numelems, int fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  RTT_ISO_FACE *out = NULL;
  int i, n = 0;
  (void)fields;
  if (*numelems)
    out = rtalloc(ctx, sizeof(RTT_ISO_FACE) * *numelems);
  for (i = 0; i < *numelems; ++i)
  {
    RTT_ISO_FACE *f = find_face(topo, ids[i]);
    if (f) copy_face(ctx, &out[n++], f);
  }
  *numelems = n;
  return out;
}

static RTT_ELEMID
cb_getFaceContainingPoint(const RTT_BE_TOPOLOGY *topo, const RTPOINT *pt)
{
  (void)topo;
  (void)pt;
  return 0; /* the universal face: this test builds no faces */
}

static int
cb_updateTopoGeomEdgeSplit(const RTT_BE_TOPOLOGY *topo, RTT_ELEMID split_edge,
                           RTT_ELEMID new_edge1, RTT_ELEMID new_edge2)
{
  (void)topo;
  (void)split_edge;
  (void)new_edge1;
  (void)new_edge2;
  return 1;
}

static int
cb_deleteEdges(const RTT_BE_TOPOLOGY *ctopo, const RTT_ISO_EDGE *sel_edge,
               int sel_fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i = 0, n = 0;
  while (i < topo->nedges)
  {
    if (edge_matches(&topo->edges[i], sel_edge, sel_fields))
    {
      if (topo->edges[i].geom) rtline_free(CTX(topo), topo->edges[i].geom);
      topo->edges[i] = topo->edges[--topo->nedges];
      ++n;
    }
    else ++i;
  }
  return n;
}

static RTT_ISO_NODE *
cb_getNodeWithinBox2D(const RTT_BE_TOPOLOGY *ctopo, const RTGBOX *box,
                      int *numelems, int fields, int limit)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int *m = match_buffer(topo->nnodes);
  int i, n = 0;
  RTT_ISO_NODE *out;
  (void)fields;
  for (i = 0; i < topo->nnodes; ++i)
    if (box_overlaps(ctx, rtpoint_as_rtgeom(ctx, topo->nodes[i].geom), box))
      m[n++] = i;
  out = node_result(topo, m, n, numelems, limit);
  free(m);
  return out;
}

static RTT_ISO_EDGE *
cb_getEdgeWithinBox2D(const RTT_BE_TOPOLOGY *ctopo, const RTGBOX *box,
                      int *numelems, int fields, int limit)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  const RTCTX *ctx = CTX(topo);
  int *m = match_buffer(topo->nedges);
  int i, n = 0;
  RTT_ISO_EDGE *out;
  (void)fields;
  for (i = 0; i < topo->nedges; ++i)
    if (box_overlaps(ctx, rtline_as_rtgeom(ctx, topo->edges[i].geom), box))
      m[n++] = i;
  out = edge_result(topo, m, n, numelems, limit);
  free(m);
  return out;
}

static RTT_ISO_EDGE *
cb_getEdgeByNode(const RTT_BE_TOPOLOGY *ctopo, const RTT_ELEMID *ids,
                 int *numelems, int fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int *m = match_buffer(topo->nedges);
  int i, j, n = 0;
  RTT_ISO_EDGE *out;
  (void)fields;
  for (i = 0; i < topo->nedges; ++i)
    for (j = 0; j < *numelems; ++j)
      if (topo->edges[i].start_node == ids[j] || topo->edges[i].end_node == ids[j])
      {
        m[n++] = i;
        break;
      }
  out = edge_result(topo, m, n, numelems, 0);
  free(m);
  return out;
}

static int
cb_updateNodes(const RTT_BE_TOPOLOGY *ctopo,
               const RTT_ISO_NODE *sel_node, int sel_fields,
               const RTT_ISO_NODE *upd_node, int upd_fields,
               const RTT_ISO_NODE *exc_node, int exc_fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i, n = 0;
  for (i = 0; i < topo->nnodes; ++i)
  {
    RTT_ISO_NODE *nd = &topo->nodes[i];
    if (!node_matches(nd, sel_node, sel_fields)) continue;
    if (exc_node && exc_fields && node_matches(nd, exc_node, exc_fields)) continue;
    update_node(CTX(topo), nd, upd_node, upd_fields);
    ++n;
  }
  return n;
}

static int
cb_updateEdgesById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ISO_EDGE *edges,
                   int numedges, int upd_fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i, n = 0;
  for (i = 0; i < numedges; ++i)
  {
    RTT_ISO_EDGE *e = find_edge(topo, edges[i].edge_id);
    if (!e) continue;
    update_edge(CTX(topo), e, &edges[i], upd_fields);
    ++n;
  }
  return n;
}

static int
cb_updateNodesById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ISO_NODE *nodes,
                   int numnodes, int upd_fields)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i, n = 0;
  for (i = 0; i < numnodes; ++i)
  {
    RTT_ISO_NODE *nd = find_node(topo, nodes[i].node_id);
    if (!nd) continue;
    update_node(CTX(topo), nd, &nodes[i], upd_fields);
    ++n;
  }
  return n;
}

static int
cb_deleteNodesById(const RTT_BE_TOPOLOGY *ctopo, const RTT_ELEMID *ids,
                   int numelems)
{
  RTT_BE_TOPOLOGY *topo = (RTT_BE_TOPOLOGY *)ctopo;
  int i = 0, j, n = 0;
  while (i < topo->nnodes)
  {
    int del = 0;
    for (j = 0; j < numelems; ++j)
      if (topo->nodes[i].node_id == ids[j]) del = 1;
    if (del)
    {
      if (topo->nodes[i].geom) rtpoint_free(CTX(topo), topo->nodes[i].geom);
      topo->nodes[i] = topo->nodes[--topo->nnodes];
      ++n;
    }
    else ++i;
  }
  return n;
}

static RTT_ISO_FACE *
cb_getFaceWithinBox2D(const RTT_BE_TOPOLOGY *topo, const RTGBOX *box,
                      int *numelems, int fields, int limit)
{
  (void)topo;
  (void)box;
  (void)fields;
  (void)limit;
  *numelems = 0; /* this test builds no faces */
  return NULL;
}

static int
cb_topoGetSRID(const RTT_BE_TOPOLOGY *topo)
{
  (void)topo;
  return 0;
}

static double
cb_topoGetPrecision(const RTT_BE_TOPOLOGY *topo)
{
  (void)topo;
  return 0;
}

static int
cb_topoHasZ(const RTT_BE_TOPOLOGY *topo)
{
  (void)topo;
  return 0;
}

static const RTT_BE_CALLBACKS callbacks = {
  cb_lastErrorMessage,
  NULL, /* createTopology */
  cb_loadTopologyByName,
  cb_freeTopology,
  cb_getNodeById,
  cb_getNodeWithinDistance2D,
  cb_insertNodes,
  cb_getEdgeById,
  cb_getEdgeWithinDistance2D,
  cb_getNextEdgeId,
  cb_insertEdges,
  cb_updateEdges,
  cb_getFaceById,
  cb_getFaceContainingPoint,
  cb_updateTopoGeomEdgeSplit,
  cb_deleteEdges,
  cb_getNodeWithinBox2D,
  cb_getEdgeWithinBox2D,
  cb_getEdgeByNode,
  cb_updateNodes,
  NULL, /* updateTopoGeomFaceSplit */
  NULL, /* insertFaces */
  NULL, /* updateFacesById */
  NULL, /* getRingEdges */
  cb_updateEdgesById,
  NULL, /* getEdgeByFace */
  NULL, /* getNodeByFace */
  cb_updateNodesById,
  NULL, /* deleteFacesById */
  cb_topoGetSRID,
  cb_topoGetPrecision,
  cb_topoHasZ,
  cb_deleteNodesById,
  NULL, /* checkTopoGeomRemEdge */
  NULL, /* updateTopoGeomFaceHeal */
  NULL, /* checkTopoGeomRemNode */
  NULL, /* updateTopoGeomEdgeHeal */
  cb_getFaceWithinBox2D
};

static RTLINE *
make_line(const RTCTX *ctx, int npoints, const double *xy)
{
  RTPOINT **pts = malloc(sizeof(RTPOINT *) * npoints);
  RTLINE *line;
  int i;
  for (i = 0; i < npoints; ++i) pts[i] = rtpoint_make2d(ctx, 0, xy[2 * i], xy[2 * i + 1]);
  line = rtline_from_ptarray(ctx, 0, npoints, pts);
  for (i = 0; i < npoints; ++i) rtpoint_free(ctx, pts[i]);
  free(pts);
  return line;
}

static void
print_topology(const RTCTX *ctx)
{
  int i;
  for (i = 0; i < be_topo.nnodes; ++i)
  {
    char *wkt = rtgeom_to_wkt(ctx, rtpoint_as_rtgeom(ctx, be_topo.nodes[i].geom),
                              RTWKT_ISO, 15, NULL);
    printf("  node %lld: %s\n", (long long)be_topo.nodes[i].node_id, wkt);
    rtfree(ctx, wkt);
  }
  for (i = 0; i < be_topo.nedges; ++i)
  {
    char *wkt = rtgeom_to_wkt(ctx, rtline_as_rtgeom(ctx, be_topo.edges[i].geom),
                              RTWKT_ISO, 15, NULL);
    printf("  edge %lld (%lld -> %lld): %s\n", (long long)be_topo.edges[i].edge_id,
           (long long)be_topo.edges[i].start_node,
           (long long)be_topo.edges[i].end_node, wkt);
    rtfree(ctx, wkt);
  }
}

static int
run_case(const char *label, int with_faces)
{
  static const double base_xy[] = {0, 0, 10, 0};
  static const double new_xy[] = {5, 5, 5, 0, 8, 0};
  RTCTX *ctx;
  RTT_BE_IFACE *iface;
  RTT_TOPOLOGY *topo;
  RTLINE *line;
  RTT_ELEMID *ids;
  int nedges = -1;
  int i, ret = 0;

  ctx = rtgeom_init(NULL, NULL, NULL);
  if (!ctx) return 1;
  rtgeom_set_error_logger(ctx, error_logger, NULL);
  rtgeom_set_notice_logger(ctx, notice_logger, NULL);

  memset(&be_data, 0, sizeof(be_data));
  memset(&be_topo, 0, sizeof(be_topo));
  be_data.ctx = ctx;
  be_topo.be = &be_data;
  be_topo.next_node_id = 1;
  be_topo.next_edge_id = 1;
  be_topo.next_face_id = 1;
  last_error[0] = '\0';

  iface = rtt_CreateBackendIface(ctx, &be_data);
  rtt_BackendIfaceRegisterCallbacks(iface, &callbacks);
  topo = rtt_LoadTopology(iface, "test");
  if (!topo)
  {
    fprintf(stderr, "%s: could not load topology\n", label);
    return 1;
  }

  /* existing edge (0 0, 10 0) */
  line = make_line(ctx, 2, base_xy);
  ids = with_faces ? rtt_AddLine(topo, line, 0, &nedges)
                   : rtt_AddLineNoFace(topo, line, 0, &nedges);
  rtline_free(ctx, line);
  if (!ids || nedges != 1)
  {
    fprintf(stderr, "%s: adding the first line failed: %s\n", label, last_error);
    return 1;
  }
  rtfree(ctx, ids);

  /* new line partly running along the existing edge */
  line = make_line(ctx, 3, new_xy);
  last_error[0] = '\0';
  ids = with_faces ? rtt_AddLine(topo, line, 0, &nedges)
                   : rtt_AddLineNoFace(topo, line, 0, &nedges);
  rtline_free(ctx, line);

  printf("%s: resulting topology\n", label);
  print_topology(ctx);

  if (!ids || last_error[0])
  {
    fprintf(stderr, "%s: adding the second line failed: %s\n", label, last_error);
    ret = 1;
  }
  else if (nedges != 2 || be_topo.nnodes != 5 || be_topo.nedges != 4)
  {
    fprintf(stderr, "%s: expected 2 edges for the new line and 5 nodes / 4 edges "
            "in total, got %d / %d / %d\n", label, nedges, be_topo.nnodes,
            be_topo.nedges);
    ret = 1;
  }
  if (ids) rtfree(ctx, ids);

  rtt_FreeTopology(topo);
  rtt_FreeBackendIface(iface);
  for (i = 0; i < be_topo.nnodes; ++i) rtpoint_free(ctx, be_topo.nodes[i].geom);
  for (i = 0; i < be_topo.nedges; ++i) rtline_free(ctx, be_topo.edges[i].geom);
  free(be_topo.nodes);
  free(be_topo.edges);
  free(be_topo.faces);
  rtgeom_finish(ctx);
  return ret;
}

int
main(void)
{
  int ret = run_case("rtt_AddLineNoFace", 0);
  if (ret) return ret;
  puts("librttopo AddLine overlay-junction test passed");
  return 0;
}
