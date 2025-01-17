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

#ifndef DALI_PIPELINE_OPERATORS_DISPLACEMENT_WARP_AFFINE_PARAMS_H_
#define DALI_PIPELINE_OPERATORS_DISPLACEMENT_WARP_AFFINE_PARAMS_H_

#include <vector>
#include <sstream>
#include "dali/pipeline/operators/operator.h"
#include "dali/kernels/imgproc/warp/affine.h"
#include "dali/kernels/imgproc/warp/mapping_traits.h"
#include "dali/pipeline/operators/displacement/warp_param_provider.h"
#include "dali/kernels/tensor_shape_print.h"

namespace dali {

template <int spatial_ndim>
using WarpAffineParams = kernels::warp::mapping_params_t<kernels::AffineMapping<spatial_ndim>>;

template <typename Backend,
          int spatial_ndim,
          typename BorderType>
class WarpAffineParamProvider
: public WarpParamProvider<Backend, spatial_ndim, WarpAffineParams<spatial_ndim>, BorderType> {
 protected:
  using MappingParams = WarpAffineParams<spatial_ndim>;
  using Base = WarpParamProvider<Backend, spatial_ndim, MappingParams, BorderType>;
  using Workspace = typename Base::Workspace;
  using Base::ws_;
  using Base::spec_;
  using Base::params_gpu_;
  using Base::params_cpu_;
  using Base::num_samples_;

  void SetParams() override {
    if (spec_->NumRegularInput() >= 2) {
      if (ws_->template InputIsType<GPUBackend>(1)) {
        UseInputAsParams(ws_->template InputRef<GPUBackend>(1));
      } else {
        UseInputAsParams(ws_->template InputRef<CPUBackend>(1));
      }
    } else {
      std::vector<float> matrix = spec_->template GetArgument<std::vector<float>>("matrix");
      DALI_ENFORCE(!matrix.empty(),
        "`matrix` argument must be provided when transforms are not passed"
        " as a regular input.");

      DALI_ENFORCE(matrix.size() == spatial_ndim*(spatial_ndim+1),
        "`matrix` parameter must have " + std::to_string(spatial_ndim*(spatial_ndim+1)) +
        " elements");

      MappingParams M;
      int k = 0;
      for (int i = 0; i < spatial_ndim; i++)
        for (int j = 0; j < spatial_ndim+1; j++, k++)
          M.transform(i, j) = matrix[k];

      auto *params = this->AllocParams(kernels::AllocType::Host);
      for (int i = 0; i < num_samples_; i++)
        params[i] = M;
    }
  }

  template <typename InputType>
  void CheckParamInput(const InputType &input) {
    DALI_ENFORCE(input.type().id() == DALI_FLOAT);

    decltype(auto) shape = input.shape();

    const kernels::TensorShape<2> mat_shape = { spatial_ndim, spatial_ndim+1 };
    int N = shape.num_samples();
    auto error_message = [&]() {
      std::stringstream ss;
      ss << "\nAffine mapping parameters must be either\n"
            "  - a list of " << N << " " << mat_shape << " tensors, or\n"
         << "  - a list containing a single " << shape_cat(N, mat_shape) << " tensor.\n";
      if (!kernels::is_uniform(shape)) {
        ss << "\nThe actual input is a list with " << shape.num_samples() << " "
          << shape.sample_dim() << "-D elements with varying size.";
      } else {
        ss << "\nThe actual input is a list with " << shape.num_samples() << " "
          << shape.sample_dim() << "-D elements with shape " << shape[0];
      }
      ss << "\n";
      return ss.str();
    };

    if (shape.num_samples() == 1) {
      DALI_ENFORCE(shape[0] == shape_cat(N, mat_shape) ||
                   (N == 1 && shape[0] == mat_shape), error_message());
    } else {
      DALI_ENFORCE(shape.num_samples() == num_samples_ &&
                   kernels::is_uniform(shape) &&
                   shape[0] == mat_shape,
                   error_message());
    }
  }

  void UseInputAsParams(const TensorList<CPUBackend> &input) {
    CheckParamInput(input);

    params_cpu_.data = static_cast<const MappingParams *>(input.raw_data());
    params_cpu_.shape = { num_samples_ };
  }

  void UseInputAsParams(const TensorVector<GPUBackend> &) {
    DALI_FAIL("This function is here only to avoid excessive complexity of mitigating the call.");
  }

  void UseInputAsParams(const TensorVector<CPUBackend> &input) {
    CheckParamInput(input);

    if (!input.IsContiguous()) {
      auto *params = this->AllocParams(kernels::AllocType::Host);
      for (int i = 0; i < num_samples_; i++) {
        auto &tensor = input[i];
        params[i] = *static_cast<const MappingParams *>(input[i].raw_data());
      }
    } else {
      params_cpu_.data = static_cast<const MappingParams *>(input[0].raw_data());
      params_cpu_.shape = { num_samples_ };
    }
  }

  void UseInputAsParams(const TensorList<GPUBackend> &input) {
    CheckParamInput(input);

    params_gpu_.data = static_cast<const MappingParams *>(input.raw_data());
    params_gpu_.shape = { num_samples_ };
  }
};

}  // namespace dali

#endif  // DALI_PIPELINE_OPERATORS_DISPLACEMENT_WARP_AFFINE_PARAMS_H_
