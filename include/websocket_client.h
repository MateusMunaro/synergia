//
// Created by HP on 08/07/2025.
//

#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <libwebsockets.h>
#include "operation.h"

#define WS_BUFFER_SIZE 4096

typedef enum {
    WS_DISCONNECTED,
    WS_CONNECTING,
    WS_CONNECTED,
    WS_ERROR
} WebSocketState;

typedef struct {
    struct lws_context* context;
    struct lws* wsi;
    char server_address[256];
    int port;
    WebSocketState state;
    char send_buffer[WS_BUFFER_SIZE];
    char recv_buffer[WS_BUFFER_SIZE];
    Operation** pending_ops;
    int pending_count;
} WebSocketClient;

// Callback para processar operações recebidas
typedef void (*operation_callback)(const Operation* op, void* user_data);

// Funções do cliente WebSocket
WebSocketClient* ws_create(const char* server, int port);
void ws_destroy(WebSocketClient* client);
int ws_connect(WebSocketClient* client);
int ws_disconnect(WebSocketClient* client);
int ws_send_operation(WebSocketClient* client, const Operation* op);
int ws_receive_operations(WebSocketClient* client, operation_callback callback, void* user_data);
int ws_service(WebSocketClient* client, int timeout_ms);
WebSocketState ws_get_state(WebSocketClient* client);

#endif // WEBSOCKET_CLIENT_H