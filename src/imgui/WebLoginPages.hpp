#pragma once

// The scripts and the page the login windows load, the same on every
// platform: they report through dmSend(name, value), which reaches WebKit's
// message handlers (macOS) or WebView2's postMessage as "name:value"
// (Windows).

#include <string>

#include <nlohmann/json.h>

namespace WebLoginPages
{
	// dmSend(name, value), in every script below
	static const char* const kSend =
		"function dmSend(n, v) {"
		"  try {"
		"    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers[n])"
		"      window.webkit.messageHandlers[n].postMessage(v);"
		"    else if (window.chrome && window.chrome.webview)"
		"      window.chrome.webview.postMessage(n + ':' + v);"
		"  } catch (e) {}"
		"}";

	// Before Discord's own scripts: once the user is in, the page's requests
	// carry the token in their Authorization header; the first such value
	// goes to the app (dmToken).
	inline std::string TokenWatcher()
	{
		return std::string("(function () {") + kSend +
			"  var sent = false;"
			"  function send(t) {"
			"    if (sent || typeof t !== 'string' || t.length < 30 || t.indexOf(' ') >= 0) return;"
			"    sent = true;"
			"    dmSend('dmToken', t);"
			"  }"
			"  var set = XMLHttpRequest.prototype.setRequestHeader;"
			"  XMLHttpRequest.prototype.setRequestHeader = function (k, v) {"
			"    if (String(k).toLowerCase() === 'authorization') send(v);"
			"    return set.apply(this, arguments);"
			"  };"
			"  var f = window.fetch;"
			"  if (f) window.fetch = function (input, init) {"
			"    try {"
			"      var h = init && init.headers;"
			"      if (h) send(typeof h.get === 'function' ? h.get('Authorization') : (h.Authorization || h.authorization));"
			"    } catch (e) {}"
			"    return f.apply(this, arguments);"
			"  };"
			"  window.__dmWatching = true;"
			"})();";
	}

	// The token the page keeps, read through a fresh frame (Discord's page
	// hides its own localStorage): a fallback once it is past the login.
	// Evaluates to the token, or ''.
	static const char* const kStoredToken =
		"(function () {"
		"  try {"
		"    var f = document.createElement('iframe'); f.style.display = 'none';"
		"    document.body.appendChild(f);"
		"    var t = f.contentWindow.localStorage.getItem('token');"
		"    f.remove();"
		"    return t ? JSON.parse(t) : '';"
		"  } catch (e) { return ''; }"
		"})()";

	// For DM_TEST_WEBLOGIN: the page's title and whether the watcher is in.
	static const char* const kTestProbe = "document.title + ' | watcher ' + (window.__dmWatching === true)";

	// A JavaScript string literal of s, safe inside a <script> element.
	inline std::string JsString(const std::string& s)
	{
		std::string j = nlohmann::json(s).dump();
		std::string out;
		for (size_t i = 0; i < j.size(); i++) {
			if (j[i] == '<' && i + 1 < j.size() && j[i + 1] == '/')
				out += "<\\";
			else
				out += j[i];
		}
		return out;
	}

	// The captcha Discord wants before a QR login completes: hCaptcha with
	// Discord's site key (so the page must be loaded as discord.com's).
	// Reports dmCaptcha (the answer) and dmCaptchaLog (its steps).
	inline std::string CaptchaPage(const std::string& sitekey, const std::string& rqdata)
	{
		return std::string(
			"<!doctype html><html><head><meta charset='utf-8'>"
			"<script>") + kSend +
			"function dmLog(m){dmSend('dmCaptchaLog', String(m));}"
			"window.onerror=function(m){dmLog('page error: '+m);};</script>"
			"<script src='https://js.hcaptcha.com/1/api.js?onload=dmReady&render=explicit' async defer"
			" onerror=\"dmLog('hCaptcha script did not load')\"></script>"
			"<style>body{background:#313338;color:#dbdee1;font:15px -apple-system,'Segoe UI',sans-serif;margin:0;"
			"height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center}"
			"p{margin:0 24px 18px;text-align:center}</style></head><body>"
			"<p>Discord wants this check before it finishes the QR login.</p><div id='c'></div>"
			"<script>function dmReady(){dmLog('widget script loaded');try{"
			"var id=hcaptcha.render('c',{sitekey:" + JsString(sitekey) + ",theme:'dark',"
			"callback:function(t){dmLog('solved');dmSend('dmCaptcha', t);},"
			"'error-callback':function(e){dmLog('widget error: '+e);},"
			"'expired-callback':function(){dmLog('answer expired');}});"
			"var rq=" + JsString(rqdata) + ";if(rq)hcaptcha.setData(id,{rqdata:rq});dmLog('widget shown'+(rq?' (with rqdata)':''));"
			"}catch(e){dmLog('render failed: '+e);}}</script></body></html>";
	}
}
