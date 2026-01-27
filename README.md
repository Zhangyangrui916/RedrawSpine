RedrawSpine
===========

通过魔改 Spine Runtime + 自定义 analyzer，建立 **贴图UV ↔ RenderTarget像素** 的映射，从而实现 **多姿势、多轮重绘后回写贴图** 的流程。
效果视频：
【StableDiffusion重绘Spine模型】 www.bilibili.com/video/BV1fhWzetE4D

## 整体思路
1. 用 Spine + analyzer 找到关键姿势，并输出对应的 UV / mask / ctrl 信息。
2. 在 SD WebUI 中按序列重绘每个姿势。
3. analyzer 将重绘结果回写到 attachment 贴图，进而渲染出下一帧姿势。
4. 多帧迭代后得到“完整的新贴图”。

## 数据流（文字版流程图）
SpineProject + attachments
        │
        ├─> spine2restposepsd.py
        │      ├─ 导出 skeleton.json
        │      ├─ 生成 fake.atlas
        │      └─ 计算全动画 AABB -> PSD/rest.psd + meta.json(viewPort)
        │
        ├─> Photoshop 标记重绘/不重绘
        │      ├─ PSoutput/images (全量)
        │      └─ PSoutput/images1 (需重绘only)
        │
        ├─> preproccess.py
        │      ├─ 生成 meta.json: slots / skipRedrawSlots
        │      ├─ analyzer 选关键帧 -> *.bin/@mask/@ctrl/sequence.txt
        │      └─ @ctrl.png -> @canny.png
        │
        └─> UVmapping.py
               ├─ rest pose 整体重绘
               ├─ analyzer 更新 attachments 并渲染下一帧
               └─ 循环直到 sequence 结束 -> sdout/<seed>/out

## 目录约定
- `SpineProject/`：Spine 工程（含 `*.spine` 与 `images/` attachments）
- `PSD/`：`rest.psd` + `meta.json`
- `PSoutput/`：PS2Spine 导出图层（含 `images/` 与 `images1/`）
- `PSoutput/redraw/`：analyzer 生成的 UV/CTRL/mask/sequence
- `PSoutput/inpaint/`：整体重绘用的 `template.png`
- `sdout/`：每次重绘的输出目录（按 seed 分文件夹）

## 快速开始（最小流程）
1. 准备 `SpineProject/`（含 attachments）。
2. 运行 `spine2restposepsd.py` 生成 `PSD/rest.psd` 与 `meta.json`。
3. 用 Photoshop 标记需要重绘/不重绘的 attachment（导出到 images / images1）。
4. 运行 `preproccess.py` 生成重绘素材（UV/mask/ctrl/canny/sequence）。
5. 运行 `UVmapping.py` 输出 `sdout/<seed>/out/` 新贴图。

## 详细流程说明

### 1) `spine2restposepsd.py`
- 遍历 Spine 工程附件，生成 `fake.atlas`（只记录尺寸）。
- 调用 analyzer 计算所有动画姿势下最大的 AABB，作为 PSD 视口。
- 导出 `PSD/rest.psd`，同时写入 `PSD/meta.json` 的 `viewPort`。

### 2) Photoshop 标记 attachment
- 用 `PhotoshopToSpine.jsx` 导出所有图层到 `PSoutput/images`。
- 手动隐藏“不希望重绘”的图层（比如脸、头发），再导出到 `PSoutput/images1`。
- `preproccess.py` 会对比两个目录的 json 来计算 `slots` 与 `skipRedrawSlots`。

> 也可以不依赖 Photoshop，直接在 `preproccess.py` 里手动写 `slots/skipRedrawSlots`。对于在rest pose时有attachment为隐藏状态的模型而言，在代码里手动添加甚至是必须的。比如由于艾米莉亚的动画会展示手的正面与背面，而restpose中对应背面的attachment被隐藏了，所以我手动在代码里把背面的衣袖加入了【需要】重绘，手掌、手指背面加入了【不需要】重绘。

### 3) `preproccess.py`
- 写入 `meta.json`：`slots` 与 `skipRedrawSlots`。
- 调用 analyzer 选出关键姿势，并生成：
  - `*.bin`：UV 映射
  - `@mask.png`：部分重绘 mask
  - `@ctrl.png`：ControlNet 输入
  - `sequence.txt`：姿势顺序
- 对 `@ctrl.png` 做 canny，生成 `@canny.png`。
- 生成纯白 attachment，用于后续覆盖。

### 4) `UVmapping.py`
对每次换皮，按 `sequence.txt` 迭代：
1. 使用 `@mask.png` + `@canny.png` 对姿势进行 inpaint。
2. 将结果交给 analyzer 更新 attachment 贴图。
3. analyzer 用更新后的贴图渲染下一帧，生成 `i+1_.png`。
4. 重复直到序列结束，最终在 `sdout/<seed>/out/` 得到新贴图。

## 参数配置说明（`utility.py`）
常用字段：
- `SD_HOST / SD_PORT`：SD WebUI 地址
- `SD_MODEL_NAME / SD_VAE_NAME`：SD 模型与 VAE
- `CONTROLNET_CANNY_MODEL`：ControlNet canny 模型名
- `DOWNSCALE`：降采样倍数（影响 SD 输入尺寸）
- `SPINE_APP_DIR / SPINE_PROJECT_DIR / SPINE_ATTACHMENTS_DIR`：Spine 路径与工程目录
- `ANALYZER_EXE`：自定义 analyzer 路径

## attachment_groups 的设计意图
`preproccess.py` 中的 `attachment_groups` 用于 **在生成 ControlNet 输入时把一组 attachment 当作一个整体**。
例：艾米莉亚的白色长筒靴被分为大腿/小腿/脚三部分，但原画中通过颜色与透明度过渡形成一体。
如果不分组，canny 会在交界处生成分界线，重绘后会出现膝盖/脚踝“断开”的违和感。
通过分组让它们共享同一 ID，避免这些分界线。

## analyzer 输入输出文件格式（概念级）
输入：
- `skeleton.json`
- `fake.atlas`
- `viewPort`（cropX,cropY,cropW,cropH）
- `-attachments` 参数：包含需要重绘/跳过/分组信息
- 可选 `-frame <png>`：将某帧重绘结果回写 attachment

输出（到 `PSoutput/redraw/`）：
- `*.bin`：当前姿势的 UV 映射数据
- `@mask.png`：局部重绘 mask
- `@ctrl.png`：ControlNet 输入
- `sequence.txt`：姿势顺序
- 以及渲染出的 `i+1_.png`（供下一次重绘）

## 其他备注
- 我已禁用 `sd-webui-animatediff`，怀疑 webuiapi 与新版 SD WebUI 不兼容。
