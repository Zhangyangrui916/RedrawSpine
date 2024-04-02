#pragma once
#include <spine/spine.h>
#include <string>

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
	std::string path;
	Texture(int width, int height, unsigned int textureID, const char* path) : width(width), height(height), textureID(textureID), path(path) {}

};
