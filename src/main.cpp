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
#include <sstream>
#include <chrono>
#include "utility.cuh"
#include <stb_image.h>

namespace cleanAttachment {
    void init();
    void clean(spine::Attachment* attachment);
}

static void save_to_file(void* data, int lengthInBytes, const char* filename) {
    std::ofstream out(filename, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<char*>(data), lengthInBytes);
        out.close();
    }
    else {
        std::cerr << "Unable to open file " << filename << std::endl;
    }
}

static char* load_from_file(const char* filename) {
    std::ifstream in(filename, std::ios::binary);
    if (in.is_open()) {
        // 获取文件大小
        in.seekg(0, std::ios::end);
        int lengthInBytes = in.tellg();
        in.seekg(0, std::ios::beg);

        // 创建缓冲区并读取文件内容
        char* data = new char[lengthInBytes];
        in.read(data, lengthInBytes);
        in.close();
        return data;
    }
    else {
        std::cerr << "Unable to open file " << filename << std::endl;
        return nullptr;
    }
}

static std::pair<std::string, std::string> extractFileName(const std::string& filePath) {
    size_t pos = filePath.find_last_of("/\\");
    if (pos != std::string::npos) {
        auto directory = filePath.substr(0, pos);
        return { filePath.substr(pos + 1), directory };
    }
    return { filePath, {} };
}

static std::pair<int, std::string> extractIntAndDirectoryFromFileName(const std::string& filePath) {
	auto&[fileName, directory] = extractFileName(filePath);
	size_t dotPos = fileName.find_last_of(".");
	if (dotPos != std::string::npos)
		fileName = fileName.substr(0, dotPos);

	// 将文件名转换为整数
	int fileNumber;
    try {
		fileNumber = std::stoi(fileName);
		std::cout << "File number: " << fileNumber << std::endl;
	}
    catch (const std::invalid_argument& e) {
		std::cerr << "Invalid file name format: " << e.what() << std::endl;
	}
    catch (const std::out_of_range& e) {
		std::cerr << "File number out of range: " << e.what() << std::endl;
	}
    return { fileNumber, directory };
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
    else if (strcmp(argv[3], "cleanAttachment") == 0) {
        cleanAttachment::init();
        auto& entries = drawable.skeleton->getData()->getDefaultSkin()->getAttachments();
        while (entries.hasNext()) {
            cleanAttachment::clean(entries.next()._attachment);
        }
        return 0;
    }
    else {
        Renderer::init(argv[3], argv[4]);
    }

    char* uvMapPath = argv[5];


    for (int i = 6; i < argc; i++) {
        std::string arg = argv[i];
        std::string temp;
        if(0 == strcmp(argv[i], "-attachments")){
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
    }

    auto slotIndex2Pixels = drawable.GetRedrawTexImage(GL_RGBA);

    for (int i = 6; i < argc; i++) {
        if (0 == strcmp(argv[i], "-frame")){  //写atlas并渲染下一帧
            std::string renderResultPath = argv[++i];
            auto renderResult = stbi_load(renderResultPath.c_str(), &Renderer::SCR_WIDTH, &Renderer::SCR_HEIGHT, nullptr, 0);
            auto&[frame, directory] = extractIntAndDirectoryFromFileName(renderResultPath);
            auto& [currentPoseUVMapPath, nextposePath] = [&]() -> std::pair<std::string, std::string> {
                std::ifstream file(uvMapPath + std::string("/sequence.txt"));
                std::string line1, line2;
                for (int i = 0; i <= frame; ++i) std::getline(file, line1);
                std::getline(file, line2);
                file.close();
                return {line1, line2};
            }();
            std::unique_ptr<GLuint[]> currentPoseUVMap((GLuint*)(load_from_file(currentPoseUVMapPath.c_str())));

            constexpr int SAME_ATTA_THRESHOLD = 40;

            for (int i = 0; i < Renderer::SCR_HEIGHT; i++) {
                for (int j = 0; j < Renderer::SCR_WIDTH; j++) {
                    int index = i * Renderer::SCR_WIDTH + j;
                    unsigned int Id_V_U = currentPoseUVMap[index];
                    if (Id_V_U == 0) continue;

                    //检查邻域是否有其他部件的像素, 若是则检查反向像素颜色是否一致，若不一致则跳过
                    if (index - 1 > 0) {
						unsigned int Id_V_U_left = currentPoseUVMap[index - 1];
                        if ((Id_V_U_left & 0xFF000000) != (Id_V_U & 0xFF000000)) {
							int deltaR = renderResult[index * 3 + 0] - renderResult[(index + 1) * 3 + 0];
                            int deltaG = renderResult[index * 3 + 1] - renderResult[(index + 1) * 3 + 1];
                            int deltaB = renderResult[index * 3 + 2] - renderResult[(index + 1) * 3 + 2];
                            if (deltaR * deltaR + deltaG * deltaG + deltaB * deltaB > SAME_ATTA_THRESHOLD) {
								continue;
							}
						}
					}
                    if (index + 1 < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
                        unsigned int Id_V_U_right = currentPoseUVMap[index + 1];
                        if ((Id_V_U_right & 0xFF000000) != (Id_V_U & 0xFF000000)) {
							int deltaR = renderResult[index * 3 + 0] - renderResult[(index - 1) * 3 + 0];
							int deltaG = renderResult[index * 3 + 1] - renderResult[(index - 1) * 3 + 1];
							int deltaB = renderResult[index * 3 + 2] - renderResult[(index - 1) * 3 + 2];
                            if (deltaR * deltaR + deltaG * deltaG + deltaB * deltaB > SAME_ATTA_THRESHOLD) {
								continue;
							}
						}
                    }
                    if (index - Renderer::SCR_WIDTH > 0) {
						unsigned int Id_V_U_up = currentPoseUVMap[index - Renderer::SCR_WIDTH];
                        if ((Id_V_U_up & 0xFF000000) != (Id_V_U & 0xFF000000)) {
                            int deltaR = renderResult[index * 3 + 0] - renderResult[(index + Renderer::SCR_WIDTH) * 3 + 0];
                            int deltaG = renderResult[index * 3 + 1] - renderResult[(index + Renderer::SCR_WIDTH) * 3 + 1];
                            int deltaB = renderResult[index * 3 + 2] - renderResult[(index + Renderer::SCR_WIDTH) * 3 + 2];
                            if (deltaR * deltaR + deltaG * deltaG + deltaB * deltaB > SAME_ATTA_THRESHOLD) {
								continue;
							}
                        }
                    }
                    if (index + Renderer::SCR_WIDTH < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
                        unsigned int Id_V_U_down = currentPoseUVMap[index + Renderer::SCR_WIDTH];
                        if ((Id_V_U_down & 0xFF000000) != (Id_V_U & 0xFF000000)) {
							int deltaR = renderResult[index * 3 + 0] - renderResult[(index - Renderer::SCR_WIDTH) * 3 + 0];
							int deltaG = renderResult[index * 3 + 1] - renderResult[(index - Renderer::SCR_WIDTH) * 3 + 1];
							int deltaB = renderResult[index * 3 + 2] - renderResult[(index - Renderer::SCR_WIDTH) * 3 + 2];
                            if (deltaR * deltaR + deltaG * deltaG + deltaB * deltaB > SAME_ATTA_THRESHOLD) {
								continue;
							}
						}
                    }

                    unsigned int AttachmentPixelIndex = Id_V_U & 0xFFFFFF;
                    unsigned int Id = (Id_V_U >> 24) & 0xFF;
                    auto& [pixels, texture] = slotIndex2Pixels[Id];
                    int width = texture->width;
                    pixels[AttachmentPixelIndex * 4 + 0] = renderResult[index * 3 + 0];
                    pixels[AttachmentPixelIndex * 4 + 1] = renderResult[index * 3 + 1];
                    pixels[AttachmentPixelIndex * 4 + 2] = renderResult[index * 3 + 2];
                }
            }

            //if (frame == 0) {
            //    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
            //        auto& [pixels, texture] = pixels_texture;
            //        floodWhitePixelWithNeighborColor(pixels.get(), texture->width, texture->height);
            //    }
            //}

            for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
				auto& [pixels, texture] = pixels_texture;
				auto& atlasPath = (texture->path);
                size_t start_pos = atlasPath.find(".png");
                if (start_pos != std::string::npos) {
					atlasPath.replace(start_pos, 4, std::string("_2") + std::string(".png"));
				}
                stbi_write_png(atlasPath.c_str(), texture->width, texture->height, 4, pixels.get(), texture->width * 4);
                glBindTexture(GL_TEXTURE_2D, texture->textureID);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
			}


            size_t pos = nextposePath.find_last_of("/\\");
            if (pos != std::string::npos) {
				nextposePath = nextposePath.substr(pos + 1);
            }
            size_t dotPos = nextposePath.find_last_of(".");
            if (dotPos != std::string::npos)
                nextposePath = nextposePath.substr(0, dotPos);
            size_t _Pos = nextposePath.find_first_of("_");
            if (_Pos != std::string::npos) {
                auto animationName = nextposePath.substr(0, _Pos);
                auto time = nextposePath.substr(_Pos + 1);
                drawable.animationState->setAnimation(0, animationName.c_str(), true);
                drawable.update(std::stof(time));
                Renderer::Clear();
                drawable.draw();
                auto& pixels = Renderer::ReadPixelsRGBA();
                std::string nextFramePath = directory + std::string("/") + std::to_string(++frame) + "_.png";
                stbi_write_png(nextFramePath.c_str(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 4, pixels.get(), Renderer::SCR_WIDTH * 4);
            }
            else {
                std::cout << "fail to get animation name and time from file name\n";
                if (nextposePath == "") {
                    std::cout << "reach the end";
                }
            }
            return 0;
		}
	}


    // 寻找keypose
    // 0. 修改texture. 透明为0，非透明255. 然后更新到GPU. (透明与非透明的阈值为 LOW_ALPHA_THRESHOLD)
    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
        slotIndex2Pixels[Id] = convertToSingleChannelOnAlpha(std::move(pixels_texture));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
        auto& [pixels, texture] = pixels_texture;
        glBindTexture(GL_TEXTURE_2D, texture->textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
    }
    // 0.5 visualize texture
    //for (auto& [Id, textureWH] : slotIndex2Pixels) {
    //    auto& [pixel, texture] = textureWH;
    //    char buffer[100];
    //    sprintf(buffer, "%d.png", Id);
    //    stbi_write_png(buffer, texture->width, texture->height, 1, pixel.get(), texture->width * 1);
    //}

    // 1.渲染restposeUV
    drawable.animationState->getData()->setDefaultMix(0.f);
    drawable.skeleton->setPosition(0.f, 0.f);
    drawable.skeleton->setToSetupPose();
    drawable.update(0);
    Renderer::Clear();
    drawable.draw();
    std::unique_ptr<GLuint[]> restPose = Renderer::ReadPixelsR32UI();
    std::string restPoseUVmapPath = uvMapPath + std::string("/restPose.bin");
    std::ostringstream sequence;
    sequence << restPoseUVmapPath << std::endl;
    save_to_file(restPose.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, restPoseUVmapPath.c_str());
    //auto rgb = decodeUVToRGB(restPose.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
    //stbi_write_png("uv/restPose.png", Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 3, rgb.get(), Renderer::SCR_WIDTH * 3);
    for (std::unique_ptr<GLuint[]> uvPose = std::move(restPose), nextuvPose; uvPose; uvPose = std::move(nextuvPose)) {
        
        // 2. 对于所有uvPose上出现过的uv，设为128   // 对于部件边缘的像素，因为不可靠，所以假设我们没画过，跳过设置，以求下次inpaint再画
        for (int i = 0; i < Renderer::SCR_HEIGHT; i++) {
            for (int j = 0; j < Renderer::SCR_WIDTH; j++) {
                int index = i * Renderer::SCR_WIDTH + j;
                unsigned int Id_V_U = uvPose[index];
                if (Id_V_U == 0) continue;

                //检查邻域是否有其他部件的像素, 若是则跳过
                if (index - 1 > 0) {
                    unsigned int Id_V_U_left = uvPose[index - 1];
                    if (Id_V_U_left != 0 && (Id_V_U_left & 0xFF000000) != (Id_V_U & 0xFF000000)) {
						continue;
					}
                }
                if (index + 1 < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
					unsigned int Id_V_U_right = uvPose[index + 1];
					if (Id_V_U_right != 0 && (Id_V_U_right & 0xFF000000) != (Id_V_U & 0xFF000000)) {
                        continue;
                    }
                }
                if (index - Renderer::SCR_WIDTH > 0) {
                    unsigned int Id_V_U_up = uvPose[index - Renderer::SCR_WIDTH];
                    if (Id_V_U_up != 0 && (Id_V_U_up & 0xFF000000) != (Id_V_U & 0xFF000000)) {
						continue;
					}
                }
                if (index + Renderer::SCR_WIDTH < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
					unsigned int Id_V_U_down = uvPose[index + Renderer::SCR_WIDTH];
                    if (Id_V_U_down != 0 && (Id_V_U_down & 0xFF000000) != (Id_V_U & 0xFF000000)) {
                        continue;
                    }
                }
                unsigned int AttachmentPixelIndex = Id_V_U & 0xFFFFFF;
                unsigned int Id = (Id_V_U >> 24) & 0xFF;
                auto& [pixels, texture] = slotIndex2Pixels[Id];
                int width = texture->width;
                pixels[AttachmentPixelIndex] = 128;
            }
        }

        Renderer::shader->setBool("u_OnlyUndrawn", true);
        // 3.更新texture到GPU，再渲染每帧。只渲染没画过的部分
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
            auto& [pixels, texture] = pixels_texture;
            glBindTexture(GL_TEXTURE_2D, texture->textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
        }
        // 3.5 visualize texture
        //for (auto& [Id, textureWH] : slotIndex2Pixels) {
        //    auto& [pixel, texture] = textureWH;
        //    char buffer[100];
        //    sprintf(buffer, "%d.png", Id);
        //    stbi_write_png(buffer, texture->width, texture->height, 1, pixel.get(), texture->width * 1);
        //}

        constexpr float frameTime = 0.1f;
        unsigned int maxcount = 0;
        const char* animName;
        float animTime;
        for (auto& animation : drawable.skeleton->getData()->getAnimations()) {
            float duration = animation->getDuration();
            drawable.animationState->setAnimation(0, animation->getName().buffer(), true);
            drawable.update(0);
            for (float time = 0; time < duration; time += frameTime, drawable.update(frameTime)) {
                Renderer::Clear();
                drawable.draw();
                std::unique_ptr<GLuint[]> pixels = Renderer::ReadPixelsR32UI();
                unsigned int count = countNotZero(pixels.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT);
                if (count > maxcount) {
                    maxcount = count;
                    animName = animation->getName().buffer();
                    animTime = time;
                    //nextuvPose = std::move(pixels);
                }
            }
        }
        if (maxcount < 256) {
            break;  //too few pixels
        }
        std::string uvMappingPath = uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + ".bin";
        sequence << uvMappingPath << std::endl;

        drawable.animationState->setAnimation(0, animName, true);
        drawable.update(animTime);

        //Renderer::shader->setBool("u_OnlyUndrawn", false);
        Renderer::Clear();
        drawable.draw();

        nextuvPose = Renderer::ReadPixelsR32UI();

        save_to_file(nextuvPose.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, uvMappingPath.c_str());
        auto nextMask = decodeUVToMask(nextuvPose.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
        stbi_write_png((uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + "@mask.png").c_str(),
            Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, nextMask.get(), Renderer::SCR_WIDTH);
        
        Renderer::StartDrawCTRL();
        Renderer::Clear();
        drawable.animationState->setAnimation(0, animName, true);
        drawable.update(animTime);
        drawable.draw();
        std::unique_ptr<GLubyte[]> CTRL = Renderer::ReadPixelsR8UI();
        stbi_write_png((uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + "@ctrl.png").c_str(),
            			Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, CTRL.get(), Renderer::SCR_WIDTH * 1);
        Renderer::EndDrawCTRL();

    }
    std::ofstream outputFile(uvMapPath + std::string("/sequence.txt"));
    outputFile << sequence.str();
    outputFile.close();

    return 0;
}