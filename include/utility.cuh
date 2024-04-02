// CUDAHeader.cuh

#ifndef CUDA_COMMON_H
#define CUDA_COMMON_H

#include <cuda.h>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include <glad/glad.h>
#include "texture.h"
#include <memory>
#include <tuple>

std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToSingleChannelOnAlpha(std::tuple<std::unique_ptr<GLubyte[]>, Texture*> rgba);

unsigned int countNotZero(GLuint* p, size_t size);

std::unique_ptr<GLubyte[]> decodeUVToRGB(GLuint* uv, int width, int height);

std::unique_ptr<GLubyte[]> decodeUVToMask(GLuint* uv, int width, int height);


#endif
