#include "utility.cuh"
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#define LOW_ALPHA_THRESHOLD 11

namespace {
constexpr int kFilterWidth = 11;
constexpr float kTwoSigmaSquare = 50.f;

// Precompute a small Gaussian kernel for soft mask edges.
const std::vector<float>& gaussianFilter() {
	static std::vector<float> filter;
	if (!filter.empty()) {
		return filter;
	}
	filter.resize(kFilterWidth * kFilterWidth);
	int half = (kFilterWidth - 1) / 2;
	for (int y = 0; y < kFilterWidth; y++) {
		for (int x = 0; x < kFilterWidth; x++) {
			int dx = x - half;
			int dy = y - half;
			filter[y * kFilterWidth + x] = std::exp(-(dx * dx + dy * dy) / kTwoSigmaSquare);
		}
	}
	return filter;
}

inline bool isColoredOpaque(const GLubyte* rgba) {
	return rgba[3] != 0 && (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255);
}

int floodPass(GLubyte* input, int width, int height, GLubyte* writtenMask, bool onlyEdgeLeft, int mode) {
	int count = 0;
	int startX = (mode == 1 || mode == 3) ? 1 : 0;
	int startY = (mode == 2 || mode == 3) ? 1 : 0;

	for (int y = startY; y < height; y += 2) {
		for (int x = startX; x < width; x += 2) {
			int index = y * width + x;
			if (writtenMask[index] != WrittenState::NotWritten) {
				continue;
			}

			int neighborCount = 0;
			int writtenNeighborCount = 0;
			int neighborRed = 0;
			int neighborGreen = 0;
			int neighborBlue = 0;

			auto addNeighbor = [&](int nx, int ny) {
				int nIndex = ny * width + nx;
				const GLubyte* nPixel = &input[nIndex * 4];
				if (!isColoredOpaque(nPixel)) {
					return;
				}
				neighborRed += nPixel[0];
				neighborGreen += nPixel[1];
				neighborBlue += nPixel[2];
				neighborCount++;
				if (writtenMask[nIndex] == WrittenState::Written) {
					writtenNeighborCount++;
				}
			};

			if (x > 0) {
				addNeighbor(x - 1, y);
			}
			if (x < width - 1) {
				addNeighbor(x + 1, y);
			}
			if (y > 0) {
				addNeighbor(x, y - 1);
				if (x > 0) {
					addNeighbor(x - 1, y - 1);
				}
				if (x < width - 1) {
					addNeighbor(x + 1, y - 1);
				}
			}
			if (y < height - 1) {
				addNeighbor(x, y + 1);
				if (x > 0) {
					addNeighbor(x - 1, y + 1);
				}
				if (x < width - 1) {
					addNeighbor(x + 1, y + 1);
				}
			}

			if (neighborCount > 1) {
				GLubyte* outPixel = &input[index * 4];
				outPixel[0] = static_cast<GLubyte>(neighborRed / neighborCount);
				outPixel[1] = static_cast<GLubyte>(neighborGreen / neighborCount);
				outPixel[2] = static_cast<GLubyte>(neighborBlue / neighborCount);
				count++;
				if (onlyEdgeLeft) {
					writtenMask[index] = WrittenState::Written;
				} else {
					writtenMask[index] = (writtenNeighborCount > 4) ? WrittenState::Written : WrittenState::Flooded;
				}
			}
		}
	}

	return count;
}

void erodePass(const GLubyte* input, GLubyte* output, int width, int height) {
	int size = width * height;
	std::copy(input, input + size, output);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int index = y * width + x;
			if (x > 0 && input[index - 1] == 0) {
				output[index] = 0;
				continue;
			}
			if (x + 1 < width && input[index + 1] == 0) {
				output[index] = 0;
				continue;
			}
			if (y > 0 && input[index - width] == 0) {
				output[index] = 0;
				continue;
			}
			if (y + 1 < height && input[index + width] == 0) {
				output[index] = 0;
				continue;
			}
		}
	}
}

} // namespace

// 不改变传入参数
// 返回的结构体之所以带上Texture*,是为了指明GLubyte[]的宽高. 如果要更新到GPU请自己更新.
std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToBinaryAlphaMask(std::tuple<std::unique_ptr<GLubyte[]>, Texture*>& rgba)
{
	auto& [input, texture] = rgba;
	int width = texture->width;
	int height = texture->height;
	std::unique_ptr<GLubyte[]> output(new GLubyte[width * height]);

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int idx = (y * width + x) * 4;
			output[y * width + x] = (input[idx + 3] > LOW_ALPHA_THRESHOLD) ? 255 : 0;
		}
	}

	return std::tuple(std::move(output), texture);
}

unsigned int countNotZero(GLuint* p, size_t size)
{
	unsigned int count = 0;
	if (size < 2) {
		return 0;
	}
	for (size_t i = 0; i + 1 < size; i++) {
		if (p[i] != 0 && p[i + 1] != 0) {
			count++;
		}
	}
	return count;
}

std::unique_ptr<GLubyte[]> decodeUVToRGB(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> rgb(new GLubyte[size * 3]());

	for (int i = 0; i < size; i++) {
		GLuint Slot_V_U = uv[i];
		if (Slot_V_U == 0) {
			continue;
		}
		GLuint U = Slot_V_U & 0xFFF;
		GLuint V = (Slot_V_U >> 12) & 0xFFF;
		GLuint Id = (Slot_V_U >> 24) & 0xFF;
		rgb[3 * i] = static_cast<GLubyte>(Id);
		rgb[3 * i + 1] = static_cast<GLubyte>(U >> 3);
		rgb[3 * i + 2] = static_cast<GLubyte>(V >> 3);
	}

	return rgb;
}

std::unique_ptr<GLubyte[]> decodeUVToMask(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> gray(new GLubyte[size]);
	std::unique_ptr<GLubyte[]> output(new GLubyte[size]);

	// 先得到硬 mask，再进行高斯模糊以获得更柔和的边缘
	for (int i = 0; i < size; i++) {
		gray[i] = uv[i] > 0 ? 255 : 0;
		output[i] = 0;
	}

	const auto& filter = gaussianFilter();
	int half = (kFilterWidth - 1) / 2;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int idx = y * width + x;
			if (gray[idx] == 255) {
				output[idx] = 255;
				continue;
			}

			float sum = 0.0f;
			for (int fy = -half; fy <= half; fy++) {
				int ny = y + fy;
				if (ny < 0 || ny >= height) {
					continue;
				}
				for (int fx = -half; fx <= half; fx++) {
					int nx = x + fx;
					if (nx < 0 || nx >= width) {
						continue;
					}
					float weight = filter[(fy + half) * kFilterWidth + (fx + half)];
					sum += weight * gray[ny * width + nx];
				}
			}
			int blurred = static_cast<int>(sum);
			output[idx] = static_cast<GLubyte>(std::min(255, std::max(0, blurred)));
		}
	}

	return output;
}

std::unique_ptr<GLubyte[]> decodeUVToSlot(GLuint* uv, int width, int height)
{
	int size = width * height;
	std::unique_ptr<GLubyte[]> rgb(new GLubyte[size * 3]());

	// 将 slotId 映射为可视化颜色（仅用于调试）
	for (int i = 0; i < size; i++) {
		GLuint Slot_V_U = uv[i];
		GLubyte slot = static_cast<GLubyte>(Slot_V_U >> 24);
		unsigned int r = static_cast<unsigned int>(slot) * 2311;
		unsigned int g = static_cast<unsigned int>(slot) * 2777;
		unsigned int b = static_cast<unsigned int>(slot) * 2659;
		rgb[i * 3] = static_cast<GLubyte>(r & 0xFF);
		rgb[i * 3 + 1] = static_cast<GLubyte>(g & 0xFF);
		rgb[i * 3 + 2] = static_cast<GLubyte>(b & 0xFF);
	}

	return rgb;
}

std::unique_ptr<GLubyte[]> canny(std::unique_ptr<GLubyte[]> input, int width, int height) {
	std::unique_ptr<GLubyte[]> output(new GLubyte[width * height]);
	std::fill(output.get(), output.get() + width * height, 0);

	for (int y = 1; y < height - 1; y++) {
		for (int x = 1; x < width - 1; x++) {
			int gradient_x = std::abs(static_cast<int>(input[y * width + x + 1]) - static_cast<int>(input[y * width + x - 1]));
			int gradient_y = std::abs(static_cast<int>(input[(y + 1) * width + x]) - static_cast<int>(input[(y - 1) * width + x]));
			output[y * width + x] = (gradient_x > 1 || gradient_y > 1) ? 255 : 0;
		}
	}

	return output;
}

bool ErodeAndCheckRemain(GLubyte* input, int width, int height) {
	int size = width * height;
	std::vector<GLubyte> current(input, input + size);
	std::vector<GLubyte> next(size);

	for (int i = 0; i < 3; i++) {
		erodePass(current.data(), next.data(), width, height);
		current.swap(next);
	}

	unsigned int count = 0;
	for (int i = 0; i + 1 < size; i++) {
		if (current[i] != 0 && current[i + 1] != 0) {
			count++;
		}
	}

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
				input[index * 4] = neightborRed / neighborcount;
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

void cleanOutliner(GLubyte* input, int width, int height)
{
	for (int y = 2; y < height - 2; y++) {
		for (int x = 2; x < width - 2; x++) {
			int idx = (y * width + x) * 4;
			if (input[idx + 3] == 0 || (input[idx] == 255 && input[idx + 1] == 255 && input[idx + 2] == 255)) {
				continue;
			}
			int countNeighborNotWhite = 0;
			for (int i = x - 2; i <= x + 2; i++) {
				for (int j = y - 2; j <= y + 2; j++) {
					if (i == x && j == y) {
						continue;
					}
					int neighborIndex = (j * width + i) * 4;
					if (input[neighborIndex] != 255 || input[neighborIndex + 1] != 255 || input[neighborIndex + 2] != 255) {
						countNeighborNotWhite++;
					}
				}
			}

			if (countNeighborNotWhite < 8) {
				input[idx] = 255;
				input[idx + 1] = 255;
				input[idx + 2] = 255;
			}
		}
	}
}

void floodWhitePixelWithNeighborColor(GLubyte* input, int width, int height, GLubyte* WrittenMask) {
	bool onlyEdgeLeft = ErodeAndCheckRemain(WrittenMask, width, height);
	int count = 0;
	do {
		count = 0;
		count += floodPass(input, width, height, WrittenMask, onlyEdgeLeft, 0);
		count += floodPass(input, width, height, WrittenMask, onlyEdgeLeft, 1);
		count += floodPass(input, width, height, WrittenMask, onlyEdgeLeft, 2);
		count += floodPass(input, width, height, WrittenMask, onlyEdgeLeft, 3);
	} while (count > 0);
}

void growImg(unsigned char* imgSrcData, int width, int height) {
	int size = width * height * 4;
	std::vector<unsigned char> img(imgSrcData, imgSrcData + size);

	int count = 0;
	do {
		count = 0;
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int idx = (y * width + x) * 4;
				if (img[idx] != 255 || img[idx + 1] != 255 || img[idx + 2] != 255 || img[idx + 3] == 0) {
					continue;
				}

				char neighbourCount = 0;
				for (int i = -1; i <= 1; i++) {
					for (int j = -1; j <= 1; j++) {
						if (i == 0 && j == 0) {
							continue;
						}
						int nx = x + i;
						int ny = y + j;
						if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
							continue;
						}
						int nIdx = (ny * width + nx) * 4;
						if (img[nIdx + 3] > 0 && (img[nIdx] != 255 || img[nIdx + 1] != 255 || img[nIdx + 2] != 255)) {
							neighbourCount++;
						}
					}
				}
				if (neighbourCount < 1) {
					continue;
				}

				char windowHalfSize = 15;
				char windowSize = windowHalfSize * 2 + 1;
				std::vector<char> skip(windowSize * windowSize, 0);

				for (int i = -windowHalfSize; i <= windowHalfSize; i++) {
					for (int j = -windowHalfSize; j <= windowHalfSize; j++) {
						int nx = x + i;
						int ny = y + j;
						int nIdx = (ny * width + nx) * 4;
						if (nx < 0 || nx >= width || ny < 0 || ny >= height || (img[nIdx] == 255 && img[nIdx + 1] == 255 && img[nIdx + 2] == 255)) {
							skip[(j + windowHalfSize) * windowSize + (i + windowHalfSize)] = 1;
						}
					}
				}

				float minialDistance = 8000000.0f;
				int minialDistanceX = -1;
				int minialDistanceY = -1;
				for (int i = std::max<int>(windowHalfSize, x - 64); i < std::min<int>(width - windowHalfSize, x + 64); i++) {
					for (int j = std::max<int>(windowHalfSize, y - 64); j < std::min<int>(height - windowHalfSize, y + 64); j++) {
						int neighborIdx = (j * width + i) * 4;
						if (img[neighborIdx + 3] > 0 && (img[neighborIdx] != 255 || img[neighborIdx + 1] != 255 || img[neighborIdx + 2] != 255)) {
							float distance = 0;
							for (int ii = -windowHalfSize; ii <= windowHalfSize; ii++) {
								for (int jj = -windowHalfSize; jj <= windowHalfSize; jj++) {
									if (skip[(jj + windowHalfSize) * windowSize + (ii + windowHalfSize)] == 1) {
										continue;
									}
									float channelTotal = 0;
									int index1 = ((j + jj) * width + (i + ii)) * 4;
									int index2 = ((y + jj) * width + (x + ii)) * 4;
									channelTotal += std::abs(static_cast<int>(img[index1]) - static_cast<int>(img[index2]));
									channelTotal += std::abs(static_cast<int>(img[index1 + 1]) - static_cast<int>(img[index2 + 1]));
									channelTotal += std::abs(static_cast<int>(img[index1 + 2]) - static_cast<int>(img[index2 + 2]));
									distance += channelTotal / (std::sqrt(static_cast<float>(ii * ii + jj * jj)) + 0.1f);
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

				if (minialDistanceX >= 0 && minialDistanceY >= 0) {
					int srcIdx = (minialDistanceY * width + minialDistanceX) * 4;
					img[idx] = img[srcIdx];
					img[idx + 1] = img[srcIdx + 1];
					img[idx + 2] = img[srcIdx + 2];
					img[idx + 3] = img[srcIdx + 3];
					count++;
				}
			}
		}
	} while (count != 0);

	std::copy(img.begin(), img.end(), imgSrcData);
}

std::unique_ptr<GLubyte[]> cleanRGBAPixelsNotMasked(std::unique_ptr<GLubyte[]> pixels, std::unique_ptr<GLubyte[]> mask, int width, int height)
{
	int size = width * height;
	for (int i = 0; i < size; i++) {
		if (mask[i] == 0) {
			int idx = i * 4;
			pixels[idx + 0] = 0;
			pixels[idx + 1] = 0;
			pixels[idx + 2] = 0;
			pixels[idx + 3] = 0;
		}
	}

	return pixels;
}

void rgb2hsv(GLubyte* pixels, int width, int height) {
	int size = width * height;
	for (int i = 0; i < size; i++) {
		float r = pixels[i * 3] / 255.0f;
		float g = pixels[i * 3 + 1] / 255.0f;
		float b = pixels[i * 3 + 2] / 255.0f;

		float maxc = std::max({r, g, b});
		float minc = std::min({r, g, b});
		float delta = maxc - minc;

		float h = 0.0f;
		if (delta > 0.00001f) {
			if (maxc == r) {
				h = std::fmod((g - b) / delta, 6.0f);
			}
			else if (maxc == g) {
				h = ((b - r) / delta) + 2.0f;
			}
			else {
				h = ((r - g) / delta) + 4.0f;
			}
			h *= 60.0f;
			if (h < 0.0f) {
				h += 360.0f;
			}
		}

		float s = (maxc <= 0.0f) ? 0.0f : (delta / maxc);
		float v = maxc;

		pixels[i * 3] = static_cast<GLubyte>(std::clamp(h / 360.0f * 255.0f, 0.0f, 255.0f));
		pixels[i * 3 + 1] = static_cast<GLubyte>(std::clamp(s * 255.0f, 0.0f, 255.0f));
		pixels[i * 3 + 2] = static_cast<GLubyte>(std::clamp(v * 255.0f, 0.0f, 255.0f));
	}
}

void hsv2rgb(GLubyte* pixels, int width, int height) {
	int size = width * height;
	for (int i = 0; i < size; i++) {
		float h = (pixels[i * 3] / 255.0f) * 360.0f;
		float s = pixels[i * 3 + 1] / 255.0f;
		float v = pixels[i * 3 + 2] / 255.0f;

		if (s <= 0.0f) {
			GLubyte value = static_cast<GLubyte>(std::clamp(v * 255.0f, 0.0f, 255.0f));
			pixels[i * 3] = value;
			pixels[i * 3 + 1] = value;
			pixels[i * 3 + 2] = value;
			continue;
		}

		float hh = h;
		if (hh >= 360.0f) {
			hh = 0.0f;
		}
		hh /= 60.0f;
		int sector = static_cast<int>(std::floor(hh));
		float f = hh - sector;

		float p = v * (1.0f - s);
		float q = v * (1.0f - s * f);
		float t = v * (1.0f - s * (1.0f - f));

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		switch (sector) {
			case 0:
				r = v; g = t; b = p; break;
			case 1:
				r = q; g = v; b = p; break;
			case 2:
				r = p; g = v; b = t; break;
			case 3:
				r = p; g = q; b = v; break;
			case 4:
				r = t; g = p; b = v; break;
			default:
				r = v; g = p; b = q; break;
		}

		pixels[i * 3] = static_cast<GLubyte>(std::clamp(r * 255.0f, 0.0f, 255.0f));
		pixels[i * 3 + 1] = static_cast<GLubyte>(std::clamp(g * 255.0f, 0.0f, 255.0f));
		pixels[i * 3 + 2] = static_cast<GLubyte>(std::clamp(b * 255.0f, 0.0f, 255.0f));
	}
}
