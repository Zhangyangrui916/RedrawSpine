#include "utility.cuh"
#include <stdio.h>
#include "stb_image_write.h"
#include <memory>
#define LOW_ALPHA_THRESHOLD 8


class cudaPerfCounter {
public:
	cudaEvent_t start, stop;
	cudaPerfCounter() {
		cudaEventCreate(&start);
		cudaEventCreate(&stop);
		cudaEventRecord(start, 0);
	}
	~cudaPerfCounter() {
		cudaEventDestroy(start);
		cudaEventDestroy(stop);
	}
	void stopCounter() {
		cudaEventRecord(stop, 0);
	}
	float elapsed() {
		float elapsed;
		cudaEventSynchronize(stop);
		cudaEventElapsedTime(&elapsed, start, stop);
		return elapsed;
	}
};

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
		GLuint pixelNext = pixels[x+1];
		if (pixel != 0 && pixelNext != 0) {
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

constexpr int filterWidth = 11;
constexpr float TwoSigmaSquare = 10.f;
constexpr int windowSize = (filterWidth - 1) * 2;
__device__ float d_filter[filterWidth][filterWidth];
__global__ void computeGaussianFilter(float TwoSigmaSquare) {
	int x = threadIdx.x;
	int y = threadIdx.y;

	int dx = threadIdx.x - (filterWidth - 1) / 2;
	int dy = threadIdx.y - (filterWidth - 1) / 2;

	d_filter[x][y] = exp(-(dx * dx + dy * dy) / TwoSigmaSquare) / (3.1415926 * TwoSigmaSquare);
}
void initGaussain() {
	static bool inited = false;

	if (inited) {
		return;
	}

	computeGaussianFilter<<<1, dim3(filterWidth, filterWidth)>>> (TwoSigmaSquare);
	inited = true;
}

__global__ void DilateGaussianBlur(GLubyte* input, GLubyte* output, int width, int height) {
	int bx = blockIdx.x * blockDim.x;
	int by = blockIdx.y * blockDim.y;

	int x = threadIdx.x + bx;
	int y = threadIdx.y + by;

	__shared__ unsigned char sharedInput[windowSize][windowSize];
	int halfFilterWidth = (filterWidth - 1) / 2;
	int OffsetX = bx - halfFilterWidth + threadIdx.x * 2;
	int OffsetY = by - halfFilterWidth + threadIdx.y * 2;
	sharedInput[threadIdx.x * 2][threadIdx.y * 2] = input[OffsetY * width + OffsetX];
	sharedInput[threadIdx.x * 2 + 1][threadIdx.y * 2] = input[OffsetY * width + OffsetX + 1];
	sharedInput[threadIdx.x * 2][threadIdx.y * 2 + 1] = input[(OffsetY + 1) * width + OffsetX];
	sharedInput[threadIdx.x * 2 + 1][threadIdx.y * 2 + 1] = input[(OffsetY + 1) * width + OffsetX + 1];
	__syncthreads();

	if (x < width && y < height) {
		if (sharedInput[(halfFilterWidth + threadIdx.x)][(halfFilterWidth + threadIdx.y)] == 255) {
			output[y * width + x] = 255;
			return;
		}


		float sum = 0;
		for (int dx = 0; dx < filterWidth; dx++) {
			for (int dy = 0; dy < filterWidth; dy++) {
				int nx = threadIdx.x + dx;
				int ny = threadIdx.y + dy;
				if (nx >= 0 && nx < windowSize && ny >= 0 && ny < windowSize) {
					sum += d_filter[dx][dy] * sharedInput[nx][ny];
				}
			}
		}
		output[y * width + x] = sum;
	}
}


__global__ void DilateGaussianBlur1(GLubyte* input, GLubyte* output, int width, int height) {
	int bx = blockIdx.x * blockDim.x;
	int by = blockIdx.y * blockDim.y;

	int x = threadIdx.x + bx;
	int y = threadIdx.y + by;

	if (x < width && y < height) {

		if (input[y * width + x] == 255) {
			output[y * width + x] = 255;
			return;
		}

		float sum = 0;
		for (int dx = 0; dx < filterWidth; dx++) {
			for (int dy = 0; dy < filterWidth; dy++) {
				int nx = threadIdx.x + dx;
				int ny = threadIdx.y + dy;
				if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
					sum += d_filter[dx][dy] * input[ny * width + nx];
				}
			}
		}
		output[y * width + x] = sum;
	}
}


std::unique_ptr<GLubyte[]> decodeUVToMask(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> gray(new GLubyte[size]);

	int block_size = 256;

	GLuint *d_uv;
	GLubyte *d_gray, *d_grayBlur;

	cudaMalloc((void**)&d_uv, size * sizeof(GLuint));
	cudaMemcpy(d_uv, uv, size * sizeof(GLuint), cudaMemcpyHostToDevice);

	cudaMalloc((void**)&d_gray, size * sizeof(GLubyte));
	cudaMemset(d_gray, 0, size * sizeof(GLubyte));

	int grid_size = (size + block_size - 1) / block_size;
	decodeUVToMaskKernel<<<grid_size, block_size>>> ((GLuint*)d_uv, size, d_gray);

	//{
	//	static int count_h = 0;
	//	std::unique_ptr<unsigned char> imgSrcData_(new unsigned char[width * height]);
	//	cudaMemcpy(imgSrcData_.get(), d_gray, width * height * sizeof(unsigned char), cudaMemcpyDeviceToHost);
	//	cudaDeviceSynchronize();
	//	stbi_write_png((std::to_string(++count_h) + "image_out.png").c_str(), width, height, 1, imgSrcData_.get(), width);
	//}

	initGaussain();

	cudaMalloc((void**)&d_grayBlur, size * sizeof(GLubyte));
	dim3 block(filterWidth-1, filterWidth-1);
	dim3 grid((width + block.x -1)/(block.x), (height + block.y - 1)/(block.y));

	cudaPerfCounter perfCounter;
	DilateGaussianBlur<<<grid, block>>> (d_gray, d_grayBlur, width, height);
	perfCounter.stopCounter();
	printf("blur: %f\n", perfCounter.elapsed());

	cudaMemcpy(gray.get(), d_grayBlur, size * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaDeviceSynchronize();
	cudaFree(d_uv);
	cudaFree(d_gray);

	return std::move(gray);
}



__global__ void cannyKernel(GLubyte* input, GLubyte* output, int width, int height) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	int y = threadIdx.y + blockIdx.y * blockDim.y;
	if (x >= 1 && y >= 1 && x < width - 1 && y < height - 1)
	{
		int gradient_x = abs((int)input[y * width + x + 1] - (int)input[y * width + x - 1]);
		int gradient_y = abs((int)input[(y + 1) * width + x] - (int)input[(y - 1) * width + x]);
		output[y * width + x] = (gradient_x > 1 || gradient_y > 1) ? 255 : 0;
	}
}


std::unique_ptr<GLubyte[]> canny(std::unique_ptr<GLubyte[]> input, int width, int height) {
	std::unique_ptr<GLubyte[]> output(new GLubyte[width * height]);

	GLubyte* d_input;
	GLubyte* d_output;
	cudaMalloc(&d_input, width * height * sizeof(GLubyte));
	cudaMalloc(&d_output, width * height * sizeof(GLubyte));
	cudaMemcpy(d_input, input.get(), width * height * sizeof(GLubyte), cudaMemcpyHostToDevice);

	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
	cannyKernel<<<grid, block>>>(d_input, d_output, width, height);
	cudaMemcpy(output.get(), d_output, width * height * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaDeviceSynchronize();
	cudaFree(d_input);
	cudaFree(d_output);
	return std::move(output);
}


__global__ void floodWhitePixelWithNeighborColorKernel(GLubyte* d_input, int width, int height, int* d_count) {

	int x = threadIdx.x + blockIdx.x * blockDim.x;
	int y = threadIdx.y + blockIdx.y * blockDim.y;

	__shared__ unsigned char sharedInput[18][18][4];
	sharedInput[threadIdx.x][threadIdx.y][0] = d_input[(y * width + x) * 4 + 0];
	sharedInput[threadIdx.x][threadIdx.y][1] = d_input[(y * width + x) * 4 + 1];
	sharedInput[threadIdx.x][threadIdx.y][2] = d_input[(y * width + x) * 4 + 2];
	sharedInput[threadIdx.x][threadIdx.y][3] = d_input[(y * width + x) * 4 + 3];
	__syncthreads();

	if (sharedInput[threadIdx.x][threadIdx.y][0] != 255 || sharedInput[threadIdx.x][threadIdx.y][1] != 255 || sharedInput[threadIdx.x][threadIdx.y][2] != 255 ||  sharedInput[threadIdx.x][threadIdx.y][3] == 0) {
		return;
	}

	if (threadIdx.x > 0) {
		if (sharedInput[threadIdx.x - 1][threadIdx.y][0] != 255 && sharedInput[threadIdx.x - 1][threadIdx.y][1] != 255 && sharedInput[threadIdx.x - 1][threadIdx.y][2] != 255 && sharedInput[threadIdx.x - 1][threadIdx.y][3] != 0) {
			d_input[(y * width + x) * 4 + 0] = sharedInput[threadIdx.x - 1][threadIdx.y][0];
			d_input[(y * width + x) * 4 + 1] = sharedInput[threadIdx.x - 1][threadIdx.y][1];
			d_input[(y * width + x) * 4 + 2] = sharedInput[threadIdx.x - 1][threadIdx.y][2];
			atomicAdd(d_count, 1);
			return;
		}
	}
	else {
		if (x > 0) {
			if (d_input[((y)*width + x - 1) * 4 + 0] != 255 && d_input[((y)*width + x - 1) * 4 + 1] != 255 && d_input[((y)*width + x - 1) * 4 + 2] != 255 && d_input[((y)*width + x - 1) * 4 + 3] != 0) {
				d_input[(y * width + x) * 4 + 0] = d_input[((y) * width + x - 1) * 4 + 0];
				d_input[(y * width + x) * 4 + 1] = d_input[((y) * width + x - 1) * 4 + 1];
				d_input[(y * width + x) * 4 + 2] = d_input[((y) * width + x - 1) * 4 + 2];
				atomicAdd(d_count, 1);
				return;
			}
		}
	}


	if (threadIdx.x < 15) {
		if (sharedInput[threadIdx.x + 1][threadIdx.y][0] != 255 && sharedInput[threadIdx.x + 1][threadIdx.y][1] != 255 && sharedInput[threadIdx.x + 1][threadIdx.y][2] != 255 && sharedInput[threadIdx.x + 1][threadIdx.y][3] != 0) {
			d_input[(y * width + x) * 4 + 0] = sharedInput[threadIdx.x + 1][threadIdx.y][0];
			d_input[(y * width + x) * 4 + 1] = sharedInput[threadIdx.x + 1][threadIdx.y][1];
			d_input[(y * width + x) * 4 + 2] = sharedInput[threadIdx.x + 1][threadIdx.y][2];
			atomicAdd(d_count, 1);
			return;
		}
	}
	else {
		if (x < width - 1) {
			if (d_input[((y)*width + x + 1) * 4 + 0] != 255 && d_input[((y)*width + x + 1) * 4 + 1] != 255 && d_input[((y)*width + x + 1) * 4 + 2] != 255 && d_input[((y)*width + x + 1) * 4 + 3] != 0) {
				d_input[(y * width + x) * 4 + 0] = d_input[((y) * width + x + 1) * 4 + 0];
				d_input[(y * width + x) * 4 + 1] = d_input[((y) * width + x + 1) * 4 + 1];
				d_input[(y * width + x) * 4 + 2] = d_input[((y) * width + x + 1) * 4 + 2];
				atomicAdd(d_count, 1);
				return;
			}
		}
	}


	if (threadIdx.y > 0) {
		if (sharedInput[threadIdx.x][threadIdx.y - 1][0] != 255 && sharedInput[threadIdx.x][threadIdx.y - 1][1] != 255 && sharedInput[threadIdx.x][threadIdx.y - 1][2] != 255 && sharedInput[threadIdx.x][threadIdx.y - 1][3] != 0) {
			d_input[(y * width + x) * 4 + 0] = sharedInput[threadIdx.x][threadIdx.y - 1][0];
			d_input[(y * width + x) * 4 + 1] = sharedInput[threadIdx.x][threadIdx.y - 1][1];
			d_input[(y * width + x) * 4 + 2] = sharedInput[threadIdx.x][threadIdx.y - 1][2];
			atomicAdd(d_count, 1);
			return;
		}
	}
	else {
		if (y > 0) {
			if (d_input[((y - 1) * width + x) * 4 + 0] != 255 && d_input[((y - 1) * width + x) * 4 + 1] != 255 && d_input[((y - 1) * width + x) * 4 + 2] != 255 && d_input[((y - 1) * width + x) * 4 + 3] != 0) {
				d_input[(y * width + x) * 4 + 0] = d_input[((y - 1) * width + x) * 4 + 0];
				d_input[(y * width + x) * 4 + 1] = d_input[((y - 1) * width + x) * 4 + 1];
				d_input[(y * width + x) * 4 + 2] = d_input[((y - 1) * width + x) * 4 + 2];
				atomicAdd(d_count, 1);
				return;
			}
		}
	}


	if (threadIdx.y < 15) {
		if (sharedInput[threadIdx.x][threadIdx.y + 1][0] != 255 && sharedInput[threadIdx.x][threadIdx.y + 1][1] != 255 && sharedInput[threadIdx.x][threadIdx.y + 1][2] != 255 && sharedInput[threadIdx.x][threadIdx.y + 1][3] != 0) {
			d_input[(y * width + x) * 4 + 0] = sharedInput[threadIdx.x][threadIdx.y + 1][0];
			d_input[(y * width + x) * 4 + 1] = sharedInput[threadIdx.x][threadIdx.y + 1][1];
			d_input[(y * width + x) * 4 + 2] = sharedInput[threadIdx.x][threadIdx.y + 1][2];
			atomicAdd(d_count, 1);
			return;
		}
	}
	else {
		if (y < height - 1) {
			if (d_input[((y + 1) * width + x) * 4 + 0] != 255 && d_input[((y + 1) * width + x) * 4 + 1] != 255 && d_input[((y + 1) * width + x) * 4 + 2] != 255 && d_input[((y + 1) * width + x) * 4 + 3] != 0) {
				d_input[(y * width + x) * 4 + 0] = d_input[((y + 1) * width + x) * 4 + 0];
				d_input[(y * width + x) * 4 + 1] = d_input[((y + 1) * width + x) * 4 + 1];
				d_input[(y * width + x) * 4 + 2] = d_input[((y + 1) * width + x) * 4 + 2];
				atomicAdd(d_count, 1);
				return;
			}
		}
	}
}

void floodWhitePixelWithNeighborColor(GLubyte* input, int width, int height) {

	GLubyte* d_input;
	cudaMalloc(&d_input, width * height * 4 * sizeof(GLubyte));
	cudaMemcpy(d_input, input, width * height * 4 * sizeof(GLubyte), cudaMemcpyHostToDevice);

	int* d_count;
	cudaMalloc(&d_count, sizeof(unsigned int));
	cudaMemset(d_count, 0, sizeof(unsigned int));

	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

RECURSIVE_FLOOD:
	floodWhitePixelWithNeighborColorKernel<<<grid, block>>>(d_input, width, height, d_count);
	
	cudaDeviceSynchronize();
	int count = -1;
	cudaMemcpy(&count, d_count, sizeof(unsigned int), cudaMemcpyDeviceToHost);

	cudaDeviceSynchronize();
	if (count != 0) {
		cudaMemset(d_count, 0, sizeof(unsigned int));
		goto RECURSIVE_FLOOD;
	}

	cudaMemcpy(input, d_input, width * height * 4 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_input);
	cudaFree(d_count);
}
