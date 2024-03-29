#pragma once
#include <spine/spine.h>
#include <vector>
#include <string>
namespace spine {

	class SkeletonDrawable {
	public:
		SkeletonDrawable(SkeletonData* skeletonData, AnimationStateData* animationStateData = nullptr);

		SkeletonDrawable(char* skeletonJsonPath, char* atlasPath);

		~SkeletonDrawable();

		void update(float delta);

		void draw();

		void stdoutAABB();

		Skeleton* skeleton;
		AnimationState* animationState;
		std::vector<std::string> NeedDrawSlots;

	private:
		void init(SkeletonData* skeletonData, AnimationStateData* animationStateData);

		bool ownsAnimationStateData;
		//SkeletonClipping clipper;
		Vector<float> worldVertices;
		Vector<int> glIndices;
		Vector<float> glVertices;
	};
}