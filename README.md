# Luminary RHI : cross-platform render hardware interface

Luminary RHI is a render hardware interface used by the prioprietary Luminary engine. It is written entirely in C. It's MIT licensed, you are free to use/modify/distribute it as you like in your project.

It is intended for modern day graphics, and thus forces a few things to work. Bindless is mandatory. Mesh shaders, raytracing and multi draw indirect can optionally be used.

## Requirements

- Metal 3 backend: Apple Silicon
- Metal 4 backend: macOS/iOS 26, Apple Silicon
- Vulkan backend: Descriptor indexing and mutable descriptor extensions
- D3D12 backend: Shader model 6.6

## ...

WIP... come back later
