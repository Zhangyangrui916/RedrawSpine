#include <renderer.h>
#include <embedded_shaders.h>

namespace Renderer {

	unsigned int VAO, VBO, EBO;

	Shader* shader = nullptr;

	GLuint framebuffer;

	int SCR_X;
	int SCR_Y;
	int SCR_WIDTH;
	int SCR_HEIGHT;

	constexpr int GLFormat[3][3]{ // {internalFormat, format, type}
		{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},
		{GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT},
		{GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE}
	};
	int mode;

	void init(char* viewport, char* imgFormat) {

		std::istringstream iss(viewport);
		std::string temp;

		std::getline(iss, temp, ',');
		SCR_X = std::stoi(temp);
		std::getline(iss, temp, ',');
		SCR_Y = std::stoi(temp);
		std::getline(iss, temp, ',');
		SCR_WIDTH = std::stoi(temp);
		std::getline(iss, temp, ',');
		SCR_HEIGHT = std::stoi(temp);

		const char* frag;
		if (strcmp(imgFormat, "rgb") == 0) {
			mode = 0;
			frag = EmbeddedShaders::RGB_FS;
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else {
			mode = 1;
			frag = EmbeddedShaders::UV_REDRAW_FS;
		}

		UpdateShader(EmbeddedShaders::VS, frag);

		//GLuint framebuffer;
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

		GLuint renderbuffer;
		glGenRenderbuffers(1, &renderbuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GLFormat[mode][0], SCR_WIDTH, SCR_HEIGHT);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			std::cout << "Error! Framebuffer is not complete!" << std::endl;

		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

		glGenVertexArrays(1, &VAO);
		glBindVertexArray(VAO);
		glGenBuffers(1, &VBO);
		glGenBuffers(1, &EBO);

		glBindBuffer(GL_ARRAY_BUFFER, VBO);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);	//xy
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));	//rgba
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));	//uv

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	}

	void Clear() {
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void UpdateShader(const char* vs, const char* fs) {
		Renderer::shader = new Shader(Shader::SourceTag{}, vs, fs);
		Renderer::shader->use();
		Renderer::shader->setFloat("width", SCR_WIDTH);
		Renderer::shader->setFloat("height", SCR_HEIGHT);
		Renderer::shader->setFloat("minx", SCR_X);
		Renderer::shader->setFloat("miny", SCR_Y);
	}

	static GLuint CTRLframebuffer = 0;
	void StartDrawCTRL() {
		if (CTRLframebuffer == 0) {
			glGenFramebuffers(1, &CTRLframebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, CTRLframebuffer);

			GLuint renderbuffer;
			glGenRenderbuffers(1, &renderbuffer);
			glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GLFormat[2][0], SCR_WIDTH, SCR_HEIGHT);

			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "Error! Framebuffer is not complete!" << std::endl;
		}
		else {
			glBindFramebuffer(GL_FRAMEBUFFER, CTRLframebuffer);
		}
		UpdateShader(EmbeddedShaders::VS, EmbeddedShaders::CTRL_FS);
	}

	void EndDrawCTRL() {
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		UpdateShader(EmbeddedShaders::VS, EmbeddedShaders::UV_REDRAW_FS);
	}

	static GLuint UVframebuffer = 0;
	void StartDrawUV() {
		if (UVframebuffer == 0) {
			glGenFramebuffers(1, &UVframebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, UVframebuffer);

			GLuint renderbuffer;
			glGenRenderbuffers(1, &renderbuffer);
			glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GLFormat[1][0], SCR_WIDTH, SCR_HEIGHT);

			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
				std::cout << "Error! Framebuffer is not complete!" << std::endl;
		}
		else {
			glBindFramebuffer(GL_FRAMEBUFFER, UVframebuffer);
		}
		UpdateShader(EmbeddedShaders::VS, EmbeddedShaders::UV_REDRAW_FS);
	}


	void Draw(spine::Vector<float> vertices, spine::Vector<int> indices)
	{
		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.buffer(), GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(int), indices.buffer(), GL_DYNAMIC_DRAW);

		glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
	}


	std::unique_ptr<GLubyte[]> ReadPixelsRGBA() {
		std::unique_ptr<GLubyte[]> pixels(new GLubyte[SCR_WIDTH * SCR_HEIGHT * 4]);
		glReadPixels(0, 0, SCR_WIDTH, SCR_HEIGHT, GLFormat[0][1], GLFormat[0][2], pixels.get());
		return pixels;
	}

	std::unique_ptr<GLuint[]> ReadPixelsR32UI() {
		std::unique_ptr<GLuint[]> pixels(new GLuint[SCR_WIDTH * SCR_HEIGHT]);
		glReadPixels(0, 0, SCR_WIDTH, SCR_HEIGHT, GLFormat[1][1], GLFormat[1][2], pixels.get());
		return pixels;
	}

	std::unique_ptr<GLubyte[]> ReadPixelsR8UI() {
		std::unique_ptr<GLubyte[]> pixels(new GLubyte[SCR_WIDTH * SCR_HEIGHT]);
		glReadPixels(0, 0, SCR_WIDTH, SCR_HEIGHT, GLFormat[2][1], GLFormat[2][2], pixels.get());
		return pixels;
	}

}


