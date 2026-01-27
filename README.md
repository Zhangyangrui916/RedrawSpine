通过魔改SpineRuntime，获取了多个Pose下各部件UV与RenderTarget.UV的对应关系，多次重绘后实现Spine模型重绘。
效果见视频：
【StableDiffusion重绘Spine模型】 https://www.bilibili.com/video/BV1fhWzetE4D/?share_source=copy_web&vd_source=34cb622516d7994683d0d507b3580978

# 准备阶段

## 执行spine2restposepsd.py
1. 通过遍历Spine工程下图片文件夹，生成fake.atlas，描述所有attachments。
2. 调用Analyzer，计算所有动画姿势下最大的AABB，以此作为视口参数导出 rest.psd。

## 在Photoshop中通过勾选图层，标记哪些attachment需要/不需要重绘
在Photoshop中利用PhotoshopToSpine.jsx导出所有图层到images文件夹。之后手动隐藏那些不希望重绘的图层（比如脸、头发，我们希望换衣服，不希望把人都换了），再次导出到images1文件夹。后续执行的preprocess.py会根据两个文件夹导出json的差异，得知哪些attachment需要重绘。这一步的本质是提供一个可视化设置的工作流，你也可以完全不依赖Photoshop，而是直接在preprocess.py中修改代码里的skipRedrawSlots、slots中的值。

## 执行preproccess.py
1.填充attachment是否需要重绘的信息进入meta.json。
2.调用Analyzer，它用贪心算法，选取出几个动画帧。随后把这几帧的一些预处理信息输出到redraw文件夹下。其中.bin文件包含【贴图uv与RenderTarget.uv映射关系】，@mask.png用于部分重绘的mask，@ctrl.png用于ControlNet的输入，sequence.txt则储存这些动画帧在重绘时的先后顺序。
3.用canny算法处理@ctrl.png得到@canny.png。
4.准备纯白的attachment，用于重绘时被覆写。

# 重绘阶段

## 执行UVmapping.py
对于每次换皮，按sequence内序列执行：
1.调用webuiapi重绘某个pose,入参附上前面准备的mask与canny ControlNet。
2.将重绘结果发给Analyzer，它用该结果更新attachment贴图，并且用新的贴图渲染下一个pose的结果，保存为{i+1}_.png。
3.循环回到步骤1，重绘{i+1}_.png。
序列循环结束后保存attachment到out文件夹下。

# 其他
1. 我disable了sdwebui中的extension sd-webui-animatediff。我怀疑是webuiapi没有维护，导致不适应新版sdwebui?