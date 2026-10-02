# Ros2 Camera Pub

A Muscat plugin project. Build, develop, and ship with `muscat-pk` CLI.

## 이 플러그인: RealSense → ROS 2 토픽

툴바 창에서 Intel RealSense의 렌즈(Color · Depth · IR Left · IR Right)를 골라 ROS 2 토픽으로
발행합니다. `rby1_additional_tools`의 `realsense_publisher`(librealsense 직접, realsense-ros 없이)를
옮긴 것으로, `config/realsense.yaml`·`config/camera_mount.yaml`의 값을 화면에서 정합니다.

| 화면 | 내용 |
|---|---|
| 상단 스위치 | 발행 시작/정지. 상태(Streaming · Stopped · Error)와 장치·모드 |
| Camera | 장치(시리얼), 해상도, fps — 연결된 장치가 켜 둔 렌즈로 함께 열 수 있는 값만 보임 |
| Lenses | 렌즈별 켜기와 image/camera_info 토픽 이름, 발행 중이면 실제 Hz. Align depth to color |
| Status | 장치·시리얼·USB·펌웨어·모드, 렌즈별 Hz 카드 |
| Exposure | 컬러/IR 자동 노출, 노출(µs), 게인, IR 프로젝터, 시작 시 버릴 프레임 |
| Frames & mount TF | 영상 좌표계 이름, `link_head_2 → camera_link → camera_optical_frame` 정적 TF |
| Calibration | 공장 보정 대신 보정 파일(camera_ws 또는 ROS 형식 YAML) |

기본 토픽은 원본과 같습니다(`/camera/image_raw`, `/camera/camera_info`, `/camera/depth/…`,
`/camera/ir_left/…`, `/camera/ir_right/…`). 컬러 `bgr8`, 깊이 `16UC1`(mm), 적외선 `mono8`, reliable QoS.

- **설정 저장**: `~/.config/<plugin_id>/camera.json` (노드 파라미터 `config_file`이나 환경 변수
  `ROS2_CAMERA_PUB_CONFIG`로 바꿈). 발행 중이었는지도 저장해서, 플러그인이 다시 뜨면 이어서 발행합니다.
- **librealsense**: `ros2_ws/src/ros2_camera_pub/cmake/librealsense.cmake`가 첫 빌드 때 GitHub에서
  librealsense(v2.58.4, RSUSB 백엔드, DDS 없음)와 libusb를 받아 정적으로 링크합니다. 로봇에 따로 설치할 것이
  없고, 첫 빌드는 몇 분 걸립니다(인터넷 필요).
- **USB 접근**: 노드가 `/dev/bus/usb`에 접근할 수 있어야 장치가 보입니다. `muscat-pk dev:back`의 개발
  컨테이너에는 USB가 넘어가지 않아 "연결된 RealSense가 없습니다"로 나옵니다 — 카메라로 시험하려면 같은
  빌드를 USB를 넘긴 컨테이너에서 띄웁니다:

  ```bash
  docker exec muscat-pk-ros2-dev-run pkill -INT -f ros2_camera_pub_node
  docker run --rm --network host --ipc host --privileged --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v /dev/bus/usb:/dev/bus/usb -v "$PWD":/plugin -w /plugin/ros2_ws \
    -e RB_PLUGIN_ID=com.example.ros2-camera-pub muscat-pk-ros2-dev \
    bash -lc 'source install/setup.bash && exec ros2 run ros2_camera_pub ros2_camera_pub_node'
  ```

  `--ipc host`와 호스트와 같은 사용자(`--user`)가 있어야 호스트·Isaac ROS 컨테이너(Humble)가 Fast DDS 공유
  메모리로 영상을 받습니다(Isaac ROS의 `run_dev.sh`와 같은 조건). root로 띄우면 공유 메모리 파일이 root 전용이
  되어 토픽은 보이는데 데이터가 오지 않습니다. 이 컨테이너 안 설정 파일은 `/tmp/.config/…`라 컨테이너를 내리면
  사라집니다.

UI ↔ 노드: `rb.action("devices" | "config/get" | "config/apply" | "start" | "stop")`, `rb.sub("status")` —
`ros2_ws/src/ros2_camera_pub/src/node.cpp` 맨 위 주석 참고.

## Quick start

```bash
muscat-pk dev        # UI preview + backend together
muscat-pk dev:ui     # UI preview only
muscat-pk dev:back   # backend only (C++ rebuild + restart in Docker)
muscat-pk build      # build + distributable ZIP
```

---

## Project structure

```
ros2-camera-pub/
├─ ui/
│   ├─ content.tsx      # plugin UI entry point
│   └─ style.css        # plugin styles auto-loaded by muscat()
├─ cpp/
│   ├─ CMakeLists.txt   # links rb_sdk::rb_sdk (C++ SDK)
│   └─ main.cpp         # C++ backend entry point
├─ assets/
│   └─ icon.svg         # icon referenced from manifest
├─ manifest.json        # metadata, contribution points, runtime config
└─ README.md            # this file (copied to dist/ on build)
```

> ROS2 (rclcpp) plugins have `ros2_ws/` instead of `cpp/`.

---

## Frontend (`ui/content.tsx`)

### JavaScript dependencies

`muscat-pk build` runs this plugin project's own Vite build. Vite resolves
imports from installed `node_modules`; it does not care whether a package is
listed under `dependencies` or `devDependencies`, but the package must be
installed.

Do not run a production-only install such as `npm install --omit=dev` before
building the plugin UI, because build tools like `vite`, `typescript`,
`tailwindcss`, `@tailwindcss/vite`, `sass`, or `less` are usually
`devDependencies`.

The scaffold keeps this split:

| package kind | examples | why |
|---|---|---|
| `dependencies` | `@rainbow-robotics/plugin-ui`, UI libraries imported by plugin source | Author-facing APIs used by `ui/*.tsx` |
| `devDependencies` | `vite`, `typescript`, `tailwindcss`, `@tailwindcss/vite`, `sass`, `less` | Build/dev tools needed before `dist/*.js` is produced |

The built plugin ZIP does not include `package.json` or `node_modules`. For
production, Muscat provides `@rainbow-robotics/plugin-ui`, React, and ReactDOM
through the host import map; plugin UI build output imports those as host
externals.

### Styles

`muscat()` automatically loads `ui/style.{css,scss,sass,less}` (or
`ui/styles.{css,scss,sass,less}`) for every UI entry, plus
`ui/{entry}.{css,scss,sass,less}` for a matching entry-specific stylesheet. You
do not need to import CSS from every `.tsx` file.

The scaffold uses Tailwind's theme/utilities layers only, so plugin utilities
are generated from files under `ui/` without applying Tailwind preflight resets
to the host page.

Built CSS selectors are scoped under the current plugin id
(`[data-muscat-plugin-id="..."]`). CSS-in-JS libraries such as Emotion work
normally, but global styles should be scoped by the author.

#### Tailwind

This template enables Tailwind in `vite.config.ts`:

```ts
import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";
import { muscat } from "@rainbow-robotics/muscat-plugin-kit/vite";

export default defineConfig({
  plugins: [tailwindcss(), muscat()],
});
```

The default `ui/style.css` uses Tailwind theme/utilities only, not preflight:

```css
@layer theme, utilities;

@import "tailwindcss/theme.css" layer(theme);
@import "tailwindcss/utilities.css" layer(utilities);

@source "./**/*.{ts,tsx}";
```

#### SCSS, Sass, and Less

Install the preprocessor package, then create one of the auto-loaded style files.

```bash
npm i -D sass
```

```scss
/* ui/style.scss */
$accent: #3b82f6;

.panel {
  border: 1px solid color-mix(in srgb, $accent 40%, transparent);

  &__title {
    color: $accent;
  }
}
```

For Less, install `less` and use `ui/style.less`.

#### Emotion

Emotion dependencies may be in `dependencies` or `devDependencies`; Vite can
bundle them when they are installed and imported by the plugin.

For `styled`, no Vite config change is needed:

```bash
npm i @emotion/react @emotion/styled
```

```tsx
import styled from "@emotion/styled";

const Panel = styled.div({
  display: "grid",
  gap: 12,
});
```

For Emotion's `css` prop, pass React plugin options through `muscat()` and add
`jsxImportSource` to `tsconfig.json`:

```bash
npm i @emotion/react @emotion/styled
npm i -D @emotion/babel-plugin
```

```ts
// vite.config.ts
import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";
import { muscat } from "@rainbow-robotics/muscat-plugin-kit/vite";

export default defineConfig({
  plugins: [
    tailwindcss(),
    muscat({
      react: {
        jsxImportSource: "@emotion/react",
        babel: {
          plugins: ["@emotion/babel-plugin"],
        },
      },
    }),
  ],
});
```

```json
{
  "compilerOptions": {
    "jsxImportSource": "@emotion/react"
  }
}
```

Emotion `Global` writes global CSS at runtime, so scope it manually or avoid it
inside plugins.

### useRb() hook

Access the communication API between the plugin module and UI from any plugin component.

```tsx
import { useRb } from "@rainbow-robotics/plugin-ui/sdk";

function App() {
  const rb = useRb();
}
```

### rb API

| Method                                                                                                             | Description                                                                                                                                                |
| ------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `rb.pub(topic, payload)`                                                                                           | Publish a message to a topic                                                                                                                               |
| `rb.sub(topic, handler)`                                                                                           | Subscribe to a topic — returns `{ undeclare() }`                                                                                                           |
| `rb.action(topic, payload?)`                                                                                       | Send a request and await a reply                                                                                                                           |
| `rb.serve(topic, handler)`                                                                                         | Handle incoming requests from the backend                                                                                                                  |
| `rb.toast({ title?, message, type?, actionLabel?, handleActionClick?, closeOnActionClick?, duration?, onClose? })` | Show a host toast notification. `message` accepts a string or `ReactElement`. `type` can be `"info"`, `"danger"`, `"warning"`, `"caution"`, or `"success"` |
| `rb.alert({ title?, message, buttonText? })`                                                                       | Show a host alert dialog. Resolves when the dialog is closed                                                                                               |
| `rb.confirm({ title?, message, buttonText? })`                                                                     | Show a host confirmation dialog. `buttonText` accepts `{ yes, no }`. Resolves to `true` or `false`                                                         |
| `rb.prompt({ title?, message, value?, placeholder?, buttonText? })`                                                | Show a host prompt dialog. `buttonText` accepts `{ yes, no }`. Resolves to the entered string or `undefined`                                               |

Topic names passed to `pub`, `sub`, `action`, and `serve` are isolated per plugin.


All methods support Generic Types:

```tsx
const status = await rb.action<{ ok: boolean }, { id: number }>(
  "get-status",
  { id: 1 },
);

const sub = await rb.sub<{ value: number }>("sensor/data", (data) => {
  console.log(data.value);
});
sub.undeclare(); // cleanup

const ok = await rb.confirm({
  title: "Run",
  message: "Start the backend action?",
});
if (ok) {
  await rb.action("run");
}
```

### Saving and restoring form data (`useRbFormData` / `useRbSave`)

If the host embeds your plugin in a form it owns (e.g. a program step editor
with its own Save button), it can hand your plugin previously-saved values and
pull the current values back out when the user saves. The host — not the
plugin — persists the data (IndexedDB, backend, etc.).

```tsx
import { useState } from "react";
import { useRbFormData, useRbSave } from "@rainbow-robotics/plugin-ui/react";

type GripArgs = { targetName: string; width: number };
const DEFAULTS: GripArgs = { targetName: "", width: 20 };

export default function App() {
  // Previously-saved values, or undefined for a brand-new instance.
  const saved = useRbFormData<GripArgs>();
  const [args, setArgs] = useState<GripArgs>({ ...DEFAULTS, ...saved });

  // Registers a getter the host calls when its Save button is clicked.
  // Always returns the CURRENT values — re-registering on every render keeps
  // the closure fresh, which useRbSave already handles for you.
  useRbSave(() => ({
    args,
    summary: `${args.targetName || "target"} · ${args.width}mm`,
  }));

  return (/* ...form UI reading/writing `args`... */);
}
```

The value returned from `useRbSave` must be JSON-serialisable — the host
persists it as-is. Not every host embeds plugins in a save/restore form; if
yours doesn't, you can skip these hooks entirely.

### UI components

Import Muscat components from `@rainbow-robotics/plugin-ui/react` and export a
React component as the default export. The host mounts that component directly
and provides `useRb()` through context.

```tsx
import { useState } from "react";
import { RBLabelButton, RBSlider, RBSwitch } from "@rainbow-robotics/plugin-ui/react";
import { useRb } from "@rainbow-robotics/plugin-ui/sdk";

export default function App() {
  const rb = useRb();
  const [width, setWidth] = useState(20);
  const [enabled, setEnabled] = useState(true);

  return (
    <div className="flex flex-col gap-4">
      <RBSlider
        value={[width]}
        min={0}
        max={100}
        onValueChange={(values: number[]) => setWidth(values[0] ?? 0)}
      />
      <RBSwitch checked={enabled} onCheckedChange={setEnabled} />
      <RBLabelButton
        disabled={!enabled}
        onClick={() => rb.action("grip", { width })}
      >
        Grip
      </RBLabelButton>
    </div>
  );
}
```

---

## Developing directly on the robot PC

If you're running `muscat-pk dev` **on the robot PC itself**, Muscat is likely already
running its own Socket.IO bridge on port 10000. In that case the CLI detects the port
conflict and asks:

```
⚠ port 10000 is already in use — can't start Socket.IO bridge.
  Proceed without Socket.IO bridge? [y/N]:
```

Answer **y**. Your plugin backend will connect to the Muscat bridge that's already
running instead of starting a new one — exactly what you want when developing on the
robot.

> If you answer **n** (or just press Enter), the CLI exits so you can free the port
> manually and retry.

---

## Backend runtimes

`manifest.json` chooses one backend runtime. `muscat-pk dev` runs backend code in
a Docker container matching the robot environment. Backend code should contain
plugin logic only; it should not start the Muscat bridge or its own copy of the
plugin gateway.

### Which backend pattern? (depends on the contribution point)

**How you control the robot depends on what your plugin contributes** (see
`manifest.json` → `contributes[].point`). Don't mix these up:

| Contribution point | What it is | Backend pattern |
| --- | --- | --- |
| `program.panel` (with `commandInfo`) | Defines a **custom command** the plugin adds to the Program page's rule-based sequence builder. The operator places it into the program tree like any built-in command (Move J, Point, Wait, …); it runs when the program reaches that step. | **`PluginProgramContext`** — integrated with the program's stop/pause and variables. See _Program command backend_ below. |
| `toolbar` / `lnb` / `move.panel` | A live UI panel/popover the operator interacts with directly | Call the robot **SDK directly** (`RBManipulateSDK`) from `cpp/main.cpp`, or `rb.action(...)` → your backend → SDK. **Not** a program step — there is no stop/pause/variable context. |

Rule of thumb: use `PluginProgramContext` **only** for a `program.panel` command
that runs as part of a program. To move the robot live from a toolbar/move panel,
use the SDK — a program-step context does not apply.

### Frontend-only (`none`)

Use this when the plugin only has UI and calls host services:

```json
{ "runtime": { "type": "none" } }
```

### C++ (`cpp`)

C++ plugins build `cpp/` with CMake and run the executable named by
`manifest.json` → `runtime.entry`. The Rainbow Robotics C++ SDK
(`rainbow-rb-sdk-dev`) is pre-installed in the `muscat-pk` dev container — you
don't install anything locally.

```json
{ "runtime": { "type": "cpp", "entry": "ros2_camera_pub" } }
```

```cmake
# cpp/CMakeLists.txt
find_package(rb_sdk REQUIRED)
add_executable(ros2_camera_pub main.cpp)           # name must equal runtime.entry
target_link_libraries(ros2_camera_pub PRIVATE rb_sdk::rb_sdk)
```

Headers live under `<rainbow/rb_sdk/...>`:

| Header | Class | Use |
| --- | --- | --- |
| `plugin/rb_plugin.hpp` | `rb_plugin::RBPluginCall` | plugin UI ↔ backend, external endpoints (`router()`) |
| `manipulate/rb_manipulate_sdk.hpp` | `RBManipulateSDK` | control a manipulator |
| `program/rb_program_sdk.hpp` | `RBProgramSDK` | program-side helpers |
| `program/plugin_program_context.hpp` | `rb_plugin::PluginProgramContext` | `program.panel` commands |
| `zenoh_client.hpp` | `ZenohClient` | low-level pub/sub/query |

Payloads are `nlohmann::json` (`#include <nlohmann/json.hpp>`, installed with the SDK).

#### Talking to the plugin UI (`RBPluginCall`)

```cpp
#include <rainbow/rb_sdk/plugin/rb_plugin.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;
using rb_plugin::RBPluginCall;

int main() {
  // plugin id comes from env RB_PLUGIN_ID (set by muscat-pk dev and by the robot)
  auto plugin = RBPluginCall::get_instance(RBPluginCall::resolve_plugin_id(std::nullopt));

  plugin->publish("state", {{"ready", true}});                 // UI: rb.sub("state", ...)

  auto sub = plugin->subscribe("event",                        // UI: rb.pub("event", ...)
      [](const std::string& topic, const json& payload) { /* ... */ });

  plugin->serve("tool/echo", [](const json& payload) {         // UI: await rb.action("tool/echo", ...)
    return json{{"echo", payload}};
  });

  json res = plugin->call("tool/ping", {{"hello", "plugin"}}, 5.0);  // UI answers with rb.serve("tool/ping", ...)

  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));    // stay resident
}
```

| C++ | UI (`useRb()`) |
| --- | --- |
| `plugin->serve(topic, fn)` | `await rb.action(topic, payload)` |
| `plugin->subscribe(topic, fn)` | `rb.pub(topic, payload)` |
| `plugin->publish(topic, payload)` | `rb.sub(topic, handler)` |
| `plugin->call(topic, payload, timeout_sec)` | `rb.serve(topic, handler)` |

- Keep the `Subscription` returned by `subscribe`; pass it to `plugin->unsubscribe(std::nullopt, sub)` to remove just that one.
- `call` throws `rb_plugin::RBPluginGatewayError` when the gateway is down or the UI doesn't answer in time.

#### External endpoints (`router()`)

`serve` / `subscribe` / `publish` are **internal only** (plugin UI ↔ backend).
To expose an endpoint to the outside world — REST, Socket.IO, TCP, MQTT,
WebRTC — use `plugin->router()`. The running Muscat (`common`) picks it up
without a restart, and it disappears when the plugin stops.

```cpp
auto& router = plugin->router();

router.on("/hello", [](const json& payload, const json& meta) {       // all 5 transports
  return json{{"msg", "hi " + payload.value("name", std::string("there"))}};
});

router.on("/admin", [](const json& payload, const json& meta) {
  if (!meta.value("principal", json::object()).value("is_admin", false))
    throw rb_plugin::PluginRouteError(403, "admin only");              // becomes the external status
  return json::object();
}, std::vector<std::string>{"api"});                                  // REST only

router.subscribe("/cmd", [](const json& payload) { /* ... */ },
                 std::vector<std::string>{"tcp"});                    // receive external TCP publish

router.emit("/state", {{"busy", true}});                               // push to external subscribers
```

```bash
curl -s -X POST http://<robot>/common/plugin/<plugin_id>/hello \
  -H 'Content-Type: application/json' -d '{"name":"kim"}'
# → {"msg":"hi kim"}
```

| API | Meaning | External address |
| --- | --- | --- |
| `router.on(path, handler, explicit_routes)` | request/response handler | `common/plugin/{plugin_id}/{path}` (REST is always `POST`) |
| `router.subscribe(topic, handler, explicit_events)` | receive external publishes | external side publishes to `common/plugin/{plugin_id}/{topic}` |
| `router.emit(topic, payload)` | plugin → external push | external side subscribes to `common/plugin/{plugin_id}/{topic}` |

- `meta` = `{"transport": "api"|"socket"|"tcp"|"mqtt"|"webrtc", "principal": object | null}`. **Role checks are the plugin's job.**
- Transport names: `api | socket | tcp | mqtt | webrtc`; `subscribe` accepts only `tcp | mqtt | webrtc`. Unknown names or duplicate paths throw `std::invalid_argument`.
- Throw `rb_plugin::PluginRouteError(code, message)` to set the status; any other exception → 500.

| Situation | Status |
| --- | --- |
| No plugin declares that path / plugin is down / transport not allowed | 404 |
| Plugin did not answer within 10 s | 504 |
| `common` ↔ gateway not connected | 503 |

During `muscat-pk dev` the local bridge accepts `router()` registrations, but
**external calls need a running `common`** (a robot or a local `common`). After
restarting the plugin, external calls may return 504 for about a minute, then 404.

#### Program command backend (`program.panel`)

A `program.panel` contribution adds a **custom command** to the Program page.
Each time the program reaches that step, the host calls your backend through
`PluginProgramContext` with the step args (what the UI saved with `useRbSave`).

```cpp
#include <rainbow/rb_sdk/program/plugin_program_context.hpp>

using rb_plugin::PluginProgramContext;

int main() {
  PluginProgramContext ctx("com.example.ros2-camera-pub");

  ctx.set_execute_handler([&](const nlohmann::json& args) {
    auto count = ctx.get_variable("counter");                  // json; null if no program runs
    ctx.set_variable("counter", count.is_number() ? count.get<int>() + 1 : 1);
    ctx.set_variable("RBP_ros2_camera_pub_result", 42);        // NEW variables must start with RBP_

    try {
      for (int i = 0; i < 100; ++i) {
        ctx.check_stop();                                      // pause → blocks, stop → throws PluginStopped
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    } catch (const rb_plugin::PluginStopped&) {
      return;                                                  // program was stopped
    }
    ctx.done();                                                // always call — the step ends here
  });

  ctx.start();                                                 // does not block
  for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
}
```

- The step does not finish until you call `ctx.done()`.
- `set_execute_handler` must be called before `start()`.
- **New** variables need the `RBP_` prefix (`RBP_{plugin_id}_{name}` recommended); existing ones pass as-is.
- `get_variable(name, robot_model, plugin_id)` — optional filters: a specific robot, or the value a given plugin last wrote.
- Executor state: `ctx.executor_state_for("C500920")`, `ctx.executor_states()`, `ctx.is_paused()`, `ctx.is_stopped()`,
  or `ctx.set_state_change_handler([](const std::string& model, const std::string& state) { ... })`.
- A long-running handler must call `check_stop()` every iteration, or it keeps running after the operator presses Stop.

Variables the Program page variable picker should offer must be declared in
`manifest.json` under `"variables"`.

#### Controlling the robot

```cpp
#include <rainbow/rb_sdk/manipulate/rb_manipulate_sdk.hpp>

RBManipulateSDK manipulate;                       // on the robot: connects to the local Zenoh
manipulate.state.call_servocontrol("C500920", 1);

IPC::MoveInput_TargetT t;
t.tar_values = {0, 0, 90, 0, 90, 0};
t.tar_frame = 0;
t.tar_unit = 1;
manipulate.move.call_move_j("C500920", t, std::nullopt, MoveSpeedArg{1, 60.f, 120.f});
manipulate.move.call_move_wait("C500920", -1);
```

Remote dev (`muscat-pk dev --robot-ip <ip> --robot-token <token>`) sets
`MUSCAT_ROBOT_HOST` / `MUSCAT_ROBOT_TOKEN`; connect **before** registering
subscriptions, because `connect()` reopens the Zenoh session:

```cpp
if (const char* host = std::getenv("MUSCAT_ROBOT_HOST"))
  if (const char* token = std::getenv("MUSCAT_ROBOT_TOKEN"))
    manipulate.connect({.token = token, .host = host});
```

AMR and RB-Y1 SDKs are Python-only for now.

### ROS2 (rclcpp)

ROS2 C++ plugins use `ros2_ws/` with `runtime.rosClient: "rclcpp"`. The node
links the C++ SDK the same way (`find_package(rb_sdk)` + `rb_sdk::rb_sdk`) and is
launched with `--ros-args -r __ns:=/{slug}` automatically.

```cpp
#include "rclcpp/rclcpp.hpp"
#include <rainbow/rb_sdk/manipulate/rb_manipulate_sdk.hpp>

class Ros2CameraPubNode : public rclcpp::Node {
public:
  Ros2CameraPubNode() : Node("ros2_camera_pub_node") {
    RCLCPP_INFO(this->get_logger(), "Ros2 Camera Pub node started");
  }
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Ros2CameraPubNode>());
  rclcpp::shutdown();
  return 0;
}
```

`RBPluginCall` and `PluginProgramContext` work in an rclcpp node too — create
them in `main()` before `rclcpp::spin`.

---

## Video streaming (MediaMTX)

Muscat runs a MediaMTX media server. The backend publishes a stream over RTSP and
the UI plays it over WebRTC. `muscat-pk dev` runs the same server (or uses a local
Muscat's), so the addresses are identical in dev and on the robot.

| | Address |
| --- | --- |
| Publish (backend) | `rtsp://127.0.0.1:8554/<plugin_id>/<name>` |
| Play (UI) | `http://<host>:8889/<plugin_id>/<name>/` |

- Prefix the path with your plugin id — paths are shared with robot cameras and other plugins.
- The robot runtime has no ffmpeg, GStreamer or OpenCV; bring your own encoder (below).

**Backend (C++)** — publish RTSP with an encoder library you bring (e.g. FFmpeg's
libavformat / libx264); the robot has none installed. Ship it with the plugin — see
_Bundling extra shared libraries_.

**UI** — embed MediaMTX's WebRTC player. `location.hostname` is `localhost` in the
preview and the robot's address on the robot:

```tsx
const STREAM = "com.example.ros2-camera-pub/cam"; // the path the backend publishes

export default function Camera() {
  return (
    <iframe
      src={`${location.protocol}//${location.hostname}:8889/${STREAM}/`}
      allow="autoplay"
      className="aspect-video w-full border-0"
    />
  );
}
```

---

## manifest.json

```jsonc
{
  // unique plugin ID — reverse-domain format recommended
  "id": "com.example.my-plugin",
  "name": "My Plugin",
  "version": "1.0.0",

  // minimum Muscat version required to load this plugin
  "minMuscatVersion": "1.0.0",

  // plugin author (empty string is fine)
  "author": "Your Name",

  // displayed in the Muscat plugin store / info panel
  // string or { en (required), ko, ja, zh, vi, th, es }
  "description": { "en": "Plugin description", "ko": "플러그인 설명" },

  // SVG string (single file) or PNG/JPG size map (all three sizes required)
  "icons": "assets/icon.svg",
  // "icons": { "16": "assets/icon-16.png", "48": "assets/icon-48.png", "128": "assets/icon-128.png" },

  // targetModels and/or targetCategorys — at least one is required
  // "all" = every target, or list specific values
  "targetModels": "all",
  // "targetModels": ["C500920", "C500930"],
  "targetCategorys": "all",
  // "targetCategorys": ["manipulate", "amr", "rby1", "vision"],

  "contributes": [
    // program.panel — defines a custom command for the Program page sequence
    //   builder; runs via PluginProgramContext when the program reaches it
    {
      "point": "program.panel",
      "icon": "assets/icon.svg",
      "label": { "ko": "내 플러그인", "en": "My Plugin" },
      "ui": "program",           // must be unique across all contributions
      "commandInfo": {
        "name": "My Plugin",
        "icon": "assets/icon.svg",
        "funcName": "rb_program_sdk.plugin_execute",
        "args": {}
      }
    },
    // toolbar — adds a button to the top toolbar (required)
    {
      "point": "toolbar",
      "icon": "assets/icon.svg",
      "label": { "ko": "내 플러그인", "en": "My Plugin" },
      "ui": "content",           // must be unique across all contributions
      "opens": "popover"         // popover | panel | page
    },
    // move.panel — adds a panel to the Move screen
    {
      "point": "move.panel",
      "icon": "assets/icon.svg",
      "label": { "ko": "내 플러그인", "en": "My Plugin" },
      "ui": "move"               // must be unique across all contributions
    }
  ],

  // optional: variables this plugin exposes to pyfm programs — the Program
  // page variable picker lists ONLY declared variables. name must start with
  // RBP_; description is optional but without it the UI can't explain the
  // variable, so write one.
  "variables": [
    { "name": "RBP_grip_force", "type": "number", "init": 0.5, "description": { "ko": "그리퍼 힘", "en": "Gripper force" } },
    { "name": "RBP_target_pose", "type": "array", "init": [0, 0, 0] }
  ],

  // runtime — one of:
  //   { "type": "none" }
  //   { "type": "python", "entry": "main.py" }
  //   { "type": "cpp", "entry": "<executable name>" }
  //   { "type": "ros2", "rosClient": "rclpy" | "rclcpp", "package": "...", "launch": "..._node" }
  "runtime": { "type": "cpp", "entry": "ros2_camera_pub" },

  // optional: TCP ports opened by this plugin backend
  // only number[] is allowed; ranges/objects are not accepted
  // external plugin ports must be within 28600-28999
  "ports": [28600]
}
```

**Backend types:**

| type | source dir | build / run | entry |
|---|---|---|---|
| `python` | `py/` | pip + run in Docker | `main.py` |
| `cpp` | `cpp/` | CMake in Docker | built executable name |
| `ros2` (rclpy) | `ros2_ws/` | colcon (ament_python) in Docker | — |
| `ros2` (rclcpp) | `ros2_ws/` | colcon (ament_cmake) in Docker | — |

> Every back end runs in a Docker container matching the robot (Ubuntu 24.04 + Python 3.12), so dev behaves like production and compiled binaries are ABI-compatible. **Docker is required** for all backend types (only a frontend-only `none` plugin skips it). No local Python/toolchain setup is needed.

**Contribution points:**

| point | Where it appears | Backend | `commandInfo` |
|---|---|---|:---:|
| `toolbar` | Top toolbar button (must have at least one) | SDK / `rb.action` | — |
| `lnb` | Left navigation entry | SDK / `rb.action` | — |
| `move.panel` | Move screen panel | SDK / `rb.action` | — |
| `program.panel` | Custom command in the Program page sequence builder | `PluginProgramContext` | ✓ |

**`commandInfo` fields** (for `program.panel`):

| field | type | description |
|---|---|---|
| `name` | `string` | Display name of the command in the program tree |
| `icon` | `string` | Icon path |
| `iconClassName` | `string?` | Optional CSS class for the icon |
| `funcName` | `string?` | Backend function name to call |
| `args` | `Record<string, any>?` | Default arguments |
| `disabled` | `boolean?` | Whether the node is disabled |
| `allowChildren` | `string[]?` | Allowed child commands |
| `allowParents` | `string[]?` | Allowed parent commands |
| `notAllowChildren` | `string[]?` | Disallowed child commands |
| `syncGroupId` | `boolean?` | Sync group ID across children |
| `disableCopy` | `boolean?` | Prevent copy |
| `disableDelete` | `boolean?` | Prevent delete |
| `requireChildren` | `boolean?` | Whether a child command is required |
| `requireParent` | `boolean?` | Whether a parent command is required |
| `childrenCommand` | `string?` | Default command type for children |

**Validation rules:**
- `id`, `name`, `version`, `minMuscatVersion` — required non-empty strings
- `author` — required non-empty string
- `description` — required (`string` or `{ en (required), ko?, ja?, zh?, vi?, th?, es? }`)
- `icons` — SVG: single string path ending in `.svg`; PNG/JPG: object with `"16"`, `"48"`, `"128"` all present
- `targetModels` — `"all"` or a non-empty array of robot model ID strings (at least one of `targetModels` / `targetCategorys` required)
- `targetCategorys` — `"all"` or a non-empty array of `"manipulate" | "amr" | "rby1" | "vision"` (at least one of `targetModels` / `targetCategorys` required)
- `contributes` — required array; must contain at least one `toolbar` entry
- `ui` — each contribution must have a unique value
- `commandInfo` — required when `point` is `"program.panel"`
- `ports` — optional `number[]`; every port must be within `28600-28999`
  and must not be a reserved robot/industrial/internal port
- `variables` — optional array of `{ name, type, init, description? }`:
  - `name` — must start with `RBP_`, then letters/digits/underscore; unique per plugin
  - `type` — `"number" | "string" | "array"`
  - `init` — required initial value, must match `type`
  - `description` — optional `string` or i18n object (`en` required in object form)

`muscat-pk build` throws on any validation error. `muscat-pk dev` prints
warnings — except `variables` errors (e.g. a name without the `RBP_` prefix),
which are hard errors in `dev` / `dev:back` too.

**Backend port rules:**
- Declare a port only if your backend opens a TCP server/listener.
- **Every listening TCP port must be declared.** `muscat-pk dev` and
  `muscat-pk dev:back` watch the backend and stop it the moment it opens a port
  that is not in `ports`. A port outside `28600-28999` can't be declared, so it
  must not be opened either — picking e.g. `5500` is not a way around the rule.
  It would escape this check *and* the robot's install-time conflict check, and
  only collide once it's running on a robot.
- Reserved ports include `502`, `2222`, `4840`, `5170-5179`, `8000-8999`,
  `10000`, `30004`, `34964`, `44818`, `50000-50005`, and `50051`.
- The robot rejects installation (HTTP 409) if a declared port is reserved,
  out of range, or already claimed by another enabled plugin.

**Reaching your backend's ports during development:**
- Declared ports are published to `127.0.0.1` on your machine, so
  `curl http://127.0.0.1:28600` works from the host on macOS, Windows, and Linux.
- On native Linux the dev container uses host networking, so the ports are on
  the host already.
- If a declared port is already taken on your machine, `muscat-pk dev` warns and
  skips publishing that one — the backend still listens inside the container.

---

## Localized documentation

You can ship per-language README files alongside the main `README.md`. Place them in the project root:

```
README.md        # default / English fallback
README.ko.md     # Korean
README.en.md     # explicit English (optional)
README.ja.md     # Japanese
```

All `README*.md` files found at the project root are automatically copied to `dist/` on
`muscat-pk build`. Muscat displays the file matching the user's locale,
falling back to `README.md` if none matches.

---

## C++ SDK & dependencies

The dev container has the C++ SDK (`rainbow-rb-sdk-dev`) pre-installed — no local
install needed. Pick a different SDK version with:

```bash
MUSCAT_PK_RB_SDK_VERSION=<version> MUSCAT_PK_REBUILD_IMAGE=1 muscat-pk dev
```

`muscat-pk build` copies the matching `librb_sdk.so` next to your binary
(`dist/<arch>/lib/`, or `install-<arch>/<pkg>/lib/` for rclcpp), so the robot needs no SDK
install and each plugin keeps the SDK version it was built with.

Other libraries: install them in the build (e.g. vendor them under `cpp/` and
`add_subdirectory`, or link statically) — the robot only has Ubuntu 24.04 base
libraries.

To use the SDK outside a plugin (your own Ubuntu 24.04 machine):

```bash
curl -fsSL https://rainbowrobotics.github.io/rb-sdk/rb-sdk-apt-repo.gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/rb-sdk-apt.gpg
echo 'deb [signed-by=/usr/share/keyrings/rb-sdk-apt.gpg] https://rainbowrobotics.github.io/rb-sdk stable main' | sudo tee /etc/apt/sources.list.d/rb-sdk.list
sudo apt update && sudo apt install rainbow-rb-sdk-dev
```


### Bundling extra shared libraries

The robot only has Ubuntu 24.04 base libraries. Copy any other `.so` your executable
needs into `${CMAKE_BINARY_DIR}/lib` — `muscat-pk build` ships that folder as
`dist/<arch>/lib/` and the executable finds it via its RPATH (`$ORIGIN/lib`):

```cmake
# cpp/CMakeLists.txt — third_party/ holds prebuilt libs for amd64 / arm64
set(ARCH_DIR ${CMAKE_SOURCE_DIR}/third_party/${CMAKE_SYSTEM_PROCESSOR})
target_link_libraries(ros2_camera_pub PRIVATE ${ARCH_DIR}/libfoo.so)
add_custom_command(TARGET ros2_camera_pub POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
  COMMAND ${CMAKE_COMMAND} -E copy ${ARCH_DIR}/libfoo.so ${CMAKE_BINARY_DIR}/lib/)
```

Build them for both arches (`x86_64`, `aarch64`) on Ubuntu 24.04 so they match the robot.
Static linking works too and needs no bundling. For an rclcpp node, `install(FILES ... DESTINATION lib)`
instead — the node finds `install-<arch>/<pkg>/lib/` the same way.
---

## Switching backend type

```bash
muscat-pk type change                 # interactive
muscat-pk type change --to cpp        # → C++
muscat-pk type change --to ros2       # → ROS2 (rclpy / Python)
muscat-pk type change --to ros2-cpp   # → ROS2 (rclcpp / C++)
muscat-pk type change --to python     # → Python
```

> Existing backend dirs (`ros2_ws/`, `cpp/`) are not deleted when switching away. Remove them manually if no longer needed.

> Switching between a Python and a C++ backend asks whether to replace this README with the matching one (the old file is kept as `README.md.bak`). Add `--readme` to answer yes without the prompt.

---

## AI coding assistants (MCP)

If you build your plugin with an AI assistant (Claude Code, Codex, …), give it
grounded knowledge of the Muscat UI components, design tokens, and SDK instead of
letting it guess. `muscat-pk mcp` is an MCP server exposing:

- `list_components` / `get_component_props` / `get_component_example` — every
  `@rainbow-robotics/plugin-ui/react` component with its real props and a working
  Storybook example.
- `list_design_tokens` / `search_design_tokens` — the Tailwind classes to use
  (`bg-bg-*`, `text-fg-*`, `typo-*`, …) so your UI matches the host theme.
- `search_rb_sdk` — how to use `useRb()`, `rb.*`, `useRbFormData`/`useRbSave`, and
  the backend runtimes.
- `list_rb_sdk` / `get_rb_sdk_api` — the installed `rainbow-rb-sdk` (Python
  backend) classes and signatures; with `lang: "cpp"`, the C++ SDK headers
  (`rainbow-rb-sdk-dev`, read out of the `muscat-pk` cpp dev image — build it
  once with `muscat-pk dev:back`).

**Browse the components live** with `muscat-pk storybook`.

### Register the server

**Claude Code** — a `.mcp.json` is already scaffolded at your plugin root, so
it works out of the box:

```json
{
  "mcpServers": {
    "muscat": {
      "command": "muscat-pk",
      "args": ["mcp"]
    }
  }
}
```

**Codex** — register once with:

```bash
muscat-pk mcp --install codex
```

This appends the server to `~/.codex/config.toml` (idempotent — safe to run
again). Equivalent manual config:

```toml
[mcp_servers.muscat]
command = "muscat-pk"
args = ["mcp"]
```

Any other MCP client works too — it's a standard stdio MCP server started with
`muscat-pk mcp`. If `muscat-pk` isn't on the client's PATH, use its absolute path
(`which muscat-pk`) as `command`.

---

## Shipping

```bash
muscat-pk build            # build + ZIP
muscat-pk build --no-zip   # build only, no ZIP
```

- Builds `ui/content.tsx` → `dist/content.js` (minified)
- Compiles `cpp/` (Release) in the Ubuntu 24.04 container per arch → `dist/<arch>/<entry>` + `dist/<arch>/lib/librb_sdk.so.*`
- ROS2 (rclcpp): runs `colcon build` per arch → `dist/ros2_ws/install-<arch>/`
- Copies `assets/`, `manifest.json`, and `README*.md` into `dist/`
- Output: `ros2-camera-pub-{version}.zip`

ZIP layout:

```
ros2-camera-pub-1.0.0.zip
├─ dist/
│   ├─ content.js        # built UI
│   ├─ manifest.json
│   ├─ assets/
│   ├─ README.md
│   ├─ README.ko.md      # if present
│   ├─ amd64/            # cpp: <entry> + lib/librb_sdk.so.*
│   ├─ arm64/
│   └─ ros2_ws/          # rclcpp: install-amd64/, install-arm64/
```

---

## All commands

```bash
muscat-pk dev        # UI + backend
muscat-pk dev:ui     # UI preview only (preview-host)
muscat-pk dev:back   # backend only (C++ rebuild + restart in Docker)
muscat-pk build      # build → dist/ + distributable ZIP (--no-zip to skip)
muscat-pk storybook  # browse the rb-components UI library in a browser
muscat-pk mcp        # MCP server for AI assistants (components/tokens/SDK docs)
```

```bash
muscat-pk type change    # switch backend type
muscat-pk update         # update muscat-plugin-kit itself
muscat-pk uninstall      # remove muscat-pk itself
```
