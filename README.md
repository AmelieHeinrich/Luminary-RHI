# Luminary RHI : cross-platform render hardware interface

Luminary RHI is a render hardware interface used by the prioprietary Luminary engine. It is written entirely in C. It's MIT licensed, you are free to use/modify/distribute it as you like in your project.

It is intended for modern day graphics, and thus forces a few things to work. Bindless is mandatory. Mesh shaders, raytracing and multi draw indirect can optionally be used. Concepts like barriers, synchronization, GPU residency, resource views are explicit.

## Status

- Metal 3 : stable
- Metal 4 : stable
- Vulkan : unimplemented
- D3D12 : unimplemented

## Requirements

- Metal 3 backend: Apple Silicon
- Metal 4 backend: macOS/iOS 26, Apple Silicon
- Vulkan backend: VK_EXT_descriptor_indexing, VK_EXT_mutable_descriptor_type, VK_KHR_unified_image_layouts
- D3D12 backend: Shader model 6.6

## ...

WIP... come back later
