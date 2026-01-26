#ifndef UTILITY_H
#define UTILITY_H

#include <cstddef>
#include <glad/glad.h>
#include "texture.h"
#include <memory>
#include <tuple>

enum WrittenState {
	Transparent = 0,
	Written = 128,
	Flooded = 200,
	NotWritten = 255
};

std::tuple<std::unique_ptr<GLubyte[]>, Texture*> convertToBinaryAlphaMask(std::tuple<std::unique_ptr<GLubyte[]>, Texture*>& rgba);

unsigned int countNotZero(GLuint* p, size_t size);

std::unique_ptr<GLubyte[]> decodeUVToRGB(GLuint* uv, int width, int height);

std::unique_ptr<GLubyte[]> decodeUVToMask(GLuint* uv, int width, int height);

std::unique_ptr<GLubyte[]> decodeUVToSlot(GLuint* uv, int width, int height);

void floodWhitePixelWithNeighborColor(GLubyte* input, int width, int height, GLubyte* WrittenMask);
void floodWhitePixelWithNeighborColorCPU(GLubyte* input, int width, int height, GLubyte* writtenMask);
void cleanOutliner(GLubyte* input, int width, int height);
void growImg(unsigned char* imgSrcData, int width, int height);

std::unique_ptr<GLubyte[]> cleanRGBAPixelsNotMasked(std::unique_ptr<GLubyte[]> pixels, std::unique_ptr<GLubyte[]> mask, int width, int height);

void rgb2hsv(GLubyte* pixels, int width, int height);
void hsv2rgb(GLubyte* pixels, int width, int height);
#endif
