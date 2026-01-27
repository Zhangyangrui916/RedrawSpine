"""
步骤 3：逐姿势迭代重绘。

流程：
1) 先对 rest pose 做整体重绘。
2) analyzer 用上一帧结果更新 attachment 贴图，并渲染出下一帧 pose。
3) 对下一帧 pose 做局部重绘，循环直到 sequence 结束。
"""

import shutil
import subprocess
import numpy as np
import utility
import os
import cv2
import time
from PIL import Image
import json
import webuiapi

# 是否启用 crop（按 pose 裁剪以节省显存/推理尺寸）
USE_CROP = True

# 整体重绘的 prompt：正向写衣服类型，负向加入裸露相关（因为我们要画衣服而不是皮肤）
prompt = "violet, solo, flower, thighhighs, breasts, ribbon, staff, sash, holding, braid, boots, skirt, wide sleeves, dress, cleavage, holding staff, thigh boots, detached sleeves, bare shoulders, miniskirt, long sleeves"

negative_prompt = "(worstquality,low quality:1.3), lowres, blurry, nude, bare, see through, transparent, bloom, facing back, backward, Uncovered, Bulky, tail, extra legs,"

meta = json.load(open(os.path.join(utility.PSD_DIR, "meta.json"), "r+"))
slots = meta["slots"]
skip_redraw_slots = meta["skipRedrawSlots"]

sequence_path = os.path.join(utility.PS_OUTPUT_DIR, "redraw", "sequence.txt")
sequence = open(sequence_path, "r").read().splitlines()

redraw_dir = os.path.join(utility.PS_OUTPUT_DIR, "redraw")

api = webuiapi.WebUIApi(host=utility.SD_HOST, port=utility.SD_PORT, sampler="DPM++ 2M", steps=25)

# models = api.util_get_model_names()
api.util_set_model(utility.SD_MODEL_NAME)
api.set_options({"sd_vae": utility.SD_VAE_NAME})

# load images for 整体重绘
inpaint_dir = os.path.join(utility.PS_OUTPUT_DIR, "inpaint")
original_img = Image.open(os.path.join(inpaint_dir, "template.png"))
width, height = original_img.size
inpaint_mask = Image.open(os.path.join(redraw_dir, "restPose@mask.png"))

canny_units = []
canny_aabbs = []
size_unit = 8 * utility.DOWNSCALE
for path in sequence:
    canny_img = Image.open(path.replace(".bin", "@canny.png"))
    width, height = canny_img.size
    if USE_CROP:
        canny_aabb = canny_img.getbbox()
        if canny_aabb is None:
            canny_aabb = (0, 0, width, height)
        tmpwidth = canny_aabb[2] - canny_aabb[0]
        tmpheight = canny_aabb[3] - canny_aabb[1]
        if tmpwidth % size_unit != 0:
            delta_width = int((tmpwidth // size_unit + 1) * size_unit - tmpwidth)
            new_left = max(0, canny_aabb[0] - delta_width)
            new_right = canny_aabb[2] + delta_width - (canny_aabb[0] - new_left)
            canny_aabb = (new_left, canny_aabb[1], new_right, canny_aabb[3])
        if tmpheight % size_unit != 0:
            delta_height = int((tmpheight // size_unit + 1) * size_unit - tmpheight)
            new_up = max(0, canny_aabb[1] - delta_height)
            new_down = canny_aabb[3] + delta_height - (canny_aabb[1] - new_up)
            canny_aabb = (canny_aabb[0], new_up, canny_aabb[2], new_down)
        canny_units.append(
            webuiapi.ControlNetUnit(
                image=canny_img.crop(canny_aabb),
                model=utility.CONTROLNET_CANNY_MODEL,
                threshold_a=1,
                threshold_b=1,
                pixel_perfect=True,
                resize_mode="Just Resize",
            )
        )
    else:
        canny_aabb = (0, 0, width, height)
        canny_units.append(
            webuiapi.ControlNetUnit(
                image=canny_img,
                model=utility.CONTROLNET_CANNY_MODEL,
                threshold_a=1,
                threshold_b=1,
                pixel_perfect=True,
                resize_mode="Just Resize",
            )
        )
    canny_aabbs.append(canny_aabb)

if USE_CROP:
    cropped_original_img = original_img.crop(canny_aabbs[0])
    cropped_inpaint_mask = inpaint_mask.crop(canny_aabbs[0])
else:
    cropped_original_img = original_img
    cropped_inpaint_mask = inpaint_mask

os.makedirs(utility.SD_OUTPUT_DIR, exist_ok=True)

start_seed = utility.getMaxId() + 1
num_to_generate = 130  # 想生成 N 个就设为 N（更直观）
end_seed = start_seed + num_to_generate

for seed in range(start_seed, end_seed):

    # 将 attachment 置为纯白（保证重绘部分能覆盖）
    for path in os.listdir(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "white")):
        if path.endswith(".png"):
            shutil.copy(
                os.path.join(utility.SPINE_ATTACHMENTS_DIR, "white", path),
                os.path.join(utility.SPINE_ATTACHMENTS_DIR, path),
            )

    folderPath = os.path.join(utility.SD_OUTPUT_DIR, str(seed))

    if os.path.exists(folderPath):
        continue

    os.makedirs(folderPath, exist_ok=True)

    # 1) rest pose 整体重绘
    inpainting_result = api.img2img(
        images=[cropped_original_img],
        mask_image=cropped_inpaint_mask,
        mask_blur=6,
        inpainting_fill=1,        # 填充：0 原图, 1 潜空间噪声
        inpaint_full_res=False,   # 必须为 False
        prompt=prompt,
        negative_prompt=negative_prompt,
        height=cropped_original_img.size[1] // utility.DOWNSCALE,
        width=cropped_original_img.size[0] // utility.DOWNSCALE,
        denoising_strength=0.75,
        seed=seed,
        eta=None,
        controlnet_units=[canny_units[0]],
    )

    for i in range(0, len(sequence)):

        if utility.DOWNSCALE == 1:
            upscale_result = inpainting_result
        else:
            upscale_result = api.extra_single_image(
                image=inpainting_result.image,
                upscaler_1="R-ESRGAN 4x+ Anime6B",
                upscaling_resize=utility.DOWNSCALE,
            )

        if USE_CROP:
            padded_img = Image.new("RGB", original_img.size, "white")
            padded_img.paste(upscale_result.image, canny_aabbs[i])
            upscale_result.images[0] = padded_img

        # 2) 将 pose_i 重绘结果发给 analyzer
        utility.saveImgandInfo(folderPath, upscale_result.image, inpainting_result.info, f"{i}.png")
        skeleton_json_path = os.path.join(utility.SPINE_PROJECT_DIR, "skeleton.json")
        argv1 = os.path.abspath(skeleton_json_path).replace("\\", "/")
        argv2 = os.path.abspath(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "fake.atlas")).replace("\\", "/")
        cropX, cropY, cropWidth, cropHeight = meta["viewPort"]
        argv3 = f"{cropX},{cropY},{cropWidth},{cropHeight}"
        argv4 = "rgb"
        argv5 = os.path.abspath(redraw_dir).replace("\\", "/")
        argv6 = "-attachments"
        argv7 = ""
        for slot in slots:
            argv7 += f"{slot},"
        argv7 += "@,"
        for slot in skip_redraw_slots:
            argv7 += f"{slot},"
        argv7 = argv7[:-1]
        argv8 = "-frame"
        argv9 = os.path.abspath(os.path.join(folderPath, f"{i}.png")).replace("\\", "/")
        args = [utility.ANALYZER_EXE, argv1, argv2, argv3, argv4, argv5, argv6, argv7, argv8, argv9]
        print(" ".join(args))
        out, err = subprocess.Popen(args=args, stdout=subprocess.PIPE).communicate()
        print(out.decode())

        if i == len(sequence) - 1:
            break

        # 3) 从 analyzer 获取下一帧 pose_i+1，并进行局部重绘
        if USE_CROP:
            next_pose_img = Image.open(os.path.join(folderPath, f"{i+1}_.png")).crop(canny_aabbs[i + 1])
            next_pose_mask = Image.open(sequence[i + 1].replace(".bin", "@mask.png")).crop(canny_aabbs[i + 1])
            cur_aabb = canny_aabbs[i + 1]
        else:
            next_pose_img = Image.open(os.path.join(folderPath, f"{i+1}_.png"))
            next_pose_mask = Image.open(sequence[i + 1].replace(".bin", "@mask.png"))
            cur_aabb = (0, 0, next_pose_img.size[0], next_pose_img.size[1])
        while True:
            try:
                inpainting_result = api.img2img(
                    images=[next_pose_img],
                    mask_image=next_pose_mask,
                    mask_blur=0,
                    inpainting_fill=1,        # 填充：0 原图, 1 潜空间噪声
                    inpaint_full_res=False,   # 必须为 False
                    prompt=prompt,
                    negative_prompt=negative_prompt,
                    height=(cur_aabb[3] - cur_aabb[1]) // utility.DOWNSCALE,
                    width=(cur_aabb[2] - cur_aabb[0]) // utility.DOWNSCALE,
                    denoising_strength=0.75,
                    seed=seed,
                    eta=None,
                    controlnet_units=[canny_units[i + 1]],
                )

                if utility.DOWNSCALE != 1:
                    upscale_result = api.extra_single_image(
                        image=inpainting_result.image,
                        upscaler_1="R-ESRGAN 4x+ Anime6B",
                        upscaling_resize=utility.DOWNSCALE,
                    )
                break
            except Exception:
                print("连接 SD 服务器失败，请检查网络连接")
                time.sleep(5)
                continue

    # 4) 读取 Spine 工程中的 attachment 并保存
    os.makedirs(os.path.join(folderPath, "out"), exist_ok=True)
    for path in os.listdir(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "white")):
        if path.endswith(".png"):
            #try:
            #    shutil.move(os.path.join(utility.SPINE_ATTACHMENTS_DIR, path), os.path.join(folderPath, "out", path))
            #except Exception:
                shutil.copy(os.path.join(utility.SPINE_ATTACHMENTS_DIR, path), os.path.join(folderPath, "out", path))
