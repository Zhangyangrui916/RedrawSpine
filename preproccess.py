"""
步骤 2：生成重绘所需的元数据 + 每个姿势的控制输入。

功能：
1) 对比 PSD 导出结果（images vs images1）判断哪些 attachment 需要重绘。
2) 写入 meta.json：slots / skipRedrawSlots。
3) 调用 analyzer 选取关键帧，并生成 UV/CTRL/mask 等资源。
4) 对 @ctrl.png 做 Canny，得到 @canny.png（ControlNet 输入）。
"""

import os
import numpy as np
import utility
import cv2
import json

# 每组内的 attachment 在生成 ControlNet 输入时会被视作一个整体。
# 例：艾米莉亚的白色长筒靴被分为大腿/小腿/脚三块，如果不分组，
# canny 会在交界处画分界线，重绘后会出现膝盖/脚踝“断开”的违和感。
# 通过分组让它们共享同一 ID，避免这些分界线。
attachment_groups = [
    ["R_leggings", "R_leg_02", "R_foot"],
    ["L_leggings", "L_leg_02", "L_foot"],
    ["bra_strap_01", "bra_strap_02", "body_cloth_02"],
]

redraw_dir = os.path.join(utility.PS_OUTPUT_DIR, "redraw")
os.makedirs(redraw_dir, exist_ok=True)

# 读取 PSD 导出 json，计算 slots / skipRedrawSlots
no_redraw_dir = os.path.join(utility.PSD_DIR, "images")
redraw_only_dir = os.path.join(utility.PSD_DIR, "images1")

slots = None
for path in os.listdir(no_redraw_dir):
    if path.endswith(".json"):
        jsonValue: dict = json.load(open(os.path.join(no_redraw_dir, path), "r+"))
        slots = [slot["attachment"] for slot in jsonValue["slots"]]
        break

slots_only_redraw = None
for path in os.listdir(redraw_only_dir):
    if path.endswith(".json"):
        jsonValue: dict = json.load(open(os.path.join(redraw_only_dir, path), "r+"))
        slots_only_redraw = [slot["attachment"] for slot in jsonValue["slots"]]
        break

if slots is None or slots_only_redraw is None:
    raise RuntimeError("Missing PSD export json in images/ or images1/.")

skip_redraw_slots = [slot for slot in slots if slot not in slots_only_redraw]

# 手动补充：这些attachment在 rest pose PSD 中不存在, 且【需要】重绘
slots += ["L_arm_cloth_01_add", "L_arm_cloth_02_add", "L_arm_lace_01_add", "L_arm_lace_02_add"]
# 手动补充：这些attachment在 rest pose PSD 中不存在, 且【不需要】重绘
skip_redraw_slots += [
    "L_arm_02_add", "L_hand_add", "L_fin_01_add", "L_fin_02_add",
    "L_fin_03_add", "L_fin_04_add", "L_hand_add_02", "L_fin_02_01_add_02",
    "L_fin_02_02_add_02", "L_fin_03_add_02", "L_fin_04_add_02",
]

meta = json.load(open(os.path.join(utility.PSD_DIR, "meta.json"), "r+"))
meta["slots"] = slots
meta["skipRedrawSlots"] = skip_redraw_slots
with open(os.path.join(utility.PSD_DIR, "meta.json"), "w+") as f:
    json.dump(meta, f)


# 读取 template.png，作为整图重绘的输入
inpaint_dir = os.path.join(utility.PS_OUTPUT_DIR, "inpaint")
templateImg = utility.cleanAlphaWhite(cv2.imread(os.path.join(no_redraw_dir, "template.png"), cv2.IMREAD_UNCHANGED))
templateImg = cv2.cvtColor(templateImg, cv2.COLOR_BGRA2BGR)
#templateImg = cv2.resize(templateImg, (templateImg.shape[1] // utility.DOWNSCALE, templateImg.shape[0] // utility.DOWNSCALE))
os.makedirs(inpaint_dir, exist_ok=True)
cv2.imwrite(os.path.join(inpaint_dir, "template.png"), templateImg)


# 调用 analyzer：选择重绘关键帧，并输出 UV/CTRL/Mask 到 redraw 目录
import subprocess
skeleton_json_path = os.path.join(utility.SPINE_PROJECT_DIR, "skeleton.json")
argv1 = os.path.abspath(skeleton_json_path).replace("\\", "/")
argv2 = os.path.abspath(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "fake.atlas")).replace("\\", "/")
cropX, cropY, cropWidth, cropHeight = meta["viewPort"]
argv3 = f"{cropX},{cropY},{cropWidth},{cropHeight}"
argv4 = "uv"
argv5 = os.path.abspath(redraw_dir).replace("\\", "/")
argv6 = "-attachments"
argv7 = ""
for slot in slots:
    argv7 += f"{slot},"
argv7 += "@,"
for slot in skip_redraw_slots:
    argv7 += f"{slot},"
argv7 += "@,"
for group in attachment_groups:
    argv7 += "@".join(group) + ","
print(utility.ANALYZER_EXE, argv1, argv2, argv3, argv4, argv5, argv6, argv7)
out, err = subprocess.Popen(args=[utility.ANALYZER_EXE, argv1, argv2, argv3, argv4, argv5, argv6, argv7], stdout=subprocess.PIPE).communicate()
print(out.decode())


# 用 Canny 处理 @ctrl.png 得到 @canny.png（ControlNet 输入）
from PIL import Image
import webuiapi
api = webuiapi.WebUIApi(host=utility.SD_HOST, port=utility.SD_PORT, sampler="DPM++ 2M Karras", steps=20)
for path in os.listdir(redraw_dir):
    if path.endswith("@ctrl.png"):
        fullPath = os.path.join(redraw_dir, path)
        ctrlImg = Image.open(fullPath).convert("RGB")
        width, height = ctrlImg.size
        ctrlImg = api.controlnet_detect(
            images=[ctrlImg],
            module="canny",
            processor_res=min(width, height),
            threshold_a=1,
            threshold_b=1,
        ).image
        cv2.imwrite(fullPath.replace("@ctrl.png", "@canny.png"), np.array(ctrlImg))


# 准备纯白的 attachment，用于重绘时被覆写
os.makedirs(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "white"), exist_ok=True)
for path in os.listdir(utility.SPINE_ATTACHMENTS_DIR):
    if not path.lower().endswith(".png"):
        continue
    attachmentName = path.split(".")[0]
    if attachmentName in slots and attachmentName not in skip_redraw_slots:
        img = cv2.imread(os.path.join(utility.SPINE_ATTACHMENTS_DIR, path), cv2.IMREAD_UNCHANGED)
        img[img[..., 3] > 0, 0:3] = 255
        cv2.imwrite(os.path.join(utility.SPINE_ATTACHMENTS_DIR, "white", path), img)
        cv2.imwrite(os.path.join(utility.SPINE_ATTACHMENTS_DIR, path), img)
