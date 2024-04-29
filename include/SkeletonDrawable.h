#pragma once
#include <spine/spine.h>
#include <glad/glad.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <texture.h>
namespace spine {

	class SkeletonDrawable {
	public:
		SkeletonDrawable(SkeletonData* skeletonData, AnimationStateData* animationStateData = nullptr);

		SkeletonDrawable(char* skeletonJsonPath, char* atlasPath);

		~SkeletonDrawable();

		void update(float delta);

		void draw();

		void stdoutAABB();

		std::map<int, std::tuple<std::unique_ptr<GLubyte[]>, Texture*>> GetRedrawTexImage(int format);

		Skeleton* skeleton;
		AnimationState* animationState;

		void SetNeedDrawAttachments(char* attachments);
		std::map<std::string, int> attachmentName2Index;
		std::map<std::string, int> attachmentName2Index_group;	//group for canny

		char* skeletonJsonPath;
		char* atlasPath;

	private:
		void init(SkeletonData* skeletonData, AnimationStateData* animationStateData);

		bool ownsAnimationStateData;
		Vector<float> worldVertices;
		Vector<int> glIndices;
		Vector<float> glVertices;
	};

	std::unique_ptr<GLubyte[]> GetTexImage(Texture* texture, int format);
}