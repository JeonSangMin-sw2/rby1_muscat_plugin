# rby1_muscat_ws

Muscat plugins for the Rainbow Robotics RB-Y1. Each folder is one plugin project made with
`muscat-pk` (`@rainbow-robotics/muscat-plugin-kit`).

| Plugin | What it does |
|---|---|
| [ros2-camera-pub](ros2-camera-pub/) | Publishes Intel RealSense lenses (Color · Depth · IR Left · IR Right) as ROS 2 image topics, set up from a Muscat toolbar popover |

## Building a plugin

```bash
cd ros2-camera-pub
npm ci
MUSCAT_PK_RB_SDK_VERSION=0.1.4 npx muscat-pk dev                # UI preview + backend
MUSCAT_PK_RB_SDK_VERSION=0.1.4 npx muscat-pk build --arch both  # dist/ + <name>-<version>.zip
```

- `MUSCAT_PK_RB_SDK_VERSION=0.1.4` is required with muscat-pk 1.3.3: its default SDK (0.1.3)
  no longer installs.
- `--arch arm64` / `both` on an x86 PC needs arm64 emulation
  (`docker run --privileged --rm tonistiigi/binfmt --install arm64`). Jetson robots are arm64.
- Upload the zip to Muscat.

Each plugin's own README has its details.

## Adding a plugin

```bash
muscat-pk new <name>
```

Create it in this folder; build output (`node_modules`, `dist`, `*.zip`, `ros2_ws/build*`,
`install`, `log`) stays out of git.
