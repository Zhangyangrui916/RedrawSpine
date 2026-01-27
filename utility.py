import os
from PIL import Image
import numpy as np
import cv2

## global define
# psd文件的路径
PSDPath = 'PSD'
# 用PS2Spine脚本从psd文件导出产物的文件夹路径
PSoutputPath = './PSoutput/'
# sd服务器地址
sdhostIP = '127.0.0.1'
# sd服务器端口
sdhostPort = 7860
# sd重绘结果输出路径
sdOutPutPath = './sdout/'
# controlnet名字
control_canny = 'control_v11p_sd15_canny [d14c016b]'
control_softedge = 'control_v11p_sd15_softedge_fp16 [f616a34f]'
# 为了减少StableDiffusion运算量，将图片的分辨率降低 downScale倍
downScale = 2
# mask可能含有一些alpha非常低的像素，这些像素不应该被视为部件的一部分，毕竟人眼看不见
lowAlphaThreshold = 5
# 拆图结果输出路径
outpath = './output/'
# Spine软件的安装目录
path2Spine = r'C:/Program Files/Spine'
# 一些中间文件存放路径
path2Cache = r'C:/Cache'
# Spine 工程文件路径、 以及其attachment图片的路径
path2SpineProject = r'./SpineProject'
path2SpineAttachment = r'./SpineProject/images'
# analyzer的路径
path2Analyzer = r"./build/Release/mask-analyzer.exe"



def cleanAlpha(img):
    img[img[..., 3] == 0, 0:4] = [0, 0, 0, 0]
    return img

def cleanAlphaWhite(img):
    img[img[..., 3] < 15, 0:4] = [255, 255, 255, 255]
    return img

def saveInpaintingResult(folderPath, inpainting_result, filename):
    os.makedirs(folderPath, exist_ok=True)
    inpainting_result.image.save(os.path.join(folderPath, filename))
    infotxt = open(os.path.join(folderPath, "info.txt"), 'a+')
    infotxt.write(str(inpainting_result.info) + "\n")

def saveImgandInfo(folderPath, img, info, filename):
    os.makedirs(folderPath, exist_ok=True)
    img.save(os.path.join(folderPath, filename))
    infotxt = open(os.path.join(folderPath, "info.txt"), 'a+')
    infotxt.write(str(info) + "\n\n")

def getMaxId():
    paths = os.listdir(sdOutPutPath)
    ids = [int(path) for path in paths]
    return int(max(ids, default=-1))


def fakeAtlas(directory):
    f = open(os.path.join(directory, "fake.atlas"), 'w+')
    paths = os.listdir(directory)
    for path in paths:
        if not path.endswith('.png'):
            continue
        slot = path.split('.')[0]
        attachmentPath = os.path.join(directory, path)
        attachment = cv2.imread(attachmentPath, cv2.IMREAD_UNCHANGED)
        width, height = attachment.shape[1], attachment.shape[0]
        f.write(f"{slot}.png\n")
        f.write(f"size: {width},{height}\n")
        f.write("filter:Linear,Linear\n")
        f.write(f"{slot}\n")
        f.write(f"bounds: 0,0,{width},{height}\n")
        f.write(f"\n")


class uv2rgbCodec:
    def __init__(self):
        self.batches = []

    def addImg(self, name, img : np.ndarray):
        uvimg = uv2rgbCodec.mapUV2RGB(img)
        self.batches.append(img)

    def mapUV2RGB(img : np.ndarray):
        width, height = img.shape[0:2]
        
        return 
    

def load_SlotUVimage(file_path, width, height, channels):
    with open(file_path, 'rb') as f:
        raw_image = np.fromfile(f, dtype=np.uint32)
    image = raw_image.reshape([height, width, channels])
    return image