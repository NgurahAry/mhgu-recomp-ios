// iOS relay sockets: NSURLSessionWebSocketTask supplies WSS/TLS and certificate checks.
// The chat core polls these non-blocking handles from its own worker thread.
#if defined(__APPLE__)
#import <Foundation/Foundation.h>

#include "chat_ws.h"

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace chatws {
namespace {

struct Socket {
    NSURLSession *session = nil;
    NSURLSessionWebSocketTask *task = nil;
    int state = CONNECTING;
    std::deque<std::string> incoming;
    std::string error;
};

std::mutex g_mu;
std::unordered_map<int, std::shared_ptr<Socket>> g_sockets;
int g_next = 1;

void set_open(int handle) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    if (it != g_sockets.end() && it->second->state == CONNECTING) it->second->state = OPEN;
}

void set_closed(int handle, NSError *error) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return;
    it->second->state = CLOSED;
    if (error && it->second->error.empty()) {
        const char *message = [[error localizedDescription] UTF8String];
        if (message) it->second->error = message;
    }
}

void enqueue(int handle, NSString *message) {
    if (!message) return;
    NSData *data = [message dataUsingEncoding:NSUTF8StringEncoding];
    if (!data) return;
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end() || it->second->state == CLOSED) return;
    // Bound memory if a relay misbehaves or the UI is not polling quickly enough.
    if (it->second->incoming.size() >= 256) it->second->incoming.pop_front();
    const char *bytes = (const char *)[data bytes];
    it->second->incoming.emplace_back(bytes ? bytes : "", (size_t)[data length]);
}

void receive_next(int handle, NSURLSessionWebSocketTask *task) {
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message, NSError *error) {
        if (error) {
            set_closed(handle, error);
            return;
        }
        if (message.type == NSURLSessionWebSocketMessageTypeString) enqueue(handle, message.string);
        else if (message.type == NSURLSessionWebSocketMessageTypeData) {
            NSString *text = [[NSString alloc] initWithData:message.data encoding:NSUTF8StringEncoding];
            enqueue(handle, text);
        }
        receive_next(handle, task);
    }];
}

}  // namespace

void platform_init(const char *) {}
bool available() { return true; }

int open(const std::string &url) {
    @autoreleasepool {
        NSString *address = [[NSString alloc] initWithBytes:url.data() length:url.size() encoding:NSUTF8StringEncoding];
        NSURL *nsurl = address ? [NSURL URLWithString:address] : nil;
        if (!nsurl || ![[nsurl.scheme lowercaseString] isEqualToString:@"wss"] || !nsurl.host.length) return 0;

        int handle;
        auto socket = std::make_shared<Socket>();
        NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        NSOperationQueue *queue = [[NSOperationQueue alloc] init];
        queue.maxConcurrentOperationCount = 1;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            handle = g_next++;
            if (g_next <= 0) g_next = 1;
            socket->session = [NSURLSession sessionWithConfiguration:configuration delegate:nil delegateQueue:queue];
            socket->task = [socket->session webSocketTaskWithURL:nsurl];
            g_sockets.emplace(handle, socket);
        }
        [socket->task resume];
        [socket->task sendPingWithPongReceiveHandler:^(NSError *error) {
            if (error) set_closed(handle, error);
            else set_open(handle);
        }];
        receive_next(handle, socket->task);
        return handle;
    }
}

int state(int handle) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    return it == g_sockets.end() ? CLOSED : it->second->state;
}

bool send(int handle, const std::string &text) {
    std::shared_ptr<Socket> socket;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_sockets.find(handle);
        if (it == g_sockets.end() || it->second->state != OPEN) return false;
        socket = it->second;
    }
    @autoreleasepool {
        NSString *message = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
        if (!message) return false;
        [socket->task sendMessage:[[NSURLSessionWebSocketMessage alloc] initWithString:message]
                completionHandler:^(NSError *error) { if (error) set_closed(handle, error); }];
        return true;
    }
}

bool poll(int handle, std::string &text) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end() || it->second->incoming.empty()) return false;
    text = std::move(it->second->incoming.front());
    it->second->incoming.pop_front();
    return true;
}

std::string error(int handle) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_sockets.find(handle);
    return it == g_sockets.end() ? std::string() : it->second->error;
}

void close(int handle) {
    std::shared_ptr<Socket> socket;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_sockets.find(handle);
        if (it == g_sockets.end()) return;
        socket = std::move(it->second);
        g_sockets.erase(it);
    }
    [socket->task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure reason:nil];
    [socket->session invalidateAndCancel];
}

}  // namespace chatws
#endif
