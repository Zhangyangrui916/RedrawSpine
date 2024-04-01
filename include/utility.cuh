// CUDAHeader.cuh

#ifndef CUDA_COMMON_H
#define CUDA_COMMON_H

#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include <glad/glad.h>
#include "texture.h"
#include <memory>
#include <tuple>


extern "C" cudaError_t ExtentGPUPrinter();
std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToSingleChannelOnAlpha(std::tuple<std::unique_ptr<GLubyte[]>, Texture*> rgba);

#endif
