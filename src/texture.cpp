#pragma once
#include <spine/spine.h>
#include <texture.h>
#include <glad/glad.h>
#include <stb_image.h>
#include <iostream>
#include <renderer.h>

using namespace spine;


SkeletonDrawable::SkeletonDrawable(SkeletonData* skeletonData, AnimationStateData* animationStateData) {
	Bone::setYDown(true);
	skeleton = new (__FILE__, __LINE__) Skeleton(skeletonData);
	
	ownsAnimationStateData = animationStateData == 0;
	if (ownsAnimationStateData) animationStateData = new (__FILE__, __LINE__) AnimationStateData(skeletonData);
	animationState = new (__FILE__, __LINE__) AnimationState(animationStateData);
}

SkeletonDrawable::~SkeletonDrawable() {
	if (ownsAnimationStateData) delete animationState->getData();
	delete animationState;
	delete skeleton;
}

void SkeletonDrawable::update(float delta) {
	animationState->update(delta);
	animationState->apply(*skeleton);
	skeleton->updateWorldTransform();
}

void SkeletonDrawable::draw() {
	Vector<unsigned short> quadIndices;
	quadIndices.add(0);
	quadIndices.add(1);
	quadIndices.add(2);
	quadIndices.add(2);
	quadIndices.add(3);
	quadIndices.add(0);
	unsigned int texture;
	

	for (unsigned i = 0; i < skeleton->getSlots().size(); ++i) {
		Slot& slot = *skeleton->getDrawOrder()[i];
		Attachment* attachment = slot.getAttachment();
		if (!attachment) continue;

		// Early out if the slot color is 0 or the bone is not active
		if (slot.getColor().a == 0 || !slot.getBone().isActive()) {
			clipper.clipEnd(slot);
			continue;
		}

		Vector<float>* vertices = &worldVertices;
		int verticesCount = 0;
		Vector<float>* uvs = NULL;
		Vector<unsigned short>* indices;
		int indicesCount = 0;
		Color* attachmentColor;

		if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
			RegionAttachment* regionAttachment = (RegionAttachment*)attachment;
			attachmentColor = &regionAttachment->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				clipper.clipEnd(slot);
				continue;
			}

			worldVertices.setSize(8, 0);
			regionAttachment->computeWorldVertices(slot, worldVertices, 0, 2);
			verticesCount = 4;
			uvs = &regionAttachment->getUVs();
			indices = &quadIndices;
			indicesCount = 6;
			texture = (unsigned int)regionAttachment->getRegion()->rendererObject;

		}
		else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
			MeshAttachment* mesh = (MeshAttachment*)attachment;
			attachmentColor = &mesh->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				clipper.clipEnd(slot);
				continue;
			}

			worldVertices.setSize(mesh->getWorldVerticesLength(), 0);
			mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), worldVertices.buffer(), 0, 2);
			texture = (unsigned int)mesh->getRegion()->rendererObject;
			verticesCount = mesh->getWorldVerticesLength() >> 1;
			uvs = &mesh->getUVs();
			indices = &mesh->getTriangles();
			indicesCount = indices->size();

		}
		else if (attachment->getRTTI().isExactly(ClippingAttachment::rtti)) {
			ClippingAttachment* clip = (ClippingAttachment*)slot.getAttachment();
			clipper.clipStart(slot, clip);
			continue;
		}
		else
			continue;

		UINT8 r = static_cast<UINT8>(skeleton->getColor().r * slot.getColor().r * attachmentColor->r * 255);
		UINT8 g = static_cast<UINT8>(skeleton->getColor().g * slot.getColor().g * attachmentColor->g * 255);
		UINT8 b = static_cast<UINT8>(skeleton->getColor().b * slot.getColor().b * attachmentColor->b * 255);
		UINT8 a = static_cast<UINT8>(skeleton->getColor().a * slot.getColor().a * attachmentColor->a * 255);

		if (clipper.isClipping()) {
			clipper.clipTriangles(worldVertices, *indices, *uvs, 2);
			vertices = &clipper.getClippedVertices();
			verticesCount = clipper.getClippedVertices().size() >> 1;
			uvs = &clipper.getClippedUVs();
			indices = &clipper.getClippedTriangles();
			indicesCount = clipper.getClippedTriangles().size();
		}

		glVertices.clear();
		for (int ii = 0; ii < verticesCount << 1; ii += 2) {
			glVertices.add((*vertices)[ii]		/ 40.f);
			glVertices.add((*vertices)[ii + 1]	/ 40.f);
			glVertices.add(r/255.0);
			glVertices.add(g/255.0);
			glVertices.add(b/255.0);
			glVertices.add(a/255.0);
			glVertices.add((*uvs)[ii]);
			glVertices.add((*uvs)[ii + 1]);
		}

		glIndices.clear();
		for (int ii = 0; ii < (int)indices->size(); ii++)
			glIndices.add((*indices)[ii]);

		glBindTexture(GL_TEXTURE_2D, texture);
		Renderer::Draw(glVertices, glIndices);

		clipper.clipEnd(slot);
	}
	clipper.clipEnd();
}


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
	else {
		std::cout << "Texture loaded at: " << textureID;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);


    page.texture = (void*)textureID;
    page.width = width;
    page.height = height;
}

void spine::OGLTextureLoader::unload(void* texture)
{
	glDeleteTextures(1, (unsigned int*)texture);

}

SpineExtension* spine::getDefaultExtension() {
	return new DefaultSpineExtension();
}