#include "webview.hpp"

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <string>

@interface GTHost : NSObject <WKScriptMessageHandler, WKNavigationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) WKWebView* webView;
@property(nonatomic, copy) void (^messageHandler)(NSString* body);
@property(nonatomic, copy) void (^closeHandler)(void);
@property(nonatomic, strong) NSMutableArray<NSString*>* pending;
@property(nonatomic, assign) BOOL ready;
@property(nonatomic, assign) BOOL shut;
@end

@implementation GTHost

- (instancetype)initWithTitle:(NSString*)title html:(NSString*)html {
  self = [super init];
  if (!self) return nil;
  _pending = [NSMutableArray array];

  WKWebViewConfiguration* config = [WKWebViewConfiguration new];
  WKUserContentController* controller = [WKUserContentController new];
  [controller addScriptMessageHandler:self name:@"reaper"];
  config.userContentController = controller;

  NSRect frame = NSMakeRect(0, 0, 1180, 760);
  _webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
  _webView.navigationDelegate = self;
  _webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  if (@available(macOS 12.0, *)) {
    _webView.underPageBackgroundColor = [NSColor colorWithCalibratedRed:0.047 green:0.055 blue:0.071 alpha:1];
  }

  _window = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = title;
  _window.delegate = self;
  _window.releasedWhenClosed = NO;
  _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  _window.backgroundColor = [NSColor colorWithCalibratedRed:0.047 green:0.055 blue:0.071 alpha:1];
  _window.contentView = _webView;
  _window.minSize = NSMakeSize(880, 540);
  [_window center];
  [_webView loadHTMLString:html baseURL:nil];
  [_window makeKeyAndOrderFront:nil];
  return self;
}

- (void)userContentController:(WKUserContentController*)userContentController didReceiveScriptMessage:(WKScriptMessage*)message {
  (void)userContentController;
  if (self.shut) return;
  if (![message.body isKindOfClass:[NSString class]]) return;
  if (self.messageHandler) self.messageHandler((NSString*)message.body);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  (void)webView;
  (void)navigation;
  self.ready = YES;
  for (NSString* script in self.pending) {
    [self.webView evaluateJavaScript:script completionHandler:nil];
  }
  [self.pending removeAllObjects];
}

- (void)eval:(NSString*)script {
  if (self.shut || script.length == 0) return;
  if (![NSThread isMainThread]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self eval:script];
    });
    return;
  }
  if (!self.ready) {
    [self enqueue:script];
    return;
  }
  [self.webView evaluateJavaScript:script completionHandler:nil];
}

- (void)enqueue:(NSString*)script {
  for (NSString* key in @[ @"gtApplyTransport", @"gtApplyMap" ]) {
    if ([script containsString:key]) {
      for (NSUInteger i = 0; i < self.pending.count; ++i) {
        if ([self.pending[i] containsString:key]) {
          self.pending[i] = script;
          return;
        }
      }
    }
  }
  if (self.pending.count > 40) [self.pending removeObjectAtIndex:0];
  [self.pending addObject:script];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  (void)sender;
  self.ready = NO;
  void (^handler)(void) = self.closeHandler;
  if (handler) handler();
  return YES;
}

- (void)shutdown {
  if (self.shut) return;
  self.shut = YES;
  self.ready = NO;
  self.messageHandler = nil;
  self.closeHandler = nil;
  self.window.delegate = nil;
  self.webView.navigationDelegate = nil;
  [self.webView.configuration.userContentController removeScriptMessageHandlerForName:@"reaper"];
  [self.window orderOut:nil];
}

@end

namespace guitartabs {
namespace {

class MacWebView final : public WebView {
 public:
  explicit MacWebView(GTHost* retained) : host_(retained) {}

  ~MacWebView() override {
    GTHost* host = host_;
    host_ = nil;
    if (!host) return;
    if ([NSThread isMainThread]) [host shutdown];
    else dispatch_sync(dispatch_get_main_queue(), ^{ [host shutdown]; });
    CFRelease((__bridge CFTypeRef)host);
  }

  void setTitle(const std::string& title) override {
    NSString* text = [NSString stringWithUTF8String:title.c_str()];
    GTHost* host = host_;
    if (!text || !host) return;
    auto apply = ^{
      host.window.title = text;
    };
    if ([NSThread isMainThread]) apply();
    else dispatch_async(dispatch_get_main_queue(), apply);
  }

  void eval(const std::string& js) override {
    if (!host_) return;
    NSString* script = [NSString stringWithUTF8String:js.c_str()];
    if (!script) return;
    [host_ eval:script];
  }

  void bringToFront() override {
    GTHost* host = host_;
    if (!host) return;
    auto apply = ^{
      [host.window makeKeyAndOrderFront:nil];
    };
    if ([NSThread isMainThread]) apply();
    else dispatch_async(dispatch_get_main_queue(), apply);
  }

 private:
  GTHost* host_ = nil;
};

}  // namespace

std::unique_ptr<WebView> WebView::open(const std::string& title, const std::string& html,
                                       std::function<void(const std::string&)> onMessage, std::function<void()> onClose) {
  @autoreleasepool {
    NSString* titleText = [NSString stringWithUTF8String:title.c_str()];
    NSString* htmlText = [NSString stringWithUTF8String:html.c_str()];
    if (!titleText || !htmlText) return nullptr;
    GTHost* host = [[GTHost alloc] initWithTitle:titleText html:htmlText];
    if (!host) return nullptr;
    host.messageHandler = ^(NSString* body) {
      if (body && onMessage) onMessage(std::string(body.UTF8String));
    };
    host.closeHandler = ^{
      if (onClose) onClose();
    };
    CFRetain((__bridge CFTypeRef)host);
    return std::make_unique<MacWebView>(host);
  }
}

}  // namespace guitartabs
