#include "utility.cuh"
#include <stdio.h>

#define LOW_ALPHA_THRESHOLD 8

__global__ void GPU_Printer()
{
	if (threadIdx.x == 1023 && blockIdx.x == 255)
	{
		printf("GPU Printer1\n");
	}
}

cudaError_t ExtentGPUPrinter()
{
	GPU_Printer <<<256, 1024>>> ();

	return cudaSuccess;
}

__global__ void rgba_to_alpha(GLubyte* input, GLubyte* output, int width, int height) {
    int x = threadIdx.x + blockIdx.x * blockDim.x;
    int y = threadIdx.y + blockIdx.y * blockDim.y;
    if (x < width && y < height) {
		int myidx = y * width + x;
        int idx = 4 * (y * width + x);
		if (input[idx + 3] > LOW_ALPHA_THRESHOLD) {
			output[myidx] = 255;
		}
    }
}

std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToSingleChannelOnAlpha(std::tuple<std::unique_ptr<GLubyte[]>, Texture*> rgba)
{
	auto& [input, texture] = rgba;
	int width = texture->width;
	int height = texture->height;
	GLubyte* d_input;
	cudaMalloc(&d_input, width * height * 4 * sizeof(GLubyte));
	cudaMemcpy(d_input, input.get(), width * height * 4 * sizeof(GLubyte), cudaMemcpyHostToDevice);
	GLubyte* d_output;
	cudaMalloc(&d_output, width * height * sizeof(GLubyte));
	cudaMemset(d_output, 0, width * height * sizeof(GLubyte));

	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
	rgba_to_alpha<<<grid, block>>> (d_input, d_output, width, height);

	std::unique_ptr<GLubyte[]> output(new GLubyte[width * height]);
	cudaMemcpy(output.get(), d_output, width * height * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaFree(d_input);
	cudaFree(d_output);
	return std::tuple(std::move(output), texture);
}