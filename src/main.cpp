#include <glad/glad.h>
#include <spine/spine.h>
#include "stb_image_write.h"
#include <shader.h>
#include <GLFW/glfw3.h>
#include <renderer.h>
#include <texture.h>
#include <SkeletonDrawable.h>
#include <string>
#include <iostream>

#include <fstream>

void save_to_file(void* data, int lengthInBytes, const char* filename) {
    std::ofstream out(filename, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<char*>(data), lengthInBytes);
        out.close();
    }
    else {
        std::cerr << "Unable to open file " << filename << std::endl;
    }
}

using namespace spine;

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
        Renderer::init(argv[3], argv[4]);
    }


    //optional args
    for (int i = 5; i < argc; i++) {
        std::string arg = argv[i];
        std::string temp;
        if (arg == "-slots") {
            std::istringstream needDrawSlots(argv[++i]);
            while (std::getline(needDrawSlots, temp, ',')) {
                drawable.NeedDrawSlots.push_back(temp);
            }
		}
        else if (arg == "-frame"){
			// only render specific frame
            return 0;
		}
	}


    // assert uv mode
    std::unique_ptr<GLuint[]> oldPose, newPose;

    drawable.animationState->getData()->setDefaultMix(0.f);
    drawable.skeleton->setPosition(0.f, 0.f);
    drawable.skeleton->setToSetupPose();
    drawable.update(0);

    Renderer::Clear();
    drawable.draw();
    oldPose = Renderer::ReadPixelsUV();

    auto& anims = drawable.skeleton->getData()->getAnimations();
    for (int i = 0; i < anims.size(); i++) {
		drawable.animationState->setAnimation(0, anims[i], true);
        drawable.update(0);
        for (float time = 0.0, duration = anims[i]->getDuration(); time < duration; time += 0.1, drawable.update(0.1)) {
            Renderer::Clear();
            drawable.draw();
            newPose = Renderer::ReadPixelsUV();

            char buffer[100]; // 需要确保这个buffer足够大以容纳转换后的字符串
            sprintf(buffer, "%d", int(time*10)); // 将int转换为字符数组
            char path[100];
            strcpy(path, "Z:/Cache/");
            strcat(path, buffer);
            strcat(path, ".bin");

            save_to_file(oldPose.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * sizeof(GLuint), path);

            oldPose = std::move(newPose);
        }
	}

    return 0;
}