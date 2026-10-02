import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";
import { muscat } from "@rainbow-robotics/muscat-plugin-kit/vite";

// The muscat() preset wires react-refresh, ui/ entry discovery, and marks the
// host-provided runtime (plugin-ui, react) as external. Add your own plugins or
// overrides here as needed.
export default defineConfig({
  plugins: [tailwindcss(), muscat()],
});
