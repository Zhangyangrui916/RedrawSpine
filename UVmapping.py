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

#整体重绘的prompt，正向prompt写衣服类型，负向prompt需要加入裸露类（因为我们要画衣服而不是皮肤）
prompt = """
violet, solo, flower, thighhighs, breasts, ribbon, staff, sash, holding, braid, boots, skirt, wide sleeves, dress, cleavage, holding staff, thigh boots, detached sleeves, bare shoulders, miniskirt, long sleeves
""".replace('\n', '')

negative_prompt = """
(worstquality,low quality:1.3), lowres, blurry, nude, bare, see through, transparent, bloom, facing back, backward, Uncovered, Bulky, tail, extra legs,
""".replace('\n', '')

meta = json.load(open(os.path.join(utility.PSDPath, "meta.json"), 'r+'))
slots = meta["slots"]
skipRedrawSlots = meta["skipRedrawSlots"]

txtPath = os.path.join(utility.PSoutputPath, "redraw", "sequence.txt")
sequence = open(txtPath, 'r').read().splitlines()

redrawPath = os.path.join(utility.PSoutputPath, "redraw")

api = webuiapi.WebUIApi(host=utility.sdhostIP, port=utility.sdhostPort, sampler='DPM++ 2M', steps=25)

#models = api.util_get_model_names() #load models
api.util_set_model('meinapastel_v6Pastel.safetensors [4679331655]')#
api.set_options({'sd_vae':'klF8Anime2VAE_klF8Anime2VAE.safetensors'})

# load images for 整体重绘
inpaintDir = os.path.join(utility.PSoutputPath, "inpaint")
originalImg = Image.open(os.path.join(inpaintDir, "template.png"))
width, height = originalImg.size
inpaintMask = Image.open(os.path.join(redrawPath, "restPose@mask.png"))

cannys = []
cannysAABB = []
SizeUnit = 8 * utility.downScale
for path in sequence:
    cannyImg = Image.open(path.replace(".bin", "@canny.png"))
    cannyAABB = cannyImg.getbbox()
    tmpwidth = cannyAABB[2] - cannyAABB[0]
    tmpHeight= cannyAABB[3] - cannyAABB[1]
    if(tmpwidth % SizeUnit != 0):
        deltaWidth = int((tmpwidth // SizeUnit + 1) * SizeUnit - tmpwidth)
        newLeft = max(0, cannyAABB[0]-deltaWidth)
        newRight= cannyAABB[2] + deltaWidth - (cannyAABB[0] - newLeft)
        cannyAABB = (newLeft, cannyAABB[1], newRight, cannyAABB[3])
    if(tmpHeight % SizeUnit != 0):
        deltaHeight = int((tmpHeight // SizeUnit + 1) * SizeUnit - tmpHeight)
        newUp = max(0, cannyAABB[1]-deltaHeight)
        newDown= cannyAABB[3] + deltaHeight - (cannyAABB[1] - newUp)
        cannyAABB = (cannyAABB[0], newUp, cannyAABB[2], newDown)
    
    cannysAABB.append(cannyAABB)
    cannys.append(webuiapi.ControlNetUnit(image=cannyImg.crop(cannyAABB), model=utility.control_canny, 
                                threshold_a=1, threshold_b=1,
                                pixel_perfect=True, resize_mode="Just Resize"))
    
croppedoriginalImg = originalImg.crop(cannysAABB[0])
croppedinpaintMask = inpaintMask.crop(cannysAABB[0])

os.makedirs(utility.sdOutPutPath, exist_ok=True)

for _ in range(utility.getMaxId() + 1, 130):

    # 将Attachment置为白色
    for path in os.listdir(os.path.join(utility.path2SpineAttachment, "white")):
        if path.endswith(".png"):
            shutil.copy(os.path.join(utility.path2SpineAttachment, "white", path), os.path.join(utility.path2SpineAttachment, path))

    folderPath = os.path.join(utility.sdOutPutPath, str(_))

    if os.path.exists(folderPath):
        continue

    os.makedirs(folderPath, exist_ok=True)
    #restpose重绘
    inpainting_result = api.img2img(images=[croppedoriginalImg],
                                    mask_image=croppedinpaintMask,
                                    mask_blur = 6,
                                    inpainting_fill = 1,        #填充、原版、潜空间噪声、潜空间0
                                    inpaint_full_res = False,   #Whole picture False, Only masked True. 必须选Whole picture
                                    prompt = prompt,
                                    negative_prompt = negative_prompt,
                                    height= croppedoriginalImg.size[1] // utility.downScale,
                                    width = croppedoriginalImg.size[0] // utility.downScale,
                                    denoising_strength = 0.75,
                                    seed= _,
                                    eta = None,                 #eta默认值是1
                                #alwayson_scripts= {"Soft inpainting": {"args": [True]}}, #alwayson_scripts= {"Tiled VAE": {"args": [True]}},
                                    controlnet_units = [cannys[0]]
                                    )

    for i in range(0, len(sequence)):
        
        if utility.downScale == 1:
            upscale_result = inpainting_result
        else: 
            upscale_result = api.extra_single_image(image=inpainting_result.image, upscaler_1 = "R-ESRGAN 4x+ Anime6B", upscaling_resize = utility.downScale)

        padded_img = Image.new('RGB', originalImg.size, 'white')
        padded_img.paste(upscale_result.image, cannysAABB[i])
        upscale_result.images[0] = padded_img
    
        #将pose_i重绘结果发给analyzer
        utility.saveImgandInfo(folderPath, upscale_result.image, inpainting_result.info, f"{i}.png")
        skeletonJsonPath = os.path.join(utility.path2SpineProject, "skeleton.json")
        argv1 = os.path.abspath(skeletonJsonPath).replace('\\', '/')
        argv2 = os.path.abspath(os.path.join(utility.path2SpineAttachment, "fake.atlas")).replace('\\', '/')
        cropX, cropY, cropWidth, cropHeight = meta["viewPort"]
        argv3 = f"{cropX},{cropY},{cropWidth},{cropHeight}"
        argv4 = "rgb"
        argv5 = os.path.abspath(redrawPath).replace('\\', '/')
        argv6 = "-attachments"
        argv7 = ""
        for slot in slots:
            argv7 += f"{slot},"
        argv7 += "@,"
        for slot in skipRedrawSlots:
            argv7 += f"{slot},"
        argv7 = argv7[:-1]
        argv8 = "-frame"
        argv9 = os.path.abspath(os.path.join(folderPath, f"{i}.png")).replace('\\', '/')
        args = [utility.path2Analyzer, argv1, argv2, argv3, argv4, argv5, argv6, argv7, argv8, argv9]
        print(" ".join(args))
        out, err = subprocess.Popen(args = args, stdout=subprocess.PIPE).communicate()
        print(out.decode())

        if i == len(sequence) - 1:
            break
        ## 从analyzer获得待重绘的pose_i+1
        nextPoseImg = Image.open(os.path.join(folderPath, f"{i+1}_.png")).crop(cannysAABB[i+1])
        nextPoseMask = Image.open(sequence[i+1].replace(".bin", "@mask.png")).crop(cannysAABB[i+1])
        cur_aabb = cannysAABB[i+1]
        while True:
            try:
                inpainting_result = api.img2img(images=[nextPoseImg],
                                            mask_image=nextPoseMask,
                                            mask_blur = 0,
                                            inpainting_fill = 1,        #填充、原版、潜空间噪声、潜空间0
                                            inpaint_full_res = False,   #Whole picture False, Only masked True. 必须选Whole picture
                                            prompt = prompt,
                                            negative_prompt = negative_prompt,
                                            height= (cur_aabb[3] - cur_aabb[1]) // utility.downScale,
                                            width = (cur_aabb[2] - cur_aabb[0]) // utility.downScale,
                                            denoising_strength = 0.75,
                                            seed= _,
                                            eta = None,                 #eta默认值是1
                                        #alwayson_scripts= {"Soft inpainting": {"args": [True]}}, #alwayson_scripts= {"Tiled VAE": {"args": [True]}},
                                            controlnet_units = [cannys[i+1]]
                                            )
                
                if utility.downScale != 1:
                    upscale_result = api.extra_single_image(image=inpainting_result.image, upscaler_1 = "R-ESRGAN 4x+ Anime6B", upscaling_resize = utility.downScale)
                break
            except Exception as e:
                print("连接sd服务器失败,请检查网络连接")
                time.sleep(5)
                continue
        
        ## Soft Inpaint
        # padded_adaptiveMask = Image.new('RGB', originalImg.size, 'black')
        # padded_adaptiveMask.paste(inpainting_result.images[1], cannysAABB[i+1])
        # padded_adaptiveMask.save(os.path.join(folderPath, f"{i+1}@adaptiveMask.png"))

    # 读取spine工程文件夹中的图片并保存
    os.makedirs(os.path.join(folderPath, "out"), exist_ok=True)
    for path in os.listdir(os.path.join(utility.path2SpineAttachment, "white")):
        if path.endswith(".png"):
            try:
                shutil.move(os.path.join(utility.path2SpineAttachment, path), os.path.join(folderPath, "out", path))
            except Exception as e:
                shutil.copy(os.path.join(utility.path2SpineAttachment, path), os.path.join(folderPath, "out", path))