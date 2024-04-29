#include <glad/glad.h>
#include <spine/spine.h>
#include <shader.h>

namespace Renderer {

	extern unsigned int VAO, VBO, EBO;
	extern int SCR_X, SCR_Y, SCR_WIDTH, SCR_HEIGHT;
	extern Shader* shader;

	void init(char* viewPort, char* imgFormat);

	void Clear();

	void UpdateShader(const char* vs, const char* fs);

	void Draw(spine::Vector<float> vertices, spine::Vector<int> indices);

	std::unique_ptr<GLubyte[]> ReadPixelsRGBA();
	std::unique_ptr<GLuint[]> ReadPixelsR32UI();
	std::unique_ptr<GLubyte[]> ReadPixelsR8UI();

	void StartDrawCTRL();
	void EndDrawCTRL();

	void StartDrawUV();

};

