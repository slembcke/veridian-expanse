# Project Drift
Project Drift is the working name for the asteroid mining game I've been working on. This is the source code, but without the assets. I have a bit of information about the game [on my blog](https://slembcke.github.io/ProjectDrift).

Some highlights:

* Simple C11 implementation
* [Job system](https://slembcke.github.io/DriftJobs) built around [Tina](https://github.com/slembcke/Tina)
* [Hotloading](https://slembcke.github.io/HotLoadC) of code, shaders, and assets in ~200 ms
* Simple, high performance [renderer](https://slembcke.github.io/Drift-Renderer) with GL3 and Vulkan backends
* Dynamic, single pass [soft shadows](https://slembcke.github.io/SuperFastSoftShadows)
* Normal mapped lighting via 2D light fields
* Simple ECS(ish) based game objects
* High performance physics engine
* High performance r-tree
* MIT License: Use whatever you find useful

# How to Build:
At the moment... you can't. The build process relies on the assets, which I haven't included.