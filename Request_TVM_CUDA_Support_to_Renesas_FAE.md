**Subject: Request for TVM Wheel with CUDA Runtime Support (R_Car_V4x_HyCo_L_TVM_v4h2 / R-Car SDK v3.39.0)**

Dear Renesas FAE Team,

We are currently using the **R-Car SDK v3.39.0** with **HyCo (ReACTION)** toolchain to evaluate DNN model accuracy on our development host equipped with an **NVIDIA GeForce RTX 3060 GPU**. We would like to request your support regarding enabling CUDA GPU acceleration for TVM runtime inference.

---

### Background

Our workflow uses `reaction start` to perform accuracy validation (`action: eval`, `task: tvm_cpu`) inside the Docker container `reaction/byom-v4h2-gpu:v3.39.0`. Currently, all TVM inference runs exclusively on the CPU at approximately **~5 iterations/second**, which is extremely slow for large-scale validation (e.g., thousands of images from COCO/ImageNet datasets).

Since the Docker image is built on `nvidia/cuda:11.8.0-cudnn8-runtime-ubuntu22.04` and mounted with `--gpus all`, the NVIDIA GPU driver and CUDA libraries are fully functional inside the container. We hoped to leverage the GPU to significantly accelerate the TVM inference process.

---

### Technical Investigation & Findings

After extensive investigation, we have confirmed the root cause: **the pre-built TVM wheel (`R_Car_V4x_HyCo_L_TVM_v4h2-0.16.dev*-cp310-cp310-linux_x86_64.whl`) does not include the CUDA Device API runtime.**

Evidence from the running ReACTION container:

```
# Python test inside the reaction eval container (container ID: 594626106de0)
>>> import tvm
>>> tvm.__version__
'0.18.0'
>>> tvm.get_global_func('device_api.cpu', allow_missing=True) is not None
True
>>> tvm.get_global_func('device_api.cuda', allow_missing=True) is not None
False          # ← CUDA device API is NOT registered
>>> tvm.cuda(0).exist
False          # ← GPU device not available to TVM
```

Binary-level analysis of `libtvm.so` (88 MB) confirms:

| Item | Status | Implication |
|---|---|---|
| `CUDADeviceAPI` class symbol | **Not found** | Core CUDA runtime class not compiled |
| CUDA Driver API imports (`cuInit`, `cuLaunchKernel`, etc.) | **None** | Cannot communicate with NVIDIA GPU |
| `libcuda.so` / `libcudart.so` linkage | **None** | No CUDA library dependency |
| `CodeGenCUDA`, `CUDAModuleCreate` | **Present** | CUDA code generation exists (cross-compile only) |
| `topi::cuda::schedule_*` functions | **Present** | CUDA scheduling templates compiled in |
| `R_OSAL_*` symbols (43 entries) | **Present** | R-Car hardware abstraction integrated |

This indicates the TVM build includes CUDA **codegen** components (for generating CUDA source code during cross-compilation), but the CUDA **runtime** (`src/runtime/cuda/cuda_device_api.cc`) was not compiled — likely built with **`USE_CUDA=OFF`** or without linking the CUDA runtime libraries.

Additionally, we noted that **both the CPU and GPU Docker images install the identical TVM wheel**, meaning there is no GPU-enabled variant available.

---

### What We Would Like to Achieve

We would like to use **TVM runtime with NVIDIA CUDA GPU** for inference during the accuracy validation phase (`reaction start` with `eval` action) on our x86_64 development host. This would:

1. **Dramatically accelerate evaluation speed** — GPU inference is typically 10–50x faster than CPU for DNN models
2. **Enable practical large-scale validation** — evaluating thousands of images on CPU is prohibitively slow
3. **Leverage existing infrastructure** — the GPU Docker container and CUDA runtime are already in place

---

### Requested Assistance

We kindly request your support on any of the following:

1. **Provide a TVM wheel built with `USE_CUDA=ON`** — a variant of `R_Car_V4x_HyCo_L_TVM_v4h2` that includes `CUDADeviceAPI` runtime support, enabling `tvm.cuda(0)` to function on NVIDIA GPUs. This would be the ideal solution.

2. **Provide build instructions or CMake configuration** — if a pre-built wheel is not available, guidance on how to rebuild the TVM wheel from source with CUDA runtime enabled while preserving the R-Car specific backends (`rcar_imp`, `rcar_dkl`, etc.) would be very helpful.

3. **Confirm the design intent** — if CUDA GPU inference on x86 host is intentionally not supported, please advise on the recommended approach for accelerating TVM-based accuracy validation in the HyCo/ReACTION workflow.

---

### Environment Details

| Component | Version / Details |
|---|---|
| R-Car SDK | v3.39.0 |
| TVM Package | `R_Car_V4x_HyCo_L_TVM_v4h2 0.16.dev20250716108096+rcar3390.nightly` |
| Target SoC | R-Car V4H2 |
| Docker Base Image | `nvidia/cuda:11.8.0-cudnn8-runtime-ubuntu22.04` |
| Host GPU | NVIDIA GeForce RTX 3060 12GB |
| NVIDIA Driver | 570.211.01 |
| Host OS | Ubuntu 22.04.5 LTS (x86_64) |
| Python | 3.10 |

Thank you for your time and support. Please let us know if any additional information is needed.

Best regards,
[Your Name]
[Your Title / Team]
[Your Company]
