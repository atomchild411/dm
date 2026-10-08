// WebLogin on macOS: a window with a WKWebView on https://discord.com/login.

#include "WebLogin.hpp"
#include "WebLoginPages.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

static NSString* NS(const std::string& s)
{
	return [NSString stringWithUTF8String:s.c_str()];
}

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
	WKUserScript* watcher = [[WKUserScript alloc] initWithSource:NS(WebLoginPages::TokenWatcher())
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
		[web evaluateJavaScript:NS(WebLoginPages::kTestProbe)
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
		[web evaluateJavaScript:NS(WebLoginPages::kStoredToken) completionHandler:^(id result, NSError*) {
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

// ---- the captcha ----------------------------------------------------------

@interface DMCaptcha : NSObject <WKScriptMessageHandler, NSWindowDelegate>
@property (strong) NSWindow* window;
@property (strong) WKWebView* web;
@property (assign) BOOL finished;
@property (assign) BOOL test;
@property (copy) void (^onDone)(NSString*);
@end

static DMCaptcha* g_captcha;

@implementation DMCaptcha

- (instancetype)initWithSitekey:(const std::string&)sitekey rqdata:(const std::string&)rqdata test:(BOOL)test
{
	self = [super init];
	_test = test;
	WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
	cfg.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
	[cfg.userContentController addScriptMessageHandler:self name:@"dmCaptcha"];
	[cfg.userContentController addScriptMessageHandler:self name:@"dmCaptchaLog"];

	// hCaptcha's widget, as Discord's page shows it (the page is Discord's
	// for the widget: the site key is Discord's)
	NSString* html = NS(WebLoginPages::CaptchaPage(sitekey, rqdata));

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
	if (!test)
		[_window makeKeyAndOrderFront:nil];
	fprintf(stderr, "dm: captcha: window open (%s)\n", rqdata.empty() ? "no rqdata" : "with rqdata");
	return self;
}

- (void)finishWith:(NSString*)answer
{
	if (self.finished)
		return;
	self.finished = YES;
	[self.web.configuration.userContentController removeScriptMessageHandlerForName:@"dmCaptcha"];
	[self.web.configuration.userContentController removeScriptMessageHandlerForName:@"dmCaptchaLog"];
	if (!answer)
		fprintf(stderr, "dm: captcha: window closed unsolved\n");
	[self.window orderOut:nil];
	[self.window close];
	auto done = self.onDone;
	g_captcha = nil;
	if (done)
		done(answer);
}

- (void)userContentController:(WKUserContentController*)ucc didReceiveScriptMessage:(WKScriptMessage*)message
{
	if (![message.body isKindOfClass:[NSString class]])
		return;
	if ([message.name isEqualToString:@"dmCaptchaLog"]) {
		fprintf(stderr, "dm: captcha: %s\n", [(NSString*) message.body UTF8String]);
		// the test: shown is as far as it goes without a person
		if (self.test && [(NSString*) message.body hasPrefix:@"widget shown"])
			[self finishWith:nil];
		return;
	}
	if ([(NSString*) message.body length] > 0)
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
	g_captcha = [[DMCaptcha alloc] initWithSitekey:sitekey rqdata:rqdata test:NO];
	g_captcha.onDone = ^(NSString* answer) {
		if (answer.length > 0) {
			if (done)
				done(std::string(answer.UTF8String));
		}
		else if (cancelled)
			cancelled();
	};
}

void WebLogin::SelfTest(std::function<void()> finished)
{
	// DM_TEST_WEBLOGIN=captcha: the captcha's page with hCaptcha's test key,
	// hidden, until the widget is shown
	const char* what = getenv("DM_TEST_WEBLOGIN");
	if (what && !strcmp(what, "captcha")) {
		g_captcha = [[DMCaptcha alloc] initWithSitekey:"10000000-ffff-ffff-ffff-000000000001" rqdata:"" test:YES];
		g_captcha.onDone = ^(NSString*) {
			if (finished)
				finished();
		};
		return;
	}
	g_done = nullptr;
	g_cancelled = finished;
	g_login = [[DMWebLogin alloc] initForTest:YES];
}
