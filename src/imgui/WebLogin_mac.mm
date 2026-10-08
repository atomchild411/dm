// WebLogin on macOS: a window with a WKWebView on https://discord.com/login.

#include "WebLogin.hpp"

#include <cstdio>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// Once the user is in, the page's requests carry the token in their
// Authorization header: the script below (in the page before Discord's
// own) hands the first such value to the app.
static NSString* const kWatcher =
	@"(function () {"
	 "  var sent = false;"
	 "  function send(t) {"
	 "    if (sent || typeof t !== 'string' || t.length < 30 || t.indexOf(' ') >= 0) return;"
	 "    sent = true;"
	 "    try { window.webkit.messageHandlers.dmToken.postMessage(t); } catch (e) {}"
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

// The token the page keeps, read through a fresh frame (Discord's page
// hides its own localStorage): a fallback once it is past the login.
static NSString* const kStoredToken =
	@"(function () {"
	 "  try {"
	 "    var f = document.createElement('iframe'); f.style.display = 'none';"
	 "    document.body.appendChild(f);"
	 "    var t = f.contentWindow.localStorage.getItem('token');"
	 "    f.remove();"
	 "    return t ? JSON.parse(t) : '';"
	 "  } catch (e) { return ''; }"
	 "})()";

@interface DMWebLogin : NSObject <WKScriptMessageHandler, WKNavigationDelegate, NSWindowDelegate>
@property (strong) NSWindow* window;
@property (strong) WKWebView* web;
@property (assign) BOOL finished;
@property (assign) BOOL test;
@end

static DMWebLogin* g_login;
static std::function<void(const std::string&)> g_done;
static std::function<void()> g_cancelled;

@implementation DMWebLogin

- (instancetype)initForTest:(BOOL)test
{
	self = [super init];
	_test = test;

	WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
	// nothing stays behind: no cookies, no storage, after the window closes
	cfg.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
	WKUserScript* watcher = [[WKUserScript alloc] initWithSource:kWatcher
		injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES];
	[cfg.userContentController addUserScript:watcher];
	[cfg.userContentController addScriptMessageHandler:self name:@"dmToken"];

	NSRect frame = NSMakeRect(0, 0, 520, 760);
	_window = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
		backing:NSBackingStoreBuffered defer:NO];
	_window.title = @"Log in to Discord";
	_window.releasedWhenClosed = NO;
	_window.delegate = self;
	_web = [[WKWebView alloc] initWithFrame:frame configuration:cfg];
	_web.navigationDelegate = self;
	_web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	_window.contentView = _web;
	[_window center];
	[_web loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"https://discord.com/login"]]];
	if (!test)
		[_window makeKeyAndOrderFront:nil];
	return self;
}

- (void)finishWithToken:(NSString*)token
{
	if (self.finished)
		return;
	self.finished = YES;
	[self.web.configuration.userContentController removeScriptMessageHandlerForName:@"dmToken"];
	[self.window orderOut:nil];
	[self.window close];
	std::string t = token ? std::string(token.UTF8String) : std::string();
	auto done = g_done;
	auto cancelled = g_cancelled;
	g_done = nullptr;
	g_cancelled = nullptr;
	g_login = nil;
	if (!t.empty()) {
		if (done)
			done(t);
	}
	else if (cancelled)
		cancelled();
}

- (void)userContentController:(WKUserContentController*)ucc didReceiveScriptMessage:(WKScriptMessage*)message
{
	if (self.test || ![message.body isKindOfClass:[NSString class]])
		return;
	[self finishWithToken:(NSString*) message.body];
}

- (void)webView:(WKWebView*)web didFinishNavigation:(WKNavigation*)nav
{
	if (self.test) {
		[web evaluateJavaScript:@"document.title + ' | watcher ' + (window.__dmWatching === true)"
			completionHandler:^(id result, NSError* error) {
				fprintf(stderr, "dm: web login test: %s: %s\n", web.URL.absoluteString.UTF8String,
					result ? [[result description] UTF8String] : error.localizedDescription.UTF8String);
				self.finished = YES;
				[self.window close];
				auto done = g_cancelled;
				g_cancelled = nullptr;
				g_login = nil;
				if (done)
					done();
			}];
		return;
	}
	// past the login (the app's pages): the stored token, should the
	// requests not have given it already
	if ([web.URL.path hasPrefix:@"/channels"] || [web.URL.path hasPrefix:@"/app"]) {
		[web evaluateJavaScript:kStoredToken completionHandler:^(id result, NSError*) {
			if ([result isKindOfClass:[NSString class]] && [(NSString*) result length] >= 30)
				[self finishWithToken:(NSString*) result];
		}];
	}
}

- (void)windowWillClose:(NSNotification*)note
{
	if (!self.finished)
		[self finishWithToken:nil];
}

@end

bool WebLogin::Available()
{
	return true;
}

void WebLogin::Open(std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (g_login) {
		[g_login.window makeKeyAndOrderFront:nil];
		return;
	}
	g_done = done;
	g_cancelled = cancelled;
	g_login = [[DMWebLogin alloc] initForTest:NO];
}

void WebLogin::SelfTest(std::function<void()> finished)
{
	g_done = nullptr;
	g_cancelled = finished;
	g_login = [[DMWebLogin alloc] initForTest:YES];
}

// ---- the captcha ----------------------------------------------------------

@interface DMCaptcha : NSObject <WKScriptMessageHandler, NSWindowDelegate>
@property (strong) NSWindow* window;
@property (strong) WKWebView* web;
@property (assign) BOOL finished;
@property (copy) void (^onDone)(NSString*);
@end

static DMCaptcha* g_captcha;

// A JavaScript string literal.
static NSString* JsString(const std::string& s)
{
	NSData* json = [NSJSONSerialization dataWithJSONObject:@[ [NSString stringWithUTF8String:s.c_str()] ] options:0 error:nil];
	NSString* arr = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
	return [arr substringWithRange:NSMakeRange(1, arr.length - 2)];
}

@implementation DMCaptcha

- (instancetype)initWithSitekey:(const std::string&)sitekey rqdata:(const std::string&)rqdata
{
	self = [super init];
	WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
	cfg.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
	[cfg.userContentController addScriptMessageHandler:self name:@"dmCaptcha"];

	// hCaptcha's widget, as Discord's page shows it (the page is Discord's
	// for the widget: the site key is Discord's)
	NSString* html = [NSString stringWithFormat:
		@"<!doctype html><html><head><meta charset='utf-8'>"
		 "<script src='https://js.hcaptcha.com/1/api.js?onload=dmReady&render=explicit' async defer></script>"
		 "<style>body{background:#313338;color:#dbdee1;font:15px -apple-system,sans-serif;margin:0;"
		 "height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center}"
		 "p{margin:0 24px 18px;text-align:center}</style></head><body>"
		 "<p>Discord wants this check before it finishes the QR login.</p><div id='c'></div>"
		 "<script>function dmReady(){var id=hcaptcha.render('c',{sitekey:%@,theme:'dark',"
		 "callback:function(t){window.webkit.messageHandlers.dmCaptcha.postMessage(t);}});"
		 "var rq=%@;if(rq)hcaptcha.setData(id,{rqdata:rq});}</script></body></html>",
		JsString(sitekey), JsString(rqdata)];

	NSRect frame = NSMakeRect(0, 0, 420, 640);
	_window = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
		backing:NSBackingStoreBuffered defer:NO];
	_window.title = @"Discord: are you human?";
	_window.releasedWhenClosed = NO;
	_window.delegate = self;
	_web = [[WKWebView alloc] initWithFrame:frame configuration:cfg];
	_web.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
	_window.contentView = _web;
	[_window center];
	[_web loadHTMLString:html baseURL:[NSURL URLWithString:@"https://discord.com/"]];
	[_window makeKeyAndOrderFront:nil];
	return self;
}

- (void)finishWith:(NSString*)answer
{
	if (self.finished)
		return;
	self.finished = YES;
	[self.web.configuration.userContentController removeScriptMessageHandlerForName:@"dmCaptcha"];
	[self.window orderOut:nil];
	[self.window close];
	auto done = self.onDone;
	g_captcha = nil;
	if (done)
		done(answer);
}

- (void)userContentController:(WKUserContentController*)ucc didReceiveScriptMessage:(WKScriptMessage*)message
{
	if ([message.body isKindOfClass:[NSString class]] && [(NSString*) message.body length] > 0)
		[self finishWith:(NSString*) message.body];
}

- (void)windowWillClose:(NSNotification*)note
{
	if (!self.finished)
		[self finishWith:nil];
}

@end

void WebLogin::ShowCaptcha(const std::string& sitekey, const std::string& rqdata,
	std::function<void(const std::string&)> done, std::function<void()> cancelled)
{
	if (g_captcha) {
		[g_captcha.window makeKeyAndOrderFront:nil];
		return;
	}
	g_captcha = [[DMCaptcha alloc] initWithSitekey:sitekey rqdata:rqdata];
	g_captcha.onDone = ^(NSString* answer) {
		if (answer.length > 0) {
			if (done)
				done(std::string(answer.UTF8String));
		}
		else if (cancelled)
			cancelled();
	};
}
