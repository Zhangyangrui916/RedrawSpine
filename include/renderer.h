#include <glad/glad.h>
#include <spine/spine.h>
#include <stb_image.h>
#include <shader.h>

namespace Renderer {

	extern unsigned int VAO, VBO, EBO;
	extern Shader* shader;

	void init();

	void Clear();

	void Draw(spine::Vector<float> vertices, spine::Vector<int> indices);

};

