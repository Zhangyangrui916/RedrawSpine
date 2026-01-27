import os
import utility
import shutil


def export_skeleton_json(path2spineProject: str, exportPath: str) -> str:
    os.makedirs(exportPath, exist_ok=True)
    exportJsonString = f"""{{
"class": "export-json",
"extension": ".json",
"format": "JSON",
"prettyPrint": true,
"nonessential": false,
"cleanUp": true,
"packAtlas": null,
"packSource": "attachments",
"packTarget": "perskeleton",
"warnings": true,
"version": null,
"all": false,
"output": "{exportPath}",
"input": "{path2spineProject}",
"open": false
}}"""
    jsonPath = os.path.join(utility.path2Cache, "export2skeleton.json")
    open(jsonPath, "w").write(exportJsonString)
    os.system(f"""
cd /d {utility.path2Spine}  && Spine -e {jsonPath}
""")
    
    skeletonJsonPath = None
    for path in os.listdir(exportPath):
        if path.endswith(".json"):
            skeletonJsonPath = os.path.join(utility.path2SpineProject, "skeleton.json")
            shutil.move(os.path.join(exportPath, path), skeletonJsonPath)
        else:
            print(path)
    if skeletonJsonPath is None:
        # 若console显示Spine命令行导出执行失败，可在Spine编辑器里手动导出json到 {exportPath}， 然后再次执行本py
        raise RuntimeError(f"No json exported to: {exportPath}")
    return skeletonJsonPath

# 导出json
for path in os.listdir(utility.path2SpineProject):
    if path.endswith(".spine"):
        path2spineProject = os.path.abspath(os.path.join(utility.path2SpineProject, path)).replace('\\', '/')
        break
exportPath = os.path.join(utility.path2Cache, "output").replace('\\', '/')
skeletonJsonPath = export_skeleton_json(path2spineProject, exportPath)
# fake atlas
utility.fakeAtlas(utility.path2SpineAttachment)

import math
import subprocess
# 计算全动画 bounding box 以决定视口大小
argv1 = os.path.abspath(skeletonJsonPath).replace('\\', '/')
argv2 = os.path.abspath(os.path.join(utility.path2SpineAttachment, "fake.atlas")).replace('\\', '/')
argv3 = "getAABB"
out, err = subprocess.Popen(args = [utility.path2Analyzer, argv1, argv2, argv3], stdout=subprocess.PIPE).communicate()
cropX, cropY, cropWidth, cropHeight = list(map(float, out.decode().split(",")))
print(f"all anim viewport: {cropX} {cropY} {cropWidth} {cropHeight}")
cropX = math.floor(cropX)
cropY = math.floor(cropY)
cropWidth = math.ceil(cropWidth)
cropHeight = math.ceil(cropHeight)

# 将视口调整为8*utility.downScale的倍数，再以此导出psd。 这保证图片降采样downScale后送进StableDiffusion时宽高为8的倍数，不然sd自己也会padding到8的倍数
import json
dw = cropWidth / utility.downScale
dh = cropHeight / utility.downScale
if not (dw % 8 == 0):
    newcropWidth = int((dw // 8 + 1) * 8 * utility.downScale)
    cropX = cropX - (newcropWidth - cropWidth) // 2
    cropWidth = newcropWidth
if not (dh % 8 == 0):
    newcropHeight = int((dh // 8 + 1) * 8 * utility.downScale)
    cropY = cropY - (newcropHeight - cropHeight) // 2
    cropHeight = newcropHeight
os.makedirs(utility.PSDPath, exist_ok=True)
open(os.path.join(utility.PSDPath, "meta.json"), 'w+').write(json.dumps({"viewPort": (cropX, cropY, cropWidth, cropHeight)}))
psdPath = os.path.abspath(os.path.join(utility.PSDPath, "rest.psd")).replace('\\', '/')
exportJsonString = f"""{{
"class": "com.esotericsoftware.spine.editor.export.ExportSettings$ExportPsd",
"exportType": "current",
"skeletonType": "single",
"skeleton": "leidian",
"animationType": "current",
"animation": null,
"skinType": "current",
"skinNone": false,
"skin": null,
"maxBounds": false,
"renderImages": true,
"renderBones": false,
"renderOthers": false,
"scale": 100,
"fitWidth": 0,
"fitHeight": 0,
"enlarge": false,
"background": null,
"lastFrame": false,
"cropWidth": {cropWidth},
"cropHeight": {cropHeight},
"rangeStart": -1,
"rangeEnd": -1,
"outputType": "layers",
"encoding": "RLE",
"compression": 6,
"pad": false,
"msaa": 4,
"smoothing": 8,
"renderSelection": false,
"cropX": {cropX},
"cropY": {cropY},
"output": "{psdPath}",
"input": "{path2spineProject}"
}}"""

jsonPath = os.path.join(utility.path2Cache, "export2psd.json")
open(jsonPath, "w").write(exportJsonString)
 
os.system(f"""
cd /d {utility.path2Spine}  && Spine -e {jsonPath}
""")


# 手动去PSD中用PS2Spine脚本导出slots
