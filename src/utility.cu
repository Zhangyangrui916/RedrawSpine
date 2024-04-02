#include "utility.cuh"
#include <stdio.h>

#define LOW_ALPHA_THRESHOLD 8

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


__global__ void countNotZeroKernel(GLuint* pixels, int size, unsigned int* count) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;

	if (x < size) {
		GLuint pixel = pixels[x];
		if (pixel != 0) { // 如果G通道（绿色）的值是255
			atomicAdd(count, 1);
		}
	}
}

unsigned int countNotZero(GLuint* p, size_t size)
{
	int block_size = 256;
	unsigned int count = 0;

	GLuint* d_image;
	unsigned int* d_count;
	cudaMalloc((void**)&d_image, size * sizeof(GLuint));
	cudaMalloc((void**)&d_count, sizeof(unsigned int));

	cudaMemcpy(d_image, p, size * sizeof(GLuint), cudaMemcpyHostToDevice);

	int grid_size = (size + block_size - 1) / block_size;

	countNotZeroKernel <<<grid_size, block_size>>> ((GLuint*)d_image, size, d_count);
	cudaMemcpy(&count, d_count, sizeof(unsigned int), cudaMemcpyDeviceToHost);

	cudaFree(d_image);
	cudaFree(d_count);

	return count;
}

__global__ void decodeUVToRGBKernel(GLuint* uv, int size, GLubyte* rgb) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	if (x < size) {
		GLuint Slot_V_U = uv[x];
		if (Slot_V_U == 0) {
			return;
		}
		GLuint U = Slot_V_U & 0xFFF;
		GLuint V = (Slot_V_U >> 12) & 0xFFF;
		GLuint Id = (Slot_V_U >> 24) & 0xFF;
		rgb[3 * x] = Id;
		rgb[3 * x + 1] = U >> 3;
		rgb[3 * x + 2] = V >> 3;
	}
}


std::unique_ptr<GLubyte[]> decodeUVToRGB(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> rgb(new GLubyte[size * 3]);

	int block_size = 256;

	GLuint* d_uv;
	GLubyte* d_rgb;

	cudaMalloc((void**)&d_uv, size * sizeof(GLuint));
	cudaMemcpy(d_uv, uv, size * sizeof(GLuint), cudaMemcpyHostToDevice);

	cudaMalloc((void**)&d_rgb, size * 3 * sizeof(GLubyte));
	cudaMemset(d_rgb, 0, size * 3 * sizeof(GLubyte));

	int grid_size = (size + block_size - 1) / block_size;
	decodeUVToRGBKernel <<<grid_size, block_size>>> ((GLuint*)d_uv, size, d_rgb);

	cudaMemcpy(rgb.get(), d_rgb, size * 3 * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaFree(d_uv);
	cudaFree(d_rgb);

	return std::move(rgb);
}



__global__ void decodeUVToMaskKernel(GLuint* uv, int size, GLubyte* gray) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	if (x < size && uv[x] > 0) {
		gray[x] = 255;
	}
}


std::unique_ptr<GLubyte[]> decodeUVToMask(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> gray(new GLubyte[size]);

	int block_size = 256;

	GLuint* d_uv;
	GLubyte* d_gray;

	cudaMalloc((void**)&d_uv, size * sizeof(GLuint));
	cudaMemcpy(d_uv, uv, size * sizeof(GLuint), cudaMemcpyHostToDevice);

	cudaMalloc((void**)&d_gray, size * sizeof(GLubyte));
	cudaMemset(d_gray, 0, size * sizeof(GLubyte));

	int grid_size = (size + block_size - 1) / block_size;
	decodeUVToMaskKernel<<<grid_size, block_size >>> ((GLuint*)d_uv, size, d_gray);

	cudaMemcpy(gray.get(), d_gray, size * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaFree(d_uv);
	cudaFree(d_gray);

	return std::move(gray);
}
