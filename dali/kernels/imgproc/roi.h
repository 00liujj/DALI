// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef DALI_KERNELS_IMGPROC_ROI_H_
#define DALI_KERNELS_IMGPROC_ROI_H_

#include <vector>
#include "dali/core/geom/box.h"
#include "dali/kernels/tensor_shape.h"

namespace dali {
namespace kernels {

/**
 * Defines region of interest.
 * 0 dimension is interpreted along x axis (horizontal)
 * 1 dimension is interpreted along y axis (vertical)
 *
 *            image.x ->
 *          +--------------------------------+
 *          |                                |
 *          |   roi.lo    roi.x              |
 *  image.y |         +-----+                |
 *       |  |    roi.y|     |                |
 *       v  |         +-----+ roi.hi         |
 *          |                                |
 *          +--------------------------------+
 *
 * Additionally, by definition, ROI is top-left inclusive and bottom-right exclusive.
 * That means, that `Roi.lo` point is included in actual ROI and `Roi.hi` point isn't.
 */
template <size_t ndims>
using Roi = Box<ndims, int>;

namespace detail {

/**
 * Create a Roi with size matching the whole image
 */
template <int ndims, size_t spatial_dims = ndims - 1>
Roi<spatial_dims> WholeImage(const TensorShape <ndims> &shape) {
  ivec<spatial_dims> size;
  for (size_t i = 0; i < spatial_dims; i++)
    size[i] = shape[spatial_dims - 1 - i];
  return {0, size};
}

}  // namespace detail


/**
 * Defines TensorShape corresponding to provided Roi.
 *
 * Function assumes, that memory layout is *HWC, and the Roi
 * is represented as [[x_lo, y_lo], [x_hi, y_hi]].
 * Therefore, while copying, order of values needs to be reversed.
 *
 * @tparam ndims Number of dims in Roi
 * @param roi Region of interest
 * @param nchannels Number of channels in data
 * @return Corresponding TensorShape
 */
template <size_t spatial_dims, int ndims = spatial_dims + 1>
TensorShape<ndims> ShapeFromRoi(const Roi<spatial_dims> &roi, int nchannels) {
  DALI_ENFORCE(all_coords(roi.hi >= roi.lo), "Cannot create a TensorShape from an invalid Roi");
  TensorShape<ndims> ret;
  auto e = roi.extent();
  auto ridx = spatial_dims;
  ret[ridx--] = nchannels;
  for (size_t idx = 0; idx < spatial_dims; idx++) {
    ret[ridx--] = e[idx];
  }
  return ret;
}


/**
 * Convenient overload for batch processing (creating TensorListShape)
 */
template <size_t spatial_dims, int ndims = spatial_dims + 1>
TensorListShape<ndims> ShapeFromRoi(const std::vector<Roi<spatial_dims>> &rois, int nchannels) {
  DALI_ENFORCE(!rois.empty(), "Provided argument doesn't contain any Roi");
  TensorListShape<ndims> ret(rois.size());
  size_t i = 0;
  for (const auto &roi : rois) {
    ret.set_tensor_shape(i++, ShapeFromRoi(roi, nchannels));
  }
  return ret;
}


/**
 * Adjusted Roi is a Roi, which doesn't overflow the image, that is given by TensorShape.
 *
 * If `rois` is not provided (roi == nullptr), that means whole image is analysed:
 * return a Roi, that has the same size as the input image
 *
 * If `rois` is provided, adjusted Roi is an intersection of provided Roi and the image.
 *
 * Assumes HWC memory layout
 */
template <int ndims, size_t spatial_dims = ndims - 1>
Roi<spatial_dims> AdjustRoi(const Roi<spatial_dims> *roi, const TensorShape <ndims> &shape) {
  auto whole_image = detail::WholeImage(shape);
  return roi ? intersection(*roi, whole_image) : whole_image;
}


/**
 * Adjusted Roi is a Roi, which doesn't overflow the image, that is given by TensorShape.
 *
 * 1. If `rois` is empty, that means whole image is analysed: return a batch of Rois, where
 *    every Roi has the same size as the input image
 * 2. If `rois` is not empty, it is assumed, that Roi is provided for every image in batch.
 *    In this case, final Roi is an intersection of provided Roi and the image.
 *    (This is a sanity-check for Rois, that can be larger than image)
 *
 * Assumes HWC memory layout
 */
template <int ndims, size_t spatial_dims = ndims - 1>
std::vector<Roi<spatial_dims>>
AdjustRoi(const std::vector<Roi<spatial_dims>> rois, const TensorListShape <ndims> &shapes) {
  DALI_ENFORCE(rois.empty() || rois.size() == static_cast<size_t>(shapes.num_samples()),
               "Either provide `rois` for every corresponding `shape`, or none.");
  std::vector<Roi<spatial_dims>> ret(shapes.num_samples());

  if (rois.empty()) {
    for (int i = 0; i < shapes.num_samples(); i++) {
      ret[i] = detail::WholeImage(shapes[i]);
    }
  } else {
    for (size_t i = 0; i < rois.size(); i++) {
      ret[i] = intersection(rois[i], detail::WholeImage(shapes[i]));
    }
  }

  return ret;
}

}  // namespace kernels
}  // namespace dali

#endif  // DALI_KERNELS_IMGPROC_ROI_H_
