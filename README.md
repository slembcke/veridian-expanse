# Veridian Expanse
Veridian Expanse is a side project I've been working on for a while now. Lately I've been putting a lot more time into it with plans to release it on Steam. (https://store.steampowered.com/app/2137670/Veridian_Expanse/) It won't be a free game, but I don't see any problem releasing the source code (without the assets) for it, especially since I keep telling people all the "good parts" anyway. I have a bit of information about the game [on my blog](https://slembcke.github.io/ProjectDrift). The working name for the game used to be "Project Drift", so there's still plenty of references around using that name.

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
