# VUCOL
VUlkan COmpute Library

## Features
- OpenGL-like syntax
- No global state machine in OpenGL
- Explicit control of GPU commands(buffer bindings, queueing commands)
- Automatic buffer protection between commands

## Motivations
This projected started as a basis of DNN library development.
While there are several alternatives including kompute, I found that most of GPGPU platforms are vendor-specific(ROCm), lack of support(OpenCL), and lack of some explicit control about GPU control resources(kompute).
Thus I started to develop an API similar to OpenGL, but slightly more explicit than OpenGL.
For that reason, this library doesn't care about CPU-GPU data transfer latency while it cares about queueing multiple commands and re-using same shader pipeline resources with different buffer bindings.

## Contribution
Though this project is started as my toy project, if you want to contribute, then please don't hesitate to contact to me via issue.
