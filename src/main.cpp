#include <glad/glad.h>
#include <spine/spine.h>
#include "stb_image_write.h"
#include <shader.h>
#include <GLFW/glfw3.h>
#include <renderer.h>
#include <texture.h>
#include <SkeletonDrawable.h>

using namespace spine;

void saveFrameBuffer2Img(const char* filename, int width, int height) {
    GLubyte* pixels = new GLubyte[width * height * 4];

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    stbi_write_png(filename, width, height, 4, pixels, width * 4);

    delete[] pixels;
}


int main(int argc, char* argv[]) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1, 1, "I use custom Framebuffer to render", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    char* skeletonJsonPath = argv[1];
    char* atlasPath = argv[2];
    spine::SkeletonDrawable drawable(skeletonJsonPath, atlasPath);

    if (strcmp(argv[3], "getAABB") == 0) {
        drawable.stdoutAABB();
		return 0;
	}
    else {
        Renderer::init(argv[3]);
    }

    drawable.animationState->getData()->setDefaultMix(0.f);
    drawable.skeleton->setPosition(0.f, 0.f);
    drawable.skeleton->setToSetupPose();
    drawable.update(0);
    Renderer::Clear();
    drawable.draw();
    saveFrameBuffer2Img("Z:/Cache/rest.png", Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);

    drawable.animationState->setAnimation(0, "default", true);


    while (true/*!glfwWindowShouldClose(window)*/)
    {
        Renderer::Clear();

        drawable.update(0.1);
        drawable.draw();

        static int count = 0;
        count++;
        char buffer[100]; // 需要确保这个buffer足够大以容纳转换后的字符串
        sprintf(buffer, "%d", count); // 将int转换为字符数组
        char* str = buffer; // 如果需要的是char*类型

        char path[100];
        strcpy(path, "Z:/Cache/");
        strcat(path, str);
        strcat(path, ".png");
        saveFrameBuffer2Img(path, Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);

    }

    return 0;
}