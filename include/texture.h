#pragma once
#include <spine/spine.h>

class OGLTextureLoader : public spine::TextureLoader {
public:
	OGLTextureLoader() = default;

	void load(spine::AtlasPage& page, const spine::String& path);

	void unload(void* texture);

};

class Texture {
public:
	int width, height;
	unsigned int textureID;
	Texture(int width, int height, unsigned int textureID) : width(width), height(height), textureID(textureID) {}

};
