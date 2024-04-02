#include <SkeletonDrawable.h>
#include <renderer.h>
#include <texture.h>
#include <algorithm>
using namespace spine;

void SkeletonDrawable::init(SkeletonData* skeletonData, AnimationStateData* animationStateData)
{
	skeleton = new (__FILE__, __LINE__) Skeleton(skeletonData);

	ownsAnimationStateData = animationStateData == 0;
	if (ownsAnimationStateData) animationStateData = new (__FILE__, __LINE__) AnimationStateData(skeletonData);
	animationState = new (__FILE__, __LINE__) AnimationState(animationStateData);
}

SkeletonDrawable::SkeletonDrawable(SkeletonData* skeletonData, AnimationStateData* animationStateData) {
	init(skeletonData, animationStateData);
}

spine::SkeletonDrawable::SkeletonDrawable(char* skeletonJsonPath, char* atlasPath)
	: skeletonJsonPath(skeletonJsonPath), atlasPath(atlasPath)
{
	OGLTextureLoader textureLoader;
	Atlas* atlas = new Atlas(atlasPath, (spine::TextureLoader*)&textureLoader);
	AtlasAttachmentLoader* attachmentLoader = new AtlasAttachmentLoader(atlas);
	SkeletonJson* json = new SkeletonJson(attachmentLoader);
	SkeletonData* skeletonData = json->readSkeletonDataFile(skeletonJsonPath);
	init(skeletonData, nullptr);
}

SkeletonDrawable::~SkeletonDrawable() {
	if (ownsAnimationStateData) delete animationState->getData();
	delete animationState;
	delete skeleton;
}

void SkeletonDrawable::update(float delta) {
	animationState->update(delta);
	animationState->apply(*skeleton);
	skeleton->updateWorldTransform();
}

void SkeletonDrawable::draw() {
	Vector<unsigned short> quadIndices;
	quadIndices.add(0);
	quadIndices.add(1);
	quadIndices.add(2);
	quadIndices.add(2);
	quadIndices.add(3);
	quadIndices.add(0);
	Texture* texture;


	for (unsigned i = 0; i < skeleton->getSlots().size(); ++i) {
		Slot& slot = *skeleton->getDrawOrder()[i];
		Attachment* attachment = slot.getAttachment();
		if (!attachment) continue;

		if (NeedDrawAttachments.size() > 0) {	//没说要绘制的attachment，就绘制全部
			for (auto& attachmentName : NeedDrawAttachments) {
				if (attachment->getName().buffer() == attachmentName) {
					int slotIndex = std::find_if(skeleton->getSlots().begin(), skeleton->getSlots().end(), [&attachmentName](Slot* slot) {
						auto SlotBindAttachmentName = slot->getData().getAttachmentName().buffer();
						return (SlotBindAttachmentName != NULL) && (0 == strcmp(SlotBindAttachmentName, attachmentName.c_str()));
						}) - skeleton->getSlots().begin();
					bool skipRedraw = std::any_of(SkipRedrawAttachments.begin(), SkipRedrawAttachments.end(), [&attachmentName](const std::string& skipAttachmentName) {
						return attachmentName == skipAttachmentName;
						});
					if (skipRedraw){
						slotIndex = 65535;
					}
					Renderer::shader->setInt("u_slotIndex", slotIndex);
					goto DRAWSLOT;
				}
			}
			continue;
		}
	DRAWSLOT:

		// Early out if the slot color is 0 or the bone is not active
		if (slot.getColor().a == 0 || !slot.getBone().isActive()) {
			continue;
		}

		Vector<float>* vertices = &worldVertices;
		int verticesCount = 0;
		Vector<float>* uvs = NULL;
		Vector<unsigned short>* indices;
		int indicesCount = 0;
		Color* attachmentColor;

		if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
			RegionAttachment* regionAttachment = (RegionAttachment*)attachment;
			attachmentColor = &regionAttachment->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				continue;
			}

			worldVertices.setSize(8, 0);
			regionAttachment->computeWorldVertices(slot, worldVertices, 0, 2);
			verticesCount = 4;
			uvs = &regionAttachment->getUVs();
			indices = &quadIndices;
			indicesCount = 6;
			texture = (Texture*)regionAttachment->getRegion()->rendererObject;

		}
		else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
			MeshAttachment* mesh = (MeshAttachment*)attachment;
			attachmentColor = &mesh->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				continue;
			}

			worldVertices.setSize(mesh->getWorldVerticesLength(), 0);
			mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), worldVertices.buffer(), 0, 2);
			texture = (Texture*)mesh->getRegion()->rendererObject;
			verticesCount = mesh->getWorldVerticesLength() >> 1;
			uvs = &mesh->getUVs();
			indices = &mesh->getTriangles();
			indicesCount = indices->size();

		}
		else
			continue;

		Renderer::shader->setInt("u_width", texture->width);
		Renderer::shader->setInt("u_height", texture->height);
		glBindTexture(GL_TEXTURE_2D, texture->textureID);

		uint8_t r = static_cast<uint8_t>(skeleton->getColor().r * slot.getColor().r * attachmentColor->r * 255);
		uint8_t g = static_cast<uint8_t>(skeleton->getColor().g * slot.getColor().g * attachmentColor->g * 255);
		uint8_t b = static_cast<uint8_t>(skeleton->getColor().b * slot.getColor().b * attachmentColor->b * 255);
		uint8_t a = static_cast<uint8_t>(skeleton->getColor().a * slot.getColor().a * attachmentColor->a * 255);

		glVertices.clear();
		for (int ii = 0; ii < verticesCount << 1; ii += 2) {
			glVertices.add((*vertices)[ii]);
			glVertices.add((*vertices)[ii + 1]);
			glVertices.add(r / 255.0);
			glVertices.add(g / 255.0);
			glVertices.add(b / 255.0);
			glVertices.add(a / 255.0);
			glVertices.add((*uvs)[ii]);
			glVertices.add((*uvs)[ii + 1]);
		}

		glIndices.clear();
		for (int ii = 0; ii < (int)indices->size(); ii++)
			glIndices.add((*indices)[ii]);

		Renderer::Draw(glVertices, glIndices);

	}
}

void spine::SkeletonDrawable::stdoutAABB()
{
	skeleton->setPosition(0.f, 0.f);
	skeleton->setToSetupPose();
	update(0);
	Vector<float> vertices;
	float minx, miny, maxw, maxh;
	skeleton->getBounds(minx, miny, maxw, maxh, vertices);
	float x, y, w, h;
	auto& animations = skeleton->getData()->getAnimations();
	for (int i = 0; i < animations.size(); ++i) {
		auto& animation = animations[i];
		float duration = animation->getDuration();
		animationState->setAnimation(0, animation->getName().buffer(), true);
		update(0);
		for (float time = 0; time < duration; time += 0.1, update(0.1)) {
			skeleton->getBounds(x, y, w, h, vertices);
			minx = std::min(minx, x);
			miny = std::min(miny, y);
			maxw = std::max(w, maxw);
			maxh = std::max(h, maxh);
		}
	}
	std::cout << minx << "," << miny << "," << maxw << "," << maxh;
}

static std::unique_ptr<GLubyte[]> GetTexImage(Texture* texture, int format)
{
	if (format == GL_RGBA) {
		std::unique_ptr<GLubyte[]> pixels(new GLubyte[texture->width * texture->height * 4]);
		glBindTexture(GL_TEXTURE_2D, texture->textureID);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
		return std::move(pixels);
	}
	else if (format == GL_RED) {
		std::unique_ptr<GLubyte[]> pixels(new GLubyte[texture->width * texture->height]);
		glBindTexture(GL_TEXTURE_2D, texture->textureID);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.get());
		return std::move(pixels);
	}
	else {
		std::cout << "Not supported format";
		return nullptr;
	}
}


std::map<int, std::tuple<std::unique_ptr<GLubyte[]>, Texture*>> spine::SkeletonDrawable::GetRedrawTexImage(int format)
{
	std::map<int, std::tuple<std::unique_ptr<GLubyte[]>, Texture*>> slotIndex2Pixels;
	assert(NeedDrawAttachments.size() > 0);
	for (auto& attachmentName : NeedDrawAttachments) {
		if (std::any_of(SkipRedrawAttachments.begin(), SkipRedrawAttachments.end(), 
			[&attachmentName](const std::string& skipAttachmentName) {
				return attachmentName == skipAttachmentName;
		})) {
			continue;
		}


		auto iter = std::find_if(skeleton->getSlots().begin(), skeleton->getSlots().end(), [&attachmentName](Slot* slot) { 
			auto SlotBindAttachmentName = slot->getData().getAttachmentName().buffer();
			return (SlotBindAttachmentName != NULL) && (0 == strcmp(SlotBindAttachmentName, attachmentName.c_str()));
			});
		assert(iter != skeleton->getSlots().end());
		int slotIndex = iter - skeleton->getSlots().begin();
		auto attachment = (*iter)->getAttachment();
		Texture* texture = [&attachment]() -> Texture* {
			if (attachment->getRTTI().isExactly(MeshAttachment::rtti) ) {
				return (Texture*)((MeshAttachment*)attachment)->getRegion()->rendererObject;
			}
			else if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
				return (Texture*)((RegionAttachment*)attachment)->getRegion()->rendererObject;
			}
			else {
				std::cout << "Not supported attachment type";
				return nullptr;
			}
		}();

		slotIndex2Pixels[slotIndex] = std::make_tuple(std::move(GetTexImage(texture, format)), texture);
	}
	return slotIndex2Pixels;
}



SpineExtension* spine::getDefaultExtension() {
	return new DefaultSpineExtension();
}