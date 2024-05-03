#include "utility.cuh"
#include <stdio.h>
#include "stb_image_write.h"
#include <memory>
#include <npp.h>

#define LOW_ALPHA_THRESHOLD 11

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

// 不改变传入参数
// 返回的结构体之所以带上Texture*,是为了指明GLubyte[]的宽高. 如果要更新到GPU请自己更新.
std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToBinaryAlphaMask(std::tuple<std::unique_ptr<GLubyte[]>, Texture*>& rgba)
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

	if ((x + 1) < size) {
		GLuint pixel = pixels[x];
		GLuint pixelNext = pixels[x + 1];
		if (pixel != 0 && pixelNext != 0) {
			atomicAdd(count, 1);
		}
	}
}

__global__ void countNotZeroKernel(GLubyte* pixels, int size, unsigned int* count) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;

	if ((x + 1) < size) {
		GLuint pixel = pixels[x];
		GLuint pixelNext = pixels[x + 1];
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



constexpr int filterWidth = 11;
constexpr float TwoSigmaSquare = 50.f;
constexpr int windowSize = (filterWidth - 1) * 2;
__device__ float d_filter[filterWidth][filterWidth];
__global__ void computeGaussianFilter(float TwoSigmaSquare) {
	int x = threadIdx.x;
	int y = threadIdx.y;

	int dx = threadIdx.x - (filterWidth - 1) / 2;
	int dy = threadIdx.y - (filterWidth - 1) / 2;

	d_filter[x][y] = exp(-(dx * dx + dy * dy) / TwoSigmaSquare);
}
void initGaussain() {
	static bool inited = false;

	if (inited) {
		return;
	}

	computeGaussianFilter<<<1, dim3(filterWidth, filterWidth)>>> (TwoSigmaSquare);
	inited = true;
}

__global__ void DilateGaussianBlurKernel(GLubyte* input, GLubyte* output, int width, int height) {
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
		output[y * width + x] = min(255, int(sum));
	}
}

void DilateGaussianBlur() {

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
	DilateGaussianBlurKernel<<<grid, block>>> (d_gray, d_grayBlur, width, height);
	perfCounter.stopCounter();
	printf("blur: %f\n", perfCounter.elapsed());

	cudaMemcpy(gray.get(), d_grayBlur, size * sizeof(GLubyte), cudaMemcpyDeviceToHost);

	cudaDeviceSynchronize();
	cudaFree(d_uv);
	cudaFree(d_gray);

	return std::move(gray);
}

__global__ void decodeUVToSlotKernel(GLuint* uv, int width, int height, GLubyte* d_rgb) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	GLuint Slot_V_U = uv[x];
	char slot = Slot_V_U >> 24;
	char r = slot * 2311;
	char b = slot * 2659;
	char g = slot * 2777;
	d_rgb[x * 3] = r;
	d_rgb[x * 3 + 1] = g;
	d_rgb[x * 3 + 2] = b;
}

std::unique_ptr<GLubyte[]> decodeUVToSlot(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> rgb(new GLubyte[size*3]);
	int block_size = 256;

	GLuint* d_uv;
	GLubyte* d_rgb;

	cudaMalloc((void**)&d_uv, size * sizeof(GLuint));
	cudaMemcpy(d_uv, uv, size * sizeof(GLuint), cudaMemcpyHostToDevice);

	cudaMalloc((void**)&d_rgb, size * 3 * sizeof(GLubyte));
	cudaMemset(d_rgb, 0, size * 3 * sizeof(GLubyte));

	int grid_size = (size + block_size - 1) / block_size;
	decodeUVToSlotKernel<<<grid_size, block_size>>> ((GLuint*)d_uv, width, height, d_rgb);

	cudaMemcpy(rgb.get(), d_rgb, size * 3 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_uv);
	cudaFree(d_rgb);

	return rgb;
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

__global__ void erodeKernel(unsigned char* input, int width, int height) {
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;
	
	if(x >= width || y >= height) {
		return;
	}

	int index = y * width + x;
	if (index - 1 >= 0 && input[index - 1] == 0) {
		input[index] = 0;
		return;
	}
	if (index + 1 < width * height && input[index + 1] == 0) {
		input[index] = 0;
		return;
	}
	if (index - width >= 0 && input[index - width] == 0) {
		input[index] = 0;
		return;
	}
	if (index + width < width * height && input[index + width] == 0) {
		input[index] = 0;
		return;
	}
}

bool ErodeAndCheckRemain(GLubyte* input, int width, int height) {

	GLubyte* d_input;
	cudaMalloc(&d_input, width * height * sizeof(GLubyte));
	cudaMemcpy(d_input, input, width * height * sizeof(GLubyte), cudaMemcpyHostToDevice);

	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

	erodeKernel <<<grid, block>>> (d_input, width, height);
	erodeKernel <<<grid, block>>> (d_input, width, height);
	erodeKernel <<<grid, block>>> (d_input, width, height);

	int block_size = 256;
	unsigned int count = 0;
	unsigned int* d_count;
	cudaMalloc(&d_count, sizeof(unsigned int));
	cudaMemset(d_count, 0, sizeof(unsigned int));
	int size = width * height;
	cudaMalloc((void**)&d_count, sizeof(unsigned int));
	int grid_size = (size + block_size - 1) / block_size;
	countNotZeroKernel <<<grid_size, block_size>>> (d_input, size, d_count);
	cudaDeviceSynchronize();
	cudaMemcpy(&count, d_count, sizeof(unsigned int), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_input);
	cudaFree(d_count);
	return count == 0;
}

void floodWhitePixelWithNeighborColorCPU(GLubyte* input, int width, int height, GLubyte* WrittenMask) {
	bool onlyEdgeLeft = ErodeAndCheckRemain(WrittenMask, width, height);
	int count = 0;
START:
	count = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int index = y * width + x;
			if (WrittenMask[index] != WrittenState::NotWritten) {
				continue;
			}
			int neighborcount = 0;
			int neightborRed = 0;
			int neightborGreen = 0;
			int neightborBlue = 0;

			if (x > 0 && WrittenMask[index - 1] == WrittenState::Written) {
				neightborRed += input[(index - 1) * 4];
				neightborGreen += input[(index - 1) * 4 + 1];
				neightborBlue += input[(index - 1) * 4 + 2];
				neighborcount++;
			}
			if (x < width - 1 && WrittenMask[index + 1] == WrittenState::Written) {
				neightborRed += input[(index + 1) * 4];
				neightborGreen += input[(index + 1) * 4 + 1];
				neightborBlue += input[(index + 1) * 4 + 2];
				neighborcount++;
			}
			if (y > 0) {
				if (WrittenMask[index - width] == WrittenState::Written) {
					neightborRed += input[(index - width) * 4];
					neightborGreen += input[(index - width) * 4 + 1];
					neightborBlue += input[(index - width) * 4 + 2];
					neighborcount++;
				}
				if (x > 0 && WrittenMask[index - width - 1] == WrittenState::Written) {
					neightborRed += input[(index - width - 1) * 4];
					neightborGreen += input[(index - width - 1) * 4 + 1];
					neightborBlue += input[(index - width - 1) * 4 + 2];
					neighborcount++;
				}
				if (x < width - 1 && WrittenMask[index - width + 1] == WrittenState::Written) {
					neightborRed += input[(index - width + 1) * 4];
					neightborGreen += input[(index - width + 1) * 4 + 1];
					neightborBlue += input[(index - width + 1) * 4 + 2];
					neighborcount++;
				}
			}
			if (y < height - 1) {
				if (input[(index + width) * 4 + 3] != 0 && WrittenMask[index + width] == WrittenState::Written) {
					neightborRed += input[(index + width) * 4];
					neightborGreen += input[(index + width) * 4 + 1];
					neightborBlue += input[(index + width) * 4 + 2];
					neighborcount++;
				}
				if (x > 0 && WrittenMask[index + width - 1] == WrittenState::Written) {
					neightborRed += input[(index + width - 1) * 4];
					neightborGreen += input[(index + width - 1) * 4 + 1];
					neightborBlue += input[(index + width - 1) * 4 + 2];
					neighborcount++;
				}
				if (x < width - 1 && WrittenMask[index + width + 1] == WrittenState::Written) {
					neightborRed += input[(index + width + 1) * 4];
					neightborGreen += input[(index + width + 1) * 4 + 1];
					neightborBlue += input[(index + width + 1) * 4 + 2];
					neighborcount++;
				}
			}
			if (neighborcount > 1) {
				input[index * 4]	 = neightborRed / neighborcount;
				input[index * 4 + 1] = neightborGreen / neighborcount;
				input[index * 4 + 2] = neightborBlue / neighborcount;
				count++;
				if (onlyEdgeLeft) {
					WrittenMask[index] = WrittenState::Written;
				}
				else {
					WrittenMask[index] = WrittenState::Flooded;
				}
			}
		}
	}
	std::string filename = "image" + std::to_string(width) + std::to_string(height) + std::to_string(count) + ".png";
	stbi_write_png(filename.c_str(), width, height, 4, input, width * 4);
	if (count > 0) {
		goto START;
	}

}

__global__ void cleanOutlinerKernel(GLubyte* d_input, int width, int height) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	int y = threadIdx.y + blockIdx.y * blockDim.y;

	if (x > 1 && y > 1 && x < width - 2 && y < height - 2) {
		if (d_input[(y * width + x) * 4 + 3] == 0 || (d_input[(y * width + x) * 4] == 255 && d_input[(y * width + x) * 4 + 1] == 255 && d_input[(y * width + x) * 4 + 2] == 255)) {
			return;
		}
		int countNeighborNotWhite = 0;
		for (int i = x - 2; i <= x + 2; i++) {
			for (int j = y - 2; j <= y + 2; j++) {
				if (i == x && j == y) {
					continue;
				}
				int neighborIndex = j * width + i;
				if (d_input[neighborIndex * 4] != 255 || d_input[neighborIndex * 4 + 1] != 255 || d_input[neighborIndex * 4 + 2] != 255) {
					countNeighborNotWhite++;
				}
			}
		}

		if (countNeighborNotWhite < 8) {
			d_input[(y * width + x) * 4] = 255;
			d_input[(y * width + x) * 4 + 1] = 255;
			d_input[(y * width + x) * 4 + 2] = 255;
		}

		return;
	}
}

void cleanOutliner(GLubyte* input, int width, int height)
{
	GLubyte* d_input;
	cudaMalloc(&d_input, width * height * 4 * sizeof(GLubyte));
	cudaMemcpy(d_input, input, width * height * 4 * sizeof(GLubyte), cudaMemcpyHostToDevice);
	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
	cleanOutlinerKernel <<<grid, block>>> (d_input, width, height);
	cudaMemcpy(input, d_input, width * height * 4 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaFree(d_input);

}


__global__ void floodWhitePixelWithNeighborColorKernel1(GLubyte* d_input, int width, int height, int* d_count, int mode, GLubyte* d_writtenMask, bool onlyEdgeLeft) {

	int x = (threadIdx.x + blockIdx.x * blockDim.x)*2;
	int y = (threadIdx.y + blockIdx.y * blockDim.y)*2;
	if (mode == 1) {
		x += 1;
	}
	else if (mode == 2) {
		y += 1;
	}
	else if (mode == 3) {
		x += 1;
		y += 1;
	}

	if(x >= width || y >= height) {
		return;
	}


	int index = y * width + x;
	if (d_writtenMask[index] != WrittenState::NotWritten) {
		return;
	}
	//if (d_input[index * 4] != 255 || d_input[index * 4 + 1] != 255 || d_input[index * 4 + 2] != 255 || d_input[index * 4 + 3] == 0) {
	//	return;
	//}

	int neighborcount = 0;
	int neightborRed = 0;
	int neightborGreen = 0;
	int neightborBlue = 0;

	if (x > 0 && d_input[(index - 1) * 4 + 3] != 0 && (d_input[(index - 1) * 4] != 255 || d_input[(index - 1) * 4 + 1] != 255 || d_input[(index - 1) * 4 + 2] != 255)) {
		neightborRed += d_input[(index - 1) * 4];
		neightborGreen += d_input[(index - 1) * 4 + 1];
		neightborBlue += d_input[(index - 1) * 4 + 2];
		neighborcount++;
	}
	if (x < width - 1 && d_input[(index + 1) * 4 + 3] != 0 && (d_input[(index + 1) * 4] != 255 || d_input[(index + 1) * 4 + 1] != 255 || d_input[(index + 1) * 4 + 2] != 255)) {
		neightborRed += d_input[(index + 1) * 4];
		neightborGreen += d_input[(index + 1) * 4 + 1];
		neightborBlue += d_input[(index + 1) * 4 + 2];
		neighborcount++;
	}
	if (y > 0) {
		if (d_input[(index - width) * 4 + 3] != 0 && (d_input[(index - width) * 4] != 255 || d_input[(index - width) * 4 + 1] != 255 || d_input[(index - width) * 4 + 2] != 255)) {
			neightborRed += d_input[(index - width) * 4];
			neightborGreen += d_input[(index - width) * 4 + 1];
			neightborBlue += d_input[(index - width) * 4 + 2];
			neighborcount++;
		}
		if (x > 0 && d_input[(index - width - 1) * 4 + 3] != 0 && (d_input[(index - width - 1) * 4] != 255 || d_input[(index - width - 1) * 4 + 1] != 255 || d_input[(index - width - 1) * 4 + 2] != 255)) {
			neightborRed += d_input[(index - width - 1) * 4];
			neightborGreen += d_input[(index - width - 1) * 4 + 1];
			neightborBlue += d_input[(index - width - 1) * 4 + 2];
			neighborcount++;
		}
		if (x < width - 1 && d_input[(index - width + 1) * 4 + 3] != 0 && (d_input[(index - width + 1) * 4] != 255 || d_input[(index - width + 1) * 4 + 1] != 255 || d_input[(index - width + 1) * 4 + 2] != 255)) {
			neightborRed += d_input[(index - width + 1) * 4];
			neightborGreen += d_input[(index - width + 1) * 4 + 1];
			neightborBlue += d_input[(index - width + 1) * 4 + 2];
			neighborcount++;
		}
	}
	if (y < height - 1) {
		if (d_input[(index + width) * 4 + 3] != 0 && (d_input[(index + width) * 4] != 255 || d_input[(index + width) * 4 + 1] != 255 || d_input[(index + width) * 4 + 2] != 255)) {
			neightborRed += d_input[(index + width) * 4];
			neightborGreen += d_input[(index + width) * 4 + 1];
			neightborBlue += d_input[(index + width) * 4 + 2];
			neighborcount++;
		}
		if (x > 0 && d_input[(index + width - 1) * 4 + 3] != 0 && (d_input[(index + width - 1) * 4] != 255 || d_input[(index + width - 1) * 4 + 1] != 255 || d_input[(index + width - 1) * 4 + 2] != 255)) {
			neightborRed += d_input[(index + width - 1) * 4];
			neightborGreen += d_input[(index + width - 1) * 4 + 1];
			neightborBlue += d_input[(index + width - 1) * 4 + 2];
			neighborcount++;
		}
		if (x < width - 1 && d_input[(index + width + 1) * 4 + 3] != 0 && (d_input[(index + width + 1) * 4] != 255 || d_input[(index + width + 1) * 4 + 1] != 255 || d_input[(index + width + 1) * 4 + 2] != 255)) {
			neightborRed += d_input[(index + width + 1) * 4];
			neightborGreen += d_input[(index + width + 1) * 4 + 1];
			neightborBlue += d_input[(index + width + 1) * 4 + 2];
			neighborcount++;
		}
	}

	if (neighborcount > 1) {
		d_input[index * 4 + 2] = neightborBlue / neighborcount;
		d_input[index * 4] = neightborRed / neighborcount;
		d_input[index * 4 + 1] = neightborGreen / neighborcount;
		(*d_count)++;
		if (onlyEdgeLeft) {
			d_writtenMask[index] = WrittenState::Written;
		}else{
			d_writtenMask[index] = WrittenState::Flooded;
		}
	}
}



void floodWhitePixelWithNeighborColor(GLubyte* input, int width, int height, GLubyte* WrittenMask) {
	bool onlyEdgeLeft = ErodeAndCheckRemain(WrittenMask, width, height);

	GLubyte* d_input;
	cudaError_t error = cudaMalloc(&d_input, width * height * 4 * sizeof(GLubyte));
	if (error != cudaSuccess)
	{
		printf("cudaMalloc returned error %s (code %d), line(%d)\n", cudaGetErrorString(error), error, __LINE__);
		return;
	}
	cudaMemcpy(d_input, input, width * height * 4 * sizeof(GLubyte), cudaMemcpyHostToDevice);

	GLubyte* d_WrittenMask;
	cudaMalloc(&d_WrittenMask, width * height * sizeof(GLubyte));
	cudaMemcpy(d_WrittenMask, WrittenMask, width * height * sizeof(GLubyte), cudaMemcpyHostToDevice);

	int* d_count;
	cudaMalloc(&d_count, sizeof(unsigned int));
	cudaMemset(d_count, 0, sizeof(unsigned int));

	dim3 block(16, 16);
	dim3 grid((int((width + 1) / 2) + block.x - 1) / block.x, (((height + 1) / 2) + block.y - 1) / block.y);

RECURSIVE_FLOOD:
	floodWhitePixelWithNeighborColorKernel1<<<grid, block>>>(d_input, width, height, d_count, 0, d_WrittenMask, onlyEdgeLeft);
	cudaDeviceSynchronize();																   				    
	floodWhitePixelWithNeighborColorKernel1<<<grid, block>>>(d_input, width, height, d_count, 1, d_WrittenMask, onlyEdgeLeft);
	cudaDeviceSynchronize();																   				    
	floodWhitePixelWithNeighborColorKernel1<<<grid, block>>>(d_input, width, height, d_count, 2, d_WrittenMask, onlyEdgeLeft);
	cudaDeviceSynchronize();																   				    
	floodWhitePixelWithNeighborColorKernel1<<<grid, block>>>(d_input, width, height, d_count, 3, d_WrittenMask, onlyEdgeLeft);
	
	int count = -1;
	cudaMemcpy(&count, d_count, sizeof(unsigned int), cudaMemcpyDeviceToHost);

	cudaDeviceSynchronize();
	if (count > 0) {
		//cudaMemcpy(input, d_input, width * height * 4 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
		//cudaDeviceSynchronize();
		//stbi_write_png(("image_out" + std::to_string(count) + ".png").c_str(), width, height, 4, input, width * 4);
		cudaMemset(d_count, 0, sizeof(unsigned int));
		goto RECURSIVE_FLOOD;
	}

	cudaMemcpy(input, d_input, width * height * 4 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaMemcpy(WrittenMask, d_WrittenMask, width * height * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_WrittenMask);
	cudaFree(d_input);
	cudaFree(d_count);
}

__global__ void fillDstEdges(unsigned char* d_imgSrc, int width, int height, char neighbourThreshold, char windowHalfSize, int* count_d) {

	int x = threadIdx.x + blockIdx.x * blockDim.x;
	int y = threadIdx.y + blockIdx.y * blockDim.y;

	// 超出边界或者不为白色的像素不处理
	int idx = (y * width + x) * 4;
	if (x >= width || y >= height || d_imgSrc[idx] != 255 || d_imgSrc[idx+1] != 255 || d_imgSrc[idx+2] != 255 || d_imgSrc[idx+3] == 0 ) {
		return;
	}

	// 处理白色图案的边缘像素（要求邻域内已知像素数量超过阈值）
	char neighbourCount = 0;
	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			if (i == 0 && j == 0) {
				continue;
			}
			int neighbourX = x + i;
			int neighbourY = y + j;
			if (neighbourX < 0 || neighbourX >= width || neighbourY < 0 || neighbourY >= height) {
				continue;
			}
			int neighborIdx = (neighbourY * width + neighbourX) * 4;
			if(d_imgSrc[neighborIdx + 3] > 0 && (d_imgSrc[neighborIdx] != 255 || d_imgSrc[neighborIdx+1] != 255 || d_imgSrc[neighborIdx+2] != 255)) {
				neighbourCount++;	//不透明且不为白色
			}

		}
	}
	if (neighbourCount < neighbourThreshold) {
		return;
	}

	// 确定哪些像素不参与distance计算
	char windowSize = windowHalfSize * 2 + 1;

	char* skip = new char[windowSize * windowSize];

	for (int i = -windowHalfSize; i <= windowHalfSize; i++) {
		for (int j = -windowHalfSize; j <= windowHalfSize; j++) {
			int neighbourX = x + i;
			int neighbourY = y + j;
			int neighbourIdx = (neighbourY * width + neighbourX) * 4;
			if (neighbourX < 0 || neighbourX >= width || neighbourY < 0 || neighbourY >= height || /*is whit pixel*/(d_imgSrc[neighbourIdx] == 255 && d_imgSrc[neighbourIdx + 1] == 255 && d_imgSrc[neighbourIdx + 2] == 255)) {
				skip[(j + windowHalfSize) * windowSize + (i + windowHalfSize)] = 1;
			}
			else {
				skip[(j + windowHalfSize) * windowSize + (i + windowHalfSize)] = 0;
			}
		}
	}

	// 遍历srcMask非零区域，计算distance
	float minialDistance = 8000000;
	int minialDistanceX = -1;
	int minialDistanceY = -1;
	for (int i = max(windowHalfSize, x - 64); i < min(width - windowHalfSize, x + 64); i++) {
		for (int j = max(windowHalfSize, y - 64); j < min(height - windowHalfSize, y + 64); j++) {
			int neighborIdx = (j * width + i) * 4;
			if (d_imgSrc[neighborIdx + 3] > 0 && (d_imgSrc[neighborIdx] != 255 || d_imgSrc[neighborIdx + 1] != 255 || d_imgSrc[neighborIdx + 2] != 255)) {

				float distance = 0;
				for (int ii = -windowHalfSize; ii <= windowHalfSize; ii++) {
					for (int jj = -windowHalfSize; jj <= windowHalfSize; jj++) {
						if (skip[(jj + windowHalfSize) * windowSize + (ii + windowHalfSize)] == 1) {
							continue;
						}
						float channelTotal = 0;
						int index1 = ((j + jj) * width + (i + ii)) * 4;
						int index2 = ((y + jj) * width + (x + ii)) * 4;
						channelTotal += abs(d_imgSrc[index1] - d_imgSrc[index2]);
						channelTotal += abs(d_imgSrc[index1 + 1] - d_imgSrc[index2 + 1]);
						channelTotal += abs(d_imgSrc[index1 + 2] - d_imgSrc[index2 + 2]);
						distance += channelTotal / (sqrtf((float)ii * ii + jj * jj) + 0.1);
					}
				}
				if (distance < minialDistance) {
					minialDistance = distance;
					minialDistanceX = i;
					minialDistanceY = j;
				}
			}
		}
	}
	delete[] skip;

	// 将最小距离的像素值赋给当前像素
	d_imgSrc[idx] = d_imgSrc[(minialDistanceY * width + minialDistanceX) * 4];
	d_imgSrc[idx + 1] = d_imgSrc[(minialDistanceY * width + minialDistanceX) * 4 + 1];
	d_imgSrc[idx + 2] = d_imgSrc[(minialDistanceY * width + minialDistanceX) * 4 + 2];
	d_imgSrc[idx + 3] = d_imgSrc[(minialDistanceY * width + minialDistanceX) * 4 + 3];

	(*count_d)++; //atomicAdd(count_d, 1);
}

void growImg(unsigned char* imgSrcData, int width, int height) {
	unsigned char* d_imgSrc;
	cudaMalloc(&d_imgSrc, width * height * 4 * sizeof(unsigned char));
	cudaMemcpy(d_imgSrc, imgSrcData, width * height * 4 * sizeof(unsigned char), cudaMemcpyHostToDevice);

	int* count_d;
	cudaMalloc((void**)&count_d, sizeof(int));

RecursiveFillDstEdge:

	cudaMemset(count_d, 0, sizeof(int));

	{
		cudaPerfCounter perfCounter;
		dim3 block(16, 16);
		dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
		fillDstEdges << <grid, block >> > (d_imgSrc, width, height, 1, 15, count_d);
		perfCounter.stopCounter();
		printf("fillDstEdges elapsed time: %f\n", perfCounter.elapsed());
	}

	int count_h = -1;
	cudaMemcpy(&count_h, count_d, sizeof(int), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();

	if (count_h != 0) {
		goto RecursiveFillDstEdge;
	}
	cudaMemcpy(imgSrcData, d_imgSrc, width * height * 4 * sizeof(unsigned char), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_imgSrc);
	cudaFree(count_d);

}

__global__ void cleanRGBAPixelsNotMaskedKernel(GLubyte* pixels, GLubyte* mask, int width, int height) {
	int x = threadIdx.x + blockIdx.x * blockDim.x;
	int y = threadIdx.y + blockIdx.y * blockDim.y;

	if (x < width && y < height) {
		if (mask[y * width + x] == 0) {
			pixels[(y * width + x) * 4 + 0] = 0;
			pixels[(y * width + x) * 4 + 1] = 0;
			pixels[(y * width + x) * 4 + 2] = 0;
			pixels[(y * width + x) * 4 + 3] = 0;
		}
	}
}

std::unique_ptr<GLubyte[]> cleanRGBAPixelsNotMasked(std::unique_ptr<GLubyte[]> pixels, std::unique_ptr<GLubyte[]> mask, int width, int height)
{
	GLubyte* d_pixels;
	cudaMalloc(&d_pixels, width * height * 4 * sizeof(GLubyte));
	cudaMemcpy(d_pixels, pixels.get(), width * height * 4 * sizeof(GLubyte), cudaMemcpyHostToDevice);

	GLubyte* d_mask;
	cudaMalloc(&d_mask, width * height * sizeof(GLubyte));
	cudaMemcpy(d_mask, mask.get(), width * height * sizeof(GLubyte), cudaMemcpyHostToDevice);

	dim3 block(16, 16);
	dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
	cleanRGBAPixelsNotMaskedKernel<<<grid, block>>>(d_pixels, d_mask, width, height);

	cudaMemcpy(pixels.get(), d_pixels, width * height * 4 * sizeof(GLubyte), cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaFree(d_pixels);
	cudaFree(d_mask);

	return std::move(pixels);
}


void rgb2hsv(GLubyte* pixels, int width, int height) {
	NppiSize size;
	size.width = width;
	size.height = height;

	// 分配设备内存
	Npp8u* d_src, * d_dst;
	cudaMalloc(&d_src, width * height * 3 * sizeof(Npp8u));
	cudaMalloc(&d_dst, width * height * 3 * sizeof(Npp8u));

	// 将RGB图像复制到设备内存
	cudaMemcpy(d_src, pixels, width * height * 3 * sizeof(Npp8u), cudaMemcpyHostToDevice);

	// 转换颜色空间
	NppStatus status = nppiRGBToHSV_8u_C3R(d_src, width * 3, d_dst, width * 3, size);


	// 将HSV图像复制回主机内存
	cudaMemcpy(pixels, d_dst, size.width * size.height * 3 * sizeof(Npp8u), cudaMemcpyDeviceToHost);

	// 释放设备内存
	cudaFree(d_src);
	cudaFree(d_dst);
}

void hsv2rgb(GLubyte* pixels, int width, int height) {

	NppiSize size;
	size.width = width;
	size.height = height;

	// 分配设备内存
	Npp8u* d_src, * d_dst;
	cudaMalloc(&d_src, size.width * size.height * 3 * sizeof(Npp8u));
	cudaMalloc(&d_dst, size.width * size.height * 3 * sizeof(Npp8u));

	// 将RGB图像复制到设备内存
	cudaMemcpy(d_src, pixels, size.width * size.height * 3 * sizeof(Npp8u), cudaMemcpyHostToDevice);

	// 转换颜色空间
	NppStatus status = nppiHSVToRGB_8u_C3R(d_src, size.width * 3, d_dst, size.width * 3, size);

	// 将HSV图像复制回主机内存
	cudaMemcpy(pixels, d_dst, size.width * size.height * 3 * sizeof(Npp8u), cudaMemcpyDeviceToHost);

	// 释放设备内存
	cudaFree(d_src);
	cudaFree(d_dst);
}