#pragma once
#include <spine/spine.h>

namespace spine {

	class SkeletonDrawable {
	public:
		SkeletonDrawable(SkeletonData* skeletonData, AnimationStateData* animationStateData = nullptr);

		SkeletonDrawable(char* skeletonJsonPath, char* atlasPath);

		~SkeletonDrawable();

		void update(float delta);

		void draw();

		Skeleton* skeleton;
		AnimationState* animationState;

	private:
		void init(SkeletonData* skeletonData, AnimationStateData* animationStateData);

		bool ownsAnimationStateData;
		SkeletonClipping clipper;
		Vector<float> worldVertices;
		Vector<int> glIndices;
		Vector<float> glVertices;
	};
}