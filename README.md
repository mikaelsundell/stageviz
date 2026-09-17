# <img src="resources/AppIcon.png" valign="middle" alt="Stageviz" width="50" height="50"> Stageviz: A playground for USD

[![License](https://img.shields.io/badge/license-BSD%203--Clause-blue.svg?style=flat-square)](https://github.com/mikaelsundell/stageviz/blob/master/README.md)

**Open a stage. See what is really there. Change it. Undo it. Script the rest.**

Stageviz is a lightweight desktop OpenUSD viewer and editor for macOS and Windows. It started as an educational project for learning USD and Hydra from the inside out, and slowly turned into the kind of USD tool I wanted to have around every day.

It is not trying to be a full DCC. It is a focused place to look, inspect, edit, experiment and build small tools around USD without carrying an entire content-creation application with you.

Stageviz is very much an evolving project. Some ideas are experimental, some workflows may still be rough around the edges, while other parts have already been heavily optimized. It is a place to explore different ways of working with USD — not necessarily define the definitive one.

Found a better way? Contributions, issues and experiments are very welcome. Help shape what a small, fast and hackable everyday USD tool can become.

<img src="resources/stageviz.png" style="padding-bottom: 20px;" />

---

## Why Stageviz?

USD is incredibly powerful, but sometimes you just want to open a file and answer a few questions.

> What's actually in this stage?
>
> Where did this prim come from?
>
> Which payloads are loaded?
>
> What happens if I switch this variant?
>
> What material is this mesh using?
>
> Can I move this prim, fix that value and save the file?
>
> Could I make a tiny tool for this instead of doing it manually?

That last question is especially important to Stageviz.

The application has a useful native editing core, but it is also deliberately **hackable**. Python, OpenUSD, Qt and the Stageviz Python bindings are available together, so a workflow can start as an idea and become a shelf tool or a complete dialog surprisingly quickly.

And when you do not feel like writing that tool yourself, there is an **Agent skill** for that too.

---

## The everyday USD stuff

### 🌳 Explore the stage, not just the file

The Stage hierarchy shows the **composed OpenUSD stage**. You are looking at the result of layers, references, payloads, variants and authored opinions working together — not merely a dump of one USD layer.

Navigate large hierarchies, select prims, inspect payloads, switch variants, rename things, reparent things and generally poke around until the stage makes sense.

Stageviz is useful both when you already know exactly what you need to change and when the first task is simply figuring out what somebody else put in the file.

### ✏️ Edit without making it scary

The **Property Tree** turns common USD properties and metadata into practical editors: numbers behave like numbers, colors like colors, booleans like switches, asset paths like paths, and known tokens can be presented as choices.

Edits are authored into the editable USD layer rather than flattening away the structure that makes USD useful.

And importantly, normal Stageviz editing is backed by a **full undo/redo command stack**.

Try something. Inspect the result. Hit Undo. Try something else.

> USD can be sophisticated without every edit feeling dangerous.

### ↩️ Undo and redo all the way

Undo/redo is not an afterthought. Stageviz editing commands are designed around the application's command stack so normal editing operations can be explored and reversed.

Hierarchy edits, namespace operations and other Stageviz commands participate in the same workflow. This also gives the Python bindings an application-level command interface for scripts that should behave like native Stageviz tools.

---

## A viewport for looking *and* understanding

### 🎥 Hydra underneath

The viewport is built on OpenUSD's **Hydra imaging architecture**. Stageviz uses Hydra for interactive rendering while keeping inspection and viewport-only behavior separate from authored scene data where possible.

Orbit, pan, zoom, frame selections and work directly against the composed stage. Scene materials and scene lights can be enabled when you want the authored look, while simpler display and lighting modes are useful when you just want to understand the geometry.

### 🔦 Light it quickly

A USD asset is much easier to judge when it is not sitting in the dark.

Stageviz supports scene lighting, **dome lights and HDRI environments**, making it easy to move from a basic inspection light to image-based lighting when materials need a more useful context.

**OpenImageIO** is part of the image pipeline, giving Stageviz access to the image formats and workflows commonly used in graphics and VFX.

### 🧪 Viewport tricks without damaging the stage

Hydra Scene Indices are used for viewport-side functionality and overrides. That opens the door to useful diagnostic views and material overrides without having to bake every temporary visualization back into the USD stage.

Selection visualization, normal-direction inspection and other debugging looks belong here: useful while you need them, gone when you do not.

---

## Materials should be visual

### 🎨 Material Browser

Materials are much nicer to work with when they look like materials instead of paths in a tree.

The **Material Browser** provides visual material browsing and swatches so you can quickly understand what is available in the stage and select the material you actually meant to work with.

### 🛠 Material Editor

The **Material Editor** exposes useful material parameters in an approachable property interface. Materials can be inspected, edited, renamed and assigned without requiring you to manually navigate every shader prim in the stage hierarchy.

Stageviz works with standard **UsdShade** workflows and supports **MaterialX** through USD/Hydra where supported by the active renderer and OpenUSD configuration.

This makes Stageviz useful for everyday material work, but also for experiments: car paint, plastics, wood-like materials, diagnostic shaders, display-color conversion, material cleanup or whatever strange look-development utility happens to be useful that day.

### 🌈 MaterialX

MaterialX is particularly interesting in Stageviz because it sits nicely between authored USD material networks and renderer-independent material descriptions.

The goal is not to turn Stageviz into a giant node editor. The interesting part is being able to expose the useful interface of a material, work with it visually and let USD, MaterialX and Hydra handle the network underneath.

---

## Python is part of the application

### 🐍 OpenUSD + Stageviz bindings

Stageviz embeds Python and exposes application functionality through its own Python bindings while keeping the normal OpenUSD `pxr` modules available.

That means a script can use **OpenUSD directly for scene data** and use **Stageviz for application state and behavior** such as selection, commands, view state, cameras and rendering.

A tiny script can create geometry directly on the current stage:

```python
import stageviz

from pxr import Gf, UsdGeom

session = stageviz.Session()
stage = session.stage()

cube = UsdGeom.Cube.Define(stage, "/Cube")
cube.CreateSizeAttr(2.0)
cube.CreateDisplayColorAttr([
    Gf.Vec3f(1.0, 0.4, 0.0)
])

session.notifyRedraw()
```

For native editing operations, the Stageviz command bindings provide access to operations that participate in the application's undo/redo workflow. For everything else, the stage returned by `Session.stage()` is a normal OpenUSD stage — use `pxr` and keep going.

### 🚀 The shelf: turn scripts into tools

This is where Stageviz becomes especially useful.

A Python script can live on the **Stageviz shelf** and become a reusable application tool. No C++ rebuild. No plug-in project. No installer for every little pipeline idea.

One shelf button might find empty Xforms. Another might clean an export hierarchy. Another can manage sublayers, inspect payloads, convert display colors into shared materials, validate a stage or build a completely custom Qt dialog.

The shelf is deliberately simple: **if a script solves the problem, that can be the tool.**

---

## 🤖 Stageviz + AI agents

This has become one of the more fun parts of the project.

Stageviz includes an **Agent skill** describing the Stageviz Python interface and the conventions an AI coding agent needs in order to build tools against it correctly.

Instead of explaining your application API from scratch every time, give the skill to a compatible AI agent and describe what you want:

> "Give me a dialog that finds empty leaf Xforms below the current selection."

> "Make a tool that lists the stage sublayers and lets me switch the edit layer."

> "Find all geometry using displayColor and convert the colors into shared materials."

> "Build a material tool that creates a MaterialX material and assigns it to my selection."

The result can be ordinary Python using **Stageviz bindings + OpenUSD + Qt**. Paste it into Stageviz, run it, and a one-off request can become an interactive dialog or a permanent shelf tool.

That makes Stageviz a particularly nice companion for AI-assisted pipeline work: the native application stays small, while specialized workflows can be generated when they are actually needed.

**Read the skill. Ask the agent. Run the tool. Keep the useful ones.**

---

## A few things Stageviz likes doing

### Composition

Payloads, references, variants, sublayers and edit layers are part of normal USD life. Stageviz aims to make them visible and practical rather than treating composition as something hidden behind the final rendered stage.

### Namespace editing

Rename and reorganize prims while working with USD namespace semantics. Stageviz also preserves the surrounding application behavior needed to make operations such as reparenting useful in an interactive editor.

### Selection

Select from the hierarchy or directly in the Hydra viewport. Multi-selection is used by tools where it makes sense, and selection is exposed to Python so scripts can naturally operate on what the user is already working with.

### Cameras and navigation

Frame selections, frame the stage and move around the scene without needing to think about the machinery underneath. Camera and view state are also exposed through the Python bindings for tools that need them.

### Drag, drop, inspect, repeat

Stageviz is a desktop application. Opening a file, dropping in an asset, selecting something, changing a value and continuing should feel ordinary even when the data underneath is USD.

---

## The idea behind it

Stageviz started as a way to learn **OpenUSD, Hydra and Qt by actually building something with them**.

That is still a big part of its personality.

It is intentionally not a giant framework around USD. Whenever possible, the application works with the concepts USD already has and exposes enough of them that Stageviz can also be useful for learning what is going on.

At the same time, educational does not have to mean toy application. The project has gradually accumulated the practical things that make it comfortable to keep open during real work: property editing, material tools, payload and variant workflows, undo/redo, selection, persistent UI, HDRI lighting, Python bindings and the shelf.

The guiding idea is simple:

> **Keep the native editor focused. Make the scripting layer powerful. Let specialized tools grow around it.**

---

## Building

Stageviz is written in C++ using **Qt** and **OpenUSD** and expects a compatible Stageviz third-party environment containing its dependencies.

### macOS

Set the third-party dependency directory:

```shell
export THIRDPARTY_DIR=<path>
```

Build Debug:

```shell
./build_app.sh debug
```

Build Release:

```shell
./build_app.sh release
```

When using debug versions of frameworks on macOS, configure the image suffix where required:

```shell
export DYLD_IMAGE_SUFFIX=_debug
```

### Windows

Configure the Visual Studio development environment. For example:

```shell
"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86_amd64
```

Set the third-party dependency directory:

```shell
set THIRDPARTY_DIR=<path>
```

Build Debug:

```shell
call build_app.bat Debug --deploy
```

Build Release:

```shell
call build_app.bat Release --deploy
```

---

## Built with

### OpenUSD

The scene model, composition system, schemas, Python API and Hydra imaging architecture at the heart of Stageviz.

[openusd.org](https://openusd.org/)

### Qt

The desktop application and UI framework used by Stageviz, and also available to Python tools for building custom dialogs and interfaces.

[qt.io](https://www.qt.io/)

### MaterialX

Renderer-independent material descriptions and shading workflows used through USD and Hydra.

[materialx.org](https://materialx.org/)

### OpenImageIO

Image I/O and image-processing infrastructure used for image-oriented workflows including environment imagery.

[openimageio.readthedocs.io](https://openimageio.readthedocs.io/)

### oneTBB

Task-based parallelism used by the wider OpenUSD/Stageviz dependency stack.

[github.com/uxlfoundation/oneTBB](https://github.com/uxlfoundation/oneTBB)

Additional dependencies are provided through the Stageviz third-party build environment.

---

## Web resources

**Stageviz on GitHub**  
https://github.com/mikaelsundell/stageviz

**Report an issue**  
https://github.com/mikaelsundell/stageviz/issues

**OpenUSD**  
https://openusd.org/

**OpenUSD documentation**  
https://openusd.org/release/index.html

**MaterialX**  
https://materialx.org/

**OpenImageIO**  
https://openimageio.readthedocs.io/

---

## Copyright

Copyright (c) 2025 - present Mikael Sundell.

Stageviz is distributed under the **BSD 3-Clause License**.

### Third-party libraries

Stageviz includes and uses software developed by third parties. The copyrights and terms of those projects remain the property of their respective copyright holders.

Stageviz uses software including OpenUSD, Qt, MaterialX, OpenImageIO, oneTBB and their respective dependencies. Use of these components is subject to the licenses and terms of their respective projects. Their inclusion does not imply endorsement of Stageviz by their authors or organizations.

---

## One more thing

Stageviz is still evolving, and that is intentional.

It began as a way to learn USD by building something with it. Now it is also a place to try ideas, inspect real production data, make small fixes and turn annoying repetitive jobs into tiny tools.

**Open a stage. Pull it apart. Understand it. Change it. Script it. Ask an agent to build the weird tool you need today.**

And hopefully make working with USD a little more enjoyable.
