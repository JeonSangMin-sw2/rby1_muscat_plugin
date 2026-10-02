"""
Muscat plugin backend — Ros2 Camera Pub

On-robot (default):
  muscat-pk dev:back
  python main.py

Remote dev (from your laptop):
  Set env vars before running:
    MUSCAT_ROBOT_HOST=192.168.1.100
    MUSCAT_ROBOT_TOKEN=<service token>
  Then: muscat-pk dev:back

Handlers:
  plugin.serve / plugin.subscribe / plugin.publish  -> plugin UI <-> backend only
  plugin.router.on / subscribe / emit               -> external endpoints
    (REST / Socket.IO / TCP / MQTT / WebRTC at common/plugin/<plugin_id>/<path>).
    Needs a robot running rainbow-rb-sdk >= 0.0.9.dev5; older robots have no
    `plugin.router` (AttributeError). See README "External endpoints".
"""

import asyncio
import os
from typing import Any

from rainbow.rb_sdk import RBManipulateSDK, RBPluginCall
from rainbow.rb_sdk.rb_sdk_cpp import ConnectOptions


PLUGIN_NAME = "Ros2 Camera Pub"

plugin = RBPluginCall()
manipulate = RBManipulateSDK()


def register_handlers():
    # Register handlers here — called after connect() so subscriptions survive
    # session reconnection in remote-dev mode.

    # --- Plugin UI <-> backend (not reachable from outside Muscat) ---

    @plugin.subscribe("event")
    def on_ui_event(payload: dict[str, Any] | None = None):
        """Handle messages published from the plugin UI with rb.pub("event", ...)."""
        print(f"[{PLUGIN_NAME}] event:", payload)

    @plugin.serve("grip")
    def grip(payload: dict[str, Any] | None = None):
        """Handle the default UI button: rb.action("grip", { width })."""
        width = (payload or {}).get("width", 20)
        print(f"[{PLUGIN_NAME}] grip requested: width={width}")
        plugin.publish("state", {"ready": True, "last_grip_width": width})
        return {"ok": True, "width": width}

    @plugin.serve("tool/echo")
    def echo(payload: dict[str, Any] | None = None):
        """Small query example callable from the UI with rb.action("tool/echo", ...)."""
        return {"echo": payload, "plugin": PLUGIN_NAME}

    # --- External endpoints (REST / Socket.IO / TCP / MQTT / WebRTC) ---
    # Exposed as common/plugin/<plugin_id>/hello, e.g.
    #   curl -X POST http://<robot>/common/plugin/<plugin_id>/hello \
    #        -H 'Content-Type: application/json' -d '{"name": "kim"}'
    # Restrict transports with explicit_routes=["api"] (REST only), etc.
    # Raise PluginRouteError(403, "msg") to set the response status.

    @plugin.router.on("/hello")
    def hello(payload: dict[str, Any] | None = None, meta: dict[str, Any] | None = None):
        """meta = {"transport": "api"|"socket"|"tcp"|"mqtt"|"webrtc", "principal": dict | None}."""
        name = (payload or {}).get("name", "there")
        return {"msg": f"hi {name}", "via": (meta or {}).get("transport")}


def move_j_example():
    """
    Example: use other robot SDKs from the same backend.

    This function is not called automatically. Add safety checks and call it
    from your own handler when the robot is ready.
    """
    return manipulate.move.call_move_j(
        robot_model="C500920",
        target={
            "tar_values": [0, 0, 0, 0, 0, 0],
            "tar_frame": 0,
            "tar_unit": 0,
        },
    )


async def main():
    robot_host = os.getenv("MUSCAT_ROBOT_HOST")
    robot_token = os.getenv("MUSCAT_ROBOT_TOKEN")

    if robot_host and robot_token:
        # Remote dev: connect to the robot before registering any handlers.
        # connect() closes and reopens the Zenoh session, so handlers registered
        # before this call would be lost.
        opts = ConnectOptions()
        opts.token = robot_token
        opts.host = robot_host
        manipulate.connect(opts)

    register_handlers()

    plugin.publish("state", {"ready": True, "plugin": PLUGIN_NAME})       # -> plugin UI
    plugin.router.emit("/state", {"ready": True, "plugin": PLUGIN_NAME})  # -> external subscribers of common/plugin/<plugin_id>/state
    print(f"[{PLUGIN_NAME}] backend ready")

    try:
        while True:
            await asyncio.sleep(1)
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        if robot_host and robot_token:
            await manipulate.disconnect()
        print(f"[{PLUGIN_NAME}] stopped")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
