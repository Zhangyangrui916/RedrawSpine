#pragma once
#include <spine/spine.h>

class OGLTextureLoader : public spine::TextureLoader {
public:
	OGLTextureLoader() = default;

	void load(spine::AtlasPage& page, const spine::String& path);

	void unload(void* texture);

};
