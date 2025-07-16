//
// Created by HP on 08/07/2025.
//
#include "websocket_client.h"
#include "utils.h"
#include <string.h>

// Contexto para callbacks do libwebsockets
typedef struct {
    WebSocketClient* client;
    operation_callback op_callback;
    void* user_data;
} WSContext;

// Protocolos WebSocket
static struct lws_protocols protocols[] = {
    {
        "myvc-protocol",
        NULL,  // callback será definido dinamicamente
        sizeof(WSContext),
        WS_BUFFER_SIZE,
    },
    { NULL, NULL, 0, 0 } // Terminador
};

// Callback do WebSocket
static int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len) {
    WSContext* ctx = (WSContext*)user;
    WebSocketClient* client = ctx ? ctx->client : NULL;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            log_message(LOG_INFO, "WebSocket connection established");
            if (client) {
                client->state = WS_CONNECTED;
            }
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (client && in && len > 0) {
                // Copiar dados recebidos para o buffer
                size_t copy_len = len < WS_BUFFER_SIZE - 1 ? len : WS_BUFFER_SIZE - 1;
                memcpy(client->recv_buffer, in, copy_len);
                client->recv_buffer[copy_len] = '\0';

                log_message(LOG_DEBUG, "Received: %s", client->recv_buffer);

                // Deserializar operação
                Operation* op = operation_deserialize(client->recv_buffer);
                if (op && ctx->op_callback) {
                    ctx->op_callback(op, ctx->user_data);
                    operation_destroy(op);
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (client && client->pending_count > 0) {
                // Enviar próxima operação pendente
                Operation* op = client->pending_ops[0];
                char* json = operation_serialize(op);

                if (json) {
                    size_t json_len = strlen(json);
                    unsigned char buf[LWS_PRE + WS_BUFFER_SIZE];

                    memcpy(&buf[LWS_PRE], json, json_len);

                    int written = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
                    if (written < 0) {
                        log_message(LOG_ERROR, "Failed to send data");
                    } else {
                        log_message(LOG_DEBUG, "Sent: %s", json);

                        // Remover operação enviada da fila
                        operation_destroy(op);
                        for (int i = 0; i < client->pending_count - 1; i++) {
                            client->pending_ops[i] = client->pending_ops[i + 1];
                        }
                        client->pending_count--;

                        // Se houver mais operações, solicitar callback de escrita
                        if (client->pending_count > 0) {
                            lws_callback_on_writable(wsi);
                        }
                    }

                    free(json);
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            log_message(LOG_ERROR, "WebSocket connection error");
            if (client) {
                client->state = WS_ERROR;
            }
            break;

        case LWS_CALLBACK_CLIENT_CLOSED:
            log_message(LOG_INFO, "WebSocket connection closed");
            if (client) {
                client->state = WS_DISCONNECTED;
            }
            break;

        default:
            break;
    }

    return 0;
}

WebSocketClient* ws_create(const char* server, int port) {
    if (!server) return NULL;

    WebSocketClient* client = (WebSocketClient*)safe_malloc(sizeof(WebSocketClient));

    strncpy(client->server_address, server, sizeof(client->server_address) - 1);
    client->server_address[sizeof(client->server_address) - 1] = '\0';

    client->port = port;
    client->state = WS_DISCONNECTED;
    client->context = NULL;
    client->wsi = NULL;
    client->pending_ops = NULL;
    client->pending_count = 0;

    memset(client->send_buffer, 0, WS_BUFFER_SIZE);
    memset(client->recv_buffer, 0, WS_BUFFER_SIZE);

    log_message(LOG_INFO, "Created WebSocket client for %s:%d", server, port);
    return client;
}

void ws_destroy(WebSocketClient* client) {
    if (!client) return;

    if (client->state == WS_CONNECTED) {
        ws_disconnect(client);
    }

    // Limpar operações pendentes
    for (int i = 0; i < client->pending_count; i++) {
        operation_destroy(client->pending_ops[i]);
    }
    safe_free(client->pending_ops);

    safe_free(client);
}

int ws_connect(WebSocketClient* client) {
    if (!client) return -1;

    if (client->state == WS_CONNECTED) {
        log_message(LOG_WARNING, "Already connected");
        return 0;
    }

    // Configurar contexto
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    protocols[0].callback = websocket_callback;
    info.gid = -1;
    info.uid = -1;

    client->context = lws_create_context(&info);
    if (!client->context) {
        log_message(LOG_ERROR, "Failed to create WebSocket context");
        return -1;
    }

    // Configurar conexã
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));

    connect_info.context = client->context;
    connect_info.address = client->server_address;
    connect_info.port = client->port;
    connect_info.path = "/";
    connect_info.host = client->server_address;
    connect_info.origin = client->server_address;
    connect_info.protocol = protocols[0].name;
    connect_info.ssl_connection = 0;  // Sem SSL por enquanto

    // Criar contexto de usuário
    WSContext* ctx = (WSContext*)safe_malloc(sizeof(WSContext));
    ctx->client = client;
    ctx->op_callback = NULL;
    ctx->user_data = NULL;
    connect_info.userdata = ctx;

    client->state = WS_CONNECTING;
    client->wsi = lws_client_connect_via_info(&connect_info);

    if (!client->wsi) {
        log_message(LOG_ERROR, "Failed to initiate WebSocket connection");
        lws_context_destroy(client->context);
        client->context = NULL;
        client->state = WS_DISCONNECTED;
        safe_free(ctx);
        return -1;
    }

    log_message(LOG_INFO, "Connecting to WebSocket server...");

    // Aguardar conexão (com timeout)
    int timeout = 5000; // 5 segundos
    int elapsed = 0;

    while (client->state == WS_CONNECTING && elapsed < timeout) {
        lws_service(client->context, 50);
        elapsed += 50;
    }

    if (client->state != WS_CONNECTED) {
        log_message(LOG_ERROR, "Connection timeout");
        ws_disconnect(client);
        return -1;
    }

    return 0;
}

int ws_disconnect(WebSocketClient* client) {
    if (!client) return -1;

    if (client->state == WS_DISCONNECTED) {
        return 0;
    }

    client->state = WS_DISCONNECTED;

    if (client->wsi) {
        lws_close_reason(client->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
        client->wsi = NULL;
    }

    if (client->context) {
        lws_context_destroy(client->context);
        client->context = NULL;
    }

    log_message(LOG_INFO, "Disconnected from WebSocket server");
    return 0;
}

int ws_send_operation(WebSocketClient* client, const Operation* op) {
    if (!client || !op) return -1;

    if (client->state != WS_CONNECTED) {
        log_message(LOG_ERROR, "Not connected to server");
        return -1;
    }

    // Adicionar à fila de operações pendentes
    if (!client->pending_ops) {
        client->pending_ops = (Operation**)safe_malloc(10 * sizeof(Operation*));
    } else if (client->pending_count % 10 == 0) {
        // Expandir array se necessário
        client->pending_ops = (Operation**)safe_realloc(
            client->pending_ops,
            (client->pending_count + 10) * sizeof(Operation*)
        );
    }

    // Criar cópia da operação
    Operation* op_copy = operation_create(
        op->op_type, op->line, op->column,
        op->text, op->author
    );
    op_copy->timestamp = op->timestamp;

    client->pending_ops[client->pending_count++] = op_copy;

    // Solicitar callback de escrita
    if (client->wsi) {
        lws_callback_on_writable(client->wsi);
    }

    return 0;
}

int ws_receive_operations(WebSocketClient* client, operation_callback callback, void* user_data) {
    if (!client || !callback) return -1;

    if (client->state != WS_CONNECTED) {
        return -1;
    }

    // Configurar callback no contexto
    WSContext* ctx = (WSContext*)lws_wsi_user(client->wsi);
    if (ctx) {
        ctx->op_callback = callback;
        ctx->user_data = user_data;
    }

    return 0;
}

int ws_service(WebSocketClient* client, int timeout_ms) {
    if (!client || !client->context) return -1;

    return lws_service(client->context, timeout_ms);
}

WebSocketState ws_get_state(WebSocketClient* client) {
    return client ? client->state : WS_DISCONNECTED;
}