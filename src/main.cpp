#include <glad/glad.h>
#include <spine/spine.h>
#include <stb_image.h>
#include "stb_image_write.h"
#include <shader.h>
#include <GLFW/glfw3.h>
#include <renderer.h>
#include <texture.h>
#include <SkeletonDrawable.h>

// settings
unsigned int SCR_WIDTH = 1000;
unsigned int SCR_HEIGHT = 1000;

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

    // glfw window creation
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "spine", NULL, NULL);
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


    Renderer::init();

    // build and compile shaders
    Renderer::shader = new Shader("C:/code/mask-analyzer/src/test.vs", "C:/code/mask-analyzer/src/test.fs");
    Renderer::shader->use();

    OGLTextureLoader textureLoader;
    spine::Atlas atlas("C:/code/mask-analyzer/build/Debug/data/fake.atlas", (spine::TextureLoader*)&textureLoader);
    spine::AtlasAttachmentLoader attachmentLoader(&atlas);
    spine::SkeletonJson json(&attachmentLoader);
    spine::SkeletonData* skeletonData = json.readSkeletonDataFile("C:/code/mask-analyzer/build/Debug/data/leidian.json");
    spine::SkeletonDrawable drawable(skeletonData);
    drawable.animationState->getData()->setDefaultMix(0.f);
    drawable.skeleton->setPosition(0.f, 0.f);
    drawable.skeleton->setToSetupPose();
    drawable.update(0);
    drawable.animationState->setAnimation(0, "default", true);

    spine::Bone::setYDown(false);

    float lastFrame = static_cast<float>(glfwGetTime());
    // render loop
    // -----------
    while (true/*!glfwWindowShouldClose(window)*/)
    {
        Renderer::Clear();

        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;



        drawable.update(deltaTime);
        drawable.draw();
        static float minx = 999, miny = 999, maxw = 0, maxh = 0;
        float x, y, w, h;
        Vector<float> vertices;
        drawable.skeleton->getBounds(x, y, w, h, vertices);
        minx = std::min(minx, x);
        miny = std::min(miny, y);
        maxw = std::max(w, maxw);
        maxh = std::max(h, maxh);
        std::cout << "minx: " << minx << " miny: " << miny << " w: " << maxw << " h: " << maxh << std::endl;

        saveFrameBuffer2Img("Z:/Cache/leidian.png", SCR_WIDTH, SCR_HEIGHT);


        glfwSwapBuffers(window);
    }


    return 0;
}