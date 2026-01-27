# 运行前先手动导出rest.psd 中的slots
# 准备需要inpaint的 mask 和 controlnet预处理器的输入
import os
import numpy as np
import utility
import cv2
import json

attachmentGroups = [["R_leggings","R_leg_02","R_foot"], ["L_leggings","L_leg_02","L_foot"], ["bra_strap_01", "bra_strap_02", "body_cloth_02"]]

redrawPath = os.path.join(utility.PSoutputPath, "redraw")
os.makedirs(redrawPath, exist_ok=True)

# 读取PSD文件夹中的json，获取slots、skipRedrawSlots并保存在PSoutputPath下的meta.json中
path_withNoNeedRedraw = os.path.join(utility.PSDPath, 'images')
for path in os.listdir(path_withNoNeedRedraw):
    if path.endswith(".json"):
        jsonValue: dict = json.load(open(os.path.join(path_withNoNeedRedraw, path), 'r+'))
        slots = [slot["attachment"] for slot in jsonValue["slots"]] 
        break
path_onlyNeedRedraw = os.path.join(utility.PSDPath, 'images1')
for path in os.listdir(path_onlyNeedRedraw):
    if path.endswith(".json"):
        jsonValue: dict = json.load(open(os.path.join(path_onlyNeedRedraw, path), 'r+'))
        slotsOnlyNeedRedraw = [slot["attachment"] for slot in jsonValue["slots"]] 
        break

skipRedrawSlots = [slot for slot in slots if slot not in slotsOnlyNeedRedraw]
slots += ["L_arm_cloth_01_add", "L_arm_cloth_02_add", "L_arm_lace_01_add", "L_arm_lace_02_add"]
skipRedrawSlots += ["L_arm_02_add", "L_hand_add", "L_fin_01_add", "L_fin_02_add", "L_fin_03_add", "L_fin_04_add", "L_hand_add_02", "L_fin_02_01_add_02", "L_fin_02_02_add_02", "L_fin_03_add_02", "L_fin_04_add_02"]
meta = json.load(open(os.path.join(utility.PSDPath, "meta.json"), 'r+'))
meta["slots"] = slots
meta["skipRedrawSlots"] = skipRedrawSlots
with open(os.path.join(utility.PSDPath, "meta.json"), 'w+') as f:
    json.dump(meta, f)


# 读取template.png，作为整体重绘的输入
inpaintPath = os.path.join(utility.PSoutputPath, "inpaint")
templateImg = utility.cleanAlphaWhite(cv2.imread(os.path.join(path_withNoNeedRedraw, "template.png"), cv2.IMREAD_UNCHANGED))
templateImg = cv2.cvtColor(templateImg, cv2.COLOR_BGRA2BGR)
#templateImg = cv2.resize(templateImg, (templateImg.shape[1] // utility.downScale, templateImg.shape[0] // utility.downScale))
os.makedirs(inpaintPath, exist_ok= True)
cv2.imwrite(os.path.join(inpaintPath, "template.png"), templateImg)


# 调用analyzer.exe, 分析在哪些动画帧进行重绘，并且准备这些帧的UV图和CTRL输入
import subprocess
skeletonJsonPath = os.path.join(utility.path2SpineProject, "skeleton.json")
argv1 = os.path.abspath(skeletonJsonPath).replace('\\', '/')
argv2 = os.path.abspath(os.path.join(utility.path2SpineAttachment, "fake.atlas")).replace('\\', '/')
cropX, cropY, cropWidth, cropHeight = meta["viewPort"]
argv3 = f"{cropX},{cropY},{cropWidth},{cropHeight}"
argv4 = "uv"
argv5 = os.path.abspath(redrawPath).replace('\\', '/')
argv6 = "-attachments"
argv7 = ""
for slot in slots:
    argv7 += f"{slot},"
argv7 += "@,"
for slot in skipRedrawSlots:
    argv7 += f"{slot},"
argv7 += "@,"
for group in attachmentGroups:
    argv7 += "@".join(group) + ","
print(utility.path2Analyzer, argv1, argv2, argv3, argv4, argv5, argv6, argv7)
out, err = subprocess.Popen(args = [utility.path2Analyzer, argv1, argv2, argv3, argv4, argv5, argv6, argv7], stdout=subprocess.PIPE).communicate()
print(out.decode())


# 将CTRL输入进行canny预处理
from PIL import Image
import webuiapi
api = webuiapi.WebUIApi(host=utility.sdhostIP, port=utility.sdhostPort, sampler='DPM++ 2M Karras', steps=20)
redrawPath = os.path.join(utility.PSoutputPath, "redraw")
for path in os.listdir(redrawPath):
    if path.endswith("@ctrl.png"):
        fullPath = os.path.join(redrawPath, path)
        ctrlImg = Image.open(fullPath).convert("RGB")
        width, height = ctrlImg.size
        width = width // utility.downScale
        height = height // utility.downScale
        ctrlImg = api.controlnet_detect(images=[ctrlImg], module='canny', processor_res=min(width, height), 
                                        threshold_a = 1, threshold_b = 1).image
        cv2.imwrite(fullPath.replace("@ctrl.png", "@canny.png"), np.array(ctrlImg))


# #准备纯白的attachment，用于重绘时被覆写
# os.makedirs(os.path.join(utility.path2SpineAttachment, "white") , exist_ok=True)
# for path in os.listdir(utility.path2SpineAttachment):
#     attachmentName = path.split(".")[0]
#     if attachmentName in slots and attachmentName not in skipRedrawSlots:
#         img = cv2.imread(os.path.join(utility.path2SpineAttachment, path), cv2.IMREAD_UNCHANGED)
#         img[img[...,3]>0, 0:3] = 255
#         cv2.imwrite(os.path.join(utility.path2SpineAttachment, "white", path), img)
#         cv2.imwrite(os.path.join(utility.path2SpineAttachment, path), img)

