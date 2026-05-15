import { ssrRenderAttrs } from "vue/server-renderer";
import { useSSRContext } from "vue";
import { _ as _export_sfc } from "./plugin-vue_export-helper.1tPrXgE0.js";
const __pageData = JSON.parse('{"title":"APRS","description":"","frontmatter":{},"headers":[],"relativePath":"guide/aprs.md","filePath":"guide/aprs.md"}');
const _sfc_main = { name: "guide/aprs.md" };
function _sfc_ssrRender(_ctx, _push, _parent, _attrs, $props, $setup, $data, $options) {
  _push(`<div${ssrRenderAttrs(_attrs)}><h1 id="aprs" tabindex="-1">APRS <a class="header-anchor" href="#aprs" aria-label="Permalink to &quot;APRS&quot;">​</a></h1><p>APRS frames are AX.25 UI frames with PID <code>0xF0</code>. AXLoRaTNC decodes incoming APRS frames and logs the parsed content (position, message, status, weather) to the console.</p><h2 id="position-beacon" tabindex="-1">Position beacon <a class="header-anchor" href="#position-beacon" aria-label="Permalink to &quot;Position beacon&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>beacon aprs 52.0167 4.7000 /&gt; LoRa TNC on 869.480 MHz</span></span></code></pre></div><p>Arguments: <code>lat lon [symbol-table+code] [comment]</code>. Default symbol is <code>/&gt;</code> (car). This sets the destination to <code>APRS</code> and formats an uncompressed APRS position info field automatically.</p><p>Then enable the beacon:</p><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>beacon interval 600    # every 10 minutes</span></span>
<span class="line"><span>beacon on</span></span></code></pre></div><h2 id="beacon-commands" tabindex="-1">Beacon commands <a class="header-anchor" href="#beacon-commands" aria-label="Permalink to &quot;Beacon commands&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>beacon                         # show config</span></span>
<span class="line"><span>beacon on / off</span></span>
<span class="line"><span>beacon now                     # transmit immediately</span></span>
<span class="line"><span>beacon text &lt;text&gt;             # set info field manually</span></span>
<span class="line"><span>beacon dest &lt;CALLSIGN-SSID&gt;    # set destination (default CQ)</span></span>
<span class="line"><span>beacon interval &lt;seconds&gt;      # 10–86400 s</span></span>
<span class="line"><span>beacon path &lt;CALL1,CALL2|off&gt;  # set repeater path</span></span></code></pre></div><h2 id="digipeater" tabindex="-1">Digipeater <a class="header-anchor" href="#digipeater" aria-label="Permalink to &quot;Digipeater&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>digi on / off</span></span>
<span class="line"><span>digi mode ui        # relay only UI frames (default, for APRS/beacons)</span></span>
<span class="line"><span>digi mode all       # relay UI and connected-mode AX.25 frames</span></span>
<span class="line"><span>digialias WIDE1-1   # set secondary alias</span></span>
<span class="line"><span>digialias off</span></span></code></pre></div><p>The digipeater matches the first unrepeated address against the node callsign or configured alias, sets the H-bit, recalculates FCS, and retransmits. A 30-second duplicate cache suppresses UI-frame loops.</p><h2 id="mheard" tabindex="-1">Mheard <a class="header-anchor" href="#mheard" aria-label="Permalink to &quot;Mheard&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>mheard          # list stations heard: uptime timestamp, RSSI, SNR, frame count, via flag</span></span>
<span class="line"><span>mheard clear    # reset the table</span></span></code></pre></div><p>The table stores up to 20 entries with first-heard and last-heard times, RSSI, SNR, frame count, and whether the station was heard via a digipeater.</p></div>`);
}
const _sfc_setup = _sfc_main.setup;
_sfc_main.setup = (props, ctx) => {
  const ssrContext = useSSRContext();
  (ssrContext.modules || (ssrContext.modules = /* @__PURE__ */ new Set())).add("guide/aprs.md");
  return _sfc_setup ? _sfc_setup(props, ctx) : void 0;
};
const aprs = /* @__PURE__ */ _export_sfc(_sfc_main, [["ssrRender", _sfc_ssrRender]]);
export {
  __pageData,
  aprs as default
};
