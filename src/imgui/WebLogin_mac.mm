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
