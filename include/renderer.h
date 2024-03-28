#include <glad/glad.h>
#include <spine/spine.h>
#include <stb_image.h>
#include <shader.h>

namespace Renderer {

	extern unsigned int VAO, VBO, EBO;
	extern int SCR_X, SCR_Y, SCR_WIDTH, SCR_HEIGHT;
	extern Shader* shader;

	void init(char* arg);

	void Clear();

	void Draw(spine::Vector<float> vertices, spine::Vector<int> indices);

};

