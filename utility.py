"""
全局配置与小工具函数（供 Python 流程脚本复用）。
"""

import os
from PIL import Image
import numpy as np
import cv2

# ============================== Paths / Config ==============================
# PSD 相关
PSD_DIR = "PSD"  # 由 Spine 导出的 rest.psd 以及 meta.json 所在目录
PS_OUTPUT_DIR = "./PSoutput/"  # PS2Spine 导出的图层文件夹

# Stable Diffusion WebUI
SD_HOST = "127.0.0.1"
SD_PORT = 7860
SD_OUTPUT_DIR = "./sdout/"

# SD 模型与 VAE（统一配置）
SD_MODEL_NAME = "meinapastel_v6Pastel.safetensors [4679331655]"
SD_VAE_NAME = "klF8Anime2VAE_klF8Anime2VAE.safetensors"

# ControlNet 模型名（WebUI 里的名字）
CONTROLNET_CANNY_MODEL = "control_v11p_sd15_canny [d14c016b]"
CONTROLNET_SOFTEDGE_MODEL = "control_v11p_sd15_softedge_fp16 [f616a34f]"

# 为了减少 StableDiffusion 运算量，将图片分辨率降低 DOWNSCALE 倍
DOWNSCALE = 2

# mask 里 alpha 很低的像素不应该被视为部件的一部分
LOW_ALPHA_THRESHOLD = 5

# Spine 安装与项目路径
SPINE_APP_DIR = r"C:/Program Files/Spine"
SPINE_PROJECT_DIR = r"./SpineProject"
SPINE_ATTACHMENTS_DIR = r"./SpineProject/images"

# analyzer 可执行文件路径
ANALYZER_EXE = r"./build/Release/mask-analyzer.exe"

CACHE_DIR = r"C:/Cache"

# ---- Legacy aliases (keep for backward compatibility) ----
PSDPath = PSD_DIR
PSoutputPath = PS_OUTPUT_DIR
sdhostIP = SD_HOST
sdhostPort = SD_PORT
sdOutPutPath = SD_OUTPUT_DIR
control_canny = CONTROLNET_CANNY_MODEL
control_softedge = CONTROLNET_SOFTEDGE_MODEL
downScale = DOWNSCALE
lowAlphaThreshold = LOW_ALPHA_THRESHOLD
path2Spine = SPINE_APP_DIR
path2Cache = CACHE_DIR
path2SpineProject = SPINE_PROJECT_DIR
path2SpineAttachment = SPINE_ATTACHMENTS_DIR
path2Analyzer = ANALYZER_EXE


def cleanAlphaWhite(img):
    # 这里用更高的阈值把“几乎透明”的像素刷成白色
    img[img[..., 3] < 15, 0:4] = [255, 255, 255, 255]
    return img

def saveImgandInfo(folderPath, img, info, filename):
    os.makedirs(folderPath, exist_ok=True)
    img.save(os.path.join(folderPath, filename))
    infotxt = open(os.path.join(folderPath, "info.txt"), "a+")
    infotxt.write(str(info) + "\n\n")


def getMaxId():
    paths = os.listdir(SD_OUTPUT_DIR)
    ids = [int(path) for path in paths]
    return int(max(ids, default=-1))


def fakeAtlas(directory):
    """
    生成一个最简 atlas（fake.atlas），只提供 attachment 尺寸信息。
    analyzer 用它来读取贴图像素，不需要真正的 packing 数据。
    """
    f = open(os.path.join(directory, "fake.atlas"), "w+")
    paths = os.listdir(directory)
    for path in paths:
        if not path.endswith(".png"):
            continue
        slot = path.split(".")[0]
        attachmentPath = os.path.join(directory, path)
        attachment = cv2.imread(attachmentPath, cv2.IMREAD_UNCHANGED)
        width, height = attachment.shape[1], attachment.shape[0]
        f.write(f"{slot}.png\n")
        f.write(f"size: {width},{height}\n")
        f.write("filter:Linear,Linear\n")
        f.write(f"{slot}\n")
        f.write(f"bounds: 0,0,{width},{height}\n")
        f.write("\n")

