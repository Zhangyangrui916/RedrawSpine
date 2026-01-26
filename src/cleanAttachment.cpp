#include <SkeletonDrawable.h>
#include <renderer.h>
#include <embedded_shaders.h>
#include <texture.h>
#include "utility.cuh"
#include "stb_image_write.h"

using namespace spine;

namespace cleanAttachment {

	unsigned int VAO, VBO, EBO;

	Shader* shader = nullptr;

	void init() {
		glDisable(GL_DEPTH_TEST);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		shader = new Shader(Shader::SourceTag{}, EmbeddedShaders::CLEAN_ATTACHMENT_VS, EmbeddedShaders::CLEAN_ATTACHMENT_FS);
		shader->use();

		GLuint framebuffer;
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		GLuint renderbuffer;
		glGenRenderbuffers(1, &renderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_R8UI, 2048, 2048);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Error! Framebuffer is not complete!" << std::endl;


		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		glGenBuffers(1, &VBO);
		glGenBuffers(1, &EBO);

		glBindBuffer(GL_ARRAY_BUFFER, VBO);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);	//xy

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

	}


	void clean(spine::Attachment* attachment) {
		if (!attachment) return;
		spine::Vector<float>* uvs;
		Vector<unsigned short>* idxes;
		Vector<unsigned short> quadIndices;
		quadIndices.add(0);
		quadIndices.add(1);
		quadIndices.add(2);
		quadIndices.add(2);
		quadIndices.add(3);
		quadIndices.add(0);
		Texture* texture;
		if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
			RegionAttachment* regionAttachment = static_cast<RegionAttachment*>(attachment);
			texture = static_cast<Texture*>(regionAttachment->getRegion()->rendererObject);
			uvs = &regionAttachment->getUVs();
			idxes = &quadIndices;
		}
		else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
			MeshAttachment* mesh = static_cast<MeshAttachment*>(attachment);
			texture = static_cast<Texture*>(mesh->getRegion()->rendererObject);
			uvs = &mesh->getUVs();
			idxes = &mesh->getTriangles();
		}
		else
			return;

		int width = texture->width;	
		int height = texture->height;

		Vector<int> glindexes;
		for (auto idx : *idxes)
			glindexes.add(idx);
		
		glViewport(0, 0, width, height);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		Renderer::Clear();

		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, uvs->size() * sizeof(float), uvs->buffer(), GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, glindexes.size() * sizeof(int), glindexes.buffer(), GL_DYNAMIC_DRAW);

		glDrawElements(GL_TRIANGLES, glindexes.size(), GL_UNSIGNED_INT, 0);

		std::unique_ptr<GLubyte[]> mask(new GLubyte[width * height]);

		glReadPixels(0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask.get());

		std::string path = "Z:/" + std::string(attachment->getName().buffer()) + ".png";

		auto cleanTex = cleanRGBAPixelsNotMasked(spine::GetTexImage(texture, GL_RGBA), std::move(mask), width, height);

		stbi_write_png(path.c_str(), width, height, 4, cleanTex.get(), width * 4);

		return;
	}


}
