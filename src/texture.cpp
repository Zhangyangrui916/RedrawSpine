#pragma once
#include <texture.h>
#include <glad/glad.h>
#include <stb_image.h>
#include <iostream>

using namespace spine;


void OGLTextureLoader::load(AtlasPage& page, const String& path) {
	unsigned int textureID;
	glGenTextures(1, &textureID);

	int width, height, nrComponents;
	stbi_set_flip_vertically_on_load(false);

	stbi_uc* data = stbi_load(path.buffer(), &width, &height, &nrComponents, 0);

	if (!data) {
		std::cout << "Texture failed to load" << path.buffer();
		return;
	}

	GLenum format = GL_RED;
	if (nrComponents == 3)
		format = GL_RGB;
	else if (nrComponents == 4)
		format = GL_RGBA;

	glBindTexture(GL_TEXTURE_2D, textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_image_free(data);

	page.texture = new Texture(width, height, textureID, path.buffer());
	page.width = width;
	page.height = height;
}


void OGLTextureLoader::unload(void* texture)
{
	glDeleteTextures(1, (unsigned int*)texture);

}
