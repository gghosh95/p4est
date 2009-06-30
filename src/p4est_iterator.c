/*
  This file is part of p4est.
  p4est is a C library to manage a parallel collection of quadtrees and/or
  octrees.

  Copyright (C) 2009 Carsten Burstedde, Lucas Wilcox,
                     Toby Isaac.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef P4_TO_P8
#include <p8est_algorithms.h>
#include <p8est_bits.h>
#include <p8est_iterator.h>
#else
#include <p4est_algorithms.h>
#include <p4est_bits.h>
#include <p4est_iterator.h>
#endif

typedef struct p4est_viter_args
{
  p4est_t            *p4est;
  int8_t              level;
  sc_array_t         *corner_in_zorder;
  int8_t              num_sides;
  int8_t             *start_idx2;
  int                *quad_idx2;
  sc_array_t         *ghost_layer;
  sc_array_t        **quadrants;
  sc_array_t         *quads;
  sc_array_t         *tree_local_num;
  sc_array_t         *tree;
  p4est_locidx_t    **index;
  p4est_locidx_t     *first_index;
  size_t             *count;
  p4est_quadrant_t  **test;
  bool                intra_tree;
}
p4est_viter_args_t;

static void
p4est_vert_iterator (p4est_viter_args_t * args, void *user_data,
                     p4est_vcb_func_t vcb_func)
{
  const int8_t        local = 0;
  const int8_t        ghost = 1;
  const int8_t        idx2_stride = P4EST_CHILDREN + 1;

  int8_t              i, side, sidetype;
  int8_t              level = args->level;
  sc_array_t         *corners = args->corner_in_zorder;
  int8_t              num_sides = args->num_sides;
  int8_t              start_level = args->level;
  size_t              num_ghosts = args->ghost_layer->elem_count;
  const int8_t       *start_idx2 = args->start_idx2;
  int                *quad_idx2 = args->quad_idx2;
  int8_t              this_corner;
  sc_array_t        **quadrants = args->quadrants;
  p4est_locidx_t    **index = args->index;
  p4est_locidx_t     *first_index = args->first_index;
  size_t             *count = args->count;
  p4est_quadrant_t  **test = args->test;
  p4est_quadrant_t  **ptemp;
  p4est_quadrant_t    temp;
  p4est_qcoord_t      mask = (-1) << (P4EST_MAXLEVEL - level);
  sc_array_t          test_view;
  p4est_vcb_info_t    info;
  size_t              temp_idx;
  p4est_locidx_t     *ploc;
  int                *pint;
  int                 level_idx2;
  int8_t              type;
  bool                all_empty, has_local;

#ifndef P4_TO_P8
  info.p4est = args->p4est;
#else
  info.p8est = args->p4est;
#endif

  info.ghost_layer = args->ghost_layer;
  info.quads = args->quads;
  info.tree_local_num = args->tree_local_num;
  info.tree = args->tree;
  info.corner_in_zorder = args->corner_in_zorder;
  info.intra_tree = args->intra_tree;

  test_view.elem_size = sizeof (p4est_quadrant_t);
  test_view.byte_alloc = -1;

  level_idx2 = level * idx2_stride;

  for (side = 0; side < num_sides; side++) {
    quad_idx2[side] = level_idx2 + start_idx2[side];
    ptemp = sc_array_index (info.quads, (size_t) side);
    *ptemp = NULL;
    for (type = local; type <= ghost; type++) {
      sidetype = side * 2 + type;
      first_index[sidetype] = index[sidetype][quad_idx2[side]];
      count[sidetype] = (size_t) (index[sidetype][quad_idx2[side] + 1] -
                                  first_index[sidetype]);
    }
  }

  all_empty = true;
  for (side = 0; side < num_sides; side++) {
    if (count[side * 2 + local]) {
      all_empty = false;
    }
  }
  if (all_empty) {
    return;
  }

  has_local = false;
  for (side = 0; side < num_sides; side++) {
    P4EST_ASSERT (side < corners->elem_count);
    this_corner = *((int8_t *) sc_array_index (corners, (size_t) side));
    for (type = local; type <= ghost; type++) {
      sidetype = side * 2 + type;
      if (count[sidetype]) {
        if (count[sidetype] == 1) {
          test[sidetype] = sc_array_index (quadrants[sidetype], (size_t)
                                           first_index[sidetype]);
          temp_idx = 0;
        }
        else {
          switch (this_corner) {
          case (0):
            P4EST_ASSERT (first_index[sidetype] <
                          quadrants[sidetype]->elem_count);
            test[sidetype] = sc_array_index (quadrants[sidetype], (size_t)
                                             first_index[sidetype]);
            temp_idx = 0;
            break;
          case (P4EST_CHILDREN - 1):
            P4EST_ASSERT (first_index[sidetype] + count[sidetype] - 1 <
                          quadrants[sidetype]->elem_count);
            test[sidetype] = sc_array_index (quadrants[sidetype], (size_t)
                                             first_index[sidetype] +
                                             count[sidetype] - 1);
            temp_idx = count[sidetype] - 1;
            break;
          default:
            P4EST_ASSERT (first_index[sidetype] <
                          quadrants[sidetype]->elem_count);
            test[sidetype] = sc_array_index (quadrants[sidetype], (size_t)
                                             first_index[sidetype]);
            temp = *(test[sidetype]);
            temp.x &= mask;
            temp.y &= mask;
#ifdef P4_TO_P8
            temp.z &= mask;
#endif
            temp.level = P4EST_QMAXLEVEL;
            P4EST_ASSERT (p4est_quadrant_is_valid (&temp));
            temp.x += (this_corner % 2) ?
              P4EST_QUADRANT_LEN (level) -
              P4EST_QUADRANT_LEN (P4EST_QMAXLEVEL) : 0;
            temp.y += ((this_corner % 4) / 2) ?
              P4EST_QUADRANT_LEN (level) -
              P4EST_QUADRANT_LEN (P4EST_QMAXLEVEL) : 0;
#ifdef P4_TO_P8
            temp.z += (this_corner / 4) ?
              P4EST_QUADRANT_LEN (level) -
              P4EST_QUADRANT_LEN (P4EST_QMAXLEVEL) : 0;
#endif
            P4EST_ASSERT (p4est_quadrant_is_valid (&temp));
            test_view.elem_count = count[sidetype];
            test_view.array = quadrants[sidetype]->array +
              sizeof (p4est_quadrant_t) * (size_t) first_index[sidetype];
            temp_idx = p4est_find_higher_bound (&test_view, &temp, 0);
            if (temp_idx == -1) {
              test[sidetype] = NULL;
            }
            else {
              P4EST_ASSERT (temp_idx < test_view.elem_count);
              test[sidetype] = sc_array_index (&test_view, (size_t) temp_idx);
            }
            break;
          }
        }
        if (test[sidetype] != NULL) {
          temp_idx += first_index[sidetype];
          temp = *(test[sidetype]);
          temp.x += (this_corner % 2) ? P4EST_QUADRANT_LEN (temp.level) : 0;
          temp.y += ((this_corner % 4) / 2) ?
            P4EST_QUADRANT_LEN (temp.level) : 0;
#ifdef P4_TO_P8
          temp.z += (this_corner / 4) ? P4EST_QUADRANT_LEN (temp.level) : 0;
#endif
          P4EST_ASSERT (p4est_quadrant_is_extended (&temp));
          if (temp.x & ~mask || temp.y & ~mask
#ifdef P4_TO_P8
              || temp.z & ~mask
#endif
            ) {
            test[sidetype] = NULL;
          }
          else {
            P4EST_ASSERT (side < info.quads->elem_count);
            ptemp = sc_array_index (info.quads, (size_t) side);
            *ptemp = test[sidetype];
            P4EST_ASSERT (side < info.tree_local_num->elem_count);
            ploc = sc_array_index (info.tree_local_num, (size_t) side);
            *ploc =
              (type == local) ? temp_idx :
              (p4est_locidx_t) (temp_idx - num_ghosts);
            if (type == local) {
              has_local = true;
            }
          }
        }
      }
    }
  }
  if (!has_local) {
    return;
  }
#ifdef P4EST_DEBUG
  for (side = 0; side < num_sides; side++) {
    ptemp = sc_array_index (info.quads, (size_t) side);
    if (*ptemp == NULL) {
      P4EST_PRODUCTIONF ("side %d not filled\n", side);
    }
  }
#endif
  vcb_func (&info, user_data);
}

#ifdef P4_TO_P8
typedef struct p8est_eiter_args
{
  int8_t              num_sides;
  p8est_t            *p8est;
  sc_array_t         *ghost_layer;
  int8_t              level;
  int8_t             *start_idx2;
  int8_t             *level_num;
  sc_array_t         *common_corner[2];
  sc_array_t        **quadrants;
  sc_array_t         *edge_in_zorder;
  sc_array_t         *tree;
  sc_array_t         *tree_local_num;
  sc_array_t         *quads;
  p4est_locidx_t    **index;
  p8est_quadrant_t  **test;
  p4est_locidx_t     *first_index;
  size_t             *count;
  int8_t             *test_level;
  int                *quad_idx2;
  bool               *refine;
  bool                intra_tree;
  p4est_viter_args_t *vert_args;
}
p8est_eiter_args_t;

static void
p8est_edge_iterator (p8est_eiter_args_t * args, void *user_data,
                     p8est_ecb_func_t ecb_func, p8est_vcb_func_t vcb_func)
{
  const int8_t        local = 0;
  const int8_t        ghost = 1;
  const int8_t        idx2_stride = P4EST_CHILDREN + 1;

  int8_t              num_sides = args->num_sides;
  int8_t              start_level = args->level;
  size_t              num_ghosts = args->ghost_layer->elem_count;
  int8_t             *start_idx2 = args->start_idx2;
  int8_t             *level_num = args->level_num;
  sc_array_t        **quadrants = args->quadrants;
  p4est_locidx_t    **index = args->index;
  sc_array_t        **common_corner = args->common_corner;
  p8est_quadrant_t  **test = args->test;
  p4est_locidx_t     *first_index = args->first_index;
  size_t             *count = args->count;
  int8_t             *test_level = args->test_level;
  int                *quad_idx2 = args->quad_idx2;
  bool               *refine = args->refine;
  p8est_quadrant_t   *test2;
  p8est_quadrant_t  **temp;
  p4est_locidx_t     *temp_loc;
  int                *temp_int;
  int8_t              i;
  int8_t              level;
  int8_t              side, n_side;
  int8_t              type, n_type;
  int8_t              sidetype, nsidentype;
  int                 level_idx2;
  p4est_ecb_info_t    info;
  sc_array_t          test_view;
  bool                all_empty, stop_refine;
  int                 temp_idx2;
  p4est_viter_args_t *vert_args = args->vert_args;
  sc_array_t         *corner_in_zorder = vert_args->corner_in_zorder;
  int                *c;

  level = start_level;
  level_idx2 = level * idx2_stride;
  for (side = 0; side < num_sides; side++) {
    quad_idx2[side] = level_idx2 + start_idx2[side];
    for (type = local; type <= ghost; type++) {
      sidetype = side * 2 + type;
      first_index[sidetype] = index[sidetype][quad_idx2[side]];
      count[sidetype] =
        (size_t) (index[sidetype][quad_idx2[side] + 1] -
                  first_index[sidetype]);
    }
  }
  all_empty = true;
  for (side = 0; side < num_sides; side++) {
    if (count[side * 2 + local]) {
      all_empty = false;
      break;
    }
  }
  if (all_empty)
    return;

  info.p8est = args->p8est;
  info.ghost_layer = args->ghost_layer;
  info.quads = args->quads;
  info.tree_local_num = args->tree_local_num;
  info.tree = args->tree;
  info.edge_in_zorder = args->edge_in_zorder;
  info.intra_tree = args->intra_tree;

  test_view.elem_size = sizeof (p4est_quadrant_t);
  test_view.byte_alloc = -1;

  level_num[start_level] = 0;
  for (;;) {
    for (side = 0; side < num_sides; side++) {
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        if (count[sidetype]) {
          P4EST_ASSERT (first_index[sidetype] <
                        quadrants[sidetype]->elem_count);
          test[sidetype] =
            sc_array_index (quadrants[sidetype],
                            (size_t) first_index[sidetype]);
          test_level[sidetype] = test[sidetype]->level;
        }
        else {
          test_level[sidetype] = -1;
        }
      }
      refine[side] = true;
    }
    stop_refine = false;
    for (side = 0; side < num_sides; side++) {
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        if (test_level[sidetype] == level) {
          P4EST_ASSERT (side < info.quads->elem_count);
          temp = sc_array_index (info.quads, (size_t) side);
          *temp = test[sidetype];
          P4EST_ASSERT (side < info.tree_local_num->elem_count);
          temp_loc = sc_array_index (info.tree_local_num, (size_t) side);
          *temp_loc =
            (type == local) ? first_index[sidetype] :
            (p4est_locidx_t) (first_index[sidetype] - num_ghosts);
          refine[side] = false;
          stop_refine = true;
        }
      }
    }
    all_empty = true;
    for (side = 0; side < num_sides; side++) {
      if (refine[side]) {
        all_empty = false;
        break;
      }
    }
    if (all_empty) {
      info.hanging_flag = false;
      info.common_corner = common_corner[0];
      ecb_func (&info, user_data);
      level_num[level]++;
      goto change_search_area;
    }
    for (side = 0; side < num_sides; side++) {
      if (refine[side]) {
        quad_idx2[side] = level_idx2 + idx2_stride;
        for (type = local; type <= ghost; type++) {
          sidetype = side * 2 + type;
          test_view.elem_count = count[sidetype];
          test_view.array = quadrants[sidetype]->array +
            sizeof (p4est_quadrant_t) * (size_t) first_index[sidetype];
          p4est_split_array (&test_view, level,
                             index[sidetype] + quad_idx2[side]);
          for (i = 0; i < idx2_stride; i++) {
            index[sidetype][quad_idx2[side] + i] += first_index[sidetype];
          }
        }
      }
    }
    if (stop_refine) {
      info.hanging_flag = true;
      for (i = 0; i < 2; i++) {
        all_empty = true;
        info.common_corner = common_corner[i];
        for (side = 0; side < num_sides; side++) {
          if (refine[side]) {
            P4EST_ASSERT (side < common_corner[i]->elem_count);
            temp_int = sc_array_index (common_corner[i], (size_t) side);
            quad_idx2[side] = level_idx2 + idx2_stride + *temp_int;
            for (type = local; type <= ghost; type++) {
              sidetype = side * 2 + type;
              first_index[sidetype] = index[sidetype][quad_idx2[side]];
              count[sidetype] =
                (size_t) index[sidetype][quad_idx2[side] + 1] -
                first_index[sidetype];
              if (count[sidetype]) {
                P4EST_ASSERT (count[sidetype] == 1);
                P4EST_ASSERT (first_index[sidetype] <
                              quadrants[sidetype]->elem_count);
                test2 =
                  sc_array_index (quadrants[sidetype],
                                  (size_t) first_index[sidetype]);
                P4EST_ASSERT (test2->level == level + 1);
                P4EST_ASSERT (side < info.quads->elem_count);
                temp = sc_array_index (info.quads, (size_t) side);
                *temp = test2;
                P4EST_ASSERT (side < info.tree_local_num->elem_count);
                temp_loc =
                  sc_array_index (info.tree_local_num, (size_t) side);
                *temp_loc = (type == local) ? first_index[sidetype]
                  : (p4est_locidx_t) (first_index[sidetype] - num_ghosts);
                if (type == local)
                  all_empty = false;
              }
            }
          }
        }
        if (!all_empty) {
          ecb_func (&info, user_data);
        }
      }
      level_num[level]++;
      goto change_search_area;
    }
    level_num[++level] = 0;
    level_idx2 += idx2_stride;
  change_search_area:
    if (level_num[start_level] > 0) {
      break;
    }
    if (level_num[level] == 2) {
      // vertex iterator goes here
      if (vcb_func != NULL) {
        vert_args->level = level;
        for (i = 0; i < 2 * num_sides; i++) {
          c = sc_array_index (corner_in_zorder, (size_t) i);
          temp_int = sc_array_index
            (common_corner[1 - i / num_sides], (size_t) i % num_sides);
          *c = *temp_int;
          temp_int = sc_array_index
            (common_corner[i / num_sides], (size_t) i % num_sides);
          start_idx2[i] = *temp_int;
          temp_idx2 = level_idx2 + start_idx2[i];
          index[i * 2 + local][temp_idx2] =
            index[(i % num_sides) * 2 + local][temp_idx2];
          index[i * 2 + local][temp_idx2 + 1] =
            index[(i % num_sides) * 2 + local][temp_idx2 + 1];
          index[i * 2 + ghost][temp_idx2] =
            index[(i % num_sides) * 2 + ghost][temp_idx2];
          index[i * 2 + ghost][temp_idx2 + 1] =
            index[(i % num_sides) * 2 + ghost][temp_idx2 + 1];
        }
        p4est_vert_iterator (vert_args, user_data, vcb_func);
      }
      level_num[--level]++;
      level_idx2 -= idx2_stride;
      goto change_search_area;
    }
    all_empty = true;
    for (side = 0; side < num_sides; side++) {
      P4EST_ASSERT (side < common_corner[level_num[level]]->elem_count);
      temp_int = sc_array_index
        (common_corner[level_num[level]], (size_t) side);
      quad_idx2[side] = level_idx2 + *temp_int;
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        first_index[sidetype] = index[sidetype][quad_idx2[side]];
        count[sidetype] = (size_t)
          (index[sidetype][quad_idx2[side] + 1] - first_index[sidetype]);
        if (type == local && count[sidetype]) {
          all_empty = false;
        }
      }
    }
    if (all_empty) {
      level_num[level]++;
      goto change_search_area;
    }
  }
}
#endif

typedef struct p4est_fiter_args
{
  p4est_t            *p4est;
  sc_array_t         *ghost_layer;
  int8_t              level;
  int8_t             *start_idx2;
  int8_t              face[2];                /** z-order */
  int8_t             *level_num;
  int8_t              orientation;
  int8_t             *num_to_child;
  sc_array_t        **quadrants;
  p4est_locidx_t    **index;
  p4est_locidx_t     *first_index;
  p4est_topidx_t      tree[2];
  bool                intra_tree;
  p4est_quadrant_t  **test;
  size_t             *count;
  int8_t             *test_level;
  int                *quad_idx2;
  bool               *refine;
  bool                outside_face;
#ifdef P4_TO_P8
  p8est_eiter_args_t *edge_args;
#endif
  p4est_viter_args_t *vert_args;
}
p4est_fiter_args_t;

#ifndef P4_TO_P8
static void
p4est_face_iterator (p4est_fiter_args_t * args, void *user_data,
                     p4est_fcb_func_t fcb_func, p4est_vcb_func_t vcb_func)
#else
static void
p8est_face_iterator (p4est_fiter_args_t * args, void *user_data,
                     p8est_fcb_func_t fcb_func, p8est_ecb_func_t ecb_func,
                     p8est_vcb_func_t vcb_func)
#endif
{

  const int8_t        left = 0;
  const int8_t        right = 1;
  const int8_t        local = 0;
  const int8_t        ghost = 1;
  const int8_t        idx2_stride = P4EST_CHILDREN + 1;
  const int8_t        ntc_str = P4EST_CHILDREN / 2;

  int8_t              start_level = args->level;
  int8_t             *start_idx2 = args->start_idx2;
  const int8_t       *face = args->face;
  int8_t             *level_num = args->level_num;
  sc_array_t        **quadrants = args->quadrants;
  p4est_locidx_t    **index = args->index;
  p4est_locidx_t     *first_index = args->first_index;
  int8_t             *num_to_child = args->num_to_child;
  p4est_quadrant_t  **test = args->test;
  size_t             *count = args->count;
  int8_t             *test_level = args->test_level;
  int                *quad_idx2 = args->quad_idx2;
  bool               *refine = args->refine;
  int8_t              limit;

  p4est_quadrant_t   *test2;
  int8_t              i, j, k, dir;
  int8_t              true_dir, v;
  int8_t              level;
  int8_t              side, n_side;
  int8_t              type, n_type;
  int8_t              sidetype, nsidentype;
  int                 level_idx2;
  p4est_fcb_info_t    info;
  sc_array_t          test_view;
  size_t              num_ghosts = args->ghost_layer->elem_count;
#ifdef P4_TO_P8
  p8est_eiter_args_t *edge_args = args->edge_args;
  sc_array_t         *common_corner[2];

  sc_array_t         *edge_in_zorder = edge_args->edge_in_zorder;
  int                *e_common_corner[2];
  int                *e_edge_in_zorder;
  common_corner[0] = edge_args->common_corner[0];
  common_corner[1] = edge_args->common_corner[1];
  e_common_corner[0] = (int *) common_corner[0]->array;
  e_common_corner[1] = (int *) common_corner[1]->array;
  e_edge_in_zorder = (int *) edge_in_zorder->array;
#endif
  int                 temp_idx2;
  p4est_viter_args_t *vert_args = args->vert_args;
  sc_array_t         *corner_in_zorder = vert_args->corner_in_zorder;
  int                *c;

  limit = args->outside_face ? left : right;
  level = start_level;
  level_idx2 = level * idx2_stride;
  for (side = left; side <= limit; side++) {
    quad_idx2[side] = level_idx2 + start_idx2[side];
    for (type = local; type <= ghost; type++) {
      sidetype = side * 2 + type;
      first_index[sidetype] = index[sidetype][quad_idx2[side]];
      count[sidetype] =
        (size_t) (index[sidetype][quad_idx2[side] + 1] -
                  first_index[sidetype]);
    }
  }
  if (!args->outside_face) {
    if (!count[left * 2 + local] && !count[right * 2 + local]) {
      return;
    }
  }
  else {
    if (!count[left * 2 + local]) {
      return;
    }
  }

#ifndef P4_TO_P8
  info.p4est = args->p4est;
#else
  info.p8est = args->p4est;
#endif
  info.ghost_layer = args->ghost_layer;
  info.orientation = args->orientation;
  info.intra_tree = args->intra_tree;

  test_view.elem_size = sizeof (p4est_quadrant_t);
  test_view.byte_alloc = -1;

  level_num[start_level] = 0;
  for (;;) {
    for (side = left; side <= limit; side++) {
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        if (count[sidetype]) {
          P4EST_ASSERT (first_index[sidetype] <
                        quadrants[sidetype]->elem_count);
          test[sidetype] =
            sc_array_index (quadrants[sidetype],
                            (size_t) first_index[sidetype]);
          test_level[sidetype] = test[sidetype]->level;
        }
        else {
          test_level[sidetype] = -1;
        }
      }
    }
    refine[left] = refine[right] = true;
    for (side = left; side <= limit; side++) {
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        if (test_level[sidetype] == level) {
          if (fcb_func != NULL) {
            P4EST_ASSERT (count[sidetype] == 1);
            P4EST_ASSERT (count[side * 2 + (type ^ 1)] == 0);
            info.left_quad = test[sidetype];
            info.left_tree_local_num = (type == local) ?
              first_index[sidetype] :
              (p4est_locidx_t) (first_index[sidetype] - num_ghosts);
            info.left_outgoing_face = face[side];
            info.left_tree = args->tree[side];
            refine[side] = false;
            if (!args->outside_face) {
              n_side = side ^ 1;
              info.right_outgoing_face = face[n_side];
              info.right_tree = args->tree[n_side];
              for (n_type = type; n_type <= ghost; n_type++) {
                nsidentype = n_side * 2 + n_type;
                if ((n_type > type || n_side > side) &&
                    test_level[nsidentype] == level) {
                  P4EST_ASSERT (count[nsidentype] == 1);
                  P4EST_ASSERT (count[n_side * 2 + (n_type ^ 1)] == 0);
                  info.right_quad = test[nsidentype];
                  info.right_tree_local_num = (n_type == local) ?
                    first_index[nsidentype] :
                    (p4est_locidx_t) (first_index[nsidentype] - num_ghosts);
                  info.left_corner = num_to_child[side * ntc_str];
                  info.right_corner = num_to_child[n_side * ntc_str];
                  info.hanging_flag = false;
                  P4EST_ASSERT (!(type == ghost && n_type == ghost));
                  fcb_func (&info, user_data);
                  level_num[level]++;
                  goto change_search_area;
                }
              }
#ifdef P4EST_DEBUG
              if (type == local) {
                P4EST_ASSERT (count[n_side * 2 + type] > 0 ||
                              count[n_side * 2 + (type ^ 1)] > 0);
              }
#endif
            }
            else {
              info.right_outgoing_face = info.left_outgoing_face;
              info.right_tree = info.left_tree;
              /**
							info.right_quad = NULL;
							*/
              info.right_quad = info.left_quad;
              info.right_tree_local_num = info.left_tree_local_num;
              info.left_corner = num_to_child[side * ntc_str];
              info.right_corner = info.left_corner;
              info.hanging_flag = false;
              fcb_func (&info, user_data);
              level_num[level]++;
              goto change_search_area;
            }
          }
          else {
            level_num[level]++;
            goto change_search_area;
          }
        }
      }
    }
    for (side = left; side <= limit; side++) {
      if (refine[side]) {
        quad_idx2[side] = level_idx2 + idx2_stride;
        for (type = local; type <= ghost; type++) {
          sidetype = side * 2 + type;
          test_view.elem_count = count[sidetype];
          test_view.array = quadrants[sidetype]->array +
            sizeof (p4est_quadrant_t) * (size_t) first_index[sidetype];
          p4est_split_array (&test_view, level,
                             index[sidetype] + quad_idx2[side]);
          for (i = 0; i < idx2_stride; i++) {
            index[sidetype][quad_idx2[side] + i] += first_index[sidetype];
          }
        }
      }
    }
    for (side = left; side <= limit; side++) {
      if (!refine[side]) {
        n_side = side ^ 1;
        info.hanging_flag = true;
        for (type = local; type <= ghost; type++) {
          if (test_level[side * 2 + type] == level) {
            for (i = 0; i < P4EST_CHILDREN / 2; i++) {
              info.left_corner = num_to_child[side * ntc_str + i];
              info.right_corner = num_to_child[n_side * ntc_str + i];
              quad_idx2[n_side] = level_idx2 + idx2_stride +
                num_to_child[n_side * ntc_str + i];
              for (n_type = local; n_type <= ghost; n_type++) {
                nsidentype = n_side * 2 + n_type;
                first_index[nsidentype] =
                  index[nsidentype][quad_idx2[n_side]];
                count[nsidentype] = (size_t)
                  (index[nsidentype][quad_idx2[n_side] + 1] -
                   first_index[nsidentype]);
                if (count[nsidentype] && (type + n_type < 2)) {
                  P4EST_ASSERT (count[nsidentype] == 1);
                  P4EST_ASSERT (index[n_side * 2 + (n_type ^ 1)]
                                [quad_idx2[n_side]]
                                == index[n_side * 2 + (n_type ^ 1)]
                                [quad_idx2[n_side] + 1]);
                  test2 = sc_array_index (quadrants[nsidentype],
                                          (size_t) first_index[nsidentype]);
                  info.right_quad = test2;
                  info.right_tree_local_num = (n_type == local) ?
                    first_index[nsidentype] :
                    (p4est_locidx_t) (first_index[nsidentype] - num_ghosts);
                  fcb_func (&info, user_data);
                  P4EST_ASSERT (test2->level == level + 1);
                }
              }
#ifdef P4EST_DEBUG
              if (type == local) {
                P4EST_ASSERT (count[n_side * 2 + local] == 1 ||
                              count[n_side * 2 + ghost] == 1);
              }
#endif
            }
            level_num[level]++;
            goto change_search_area;
          }
        }
      }
    }
    level_num[++level] = 0;
    level_idx2 += idx2_stride;
  change_search_area:
    if (level_num[start_level] > 0) {
      break;
    }
    if (level_num[level] == P4EST_CHILDREN / 2) {
#ifdef P4_TO_P8
      // edge iterator goes here
      if (ecb_func != NULL || vcb_func != NULL) {
        edge_args->level = level;
        if (!args->outside_face) {
          for (dir = 0; dir < 2; dir++) {
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 4; j++) {
                k = j >> 1;
                if (dir == 0) {
                  e_common_corner[side][j] =
                    num_to_child[(j % 2) * ntc_str + (2 * (1 - k) + side)];
                }
                else {
                  e_common_corner[side][j] =
                    num_to_child[(j % 2) * ntc_str + ((1 - k) + 2 * side)];
                }
              }
            }
            for (j = 0; j < 4; j++) {
              v = e_common_corner[0][j];
              true_dir = e_common_corner[1][j] - v;
              true_dir = (true_dir > 0) ? true_dir : -true_dir;
              e_edge_in_zorder[j] = p8est_corner_edges[v][true_dir >> 1];
            }
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 4; j++) {
                k = j >> 1;
                if (dir == 0) {
                  start_idx2[j] =
                    num_to_child[(j % 2) * ntc_str + side + 2 * k];
                }
                else {
                  start_idx2[j] =
                    num_to_child[(j % 2) * ntc_str + 2 * side + k];
                }
                temp_idx2 = level_idx2 + start_idx2[j];
                index[j * 2 + local][temp_idx2] = index[(j % 2) * 2 + local]
                  [temp_idx2];
                index[j * 2 + local][temp_idx2 + 1] =
                  index[(j % 2) * 2 + local]
                  [temp_idx2 + 1];
                index[j * 2 + ghost][temp_idx2] = index[(j % 2) * 2 + ghost]
                  [temp_idx2];
                index[j * 2 + ghost][temp_idx2 + 1] =
                  index[(j % 2) * 2 + ghost]
                  [temp_idx2 + 1];
              }
              p8est_edge_iterator (edge_args, user_data, ecb_func, vcb_func);
            }
          }
        }
        else {
          for (dir = 0; dir < 2; dir++) {
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 2; j++) {
                if (dir == 0) {
                  e_common_corner[side][j] =
                    num_to_child[(2 * (1 - j) + side)];
                }
                else {
                  e_common_corner[side][j] =
                    num_to_child[((1 - j) + 2 * side)];
                }
              }
            }
            for (j = 0; j < 2; j++) {
              v = e_common_corner[0][j];
              true_dir = e_common_corner[1][j] - v;
              true_dir = (true_dir > 0) ? true_dir : -true_dir;
              e_edge_in_zorder[j] = p8est_corner_edges[v][true_dir >> 1];
            }
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 2; j++) {
                if (dir == 0) {
                  start_idx2[j] = num_to_child[side + 2 * j];
                }
                else {
                  start_idx2[j] = num_to_child[2 * side + j];
                }
                temp_idx2 = level_idx2 + start_idx2[j];
                index[j * 2 + local][temp_idx2] = index[local][temp_idx2];
                index[j * 2 + local][temp_idx2 + 1] = index[local]
                  [temp_idx2 + 1];
                index[j * 2 + ghost][temp_idx2] = index[ghost][temp_idx2];
                index[j * 2 + ghost][temp_idx2 + 1] = index[ghost]
                  [temp_idx2 + 1];
              }
              p8est_edge_iterator (edge_args, user_data, ecb_func, vcb_func);
            }
          }
        }
      }
#endif
      // vertex iterator goes here
      if (vcb_func != NULL) {
        vert_args->level = level;
        if (!args->outside_face) {
          for (j = 0; j < P4EST_CHILDREN; j++) {
            c = sc_array_index (corner_in_zorder, (size_t) j);
            *c = num_to_child
              [(j % 2) * ntc_str + P4EST_CHILDREN / 2 - 1 - (j / 2)];
            start_idx2[j] = num_to_child[(j % 2) * ntc_str + (j / 2)];
            temp_idx2 = level_idx2 + start_idx2[j];
            index[j * 2 + local][temp_idx2] = index[(j % 2) * 2 + local]
              [temp_idx2];
            index[j * 2 + local][temp_idx2 + 1] = index[(j % 2) * 2 + local]
              [temp_idx2 + 1];
            index[j * 2 + ghost][temp_idx2] = index[(j % 2) * 2 + ghost]
              [temp_idx2];
            index[j * 2 + ghost][temp_idx2 + 1] = index[(j % 2) * 2 + ghost]
              [temp_idx2 + 1];
          }
        }
        else {
          for (j = 0; j < P4EST_CHILDREN / 2; j++) {
            c = sc_array_index (corner_in_zorder, (size_t) j);
            *c = num_to_child[P4EST_CHILDREN / 2 - 1 - j];
            start_idx2[j] = num_to_child[j];
            temp_idx2 = level_idx2 + start_idx2[j];
            index[j * 2 + local][temp_idx2] = index[local][temp_idx2];
            index[j * 2 + local][temp_idx2 + 1] = index[local]
              [temp_idx2 + 1];
            index[j * 2 + ghost][temp_idx2] = index[ghost][temp_idx2];
            index[j * 2 + ghost][temp_idx2 + 1] = index[ghost]
              [temp_idx2 + 1];
          }
        }
        p4est_vert_iterator (vert_args, user_data, vcb_func);
      }
      level_num[--level]++;
      level_idx2 -= idx2_stride;
      goto change_search_area;
    }
    for (side = left; side <= limit; side++) {
      quad_idx2[side] =
        level_idx2 + num_to_child[side * ntc_str + level_num[level]];
    }
    for (side = left; side <= limit; side++) {
      for (type = local; type <= ghost; type++) {
        sidetype = side * 2 + type;
        first_index[sidetype] = index[sidetype][quad_idx2[side]];
        count[sidetype] = (size_t)
          (index[sidetype][quad_idx2[side] + 1] - first_index[sidetype]);
      }
    }
    if (!args->outside_face) {
      if (!count[left * 2 + local] && !count[right * 2 + local]) {
        level_num[level]++;
        goto change_search_area;
      }
    }
    else {
      if (!count[left * 2 + local]) {
        level_num[level]++;
        goto change_search_area;
      }
    }
  }
}

void static
p4est_quad_iterator ()
{
};

#ifndef P4_TO_P8
void
p4est_iterator (p4est_t * p4est, sc_array_t * ghost_layer, void *user_data,
                p4est_qcb_func_t qcb_func, p4est_fcb_func_t fcb_func,
                p4est_vcb_func_t vcb_func)
#else
void
p8est_iterator (p8est_t * p4est, sc_array_t * ghost_layer, void *user_data,
                p8est_qcb_func_t qcb_func,
                p8est_fcb_func_t fcb_func,
                p8est_ecb_func_t ecb_func, p8est_vcb_func_t vcb_func)
#endif
{
  const int8_t        left = 0;
  const int8_t        right = 1;
  const int8_t        local = 0;
  const int8_t        ghost = 1;
  const int8_t        idx2_stride = P4EST_CHILDREN + 1;
  const int8_t        ntc_str = P4EST_CHILDREN / 2;

  sc_array_t         *trees = p4est->trees, **quadrants, test_view;
  p4est_tree_t       *tree;
  p4est_connectivity_t *conn = p4est->connectivity;
  size_t              num_ghosts, num_local;
  p4est_quadrant_t  **test;
#ifdef P4EST_DEBUG
  p4est_quadrant_t    tmpq;
#endif
#ifndef P4_TO_P8
  int8_t              rface[2];
#endif
  int8_t              level, side, type, left_side, right_side, dir,
    *test_level, *level_num, modulus, i, j, k, l, m, *face,
    num_to_child[P4EST_CHILDREN], *ttf = conn->tree_to_face, *start_idx2;
  p4est_topidx_t     *ttt = conn->tree_to_tree,
    global_num_trees, nt, t, e, c, guess_tree;
  p4est_topidx_t      alloc_size = 4;
  p4est_locidx_t    **index, *this_index, *tree_first_ghost, *first_index;
  size_t              guess, guess_low, *count;
#ifdef P4_TO_P8
  p4est_topidx_t     *tte = conn->tree_to_edge;
  p4est_topidx_t      num_edges = conn->num_edges;
  p4est_topidx_t     *ett_offset = conn->ett_offset;
  p4est_topidx_t     *edge_to_tree = conn->edge_to_tree;
  p4est_topidx_t      this_edge;
  int8_t             *edge_to_edge = conn->edge_to_edge;
  p4est_topidx_t      max_edge_size = 4;
  p8est_eiter_args_t  edge_args;
  p4est_topidx_t      edge_size;
  sc_array_t          common_corner[2];
  sc_array_t          edge_in_zorder;
  sc_array_t          edge_tree_array;
  sc_array_t          edge_tree_local_num;
  sc_array_t          edge_quads;
  int                *e_common_corner[2], *e_edge_in_zorder;
  int8_t              perm, corner0, corner1, ne;
  p4est_locidx_t     *e_tree_local_num;
  p4est_quadrant_t  **e_quads;
  p4est_topidx_t     *e_tree;
#endif
  int8_t              orientation, nc;
  p4est_topidx_t      this_corner;
#ifndef P4_TO_P8
  p4est_topidx_t     *ttc = conn->tree_to_vertex;
  p4est_topidx_t      num_corners = conn->num_vertices;
  p4est_topidx_t     *ctt_offset = conn->vtt_offset;
  p4est_topidx_t     *corner_to_tree = conn->vertex_to_tree;
  p4est_topidx_t     *corner_to_corner = conn->vertex_to_vertex;
  p4est_corner_transform_t *ct;
  sc_array_t          ctransforms, *cta = &ctransforms;
#else
  p4est_topidx_t     *ttc = conn->tree_to_corner;
  p4est_topidx_t      num_corners = conn->num_corners;
  p4est_topidx_t     *ctt_offset = conn->ctt_offset;
  p4est_topidx_t     *corner_to_tree = conn->corner_to_tree;
  int8_t             *corner_to_corner = conn->corner_to_corner;
#endif
  p4est_topidx_t      max_corner_size = P4EST_CHILDREN;
  p4est_topidx_t      corner_size;
  sc_array_t          corner_in_zorder;
  sc_array_t          vert_tree_array;
  sc_array_t          vert_tree_local_num;
  sc_array_t          vert_quads;
  int                *v_corner_in_zorder;
  p4est_locidx_t     *v_tree_local_num;
  p4est_quadrant_t  **v_quads;
  p4est_topidx_t     *v_tree;
  int                *quad_idx2;
  bool               *refine;
  p4est_fiter_args_t  face_args;
  p4est_viter_args_t  vert_args;
  int                 level_idx2;
  p4est_qcb_info_t    info;

  if (p4est->first_local_tree < 0) {
    return;
  }

  P4EST_ASSERT (p4est_is_valid (p4est));
  P4EST_ASSERT (ghost_layer != NULL);
  P4EST_ASSERT (sc_array_is_sorted
                (ghost_layer, p4est_quadrant_compare_piggy));

  if (fcb_func == NULL && vcb_func == NULL
#ifdef P4_TO_P8
      && ecb_func == NULL
#endif
    ) {
    p4est_quad_iterator ();
    return;
  }

#ifdef P4_TO_P8
  if (ecb_func != NULL || vcb_func != NULL) {
    for (e = 0; e < num_edges; e++) {
      edge_size = ett_offset[e + 1] - ett_offset[e];
      max_edge_size = (edge_size > max_edge_size) ? edge_size : max_edge_size;
    }
  }
  alloc_size = (2 * max_edge_size > alloc_size) ?
    2 * max_edge_size : alloc_size;
  max_corner_size =
    (max_edge_size * 2 >
     max_corner_size) ? max_edge_size * 2 : max_corner_size;
#endif

  if (vcb_func != NULL) {
    for (c = 0; c < num_corners; c++) {
      corner_size = ctt_offset[c + 1] - ctt_offset[c];
      max_corner_size = (corner_size > max_corner_size) ? corner_size :
        max_corner_size;
    }
  }
  alloc_size = (2 * max_corner_size > alloc_size) ?
    2 * max_corner_size : alloc_size;

  /** initialize arrays that keep track of where we are in the search */
  index = P4EST_ALLOC (p4est_locidx_t *, alloc_size);
  quadrants = P4EST_ALLOC (sc_array_t *, alloc_size);
  start_idx2 = P4EST_ALLOC (int8_t, alloc_size / 2);
  first_index = P4EST_ALLOC (p4est_locidx_t, alloc_size);
  test = P4EST_ALLOC (p4est_quadrant_t *, alloc_size);
  count = P4EST_ALLOC (size_t, alloc_size);
  test_level = P4EST_ALLOC (int8_t, alloc_size);
  quad_idx2 = P4EST_ALLOC (int, alloc_size / 2);
  refine = P4EST_ALLOC (bool, alloc_size / 2);
  for (i = 0; i < alloc_size; i++) {
    index[i] = P4EST_ALLOC (p4est_locidx_t, P4EST_MAXLEVEL * idx2_stride);
    if (i % 2)
      quadrants[i] = ghost_layer;
  }
  level_num = P4EST_ALLOC (int8_t, P4EST_MAXLEVEL);

  test_view.elem_size = sizeof (p4est_quadrant_t);
  test_view.byte_alloc = -1;

  /** Divide the ghost_layer by p.which_tree */
  global_num_trees = trees->elem_count;
  tree_first_ghost = P4EST_ALLOC (p4est_topidx_t, global_num_trees + 1);
  guess_low = 0;
  tree_first_ghost[0] = 0;
  num_ghosts = ghost_layer->elem_count;
  for (t = 1; t <= global_num_trees; t++) {
    tree_first_ghost[t] = num_ghosts;
  }
  if (num_ghosts) {
    guess = num_ghosts / 2;
    t = 1;
    for (;;) {
      P4EST_ASSERT (guess < ghost_layer->elem_count);
      test[0] = sc_array_index (ghost_layer, guess);
      guess_tree = test[0]->p.which_tree;
      if (guess_tree < t) {
        guess_low = guess + 1;
      }
      else {
        for (nt = t; nt <= guess_tree; nt++) {
          tree_first_ghost[nt] = (p4est_locidx_t) guess;
          if ((size_t) tree_first_ghost[nt] == guess_low)
            t++;
        }
      }
      if (t == global_num_trees || guess_low == num_ghosts)
        break;
      guess = guess_low + ((size_t) tree_first_ghost[t] - guess_low) / 2;
    }
  }

  /** set up the arguments passed to face_iterator that are invariant */
  face_args.p4est = p4est;
  face_args.ghost_layer = ghost_layer;
  face_args.level_num = level_num;
  face_args.index = index;
  face_args.quadrants = quadrants;
  face_args.num_to_child = num_to_child;
  face_args.start_idx2 = start_idx2;
  face_args.first_index = first_index;
  face_args.test = test;
  face_args.count = count;
  face_args.test_level = test_level;
  face_args.quad_idx2 = quad_idx2;
  face_args.refine = refine;
#ifdef P4_TO_P8
  face_args.edge_args = &edge_args;
#endif
  face_args.vert_args = &vert_args;
  face = face_args.face;

  /** set up the arguments passed to vert_iterator that are invariant */
  if (vcb_func != NULL) {
    vert_args.p4est = p4est;
    vert_args.ghost_layer = ghost_layer;
    vert_args.quadrants = quadrants;
    vert_args.index = index;
    vert_args.start_idx2 = start_idx2;
    vert_args.first_index = first_index;
    vert_args.test = test;
    vert_args.count = count;
    vert_args.quad_idx2 = quad_idx2;
    v_corner_in_zorder = P4EST_ALLOC (int, max_corner_size);
    corner_in_zorder.elem_size = sizeof (int);
    corner_in_zorder.byte_alloc = -1;
    corner_in_zorder.array = (char *) (v_corner_in_zorder);
    vert_args.corner_in_zorder = &corner_in_zorder;
    v_tree = P4EST_ALLOC (p4est_topidx_t, max_corner_size);
    v_tree_local_num = P4EST_ALLOC (p4est_locidx_t, max_corner_size);
    v_quads = P4EST_ALLOC (p4est_quadrant_t *, max_corner_size);
    vert_tree_array.elem_size = sizeof (p4est_topidx_t);
    vert_tree_array.byte_alloc = -1;
    vert_tree_array.array = (char *) v_tree;
    vert_tree_local_num.elem_size = sizeof (p4est_locidx_t);
    vert_tree_local_num.byte_alloc = -1;
    vert_tree_local_num.array = (char *) v_tree_local_num;
    vert_quads.elem_size = sizeof (p4est_quadrant_t *);
    vert_quads.byte_alloc = -1;
    vert_quads.array = (char *) v_quads;
    vert_args.tree = &vert_tree_array;
    vert_args.tree_local_num = &vert_tree_local_num;
    vert_args.quads = &vert_quads;
  }

#ifdef P4_TO_P8
  /** set up the arguments passed to edge_iterator that are invariant */
  if (ecb_func != NULL || vcb_func != NULL) {
    edge_args.p8est = p4est;
    edge_args.ghost_layer = ghost_layer;
    edge_args.level_num = level_num;
    edge_args.quadrants = quadrants;
    edge_args.index = index;
    edge_args.start_idx2 = start_idx2;
    edge_args.first_index = first_index;
    edge_args.test = test;
    edge_args.count = count;
    edge_args.test_level = test_level;
    edge_args.quad_idx2 = quad_idx2;
    edge_args.refine = refine;
    e_common_corner[0] = P4EST_ALLOC (int, max_edge_size);
    e_common_corner[1] = P4EST_ALLOC (int, max_edge_size);
    e_edge_in_zorder = P4EST_ALLOC (int, max_edge_size);
    common_corner[0].elem_size = sizeof (int);
    common_corner[0].byte_alloc = -1;
    common_corner[0].array = (char *) (e_common_corner[0]);
    common_corner[1].elem_size = sizeof (int);
    common_corner[1].byte_alloc = -1;
    common_corner[1].array = (char *) (e_common_corner[1]);
    edge_in_zorder.elem_size = sizeof (int);
    edge_in_zorder.byte_alloc = -1;
    edge_in_zorder.array = (char *) e_edge_in_zorder;
    edge_args.common_corner[0] = &(common_corner[0]);
    edge_args.common_corner[1] = &(common_corner[1]);
    edge_args.edge_in_zorder = &edge_in_zorder;
    edge_args.vert_args = &vert_args;
    e_tree = P4EST_ALLOC (p4est_topidx_t, max_edge_size);
    e_tree_local_num = P4EST_ALLOC (p4est_locidx_t, max_edge_size);
    e_quads = P4EST_ALLOC (p4est_quadrant_t *, max_edge_size);
    edge_tree_array.elem_size = sizeof (p4est_topidx_t);
    edge_tree_array.byte_alloc = -1;
    edge_tree_array.array = (char *) e_tree;
    edge_tree_local_num.elem_size = sizeof (p4est_locidx_t);
    edge_tree_local_num.byte_alloc = -1;
    edge_tree_local_num.array = (char *) e_tree_local_num;
    edge_quads.elem_size = sizeof (p4est_quadrant_t *);
    edge_quads.byte_alloc = -1;
    edge_quads.array = (char *) e_quads;
    edge_args.tree = &edge_tree_array;
    edge_args.tree_local_num = &edge_tree_local_num;
    edge_args.quads = &edge_quads;
  }
#endif

  /** set up the arguments passed to qcb_func that are invariant */
#ifndef P4_TO_P8
  info.p4est = p4est;
#else
  info.p8est = p4est;
#endif
  info.ghost_layer = ghost_layer;

  for (t = 0; t < global_num_trees; t++) {

    tree = sc_array_index (trees, t);
    quadrants[local] = &(tree->quadrants);

    P4EST_ASSERT (p4est_tree_is_sorted (tree));
    P4EST_ASSERT (tree->maxlevel <= P4EST_QMAXLEVEL);

    this_index = index[left * 2 + local];
    this_index[0] = 0;
    num_local = quadrants[local]->elem_count;
    this_index[1] = (p4est_locidx_t) num_local;
    this_index = index[left * 2 + ghost];
    this_index[0] = tree_first_ghost[t];
    this_index[1] = tree_first_ghost[t + 1];
    num_ghosts = (size_t) (this_index[1] - this_index[0]);

    if (!num_local) {
      continue;
    }

#ifdef P4EST_DEBUG
    if (num_local) {
      this_index = index[left * 2 + local];
      P4EST_ASSERT ((size_t) this_index[0] < quadrants[local]->elem_count);
      test[0] = sc_array_index (quadrants[local], (size_t) this_index[0]);
      P4EST_ASSERT (p4est_quadrant_is_inside_root (test[0]));
      P4EST_ASSERT ((size_t) (this_index[1] - 1)
                    < quadrants[local]->elem_count);
      test[0] = sc_array_index (quadrants[local], (size_t) this_index[1] - 1);
      P4EST_ASSERT (p4est_quadrant_is_inside_root (test[0]));
    }
    if (num_ghosts) {
      this_index = index[left * 2 + ghost];
      P4EST_ASSERT ((size_t) this_index[0] < quadrants[ghost]->elem_count);
      test[0] = sc_array_index (quadrants[ghost], (size_t) this_index[0]);
      P4EST_ASSERT (p4est_quadrant_is_inside_root (test[0]));
      P4EST_ASSERT ((size_t) (this_index[1] - 1)
                    < quadrants[ghost]->elem_count);
      test[0] = sc_array_index (quadrants[ghost], (size_t) this_index[1] - 1);
      P4EST_ASSERT (p4est_quadrant_is_inside_root (test[0]));
    }
#endif

    face_args.tree[left] = t;
    face_args.tree[right] = t;
    face_args.intra_tree = true;
    face_args.outside_face = false;
    face_args.quadrants[left * 2 + local] = quadrants[local];
    face_args.quadrants[right * 2 + local] = quadrants[local];
    face_args.orientation = 0;

#ifdef P4_TO_P8
    if (ecb_func != NULL || vcb_func != NULL) {
      edge_args.intra_tree = true;
      edge_args.num_sides = 4;
      common_corner[0].elem_count = 4;
      common_corner[1].elem_count = 4;
      edge_in_zorder.elem_count = 4;
      edge_tree_array.elem_count = 4;
      e_tree[0] = e_tree[1] = e_tree[2] = e_tree[3] = t;
      edge_tree_local_num.elem_count = 4;
      edge_quads.elem_count = 4;
      quadrants[2] = quadrants[local];
      quadrants[4] = quadrants[local];
      quadrants[6] = quadrants[local];
    }
#endif

    if (vcb_func != NULL) {
      vert_args.intra_tree = true;
      vert_args.num_sides = P4EST_CHILDREN;
      corner_in_zorder.elem_count = P4EST_CHILDREN;
      for (i = 0; i < P4EST_CHILDREN; i++) {
        v_tree[i] = t;
        quadrants[2 * i] = quadrants[local];
      }
      vert_tree_array.elem_count = P4EST_CHILDREN;
      vert_tree_local_num.elem_count = P4EST_CHILDREN;
      vert_quads.elem_count = P4EST_CHILDREN;
    }

    info.tree = t;

    level = 0;
    level_idx2 = 0;
    level_num[0] = 0;
    while (level_num[0] == 0) {
      if (level_num[level] == P4EST_CHILDREN) {
        for (left_side = 0, right_side = 1; right_side < 2 * P4EST_DIM;
             left_side += 2, right_side += 2) {
          face[left] = right_side;
          face[right] = left_side;
          for (i = 0; i < P4EST_CHILDREN / 2; i++) {
#ifndef P4_TO_P8
            num_to_child[left * ntc_str + i] = p4est_face_corners
              [p4est_zface_to_rface[right_side]][i];
            num_to_child[right * ntc_str + i] = p4est_face_corners
              [p4est_zface_to_rface[left_side]][i];
#else
            num_to_child[left * ntc_str + i] =
              p8est_face_corners[right_side][i];
            num_to_child[right * ntc_str + i] =
              p8est_face_corners[left_side][i];
#endif
          }
          face_args.level = level;
          for (i = 0; i < P4EST_CHILDREN / 2; i++) {
#ifndef P4_TO_P8
            start_idx2[left] =
              p4est_face_corners[p4est_zface_to_rface[left_side]][i];
            start_idx2[right] =
              p4est_face_corners[p4est_zface_to_rface[right_side]][i];
#else
            start_idx2[left] = p8est_face_corners[left_side][i];
            start_idx2[right] = p8est_face_corners[right_side][i];
#endif
            /** initialize index[right] for use in face_adjacency */
            quad_idx2[0] = level_idx2 + start_idx2[right];
            index[right * 2 + local][quad_idx2[0]] =
              index[left * 2 + local][quad_idx2[0]];
            index[right * 2 + local][quad_idx2[0] + 1] =
              index[left * 2 + local][quad_idx2[0] + 1];
            index[right * 2 + ghost][quad_idx2[0]] =
              index[left * 2 + ghost][quad_idx2[0]];
            index[right * 2 + ghost][quad_idx2[0] + 1] =
              index[left * 2 + ghost][quad_idx2[0] + 1];
#ifndef P4_TO_P8
            p4est_face_iterator (&face_args, user_data, fcb_func, vcb_func);
#else
            p8est_face_iterator (&face_args, user_data, fcb_func, ecb_func,
                                 vcb_func);
#endif
          }
        }
#ifdef P4_TO_P8
        if (ecb_func != NULL || vcb_func != NULL) {
          edge_args.level = level;
          for (dir = 0; dir < P4EST_DIM; dir++) {
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 4; j++) {
                e_common_corner[side][j] =
                  p8est_face_corners[dir * 2 + side][3 - j];
              }
            }
            for (j = 0; j < 4; j++) {
              e_edge_in_zorder[j] = 4 * dir + (3 - j);
            }
            for (side = 0; side < 2; side++) {
              for (j = 0; j < 4; j++) {
                start_idx2[j] = p8est_face_corners[dir * 2 + side][j];
                if (j != 0) {
                  quad_idx2[0] = level_idx2 + start_idx2[j];
                  index[j * 2 + local][quad_idx2[0]] =
                    index[local][quad_idx2[0]];
                  index[j * 2 + local][quad_idx2[0] + 1] =
                    index[local][quad_idx2[0] + 1];
                  index[j * 2 + ghost][quad_idx2[0]] =
                    index[ghost][quad_idx2[0]];
                  index[j * 2 + ghost][quad_idx2[0] + 1] =
                    index[ghost][quad_idx2[0] + 1];
                }
              }
              p8est_edge_iterator (&edge_args, user_data, ecb_func, vcb_func);
            }
          }
        }
#endif
        if (vcb_func != NULL) {
          vert_args.level = level;
          for (i = 0; i < P4EST_CHILDREN; i++) {
            v_corner_in_zorder[i] = P4EST_CHILDREN - i - 1;
            start_idx2[i] = i;
            quad_idx2[0] = level_idx2 + start_idx2[i];
            index[i * 2 + local][quad_idx2[0]] = index[local][quad_idx2[0]];
            index[i * 2 + local][quad_idx2[0] + 1] =
              index[local][quad_idx2[0] + 1];
            index[i * 2 + ghost][quad_idx2[0]] = index[ghost][quad_idx2[0]];
            index[i * 2 + ghost][quad_idx2[0] + 1] =
              index[ghost][quad_idx2[0] + 1];
          }
          p4est_vert_iterator (&vert_args, user_data, vcb_func);
        }
        level_num[--level]++;
        level_idx2 -= idx2_stride;
        continue;
      }
      quad_idx2[0] = level_idx2 + level_num[level];
      for (type = local; type <= ghost; type++) {
        this_index = index[left * 2 + type];
        first_index[type] = this_index[quad_idx2[0]];
        count[type] =
          (size_t) (this_index[quad_idx2[0] + 1] - first_index[type]);
      }
      if (!count[local]) {
        level_num[level]++;
        continue;
      }
      for (type = local; type <= ghost; type++) {
        if (count[type]) {
          P4EST_ASSERT ((size_t) first_index[type]
                        < quadrants[type]->elem_count);
          test[type] = sc_array_index (quadrants[type],
                                       (size_t) first_index[type]);
          test_level[type] = test[type]->level;
          if (test_level[type] == level) {
            if (type == local) {
              info.quad = test[type];
              info.tree_local_num = first_index[type];
              if (qcb_func != NULL)
                qcb_func (&info, user_data);
            }
            break;
          }
        }
        else {
          test_level[type] = -1;
        }
      }
      if (type <= ghost) {
        level_num[level]++;
        continue;
      }
      quad_idx2[0] = level_idx2 + idx2_stride;
      for (type = local; type <= ghost; type++) {
        test_view.elem_count = count[type];
        P4EST_ASSERT ((size_t) first_index[type] + count[type]
                      <= quadrants[type]->elem_count);
        test_view.array = quadrants[type]->array +
          sizeof (p4est_quadrant_t) * (size_t) first_index[type];
        this_index = index[left * 2 + type];
        p4est_split_array (&test_view, level, this_index + quad_idx2[0]);

        for (i = 0; i < idx2_stride; i++) {
          this_index[quad_idx2[0] + i] += first_index[type];
        }
      }
      level_num[++level] = 0;
      level_idx2 += idx2_stride;
    }

    face_args.intra_tree = false;
#ifdef P4_TO_P8
    edge_args.intra_tree = false;
#endif
    vert_args.intra_tree = false;

    for (i = 0; i < 2 * P4EST_DIM; i++) {
      face_args.level = 0;
      face_args.start_idx2[left] = face_args.start_idx2[right] = 0;
#ifndef P4_TO_P8
      rface[left] = p4est_zface_to_rface[i];
#endif
      for (j = 0; j < P4EST_CHILDREN / 2; j++) {
#ifndef P4_TO_P8
        num_to_child[left * ntc_str + j] = p4est_face_corners[rface[left]][j];
#else
        num_to_child[left * ntc_str + j] = p4est_face_corners[i][j];
#endif
      }
      face[left] = i;
#ifndef P4_TO_P8
      nt = ttt[t * 2 * P4EST_DIM + rface[left]];
      rface[right] = ttf[t * 2 * P4EST_DIM + rface[left]];
      face_args.orientation = rface[right] / 4;
      rface[right] %= 4;
      P4EST_ASSERT (ttt[nt * 2 * P4EST_DIM + rface[right]] == t);
      face[right] = p4est_rface_to_zface[rface[right]];
#else
      nt = ttt[t * 2 * P4EST_DIM + face[left]];
      face[right] = ttf[t * 2 * P4EST_DIM + face[left]];
      face_args.orientation = face[right] / 6;
      face[right] %= 6;
#endif
      if ((t > nt) || (t == nt && face[left] > face[right])) {
        face_args.outside_face = false;
#ifndef P4_TO_P8
        modulus = face[left] ^ face[right];
        modulus = ((modulus & 2) >> 1) ^ (modulus & 1) ^ 1;
        if ((modulus ^ face_args.orientation) == 0) {
          num_to_child[right * ntc_str] = p4est_face_corners[rface[right]][0];
          num_to_child[right * ntc_str + 1] =
            p4est_face_corners[rface[right]][1];
        }
        else {
          num_to_child[right * ntc_str] = p4est_face_corners[rface[right]][1];
          num_to_child[right * ntc_str + 1] =
            p4est_face_corners[rface[right]][0];
        }
#else
        modulus = p8est_face_permutation_refs[face[left]][face[right]];
        modulus = p8est_face_permutation_sets[modulus][face_args.orientation];
        for (j = 0; j < P4EST_CHILDREN / 2; j++) {
          k = p8est_face_permutations[modulus][j];
          num_to_child[right * ntc_str + j] =
            p8est_face_corners[face[right]][k];
        }
#endif
        level_num[0] = 0;
        P4EST_ASSERT (nt < trees->elem_count);
        tree = sc_array_index (trees, nt);
        face_args.quadrants[right * 2 + local] = &(tree->quadrants);
        face_args.tree[right] = nt;
        index[right * 2 + local][0] = 0;
        index[right * 2 + local][1] = num_local = (p4est_locidx_t)
          tree->quadrants.elem_count;
        index[right * 2 + ghost][0] = tree_first_ghost[nt];
        index[right * 2 + ghost][1] = tree_first_ghost[nt + 1];

#ifdef P4_TO_P8
        if (ecb_func != NULL || vcb_func != NULL) {
          edge_args.num_sides = 4;
          common_corner[0].elem_count = 4;
          common_corner[1].elem_count = 4;
          edge_in_zorder.elem_count = 4;
          edge_tree_array.elem_count = 4;
          e_tree[0] = e_tree[2] = t;
          e_tree[1] = e_tree[3] = nt;
          edge_tree_local_num.elem_count = 4;
          edge_quads.elem_count = 4;
          quadrants[2] = &(tree->quadrants);
          quadrants[4] = quadrants[local];
          quadrants[6] = &(tree->quadrants);
        }
#endif

        if (vcb_func != NULL) {
          vert_args.num_sides = P4EST_CHILDREN;
          corner_in_zorder.elem_count = P4EST_CHILDREN;
          for (j = 0; j < P4EST_CHILDREN; j++) {
            if (j % 2) {
              v_tree[j] = nt;
              quadrants[2 * j] = &(tree->quadrants);
            }
            else {
              v_tree[j] = t;
              quadrants[2 * j] = quadrants[local];
            }
          }
          vert_tree_array.elem_count = P4EST_CHILDREN;
          vert_tree_local_num.elem_count = P4EST_CHILDREN;
          vert_quads.elem_count = P4EST_CHILDREN;
        }

#ifndef P4_TO_P8
        p4est_face_iterator (&face_args, user_data, fcb_func, vcb_func);
#else
        p8est_face_iterator (&face_args, user_data, fcb_func, ecb_func,
                             vcb_func);
#endif
      }
      else if (t == nt && face[left] == face[right]) {
        face_args.outside_face = true;
        level_num[0] = 0;
#ifdef P4_TO_P8
        if (ecb_func != NULL || vcb_func != NULL) {
          edge_args.num_sides = 2;
          common_corner[0].elem_count = 2;
          common_corner[1].elem_count = 2;
          edge_in_zorder.elem_count = 2;
          edge_tree_array.elem_count = 2;
          e_tree[0] = e_tree[1] = t;
          edge_tree_local_num.elem_count = 2;
          edge_quads.elem_count = 2;
          quadrants[2] = quadrants[local];
        }
#endif

        if (vcb_func != NULL) {
          vert_args.num_sides = P4EST_CHILDREN / 2;
          corner_in_zorder.elem_count = P4EST_CHILDREN / 2;
          for (j = 0; j < P4EST_CHILDREN / 2; j++) {
            v_tree[j] = t;
            quadrants[2 * j] = quadrants[local];
          }
          vert_tree_array.elem_count = P4EST_CHILDREN / 2;
          vert_tree_local_num.elem_count = P4EST_CHILDREN / 2;
          vert_quads.elem_count = P4EST_CHILDREN / 2;
        }

#ifndef P4_TO_P8
        p4est_face_iterator (&face_args, user_data, fcb_func, vcb_func);
#else
        p8est_face_iterator (&face_args, user_data, fcb_func, ecb_func,
                             vcb_func);
#endif
      }
    }

#ifdef P4_TO_P8
    if (ecb_func != NULL || vcb_func != NULL) {
      for (i = 0; i < 12; i++) {
        edge_args.level = 0;
        e_edge_in_zorder[0] = i;
        if (num_edges > 0) {
          this_edge = tte[t * 12 + i];
        }
        else {
          this_edge = -1;
        }
        if (this_edge > -1) {
          for (j = ett_offset[this_edge]; j < ett_offset[this_edge + 1]; j++) {
            if (edge_to_tree[j] == t) {
              modulus = edge_to_edge[j];
              if (modulus % 12 == i) {
                modulus /= 12;
                break;
              }
            }
          }
        }
        else {
          modulus = 0;
        }
        edge_args.num_sides = 1;
        e_tree[0] = t;
        if (modulus == 0) {
          e_common_corner[0][0] = p8est_edge_corners[i][0];
          e_common_corner[1][0] = p8est_edge_corners[i][1];
        }
        else {
          e_common_corner[0][0] = p8est_edge_corners[i][1];
          e_common_corner[1][0] = p8est_edge_corners[i][0];
        }
        for (side = left; side <= right; side++) {
          face[0] = p8est_edge_faces[i][side];
          nt = ttt[t * 6 + face[0]];
          face[1] = ttf[t * 6 + face[0]];
          orientation = face[1] / 6;
          face[1] %= 6;
          if (nt != t || face[1] != face[0]) {
            perm = p8est_face_permutation_refs[face[0]][face[1]];
            perm = p8est_face_permutation_sets[perm][orientation];
            corner0 = p8est_face_permutations[perm]
              [p8est_edge_face_corners[i][face[0]][0]];
            corner1 = p8est_face_permutations[perm]
              [p8est_edge_face_corners[i][face[0]][1]];
            k = edge_args.num_sides;
            if (modulus == 0) {
              e_common_corner[0][k] = p8est_face_corners[face[1]][corner0];
              e_common_corner[1][k] = p8est_face_corners[face[1]][corner1];
            }
            else {
              e_common_corner[0][k] = p8est_face_corners[face[1]][corner1];
              e_common_corner[1][k] = p8est_face_corners[face[1]][corner0];
            }
            ne = e_common_corner[1][k] - e_common_corner[0][k];
            ne = (ne > 0) ? ne : -ne;
            ne = p8est_corner_edges[e_common_corner[0][k]][ne >> 1];
            e_edge_in_zorder[k] = ne;
            e_tree[k] = nt;
            edge_args.num_sides++;
          }
        }
        if (this_edge > -1) {
          for (j = ett_offset[this_edge]; j < ett_offset[this_edge + 1]; j++) {
            nt = edge_to_tree[j];
            ne = edge_to_edge[j];
            orientation = ne / 12;
            ne %= 12;
            if (orientation == 0) {
              corner0 = p8est_edge_corners[ne][0];
              corner1 = p8est_edge_corners[ne][1];
            }
            else {
              corner0 = p8est_edge_corners[ne][1];
              corner1 = p8est_edge_corners[ne][0];
            }
            for (k = 0; k < edge_args.num_sides; k++) {
              if (nt == e_tree[k]) {
                if (corner0 == e_common_corner[0][k] &&
                    corner1 == e_common_corner[1][k]) {
                  break;
                }
              }
            }
            if (k == edge_args.num_sides) {
              e_tree[k] = nt;
              e_edge_in_zorder[k] = ne;
              e_common_corner[0][k] = corner0;
              e_common_corner[1][k] = corner1;
              edge_args.num_sides++;
            }
          }
        }
        for (j = 1; j < edge_args.num_sides; j++) {
          if (e_tree[j] > t) {
            break;
          }
          if (e_tree[j] == t && e_edge_in_zorder[j] > i) {
            break;
          }
        }
        if (j == edge_args.num_sides) {
          for (j = 0; j < edge_args.num_sides; j++) {
            edge_args.start_idx2[j] = 0;
            index[j * 2 + local][0] = 0;
            tree = sc_array_index (trees, (size_t) e_tree[j]);
            quadrants[j * 2 + local] = &(tree->quadrants);
            index[j * 2 + local][1] = quadrants[j * 2 + local]->elem_count;
            index[j * 2 + ghost][0] = tree_first_ghost[e_tree[j]];
            index[j * 2 + ghost][1] = tree_first_ghost[e_tree[j] + 1];
          }
          common_corner[0].elem_count = edge_args.num_sides;
          common_corner[1].elem_count = edge_args.num_sides;
          edge_in_zorder.elem_count = edge_args.num_sides;
          edge_tree_array.elem_count = edge_args.num_sides;
          edge_tree_local_num.elem_count = edge_args.num_sides;
          edge_quads.elem_count = edge_args.num_sides;

          if (vcb_func != NULL) {
            vert_args.num_sides = edge_args.num_sides * 2;
            corner_in_zorder.elem_count = vert_args.num_sides;
            for (j = 0; j < vert_args.num_sides; j++) {
              v_tree[j] = e_tree[j % edge_args.num_sides];
              quadrants[2 * j] = quadrants[2 * (j % edge_args.num_sides)];
            }
            vert_tree_array.elem_count = vert_args.num_sides;
            vert_tree_local_num.elem_count = vert_args.num_sides;
            vert_quads.elem_count = vert_args.num_sides;
          }

          p8est_edge_iterator (&edge_args, user_data, ecb_func, vcb_func);
        }
      }
    }
#endif

    if (vcb_func != NULL) {
#ifndef P4_TO_P8
      sc_array_init (cta, sizeof (p4est_corner_transform_t));
#endif
      for (i = 0; i < P4EST_CHILDREN; i++) {
        vert_args.level = 0;
        v_tree[0] = t;
        v_corner_in_zorder[0] = i;
        vert_args.num_sides = 1;

        for (j = 0; j < P4EST_DIM; j++) {
          face[0] = p4est_corner_faces[i][j];
          nt = ttt[t * P4EST_DIM * 2 + face[0]];
          face[1] = ttf[t * P4EST_DIM * 2 + face[0]];
          orientation = face[1] / (P4EST_DIM * 2);
          face[1] = face[1] % (P4EST_DIM * 2);
          if (nt == t && face[0] == face[1]) {
            continue;
          }
          v_tree[vert_args.num_sides] = nt;
          for (k = 0; k < P4EST_CHILDREN / 2; k++) {
            if (p4est_face_corners[face[0]][k] == i) {
#ifndef P4_TO_P8
              rface[0] = face[0];
              rface[1] = face[1];
              face[0] = p4est_rface_to_zface[rface[0]];
              face[1] = p4est_rface_to_zface[rface[1]];
              modulus = face[0] ^ face[1];
              modulus = ((modulus & 2) >> 1) ^ (modulus & 1) ^ 1;
              face[0] = rface[0];
              face[1] = rface[1];
              v_corner_in_zorder[vert_args.num_sides] =
                p4est_face_corners[face[1]][modulus ^ orientation ^ k];
#else
              modulus = p8est_face_permutation_refs[face[0]][face[1]];
              modulus = p8est_face_permutation_sets[modulus][orientation];
              v_corner_in_zorder[vert_args.num_sides] =
                p8est_face_corners[face[1]]
                [p8est_face_permutations[modulus][k]];
#endif
            }
          }
          vert_args.num_sides++;
        }

#ifdef P4_TO_P8
        if (num_edges > 0) {
          for (j = 0; j < P4EST_DIM; j++) {
            m = p8est_corner_edges[i][j];
            this_edge = tte[t * 12 + m];
            if (this_edge == -1) {
              continue;
            }
            else {
              for (k = ett_offset[this_edge];
                   k < ett_offset[this_edge + 1]; k++) {
                if (edge_to_tree[k] == t) {
                  modulus = edge_to_edge[k];
                  if (modulus % 12 == m) {
                    modulus /= 12;
                    break;
                  }
                }
              }
            }
            if (p8est_edge_corners[m][modulus] == i) {
              modulus = 0;
            }
            else {
              modulus = 1;
            }
            for (k = ett_offset[this_edge];
                 k < ett_offset[this_edge + 1]; k++) {
              nt = edge_to_tree[k];
              ne = edge_to_edge[k];
              orientation = ne / 12;
              ne %= 12;
              nc = p8est_edge_corners[ne][orientation ^ modulus];
              for (l = 0; l < vert_args.num_sides; l++) {
                if (nt == v_tree[l] && nc == v_corner_in_zorder[l]) {
                  break;
                }
              }
              if (l == vert_args.num_sides) {
                v_tree[l] = nt;
                v_corner_in_zorder[l] = nc;
                vert_args.num_sides++;
              }
            }
          }
        }
#endif

        if (num_corners > 0) {
#ifndef P4_TO_P8
          this_corner = ttc[t * P4EST_CHILDREN + p4est_corner_to_zorder[i]];
#else
          this_corner = ttc[t * P4EST_CHILDREN + i];
#endif
        }
        else {
          this_corner = -1;
        }

        if (this_corner != -1) {
#ifndef P4_TO_P8
          p4est_find_corner_transform (conn, t, p4est_corner_to_zorder[i],
                                       cta);
          for (j = 0; j < cta->elem_count; j++) {
            ct = sc_array_index (cta, (size_t) j);
            nt = ct->ntree;
            nc = ct->ncorner;
            for (k = 0; k < vert_args.num_sides; k++) {
              if (nt == v_tree[k] && nc == v_corner_in_zorder[k]) {
                break;
              }
            }
            if (k == vert_args.num_sides) {
              v_tree[k] = nt;
              v_corner_in_zorder[k] = nc;
              vert_args.num_sides++;
            }
          }
#else
          for (j = ctt_offset[this_corner];
               j < ctt_offset[this_corner + 1]; j++) {
            nt = corner_to_tree[j];
            nc = corner_to_corner[j];
            for (k = 0; k < vert_args.num_sides; k++) {
              if (nt == v_tree[k] && nc == v_corner_in_zorder[k]) {
                break;
              }
            }
            if (k == vert_args.num_sides) {
              v_tree[k] = nt;
              v_corner_in_zorder[k] = nc;
              vert_args.num_sides++;
            }
          }
#endif
        }

        for (j = 0; j < vert_args.num_sides; j++) {
          if (v_tree[j] > t) {
            break;
          }
          if (v_tree[j] == t && v_corner_in_zorder[j] > i) {
            break;
          }
        }
        if (j == vert_args.num_sides) {
          for (j = 0; j < vert_args.num_sides; j++) {
            vert_args.start_idx2[j] = 0;
            index[j * 2 + local][0] = 0;
            tree = sc_array_index (trees, (size_t) v_tree[j]);
            quadrants[j * 2 + local] = &(tree->quadrants);
            index[j * 2 + local][1] = quadrants[j * 2 + local]->elem_count;
            index[j * 2 + ghost][0] = tree_first_ghost[v_tree[j]];
            index[j * 2 + ghost][1] = tree_first_ghost[v_tree[j] + 1];
          }
          corner_in_zorder.elem_count = vert_args.num_sides;
          vert_tree_array.elem_count = vert_args.num_sides;
          vert_tree_local_num.elem_count = vert_args.num_sides;
          vert_quads.elem_count = vert_args.num_sides;

          p4est_vert_iterator (&vert_args, user_data, vcb_func);
        }
      }
#ifndef P4_TO_P8
      sc_array_reset (cta);
#endif
    }

  }

  if (vcb_func != NULL) {
    P4EST_FREE (v_corner_in_zorder);
    P4EST_FREE (v_tree);
    P4EST_FREE (v_tree_local_num);
    P4EST_FREE (v_quads);
  }

#ifdef P4_TO_P8
  if (ecb_func != NULL || vcb_func != NULL) {
    P4EST_FREE (e_common_corner[0]);
    P4EST_FREE (e_common_corner[1]);
    P4EST_FREE (e_tree);
    P4EST_FREE (e_quads);
    P4EST_FREE (e_tree_local_num);
    P4EST_FREE (e_edge_in_zorder);
  }
#endif
  P4EST_FREE (tree_first_ghost);
  P4EST_FREE (level_num);
  for (i = 0; i < alloc_size; i++) {
    P4EST_FREE (index[i]);
  }
  P4EST_FREE (index);
  P4EST_FREE (refine);
  P4EST_FREE (quad_idx2);
  P4EST_FREE (test_level);
  P4EST_FREE (count);
  P4EST_FREE (test);
  P4EST_FREE (first_index);
  P4EST_FREE (start_idx2);
  P4EST_FREE (quadrants);

}