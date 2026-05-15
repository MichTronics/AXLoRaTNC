import { ssrRenderAttrs } from "vue/server-renderer";
import { useSSRContext } from "vue";
import { _ as _export_sfc } from "./plugin-vue_export-helper.1tPrXgE0.js";
const __pageData = JSON.parse('{"title":"Radio config","description":"","frontmatter":{},"headers":[],"relativePath":"guide/radio-config.md","filePath":"guide/radio-config.md"}');
const _sfc_main = { name: "guide/radio-config.md" };
function _sfc_ssrRender(_ctx, _push, _parent, _attrs, $props, $setup, $data, $options) {
  _push(`<div${ssrRenderAttrs(_attrs)}><h1 id="radio-config" tabindex="-1">Radio config <a class="header-anchor" href="#radio-config" aria-label="Permalink to &quot;Radio config&quot;">​</a></h1><p>All radio parameters are set from the serial console and persisted in NVS.</p><h2 id="show-current-config" tabindex="-1">Show current config <a class="header-anchor" href="#show-current-config" aria-label="Permalink to &quot;Show current config&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>radio</span></span></code></pre></div><p>Displays frequency, bandwidth, SF, CR, TX power, and live RSSI/SNR.</p><h2 id="set-parameters" tabindex="-1">Set parameters <a class="header-anchor" href="#set-parameters" aria-label="Permalink to &quot;Set parameters&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>radio freq 869.480    # MHz</span></span>
<span class="line"><span>radio bw   125        # kHz — valid: 7.8 10.4 15.6 20.8 31.25 41.7 62.5 125 250 500</span></span>
<span class="line"><span>radio sf   7          # spreading factor 6–12</span></span>
<span class="line"><span>radio cr   5          # coding rate denominator: 5=4/5  6=4/6  7=4/7  8=4/8</span></span>
<span class="line"><span>radio power 22        # dBm</span></span>
<span class="line"><span>radio reset           # restore variant defaults</span></span></code></pre></div><p>Changes are applied immediately and written to NVS.</p><h2 id="quick-profiles" tabindex="-1">Quick profiles <a class="header-anchor" href="#quick-profiles" aria-label="Permalink to &quot;Quick profiles&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>profile fast      # txdelay=0 p=255 slot=1 fulldup=0 duty=off   (lab/bench only)</span></span>
<span class="line"><span>profile normal    # txdelay=30 p=63 slot=10 fulldup=0 duty=on</span></span></code></pre></div><div class="warning custom-block"><p class="custom-block-title">WARNING</p><p><code>profile fast</code> disables the duty-cycle guard. Use it only on a dummy load or shielded lab setup. On 869 MHz in EU/NL the duty-cycle guard must remain enabled during normal operation.</p></div><h2 id="duty-cycle-guard" tabindex="-1">Duty-cycle guard <a class="header-anchor" href="#duty-cycle-guard" aria-label="Permalink to &quot;Duty-cycle guard&quot;">​</a></h2><div class="language-text vp-adaptive-theme"><button title="Copy Code" class="copy"></button><span class="lang">text</span><pre class="shiki shiki-themes github-light github-dark vp-code" tabindex="0"><code><span class="line"><span>duty          # show current state</span></span>
<span class="line"><span>duty on       # enable (default)</span></span>
<span class="line"><span>duty off      # disable (lab only)</span></span>
<span class="line"><span>duty 10       # set 10 % limit</span></span>
<span class="line"><span>duty 1        # set 1 % limit</span></span>
<span class="line"><span>duty 0.1      # set 0.1 % limit</span></span></code></pre></div><h2 id="kiss-timing-parameters" tabindex="-1">KISS timing parameters <a class="header-anchor" href="#kiss-timing-parameters" aria-label="Permalink to &quot;KISS timing parameters&quot;">​</a></h2><p>KISS TxDelay, Persistence, SlotTime, and FullDuplex are set by the host over the KISS protocol and drive the p-persistent CSMA algorithm.</p><table tabindex="0"><thead><tr><th>Parameter</th><th>Default</th><th>Description</th></tr></thead><tbody><tr><td>TxDelay</td><td>30 (×10 ms = 300 ms)</td><td>Key-up delay before TX</td></tr><tr><td>Persistence</td><td>63</td><td>Probability: (p+1)/256</td></tr><tr><td>SlotTime</td><td>10 (×10 ms = 100 ms)</td><td>CSMA slot interval</td></tr><tr><td>FullDuplex</td><td>0</td><td>1 = skip CSMA listen</td></tr></tbody></table></div>`);
}
const _sfc_setup = _sfc_main.setup;
_sfc_main.setup = (props, ctx) => {
  const ssrContext = useSSRContext();
  (ssrContext.modules || (ssrContext.modules = /* @__PURE__ */ new Set())).add("guide/radio-config.md");
  return _sfc_setup ? _sfc_setup(props, ctx) : void 0;
};
const radioConfig = /* @__PURE__ */ _export_sfc(_sfc_main, [["ssrRender", _sfc_ssrRender]]);
export {
  __pageData,
  radioConfig as default
};
