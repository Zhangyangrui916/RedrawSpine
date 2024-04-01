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
#include <chrono>
#include "utility.cuh"

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
        if (arg == "-attachments") {
            std::istringstream needDrawSlots(argv[++i]);
            auto* attachments = &drawable.NeedDrawAttachments;
            while (std::getline(needDrawSlots, temp, ',')) {
                if (temp == "@") {
                    attachments = &drawable.SkipRedrawAttachments;
                    continue;
                }
                attachments->push_back(temp);
            }
		}
        else if (arg == "-frame"){
			// only render specific frame
            return 0;
		}
	}

    // 寻找keypose  //assert(argv[4] == "uv")
    // 
    // 1.渲染restposeUV
    drawable.animationState->getData()->setDefaultMix(0.f);
    drawable.skeleton->setPosition(0.f, 0.f);
    drawable.skeleton->setToSetupPose();
    drawable.update(0);
    Renderer::Clear();
    drawable.draw();
    std::unique_ptr<GLuint[]> restPose = Renderer::ReadPixelsR32UI();
    save_to_file(restPose.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, "restpose.bin");

    // 2. 修改texture. 透明为0，非透明255， 然后对于所有restpose上出现过的uv，设为128
    auto slotIndex2Pixels = drawable.GetRedrawTexImage(GL_RGBA);
    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
        auto& [pixels, texture] = pixels_texture;
        auto singlechan = convertToSingleChannelOnAlpha(std::move(pixels_texture));
        slotIndex2Pixels[Id] = std::move(singlechan);
    }
    long long start = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (int i = 0; i < Renderer::SCR_HEIGHT; i++) {
        for (int j = 0; j < Renderer::SCR_WIDTH; j++) {
			int index = i*Renderer::SCR_WIDTH + j;
            unsigned int Id_V_U = restPose[index];
            if(Id_V_U == 0) continue; 
            unsigned int U = Id_V_U & 0xFFF;
            unsigned int V = (Id_V_U >> 12) & 0xFFF;
            unsigned int Id = (Id_V_U >> 24) & 0xFF;
            auto& [pixels, texture] = slotIndex2Pixels[Id];
            int width = texture->width;
			pixels[(V * width + U)] = 127;
		}
	}
    long long end = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::cout << "overwrite texture Time: " << end - start << "ms" << std::endl;

    // 3.更新texture到GPU，再渲染每帧。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
		auto& [pixels, texture] = pixels_texture;
        glBindTexture(GL_TEXTURE_2D, texture->textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
	}
    Renderer::UpdateShader("C:/code/mask-analyzer/src/vs.glsl", "C:/code/mask-analyzer/src/uv_redraw.glsl");

    start = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto& animations = drawable.skeleton->getData()->getAnimations();
    constexpr float frameTime = 0.1f;
    for (auto& animation : animations) {
		float duration = animation->getDuration();
		drawable.animationState->setAnimation(0, animation->getName().buffer(), true);
		drawable.update(0);
        for (float time = 0; time < duration; time += frameTime, drawable.update(frameTime)) {
			Renderer::Clear();
			drawable.draw();
			std::unique_ptr<GLuint[]> pixels = Renderer::ReadPixelsR32UI();
			char buffer[100];
			sprintf(buffer, "Z:/Cache/%s_%.2f.bin", animation->getName().buffer(), time);
            save_to_file(pixels.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, buffer);
		}
	}   
    end = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::cout << "Time: " << end - start << "ms" << std::endl;

    //for (auto& [Id, textureWH] : slotIndex2Pixels) {
    //    auto& [texture, width, height] = textureWH;
    //    char buffer[100];
    //    sprintf(buffer, "%d.png", Id);
    //    stbi_write_png(buffer, width, height, 1, texture.get(), width * 1); //stbi_write_png(buffer, width, height, 4, texture.get(), width * 4);
    //}

    return 0;
}