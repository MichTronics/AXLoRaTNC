import { ssrRenderAttrs } from "vue/server-renderer";
import { useSSRContext } from "vue";
import { _ as _export_sfc } from "./plugin-vue_export-helper.1tPrXgE0.js";
const __pageData = JSON.parse('{"title":"","description":"","frontmatter":{"layout":"home","hero":{"name":"AXLoRaTNC","text":"AX.25 over LoRa for ESP32","tagline":"Full AX.25 Level 2 connected mode — LoRa replaces the classic AFSK/FSK modem layer. KISS · WA8DED · APRS · NET/ROM · BBS","actions":[{"theme":"brand","text":"Flash firmware →","link":"/flash/"},{"theme":"alt","text":"Getting started","link":"/guide/"},{"theme":"alt","text":"GitHub","link":"https://github.com/MichTronics/AXLoRaTNC"}]},"features":[{"title":"AX.25 Level 2","details":"Full connected-mode state machine — SABM/UA/DISC, sliding window, T1/T2/T3 timers, N2 retry, REJ and SREJ selective retransmit, RNR flow control."},{"title":"KISS & WA8DED","details":"Compatible with kissattach, Dire Wolf, LinBPQ, BPQ32, F6FBB, Graphic Packet, and PaxTerm out of the box."},{"title":"APRS","details":"Automatic APRS frame decode on receive. One-command position beacon with symbol and comment. Digipeater with H-bit update and duplicate suppression."},{"title":"NET/ROM & BBS","details":"NET/ROM NODES broadcast, route table with auto-expiry. Connected node shell with INFO, NODES, MHEARD commands and a built-in NVS mailbox BBS."},{"title":"Supported hardware","details":"ESP32 DevKit V1 + EBYTE E22 (SX1262) · Heltec WiFi LoRa 32 V3 (ESP32-S3) · TTGO T-Beam (SX1276) · LilyGo T3 LoRa32 V1.6.1 · LILYGO T-Beam SUPREME 433MHz"},{"title":"Persistent config","details":"All settings — callsign, radio params, beacon, digi, NET/ROM alias, BBS messages — survive reboots via ESP32 NVS flash storage."}]},"headers":[],"relativePath":"index.md","filePath":"index.md"}');
const _sfc_main = { name: "index.md" };
function _sfc_ssrRender(_ctx, _push, _parent, _attrs, $props, $setup, $data, $options) {
  _push(`<div${ssrRenderAttrs(_attrs)}></div>`);
}
const _sfc_setup = _sfc_main.setup;
_sfc_main.setup = (props, ctx) => {
  const ssrContext = useSSRContext();
  (ssrContext.modules || (ssrContext.modules = /* @__PURE__ */ new Set())).add("index.md");
  return _sfc_setup ? _sfc_setup(props, ctx) : void 0;
};
const index = /* @__PURE__ */ _export_sfc(_sfc_main, [["ssrRender", _sfc_ssrRender]]);
export {
  __pageData,
  index as default
};
