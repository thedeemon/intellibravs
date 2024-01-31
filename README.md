Intelligent Brightness plugin for AviSynth+

Same as this VirtualDub plugin: https://www.infognition.com/brightness/

See that page to learn what it does and what the parameters mean.

Download Windows version, 64 and 32 bits, here: 
https://www.infognition.com/brightness/intellibravs.zip

Example AVS script:
```
LoadPlugin("intellibravs.dll")
clip = AVISource("video_yv12.avi") # RGB32 and YV12 are supported
return clip.Intellibr()
```
or with full set of parameters:
```
...
return clip.Intellibr(p="max", min=0, max=255, dynamicity=5, scene=20, \
  start=0, end=153, start_angle=0.14, end_angle=0.02)
```
All the parameters are optional and can be skipped. If some parameters are skipped 
default values will be used, the ones shown here. 
Parameter p can be either "max", "average" or "median".
Again, see the page above to see what that means.
