#include <vector>

#include "caffe/filler.hpp"
#include "caffe/layer.hpp"
#include "caffe/util/im2col.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void ConvolutionLayer<Dtype>::compute_output_shape() {
  this->height_out_ = (this->height_ + 2 * this->pad_h_ - this->kernel_h_)
      / this->stride_h_ + 1;
  this->width_out_ = (this->width_ + 2 * this->pad_w_ - this->kernel_w_)
      / this->stride_w_ + 1;
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* top_data = top[i]->mutable_cpu_data();
    for (int n = 0; n < this->num_; ++n) {
      this->forward_cpu_gemm(bottom_data + bottom[i]->offset(n), weight,
          top_data + top[i]->offset(n));
      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->cpu_data();
        this->forward_cpu_bias(top_data + top[i]->offset(n), bias);
      }
    }
  }

  CHECK_BLOB_DATA(top[0],20, "top[0]");
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->cpu_diff();
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1]) {
      Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->backward_cpu_bias(bias_diff, top_diff + top[i]->offset(n));
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      for (int n = 0; n < this->num_; ++n) {
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0]) {
          this->weight_cpu_gemm(bottom_data + bottom[i]->offset(n),
              top_diff + top[i]->offset(n), weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_cpu_gemm(top_diff + top[i]->offset(n), weight,
              bottom_diff + bottom[i]->offset(n));
        }
      }
    }
  }
  CHECK_CPU_MEM_DATA(weight_diff, this->blobs_[0]->count(), 20, "weight_diff");
  CHECK_CPU_MEM_DATA(bottom[0]->mutable_cpu_diff(), bottom[0]->count(), 20, "bottom_diff");
  CHECK_CPU_MEM_DATA(top[0]->cpu_diff(), top[0]->count(), 20, "top_diff");

}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    const  vector<Blob<Dtype>*>& top) {
  if (use_packing_scheme && global_packing_N >1)
   Forward_gpu_opt(bottom, top);
  else
   Forward_gpu_org(bottom, top);
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
       const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
    if (use_packing_scheme && global_packing_N >1)
      Backward_gpu_opt(top, propagate_down, bottom);
    else
      Backward_gpu_org(top, propagate_down, bottom);
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_gpu_opt(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Dtype* weight = this->blobs_[0]->gpu_data();
  this->forward_gpu_opt(bottom, weight, top);

/*
#ifdef check_gradient
   const Dtype *cpu_bottom_data = bottom[0]->cpu_data();   Dtype *cpu_top_data = (Dtype*)(*top)[0]->cpu_data();

   printf("\n\nbottom data GPU:\n");
   for(int i=0; i<channels_*height_*width_; i+=1000){
       printf("%f,",cpu_bottom_data[i]);
       if(i%16==15) printf("\n");
   }
  printf("\n\ntop data GPU:\n");
   for(int i=0; i<M_org*N_*num_; i+=100000){
       printf("%f,",cpu_top_data[i]);
      if(i%16==15) printf("\n");
   }
  printf("\n\n");#endif
*/
#ifdef Track_layer
  LOG(WARNING) << "conv fp done";
#endif

}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_gpu_opt2(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {

  const Dtype* weight = this->blobs_[0]->gpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->gpu_data();
     //CHECK_BLOB_DATA(bottom[i],10,"bottom");

    Dtype* top_data = top[i]->mutable_gpu_data();
    //int col_offset = K_ * N_;
    //int top_offset = M_ * N_;
    //int weight_offset = M_ * K_;
    int opt_num2 = global_packing_N;

    for (int n = 0; n < this->num_; ++n) {
      opt_num2 = opt_num2 > (num_ - n)? (num_ - n) : opt_num2;
       //two intermediate variables to pass offset
      this->top_offset_ = M_ * N_ * opt_num2;
      this->col_offset_ = K_ * N_ * opt_num2;
      this->bottom_offset_ = bottom[i]->offset(n);
      this->forward_gpu_gemm_opt(bottom_data, weight,
            top_data);

      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->gpu_data();
          this->forward_gpu_bias(top_data, bias);
      }
    }
  }

  // CHECK_BLOB_DATA(this->blobs_[0],20, "weights");
  CHECK_BLOB_DATA(top[0],20, "top[0]");

}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_gpu_org(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Dtype* weight = this->blobs_[0]->gpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->gpu_data();
     //CHECK_BLOB_DATA(bottom[i],10,"bottom");

    Dtype* top_data = top[i]->mutable_gpu_data();
    for (int n = 0; n < this->num_; ++n) {
       //two intermediate variables to pass offset
       this->bottom_offset_ = bottom[i]->offset(n);
       this->top_offset_ = top[i]->offset(n); 
       this->forward_gpu_gemm(bottom_data, weight,
            top_data);

      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->gpu_data();
          this->forward_gpu_bias(top_data, bias);
      }
    }
  }

  // CHECK_BLOB_DATA(this->blobs_[0],20, "weights");
  CHECK_BLOB_DATA(top[0],20, "top[0]");
}
template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_gpu_opt(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
      this->backward_gpu_opt(top, propagate_down, bottom);
}
template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_gpu_org(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->gpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_gpu_diff();
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->gpu_diff();
    
    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1]) {
      Dtype* bias_diff = this->blobs_[1]->mutable_gpu_diff();
      for (int n = 0; n < this->num_; ++n) {
       //
        this->top_offset_ = top[i]->offset(n);
        this->bottom_offset_ = bottom[i]->offset(n);
        this->backward_gpu_bias(bias_diff, top_diff);
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      const Dtype* bottom_data = bottom[i]->gpu_data();
      Dtype* bottom_diff = bottom[i]->mutable_gpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->top_offset_ = top[i]->offset(n);
        this->bottom_offset_ = bottom[i]->offset(n);
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0]) {
          this->weight_gpu_gemm(bottom_data,
              top_diff, weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_gpu_gemm(top_diff, weight,
              bottom_diff);
        }
      }
    }
  }
  
  CHECK_GLOBAL_MEM_DATA(weight_diff, this->blobs_[0]->count(), 20, "weight_diff");  
  CHECK_GLOBAL_MEM_DATA(bottom[0]->mutable_gpu_diff(), bottom[0]->count(), 20, "bottom_diff");
  CHECK_GLOBAL_MEM_DATA(top[0]->gpu_diff(), top[0]->count(), 20, "top_diff");
  CHECK_BLOB_DATA(bottom[0], 20, "bottom[0]");
}

#ifdef CPU_ONLY
STUB_GPU(ConvolutionLayer);
#endif

INSTANTIATE_CLASS(ConvolutionLayer);

}  // namespace caffe
