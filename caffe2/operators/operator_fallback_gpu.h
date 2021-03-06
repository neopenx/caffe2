#ifndef CAFFE2_OPERATORS_OPERATOR_FALLBACK_H_
#define CAFFE2_OPERATORS_OPERATOR_FALLBACK_H_

#include "caffe2/core/common.h"
#include "caffe2/core/context.h"
#include "caffe2/core/context_gpu.h"
#include "caffe2/core/operator.h"
#include "caffe2/proto/caffe2.pb.h"

namespace caffe2 {

/**
 * @brief A templated class to allow one to wrap a CPU operator as a CUDA
 * operator.
 *
 * This class can be used when one does not have the CUDA implementation ready
 * yet for an operator. Essentially, what this op does is to automatically
 * deal with data copy for you. Plausibly, this causes a lot of overhead and
 * is not optimal, so you should use this operator mostly for quick prototyping
 * purpose.
 *
 * All the input and output of the original operator should be TensorCPU.
 *
 * Example usage: if you have a class MyMagicOp that is CPU based, and you use
 * the registration code
 *     REGISTER_CPU_OPERATOR(MyMagic, MyMagicOp);
 * to register the CPU side, you can create its corresponding GPU operator
 * (with performance hits of course) via
 *     REGISTER_CUDA_OPERATOR(MyMagic,
 *                            GPUFallbackOp<MyMagicOp>);
 */
template <class CPUOp>
class GPUFallbackOp final : public Operator<CUDAContext> {
 public:
  USE_OPERATOR_FUNCTIONS(CUDAContext);
  GPUFallbackOp(const OperatorDef& def, Workspace* ws)
      : Operator<CUDAContext>(def, ws) {
    CAFFE_CHECK_EQ(def.device_option().device_type(), CUDA);
    OperatorDef base_def_(def);
    // base_def_ runs on CPU, so we will set its device option to CPU.
    base_def_.clear_device_option();
    base_def_.mutable_device_option()->set_device_type(CPU);
    // Set up the symbols for the local workspace.
    for (const string& name : def.input()) {
      local_input_blobs_.push_back(local_ws_.CreateBlob(name));
      CAFFE_CHECK_NOTNULL(local_input_blobs_.back());
    }
    base_op_.reset(new CPUOp(base_def_, &local_ws_));
    for (const string& name : def.output()) {
      local_output_blobs_.push_back(local_ws_.GetBlob(name));
      CAFFE_CHECK_NOTNULL(local_output_blobs_.back());
    }
  }

  bool RunOnDevice() override {
    for (int i = 0; i < InputSize(); ++i) {
      local_input_blobs_[i]->GetMutable<TensorCPU>()->CopyFrom(
          Input(i), &context_);
    }
    // Sync to make sure copies are done.
    context_.FinishDeviceComputation();
    if (!base_op_->Run()) {
      CAFFE_LOG_ERROR << "Base op run failed in GPUFallbackOp. Def: "
                      << ProtoDebugString(def());
      return false;
    }
    for (int i = 0; i < OutputSize(); ++i) {
      Output(i)->CopyFrom(
          local_output_blobs_[i]->Get<TensorCPU>(), &context_);
    }
    return true;
  }

 protected:
  Workspace local_ws_;
  vector<Blob*> local_input_blobs_;
  vector<Blob*> local_output_blobs_;
  std::unique_ptr<CPUOp> base_op_;
  DISABLE_COPY_AND_ASSIGN(GPUFallbackOp);
};

}  // namespace caffe2

#endif  // CAFFE2_OPERATORS_OPERATOR_FALLBACK_H_
