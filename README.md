Intelligent Brightness plugin for AviSynth+

Same as this VirtualDub plugin: https://www.infognition.com/brightness/

Example AVS script:
```
LoadPlugin("intellibravs.dll")
clip = AVISource("video_yv12.avi") # RGB32 and YV12 are supported
return clip.Intellibr()
```
