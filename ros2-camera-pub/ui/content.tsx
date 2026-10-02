import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";
import {
  Option,
  RBAccordion,
  RBCheckBox,
  RBInlineLabelButton,
  RBLabelButton,
  RBLight,
  RBSelectBox,
  RBSwitch,
  RBTextField,
  RBTextInput,
} from "@rainbow-robotics/plugin-ui/react";
import { useRb } from "@rainbow-robotics/plugin-ui/sdk";

// What the backend (ros2_ws/src/ros2_camera_pub) keeps and reports.
type LensKey = "color" | "depth" | "ir_left" | "ir_right";
type Stream = { enabled: boolean; image_topic: string; info_topic: string };
type Vec3 = [number, number, number];
type Config = {
  serial: string;
  width: number;
  height: number;
  fps: number;
  streams: Record<LensKey, Stream>;
  align_depth_to_color: boolean;
  frame_id: string;
  depth_frame_id: string;
  ir_right_frame_id: string;
  auto_exposure: boolean;
  exposure: number;
  gain: number;
  ir_auto_exposure: boolean;
  ir_exposure: number;
  emitter: boolean;
  skip_frames: number;
  use_custom_intrinsics: boolean;
  intrinsics_file: string;
  mount: { enabled: boolean; parent: string; frame: string; xyz: Vec3; rpy: Vec3 };
  running: boolean;
};
type Mode = [number, number, number]; // width, height, fps
type DeviceInfo = { name: string; serial: string; usb: string; firmware: string };
type Device = DeviceInfo & { modes: Record<LensKey, Mode[]> };
type StreamStatus = { on: boolean; topic: string; hz: number; frames: number };
type Status = {
  running: boolean;
  device: DeviceInfo | null;
  mode: string;
  streams: Record<LensKey, StreamStatus>;
  problem: string;
  settings_file?: string;
};
type Reply = {
  ok: boolean;
  error?: string;
  config?: Config;
  defaults?: Config;
  status?: Status;
  devices?: Device[];
};

const LENSES: Array<{ key: LensKey; label: string; format: string }> = [
  { key: "color", label: "Color", format: "bgr8" },
  { key: "depth", label: "Depth", format: "16UC1 · mm" },
  { key: "ir_left", label: "IR Left", format: "mono8 · depth 기준" },
  { key: "ir_right", label: "IR Right", format: "mono8" },
];
// Offered when no device is connected to ask.
const FALLBACK_SIZES = ["1280x720", "848x480", "640x480", "640x360", "480x270", "424x240"];
const FALLBACK_FPS = [5, 15, 30, 60, 90];
const FIRST_FOUND = "auto";

const sizeOf = (w: number, h: number) => `${w}x${h}`;
const area = (size: string) => size.split("x").map(Number).reduce((a, b) => a * b, 1);
const errorText = (error: unknown) => (error instanceof Error ? error.message : String(error));
// Settings as saved, without the streaming flag (the switch owns that).
const settingsOf = (config: Config | null) => (config ? JSON.stringify({ ...config, running: false }) : "");

function intersect<T>(sets: Array<Set<T>>): Set<T> {
  if (sets.length === 0) return new Set();
  return sets.reduce((acc, next) => new Set([...acc].filter((v) => next.has(v))));
}

export default function App() {
  const rb = useRb();
  const [saved, setSaved] = useState<Config | null>(null);
  const [draft, setDraft] = useState<Config | null>(null);
  const [defaults, setDefaults] = useState<Config | null>(null);
  const [status, setStatus] = useState<Status | null>(null);
  const [devices, setDevices] = useState<Device[] | null>(null);
  const [offline, setOffline] = useState<string | null>(null);
  const [scanning, setScanning] = useState(false);
  const [pending, setPending] = useState<"apply" | "start" | "stop" | null>(null);

  const call = useCallback(
    async (topic: string, payload?: unknown) => {
      const reply = await rb.action<Reply>(topic, payload);
      if (!reply || reply.ok === false) throw new Error(reply?.error ?? `${topic} failed`);
      return reply;
    },
    [rb],
  );

  const scan = useCallback(async () => {
    setScanning(true);
    try {
      setDevices((await call("devices")).devices ?? []);
    } catch (error) {
      rb.toast({ type: "danger", message: `장치 검색 실패: ${errorText(error)}` });
    } finally {
      setScanning(false);
    }
  }, [call, rb]);

  const load = useCallback(async () => {
    try {
      const reply = await call("config/get");
      setSaved(reply.config ?? null);
      setDraft(reply.config ?? null);
      setDefaults(reply.defaults ?? null);
      setStatus(reply.status ?? null);
      setOffline(null);
      void scan();
    } catch (error) {
      setOffline(errorText(error));
    }
  }, [call, scan]);

  useEffect(() => {
    let alive = true;
    let undeclare: (() => void) | undefined;
    void load();
    rb.sub<Status>("status", (next) => {
      if (alive) setStatus(next);
    })
      .then((sub) => {
        if (alive) undeclare = () => sub.undeclare();
        else sub.undeclare();
      })
      .catch(() => undefined);
    return () => {
      alive = false;
      undeclare?.();
    };
  }, [rb, load]);

  const set = (patch: Partial<Config>) => setDraft((d) => (d ? { ...d, ...patch } : d));
  const setStream = (key: LensKey, patch: Partial<Stream>) =>
    setDraft((d) => (d ? { ...d, streams: { ...d.streams, [key]: { ...d.streams[key], ...patch } } } : d));
  const setMount = (patch: Partial<Config["mount"]>) =>
    setDraft((d) => (d ? { ...d, mount: { ...d.mount, ...patch } } : d));

  const dirty = settingsOf(draft) !== settingsOf(saved);
  const running = status?.running ?? false;

  // The device the settings point at: by serial, or the first one found.
  const device = useMemo(() => {
    if (!devices || !draft) return undefined;
    return draft.serial ? devices.find((d) => d.serial === draft.serial) : devices[0];
  }, [devices, draft]);

  const onLenses = draft ? LENSES.filter((l) => draft.streams[l.key].enabled).map((l) => l.key) : [];
  const size = draft ? sizeOf(draft.width, draft.height) : "";

  // Sizes every switched-on lens opens, and the rates they share at the chosen size.
  const sizes = useMemo(() => {
    if (!device) return FALLBACK_SIZES;
    const keys = onLenses.length ? onLenses : LENSES.map((l) => l.key);
    const found = intersect(keys.map((k) => new Set(device.modes[k].map(([w, h]) => sizeOf(w, h)))));
    return [...found].sort((a, b) => area(b) - area(a));
  }, [device, onLenses.join()]);
  const rates = useMemo(() => {
    if (!device) return FALLBACK_FPS;
    const keys = onLenses.length ? onLenses : LENSES.map((l) => l.key);
    const found = intersect(
      keys.map((k) => new Set(device.modes[k].filter(([w, h]) => sizeOf(w, h) === size).map(([, , f]) => f))),
    );
    return [...found].sort((a, b) => a - b);
  }, [device, onLenses.join(), size]);
  const modeOk = !device || (sizes.includes(size) && rates.includes(draft?.fps ?? 0));

  const finish = (reply: Reply, keepDraft: boolean) => {
    if (reply.config) {
      setSaved(reply.config);
      if (!keepDraft) setDraft(reply.config);
    }
    if (reply.status) setStatus(reply.status);
  };

  const toggle = async (on: boolean) => {
    if (!draft) return;
    setPending(on ? "start" : "stop");
    try {
      if (on) {
        finish(await call("start", { config: draft }), false);
        rb.toast({ type: "success", message: "토픽 발행을 시작했습니다" });
      } else {
        finish(await call("stop"), true);
      }
    } catch (error) {
      rb.toast({ type: "danger", message: errorText(error) });
      void load();
    } finally {
      setPending(null);
    }
  };

  const apply = async () => {
    if (!draft) return;
    setPending("apply");
    try {
      finish(await call("config/apply", { config: draft }), false);
      rb.toast({ type: "success", message: running ? "적용하고 카메라를 다시 시작했습니다" : "설정을 저장했습니다" });
    } catch (error) {
      rb.toast({ type: "danger", message: errorText(error) });
      if (running) void load();
    } finally {
      setPending(null);
    }
  };

  const resetDefaults = async () => {
    if (!defaults || !draft) return;
    const ok = await rb.confirm({
      title: "기본값으로",
      message: "모든 설정을 기본값으로 되돌릴까요? (적용을 눌러야 저장됩니다)",
      buttonText: { yes: "되돌리기", no: "취소" },
    });
    if (ok) setDraft({ ...defaults, serial: draft.serial, running: draft.running });
  };

  if (!draft) {
    return (
      <Shell>
        <div className="flex flex-col items-start gap-3 p-4">
          <p className="typo-body-sm-normal text-fg-subtle">
            {offline ? "카메라 노드에 연결할 수 없습니다." : "불러오는 중…"}
          </p>
          {offline && (
            <>
              <p className="typo-code-xs-normal text-fg-danger break-all">{offline}</p>
              <RBLabelButton size="xs" variant="outlined" onClick={() => void load()}>
                다시 시도
              </RBLabelButton>
            </>
          )}
        </div>
      </Shell>
    );
  }

  const shown = status?.device ?? device ?? null;
  const problem = status?.problem ?? "";

  return (
    <Shell>
      <PublishBar
        running={running}
        problem={problem}
        offline={offline}
        summary={
          running
            ? `${status?.device?.name?.replace("Intel RealSense ", "") ?? ""} · ${status?.mode ?? ""}`
            : "켜면 선택한 렌즈를 토픽으로 발행합니다"
        }
        pending={pending === "start" || pending === "stop"}
        disabled={pending !== null}
        onToggle={(on) => void toggle(on)}
      />

      <div className="flex flex-col gap-6 p-4">
        {/* Camera */}
        <section className="flex flex-col gap-4">
          <Heading title="Camera" subtitle="토픽으로 내보낼 RealSense" />

          <RBTextField
            label="Device"
            helperText={
              devices === null
                ? "검색 중…"
                : devices.length === 0
                  ? "연결된 RealSense가 없습니다 (USB 3 포트, /dev/bus/usb 접근 확인)"
                  : `${devices.length}대 연결됨`
            }
            helperTone={devices?.length === 0 ? "error" : "default"}
          >
            <div className="flex items-center gap-2">
              <RBSelectBox
                className="min-w-0 flex-1"
                value={draft.serial || FIRST_FOUND}
                handleChange={(value) => set({ serial: value === FIRST_FOUND ? "" : String(value) })}
              >
                <Option value={FIRST_FOUND}>처음 찾은 장치</Option>
                {(devices ?? []).map((d) => (
                  <Option key={d.serial} value={d.serial}>
                    {`${d.name.replace("Intel RealSense ", "")} · ${d.serial}`}
                  </Option>
                ))}
                {draft.serial && devices && !devices.some((d) => d.serial === draft.serial) ? (
                  <Option value={draft.serial}>{`${draft.serial} (연결 안 됨)`}</Option>
                ) : null}
              </RBSelectBox>
              <RBLabelButton size="xs2" variant="outlined" loading={scanning} onClick={() => void scan()}>
                검색
              </RBLabelButton>
            </div>
          </RBTextField>

          <div className="grid grid-cols-2 gap-3">
            <RBTextField label="Resolution">
              <RBSelectBox
                className="w-full"
                value={size}
                handleChange={(value) => {
                  const [width, height] = String(value).split("x").map(Number);
                  set({ width, height });
                }}
              >
                {(sizes.includes(size) ? sizes : [size, ...sizes]).map((s) => (
                  <Option key={s} value={s}>
                    {sizes.includes(s) ? s.replace("x", " × ") : `${s.replace("x", " × ")} (지원 안 됨)`}
                  </Option>
                ))}
              </RBSelectBox>
            </RBTextField>
            <RBTextField label="Frame rate">
              <RBSelectBox className="w-full" value={draft.fps} handleChange={(value) => set({ fps: Number(value) })}>
                {(rates.includes(draft.fps) ? rates : [draft.fps, ...rates]).map((f) => (
                  <Option key={f} value={f}>
                    {rates.includes(f) ? `${f} fps` : `${f} fps (지원 안 됨)`}
                  </Option>
                ))}
              </RBSelectBox>
            </RBTextField>
          </div>
          {!modeOk && (
            <p className="typo-body-xs-normal text-fg-danger">
              켜 둔 렌즈를 이 해상도·fps로 함께 열 수 없습니다. 목록에 있는 값을 고르세요.
            </p>
          )}
        </section>

        {/* Lenses */}
        <section className="flex flex-col gap-3">
          <Heading title="Lenses" subtitle="발행할 렌즈를 고르고 토픽 이름을 정합니다" />
          {LENSES.map((lens) => {
            const s = draft.streams[lens.key];
            const missing = device ? device.modes[lens.key].length === 0 : false;
            const live = status?.streams?.[lens.key];
            return (
              <div
                key={lens.key}
                className={`flex flex-col gap-3 rounded-md border p-3 ${
                  s.enabled ? "border-border-default" : "border-border-subtle"
                }`}
              >
                <div className="flex items-center justify-between gap-3">
                  <RBCheckBox
                    name={`lens-${lens.key}`}
                    size="sm"
                    checkValue={true}
                    unCheckValue={false}
                    value={s.enabled}
                    disabled={missing && !s.enabled}
                    handleChange={(value) => setStream(lens.key, { enabled: Boolean(value) })}
                  >
                    <span className="typo-body-sm-semibold text-fg-default">{lens.label}</span>
                    <span className="typo-body-xs-normal text-fg-subtle ml-2">
                      {missing ? "이 장치에 없음" : lens.format}
                    </span>
                  </RBCheckBox>
                  {running && live?.on ? (
                    <span className="typo-code-xs-semibold text-fg-success whitespace-nowrap">
                      {live.hz.toFixed(1)} Hz
                    </span>
                  ) : null}
                </div>
                {s.enabled && (
                  <div className="flex flex-col gap-2">
                    <RBTextField label="Image topic">
                      <RBTextInput
                        size="sm"
                        className="w-full"
                        value={s.image_topic}
                        handleChange={(value) => setStream(lens.key, { image_topic: String(value) })}
                      />
                    </RBTextField>
                    <RBTextField label="Camera info topic">
                      <RBTextInput
                        size="sm"
                        className="w-full"
                        value={s.info_topic}
                        handleChange={(value) => setStream(lens.key, { info_topic: String(value) })}
                      />
                    </RBTextField>
                  </div>
                )}
              </div>
            );
          })}
          <SwitchRow
            label="Align depth to color"
            description="깊이를 컬러 화소·좌표계에 맞춤 (Color, Depth 모두 켤 때)"
            checked={draft.align_depth_to_color}
            disabled={!(draft.streams.color.enabled && draft.streams.depth.enabled)}
            onChange={(v) => set({ align_depth_to_color: v })}
          />
        </section>

        {/* Status */}
        <section className="flex flex-col gap-3">
          <Heading title="Status" subtitle={running ? "지금 발행 중인 값" : "검색된 장치"} />
          {problem ? (
            <p className="typo-body-xs-normal text-fg-danger rounded-md border border-border-danger px-3 py-2">
              {problem}
            </p>
          ) : null}
          <div className="flex flex-col gap-1.5 rounded-md border border-border-subtle p-3">
            <KeyValue label="Device" value={shown?.name || "—"} />
            <KeyValue label="Serial" value={shown?.serial || "—"} />
            <KeyValue
              label="USB"
              value={shown?.usb || "—"}
              tone={shown?.usb?.startsWith("2") ? "text-fg-warning" : undefined}
            />
            <KeyValue label="Firmware" value={shown?.firmware || "—"} />
            <KeyValue label="Mode" value={running ? status?.mode || "—" : "stopped"} />
          </div>
          {running && (
            <div className="grid grid-cols-2 gap-2">
              {LENSES.filter((l) => status?.streams?.[l.key]?.on).map((l) => {
                const live = status!.streams[l.key];
                return (
                  <div key={l.key} className="flex flex-col gap-1 rounded-md border border-border-subtle p-3">
                    <span className="typo-body-2xs-normal text-fg-subtle">{l.label}</span>
                    <span className="flex items-baseline gap-1">
                      <span className="typo-code-lg-semibold text-fg-default">{live.hz.toFixed(1)}</span>
                      <span className="typo-body-xs-normal text-fg-subtle">Hz</span>
                    </span>
                    <span className="typo-code-2xs-normal text-fg-subtler truncate" title={live.topic}>
                      {live.topic}
                    </span>
                  </div>
                );
              })}
            </div>
          )}
        </section>

        {/* Details */}
        <section className="flex flex-col">
          <RBAccordion type="transparent" title="Exposure">
            <div className="flex flex-col gap-3 py-2">
              <SwitchRow
                label="Color auto exposure"
                description="끄면 아래 노출 시간을 씁니다"
                checked={draft.auto_exposure}
                onChange={(v) => set({ auto_exposure: v })}
              />
              <div className="grid grid-cols-2 gap-3">
                <NumberField
                  label="Exposure (µs)"
                  value={draft.exposure}
                  disabled={draft.auto_exposure}
                  onChange={(v) => set({ exposure: v })}
                />
                <NumberField
                  label="Gain"
                  helper="음수면 장치 값"
                  value={draft.gain}
                  onChange={(v) => set({ gain: v })}
                />
              </div>
              <SwitchRow
                label="IR auto exposure"
                description="적외선·깊이 (D405는 컬러와 센서 공유: 컬러 값 적용)"
                checked={draft.ir_auto_exposure}
                onChange={(v) => set({ ir_auto_exposure: v })}
              />
              <NumberField
                label="IR exposure (µs)"
                value={draft.ir_exposure}
                disabled={draft.ir_auto_exposure}
                onChange={(v) => set({ ir_exposure: v })}
              />
              <SwitchRow
                label="IR emitter"
                description="깊이엔 켜고, 깨끗한 적외선 영상엔 끔"
                checked={draft.emitter}
                onChange={(v) => set({ emitter: v })}
              />
              <NumberField
                label="Skip frames"
                helper="시작할 때 노출이 자리 잡는 동안 버리는 프레임"
                value={draft.skip_frames}
                integer
                onChange={(v) => set({ skip_frames: v })}
              />
            </div>
          </RBAccordion>

          <RBAccordion type="transparent" title="Frames & mount TF">
            <div className="flex flex-col gap-3 py-2">
              <TextField label="Color frame" value={draft.frame_id} onChange={(v) => set({ frame_id: v })} />
              <TextField
                label="Depth / IR left frame"
                value={draft.depth_frame_id}
                onChange={(v) => set({ depth_frame_id: v })}
              />
              <TextField
                label="IR right frame"
                value={draft.ir_right_frame_id}
                onChange={(v) => set({ ir_right_frame_id: v })}
              />
              <SwitchRow
                label="Publish mount TF"
                description={`${draft.mount.parent || "parent"} → ${draft.mount.frame || "frame"} → ${draft.frame_id} (static)`}
                checked={draft.mount.enabled}
                onChange={(v) => setMount({ enabled: v })}
              />
              {draft.mount.enabled && (
                <>
                  <div className="grid grid-cols-2 gap-3">
                    <TextField
                      label="Parent link"
                      value={draft.mount.parent}
                      onChange={(v) => setMount({ parent: v })}
                    />
                    <TextField
                      label="Camera frame"
                      value={draft.mount.frame}
                      onChange={(v) => setMount({ frame: v })}
                    />
                  </div>
                  <Vec3Field
                    label="Position xyz (m)"
                    value={draft.mount.xyz}
                    onChange={(xyz) => setMount({ xyz })}
                  />
                  <Vec3Field
                    label="Rotation rpy (rad)"
                    value={draft.mount.rpy}
                    onChange={(rpy) => setMount({ rpy })}
                  />
                </>
              )}
            </div>
          </RBAccordion>

          <RBAccordion type="transparent" title="Calibration">
            <div className="flex flex-col gap-3 py-2">
              <SwitchRow
                label="Custom color intrinsics"
                description="공장 보정 대신 보정 파일 (camera_ws 또는 ROS 형식 YAML)"
                checked={draft.use_custom_intrinsics}
                onChange={(v) => set({ use_custom_intrinsics: v })}
              />
              {draft.use_custom_intrinsics && (
                <TextField
                  label="Intrinsics file"
                  helper="로봇(플러그인 노드) 쪽 경로"
                  placeholder="/path/camera_intrinsics.yaml"
                  value={draft.intrinsics_file}
                  onChange={(v) => set({ intrinsics_file: v })}
                />
              )}
            </div>
          </RBAccordion>
        </section>
      </div>

      <footer className="bg-bg-default border-border-subtle sticky bottom-0 flex items-center justify-between gap-3 border-t px-4 py-3">
        <RBInlineLabelButton size="sm" onClick={() => void resetDefaults()} disabled={pending !== null}>
          기본값
        </RBInlineLabelButton>
        <div className="flex items-center gap-2">
          {dirty && <span className="typo-body-xs-normal text-fg-warning">변경됨</span>}
          <RBLabelButton
            size="xs"
            variant="outlined"
            disabled={!dirty || pending !== null}
            onClick={() => setDraft(saved)}
          >
            되돌리기
          </RBLabelButton>
          <RBLabelButton
            size="xs"
            variant="brand"
            disabled={!dirty || pending !== null}
            loading={pending === "apply"}
            onClick={() => void apply()}
          >
            {running ? "적용 · 재시작" : "적용"}
          </RBLabelButton>
        </div>
      </footer>
    </Shell>
  );
}

function Shell({ children }: { children: ReactNode }) {
  return (
    <div className="bg-bg-default text-fg-default flex max-h-[80vh] w-[440px] max-w-full flex-col overflow-y-auto">
      {children}
    </div>
  );
}

// Streaming on/off with where it stands; stays on top while the settings scroll.
function PublishBar(props: {
  running: boolean;
  problem: string;
  offline: string | null;
  summary: string;
  pending: boolean;
  disabled: boolean;
  onToggle: (on: boolean) => void;
}) {
  return (
    <header className="bg-bg-alt-1 border-border-subtle sticky top-0 z-10 flex items-center justify-between gap-3 border-b px-4 py-3">
      <div className="flex min-w-0 flex-col gap-0.5">
        <StatePill running={props.running} problem={props.problem} offline={props.offline} />
        <p className="typo-body-xs-normal text-fg-subtle truncate">{props.summary}</p>
      </div>
      <RBSwitch
        checked={props.pending ? !props.running : props.running}
        disabled={props.disabled}
        onCheckedChange={(on: boolean) => props.onToggle(on)}
      />
    </header>
  );
}

function Heading({ title, subtitle }: { title: string; subtitle?: string }) {
  return (
    <div>
      <h3 className="typo-body-md-semibold text-fg-default">{title}</h3>
      {subtitle && <p className="typo-body-xs-normal text-fg-subtle">{subtitle}</p>}
    </div>
  );
}

function StatePill({ running, problem, offline }: { running: boolean; problem: string; offline: string | null }) {
  const [color, text] = offline
    ? (["gray", "Offline"] as const)
    : running
      ? problem
        ? (["orange", "No frames"] as const)
        : (["green", "Streaming"] as const)
      : problem
        ? (["red", "Error"] as const)
        : (["gray", "Stopped"] as const);
  return (
    <span className="flex items-center gap-1.5 whitespace-nowrap">
      <RBLight color={color} size="sm" />
      <span className="typo-body-sm-semibold text-fg-default">{text}</span>
    </span>
  );
}

function KeyValue({ label, value, tone }: { label: string; value: string; tone?: string }) {
  return (
    <div className="flex items-center justify-between gap-3">
      <span className="typo-body-xs-normal text-fg-subtle">{label}</span>
      <span className={`typo-code-xs-normal truncate text-right ${tone ?? "text-fg-default"}`}>{value}</span>
    </div>
  );
}

function SwitchRow(props: {
  label: string;
  description?: string;
  checked: boolean;
  disabled?: boolean;
  onChange: (value: boolean) => void;
}) {
  return (
    <div className="flex items-center justify-between gap-3">
      <div className={props.disabled ? "opacity-50" : undefined}>
        <p className="typo-body-sm-medium text-fg-default">{props.label}</p>
        {props.description && <p className="typo-body-xs-normal text-fg-subtle">{props.description}</p>}
      </div>
      <RBSwitch
        size="sm"
        checked={props.checked}
        disabled={props.disabled}
        onCheckedChange={(v: boolean) => props.onChange(v)}
      />
    </div>
  );
}

function TextField(props: {
  label: string;
  value: string;
  helper?: string;
  placeholder?: string;
  onChange: (value: string) => void;
}) {
  return (
    <RBTextField label={props.label} helperText={props.helper}>
      <RBTextInput
        size="sm"
        className="w-full"
        value={props.value}
        placeholder={props.placeholder}
        handleChange={(value) => props.onChange(String(value))}
      />
    </RBTextField>
  );
}

// Keeps what is typed until it is a number, so "-" or "0." can be typed through.
function NumberField(props: {
  label: string;
  value: number;
  helper?: string;
  disabled?: boolean;
  integer?: boolean;
  onChange: (value: number) => void;
}) {
  const [text, setText] = useState(String(props.value));
  useEffect(() => {
    if (Number(text) !== props.value) setText(String(props.value));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [props.value]);
  return (
    <RBTextField label={props.label} helperText={props.helper}>
      <RBTextInput
        size="sm"
        className="w-full"
        inputMode="decimal"
        value={text}
        disabled={props.disabled}
        handleChange={(value) => {
          const next = String(value);
          setText(next);
          const parsed = props.integer ? Number.parseInt(next, 10) : Number(next);
          if (next.trim() !== "" && Number.isFinite(parsed)) props.onChange(parsed);
        }}
      />
    </RBTextField>
  );
}

function Vec3Field({ label, value, onChange }: { label: string; value: Vec3; onChange: (v: Vec3) => void }) {
  return (
    <div className="flex flex-col gap-1.5">
      <span className="typo-body-xs-semibold text-fg-default">{label}</span>
      <div className="grid grid-cols-3 gap-2">
        {(["x", "y", "z"] as const).map((axis, i) => (
          <NumberField
            key={axis}
            label={label.includes("rpy") ? ["roll", "pitch", "yaw"][i] : axis}
            value={value[i]}
            onChange={(v) => {
              const next = [...value] as Vec3;
              next[i] = v;
              onChange(next);
            }}
          />
        ))}
      </div>
    </div>
  );
}
