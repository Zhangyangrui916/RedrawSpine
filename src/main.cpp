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
#include <algorithm>
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
    // argv:
    // 1: skeleton.json
    // 2: atlas (fake.atlas is ok)
    // 3: "getAABB" | "cleanAttachment" | viewport string "x,y,w,h"
    // 4: output mode "uv" | "rgb" (when viewport is used)
    // 5: uvMapPath (output dir, used by analyzer pipeline)
    // flags:
    //   -attachments <list>
    //     format: draw_list,@,skip_list,@,group1@group1@...,group2@...
    //   -frame <png> : apply a frame result and render next pose
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


    // attachment 过滤与分组由 analyzer 传入
    for (int i = 6; i < argc; i++) {
        std::string arg = argv[i];
        if(0 == strcmp(argv[i], "-attachments")){
            drawable.SetNeedDrawAttachments(argv[++i]);
        }
    }

    auto slotIndex2Pixels = drawable.GetRedrawTexImage(GL_RGBA);

    for (int i = 6; i < argc; i++) {
        if (0 == strcmp(argv[i], "-frame")){  // 写 atlas 并渲染下一帧
            std::string renderResultPath = argv[++i];
            auto renderResult = stbi_load(renderResultPath.c_str(), &Renderer::SCR_WIDTH, &Renderer::SCR_HEIGHT, nullptr, STBI_rgb);

            //std::string softInpaintMaskPath = renderResultPath;
            //softInpaintMaskPath.replace(softInpaintMaskPath.find(".png"), 4, std::string("@adaptiveMask.png"));
            //auto softInpaintMask = stbi_load(softInpaintMaskPath.c_str(), &Renderer::SCR_WIDTH, &Renderer::SCR_HEIGHT, nullptr, STBI_grey);

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
            auto rgbslot = decodeUVToSlot(currentPoseUVMap.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
            stbi_write_png(std::string("rgbslot.png").c_str(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 3, rgbslot.get(), Renderer::SCR_WIDTH * 3);

            std::map<int, std::unique_ptr<GLubyte[]>> Id2WrittenMask;
            if (frame == 0) {   // gen new written mask
                for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
                    auto [pixels, texture] = convertToBinaryAlphaMask(pixels_texture);
                    Id2WrittenMask[Id] = std::move(pixels);
                }
            }
            else { // read old written mask
                for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
                    auto& [pixels, texture] = pixels_texture;
                    auto writtenMaskFileName = texture->path;
                    Id2WrittenMask[Id] = std::unique_ptr<GLubyte[]>((GLubyte*)load_from_file((writtenMaskFileName.replace(writtenMaskFileName.find(".png"), 4, std::string(".writtenMask"))).c_str()));
                    size_t size = texture->width * texture->height;
                    for (size_t i = 0; i < size; i++) {
						if (Id2WrittenMask[Id][i] == WrittenState::Flooded) {
                            Id2WrittenMask[Id][i] = WrittenState::NotWritten;
						}
					}
                }
            }

            auto start = std::chrono::high_resolution_clock::now();
            rgb2hsv(renderResult, Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
            auto end = std::chrono::high_resolution_clock::now();
            auto timeelapsed =  std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << timeelapsed <<std::endl;

            // 剔除边缘像素（UV 邻域存在其他部件时，RGB 梯度往往异常）
            // 同时剔除 writtenMask 已经写过的像素，避免重复覆盖
            {
                auto out = std::make_unique<GLubyte[]>(Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT);
                std::fill_n(out.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT, 0);
                int windowSize = 5;
                for (int i = 1; i < Renderer::SCR_HEIGHT-1; i++) {
                    for (int j = 1; j < Renderer::SCR_WIDTH-1; j++) {
                        int index = i * Renderer::SCR_WIDTH + j;
                        unsigned char red = renderResult[index * 3];
                        unsigned char green = renderResult[index * 3 + 1];
                        unsigned char blue = renderResult[index * 3 + 2];

                        // 查看周围是否有不同 slot 的像素（用于边缘判断）
                        unsigned int Id_V_U = currentPoseUVMap[index];
                        if (Id_V_U == 0) continue;

                        // 检查是否已经写过
                        unsigned int myId = Id_V_U >> 24;
                        unsigned int AttachmentPixelIndex = Id_V_U & 0xFFFFFF;
                        //if (Id2WrittenMask[myId][AttachmentPixelIndex] == WrittenState::Written) {
                        //    currentPoseUVMap[index] = 0;
                        //}
                        
                        if (Id2WrittenMask[myId][AttachmentPixelIndex] == WrittenState::Written /*&& softInpaintMask[index] < 128*/ ) {
                            currentPoseUVMap[index] = 0;
                        }

                        for(int ii=-2;ii<=2;ii++){
							for(int jj=-2;jj<=2;jj++){
                                if(i+ii<0 || i+ii>=Renderer::SCR_HEIGHT || j+jj<0 || j+jj>=Renderer::SCR_WIDTH) continue;
								int neighborIndex = (i + ii) * Renderer::SCR_WIDTH + j + jj;
								if (ii == 0 && jj == 0) continue;
								if ((currentPoseUVMap[neighborIndex] >> 24) != myId) {
                                    goto THIS_PIXEL_AT_EDGE;
								}
							}
						}
                        continue;   // if not at edge, don't handle it
THIS_PIXEL_AT_EDGE:

                        // 计算像素梯度（用于判断边缘）
                        auto value = renderResult[index * 3 + 2];
                        if (value < 32) {
                            out[index] = 255;
                            continue;
                        }

                        int gradValX = renderResult[(index - 1) * 3 + 2] - renderResult[(index + 1) * 3 + 2];
                        int gradValY = renderResult[(index - Renderer::SCR_WIDTH) * 3 + 2] - renderResult[(index + Renderer::SCR_WIDTH) * 3 + 2];
                        int gradVal = gradValX * gradValX + gradValY * gradValY;

                        int gradHueX = abs(renderResult[(index - 1) * 3] - renderResult[(index + 1) * 3]);
                        if(gradHueX > 128) gradHueX = 256 - gradHueX;
                        int gradHueY = abs(renderResult[(index - Renderer::SCR_WIDTH) * 3] - renderResult[(index + Renderer::SCR_WIDTH) * 3]);
                        if (gradHueY > 128) gradHueY = 256 - gradHueY;
                        int gradHue = gradHueX * gradHueX + gradHueY * gradHueY;
  
                        if (gradVal > 3000 || gradHue > 3000) {
                            out[index] = 255;
                            continue;
                        }

                        int mini = std::max(0, i - windowSize);
                        int maxi = std::min(Renderer::SCR_HEIGHT - 1, i + windowSize);
                        int minj = std::max(0, j - windowSize);
                        int maxj = std::min(Renderer::SCR_WIDTH - 1, j + windowSize);


                        int sameslotcount = 0, notsameslotcount = 0;
                        for (int ii = mini; ii <= maxi; ii++) {
                            for (int jj = minj; jj <= maxj; jj++) {
								int neighborIndex = ii * Renderer::SCR_WIDTH + jj;
								if (ii == i && jj == j) continue;
                                unsigned int Id_V_U_neighbor = currentPoseUVMap[neighborIndex];
                                int deltaH = renderResult[neighborIndex * 3] - red;
                                int deltaS = renderResult[neighborIndex * 3 + 1] - green;
                                int deltaBlue = renderResult[neighborIndex * 3 + 2] - blue;
                                if (/*deltaBlue * deltaBlue*/ + deltaS * deltaS + deltaH * deltaH < 200) {
                                    if ((Id_V_U_neighbor & 0xFF000000) != (Id_V_U & 0xFF000000)) {
										notsameslotcount++;
                                    }
                                    else {
                                        sameslotcount++;
                                    }
								}

							}
						}
                        if (notsameslotcount != 0 && notsameslotcount + 3 > sameslotcount) {
                            out[index] = 255;
                        }
                    }
                }
                //stbi_write_png(std::string("out.png").c_str(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, out.get(), Renderer::SCR_WIDTH * 1);
                for(int i=0; i< Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT; i++){
					if(out[i] == 255){
                        currentPoseUVMap[i] = 0;
					}
				}
            }
            hsv2rgb(renderResult, Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);

            constexpr int SAME_ATTA_THRESHOLD = 40;
            start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < Renderer::SCR_HEIGHT; i++) {
                for (int j = 0; j < Renderer::SCR_WIDTH; j++) {
                    int index = i * Renderer::SCR_WIDTH + j;
                    unsigned int Id_V_U = currentPoseUVMap[index];
                    if (Id_V_U == 0) continue;

                    unsigned int AttachmentPixelIndex = Id_V_U & 0xFFFFFF;
                    unsigned int Id = (Id_V_U >> 24) & 0xFF;

                    //if (softInpaintMask[index] < 128) {
                        if (Id2WrittenMask[Id][AttachmentPixelIndex] == WrittenState::Written) continue;
                        Id2WrittenMask[Id][AttachmentPixelIndex] = WrittenState::Written;
                    //}
                    //else {
                    //    //overwrite it anyway
                    //}

                    auto& [pixels, _] = slotIndex2Pixels[Id];
                    pixels[AttachmentPixelIndex * 4 + 0] = renderResult[index * 3 + 0];
                    pixels[AttachmentPixelIndex * 4 + 1] = renderResult[index * 3 + 1];
                    pixels[AttachmentPixelIndex * 4 + 2] = renderResult[index * 3 + 2];
                }
            }
            end = std::chrono::high_resolution_clock::now();
            timeelapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << timeelapsed << std::endl;

            // 用着色像素填充白色像素（仅前几帧需要，避免明显白洞）
            if (frame < 3) {
                if (frame == 0) {
                    //clean outliner because they mess up the floodfill
                    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
                        auto& [pixels, texture] = pixels_texture;
                        cleanOutliner(pixels.get(), texture->width, texture->height);
                    }
                }

                //for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
                //    auto& [pixels, texture] = pixels_texture;
                //    auto lastSlashPos = texture->path.find_last_of("/\\");
                //    if (lastSlashPos != std::string::npos) {
                //        stbi_write_png((texture->path.substr(lastSlashPos + 1)).c_str(), texture->width, texture->height, 4, pixels.get(), texture->width * 4);
                //    }
                //}


                for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
                    auto& [pixels, texture] = pixels_texture;
                    floodWhitePixelWithNeighborColor(pixels.get(), texture->width, texture->height, Id2WrittenMask[Id].get());
                    //floodWhitePixelWithNeighborColorCPU(pixels.get(), texture->width, texture->height, Id2WrittenMask[Id].get());
                }
            }

            for (auto& [Id, WrittenMask] : Id2WrittenMask) {
                auto& [pixels, texture] = slotIndex2Pixels[Id];
				auto atlasPath = (texture->path);
				size_t start_pos = atlasPath.find(".png");
				if (start_pos != std::string::npos) {
					atlasPath.replace(start_pos, 4, std::string(".writtenMask"));
				}
				save_to_file(WrittenMask.get(), texture->width * texture->height, atlasPath.c_str());
            }

            for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
				auto& [pixels, texture] = pixels_texture;
				auto& atlasPath = (texture->path);
    //            size_t start_pos = atlasPath.find(".png");
    //            if (start_pos != std::string::npos) {
				//	atlasPath.replace(start_pos, 4, std::string("@2") + std::string(".png"));
				//}
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



                Renderer::StartDrawUV();
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                for (auto& [Id, pixels] : Id2WrittenMask) {
                    auto& [_, texture] = slotIndex2Pixels[Id];
                    glBindTexture(GL_TEXTURE_2D, texture->textureID);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
                }
                Renderer::Clear();
                Renderer::shader->setBool("u_OnlyUndrawn", true);
                drawable.draw();
                auto& uvPixels = Renderer::ReadPixelsR32UI();
                //std::string uvMappingPath = uvMapPath + std::string("/") + animationName + "_" + time + ".bin";
                //save_to_file(uvPixels.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, uvMappingPath.c_str());


                auto nextMask = decodeUVToMask(uvPixels.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
                stbi_write_png((uvMapPath + std::string("/") + animationName + "_" + time + "@mask.png").c_str(),
                    Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, nextMask.get(), Renderer::SCR_WIDTH);
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


    // ========================  preprocess  寻找 keypose  ==============================
    // 0. 修改 texture。透明=0，非透明=255，然后更新到 GPU（阈值为 LOW_ALPHA_THRESHOLD）
    for (auto& [Id, pixels_texture] : slotIndex2Pixels) {
        slotIndex2Pixels[Id] = convertToBinaryAlphaMask(std::move(pixels_texture));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (auto& [_, pixels_texture] : slotIndex2Pixels) {
        auto& [pixels, texture] = pixels_texture;
        glBindTexture(GL_TEXTURE_2D, texture->textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
    }

    // 1.0 渲染 rest pose 的 UV
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

    // 1.1 mask
    stbi_write_png((uvMapPath + std::string("/restPose@mask.png")).c_str(),
        Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, decodeUVToMask(restPose.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT).get(), Renderer::SCR_WIDTH);

    // 1.2 渲染 rest pose 的 ControlNet 输入
    Renderer::StartDrawCTRL();
    Renderer::Clear();
    std::swap(drawable.attachmentName2Index, drawable.attachmentName2Index_group);
    drawable.draw();
    stbi_write_png((uvMapPath + std::string("/restPose@ctrl.png")).c_str(),
        Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, Renderer::ReadPixelsR8UI().get(), Renderer::SCR_WIDTH * 1);
    std::swap(drawable.attachmentName2Index, drawable.attachmentName2Index_group);
    Renderer::EndDrawCTRL();

    const int maxIterations = 9;
    const float minGainRatio = 0.015f;
    const unsigned int minGain = 512;
    unsigned int firstGain = 0;
    int iteration = 0;
    for (std::unique_ptr<GLuint[]> uvPose = std::move(restPose), nextuvPose; uvPose; uvPose = std::move(nextuvPose)) {
        iteration++;
        if (iteration > maxIterations) {
            break;  // 达到最大迭代次数
        }
        
        // 2. 对于所有uvPose上出现过的uv，设为128   // 对于部件边缘的像素，因为不可靠，所以假设我们没画过，跳过设置，以求下次inpaint再画
        for (int i = 0; i < Renderer::SCR_HEIGHT; i++) {
            for (int j = 0; j < Renderer::SCR_WIDTH; j++) {
                int index = i * Renderer::SCR_WIDTH + j;
                unsigned int Id_V_U = uvPose[index];
                if (Id_V_U == 0) continue;

     //           //检查邻域是否有其他部件的像素, 若是则跳过
     //           if (index - 1 > 0) {
     //               unsigned int Id_V_U_left = uvPose[index - 1];
     //               if (Id_V_U_left != 0 && (Id_V_U_left & 0xFF000000) != (Id_V_U & 0xFF000000)) {
					//	continue;
					//}
     //           }
     //           if (index + 1 < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
					//unsigned int Id_V_U_right = uvPose[index + 1];
					//if (Id_V_U_right != 0 && (Id_V_U_right & 0xFF000000) != (Id_V_U & 0xFF000000)) {
     //                   continue;
     //               }
     //           }
     //           if (index - Renderer::SCR_WIDTH > 0) {
     //               unsigned int Id_V_U_up = uvPose[index - Renderer::SCR_WIDTH];
     //               if (Id_V_U_up != 0 && (Id_V_U_up & 0xFF000000) != (Id_V_U & 0xFF000000)) {
					//	continue;
					//}
     //           }
     //           if (index + Renderer::SCR_WIDTH < Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT) {
					//unsigned int Id_V_U_down = uvPose[index + Renderer::SCR_WIDTH];
     //               if (Id_V_U_down != 0 && (Id_V_U_down & 0xFF000000) != (Id_V_U & 0xFF000000)) {
     //                   continue;
     //               }
     //           }
                unsigned int AttachmentPixelIndex = Id_V_U & 0xFFFFFF;
                unsigned int Id = (Id_V_U >> 24) & 0xFF;
                auto& [pixels, texture] = slotIndex2Pixels[Id];
                pixels[AttachmentPixelIndex] = 128;
            }
        }

        Renderer::shader->setBool("u_OnlyUndrawn", true);
        // 3.更新texture到GPU，再渲染每帧。只渲染没画过的部分
        // red 语义：未绘制=1.0(255)，已绘制=0.5(128)；shader 中 u_OnlyUndrawn 只保留 red>=0.55 的像素
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (auto& [_, pixels_texture] : slotIndex2Pixels) {
            auto& [pixels, texture] = pixels_texture;
            glBindTexture(GL_TEXTURE_2D, texture->textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texture->width, texture->height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
        }

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
        if (firstGain == 0) {
            firstGain = maxcount;
        }
        unsigned int dynamicMinGain = std::max(minGain, static_cast<unsigned int>(firstGain * minGainRatio));
        if (maxcount < dynamicMinGain) {
            break;  // 本轮新增像素太少，收益已明显下降
        }
        std::string uvMappingPath = uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + ".bin";
        sequence << uvMappingPath << std::endl;

        drawable.animationState->setAnimation(0, animName, true);
        drawable.update(animTime);

        Renderer::shader->setBool("u_OnlyUndrawn", false);
        Renderer::Clear();
        drawable.draw();

        nextuvPose = Renderer::ReadPixelsR32UI();

        save_to_file(nextuvPose.get(), Renderer::SCR_WIDTH * Renderer::SCR_HEIGHT * 4, uvMappingPath.c_str());
        //auto nextMask = decodeUVToMask(nextuvPose.get(), Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT);
        //stbi_write_png((uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + "@mask.png").c_str(),
        //    Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, nextMask.get(), Renderer::SCR_WIDTH);
        
        Renderer::StartDrawCTRL();
        Renderer::Clear();
        std::swap(drawable.attachmentName2Index, drawable.attachmentName2Index_group);
        drawable.draw();
        std::unique_ptr<GLubyte[]> CTRL = Renderer::ReadPixelsR8UI();
        stbi_write_png((uvMapPath + std::string("/") + animName + "_" + std::to_string(animTime) + "@ctrl.png").c_str(),
            			Renderer::SCR_WIDTH, Renderer::SCR_HEIGHT, 1, CTRL.get(), Renderer::SCR_WIDTH * 1);
        std::swap(drawable.attachmentName2Index, drawable.attachmentName2Index_group);
        Renderer::EndDrawCTRL();

    }
    std::ofstream outputFile(uvMapPath + std::string("/sequence.txt"));
    outputFile << sequence.str();
    outputFile.close();

    return 0;
}
