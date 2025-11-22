use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::sync::mpsc;
use tokio_tungstenite::{connect_async, tungstenite::protocol::Message, tungstenite::client::IntoClientRequest};
use url::Url;
use reqwest::Client;

const C_CPP_CENTER_ADDR: &str = "127.0.0.1:8000";
const RUST_BRIDGE_ADDR: &str = "127.0.0.1:8001";

#[derive(Deserialize, Debug)]
struct BridgeCommand {
    cmd: String,
    url: Option<String>,
    headers: Option<std::collections::HashMap<String, String>>,
    body: Option<String>,
}

enum ManagerMsg {
    Connect { url: String, headers: Option<std::collections::HashMap<String, String>> },
    SendText(String),
    SendBinary(Vec<u8>),
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt::init();
    tracing::info!("Starting Nexus Network Bridge...");

    let udp_socket = Arc::new(UdpSocket::bind(RUST_BRIDGE_ADDR).await?);
    tracing::info!("IPC UDP listening on {}", RUST_BRIDGE_ADDR);

    let http_client = Client::builder().danger_accept_invalid_certs(true).build()?;

    // Channel to WS Manager
    let (mgr_tx, mut mgr_rx) = mpsc::channel::<ManagerMsg>(100);

    // WS Manager Task
    let udp_sender = udp_socket.clone();
    tokio::spawn(async move {
        let mut ws_writer: Option<futures_util::stream::SplitSink<_, Message>> = None;
        
        while let Some(msg) = mgr_rx.recv().await {
            match msg {
                ManagerMsg::Connect { url, headers } => {
                    tracing::info!("Connecting to WS: {}", url);
                    if let Ok(parsed_url) = Url::parse(&url) {
                        if let Ok(mut request) = parsed_url.into_client_request() {
                            if let Some(h) = headers {
                                let req_headers = request.headers_mut();
                                for (k, v) in h {
                                    if let Ok(h_name) = tokio_tungstenite::tungstenite::http::header::HeaderName::from_bytes(k.as_bytes()) {
                                        if let Ok(h_val) = tokio_tungstenite::tungstenite::http::header::HeaderValue::from_str(&v) {
                                            req_headers.insert(h_name, h_val);
                                        }
                                    }
                                }
                            }

                            match connect_async(request).await {
                                Ok((ws_stream, _)) => {
                                    tracing::info!("WS Connected");
                                    let (write, mut read) = ws_stream.split();
                                    ws_writer = Some(write);

                                    // Spawn Reader
                                    let sender = udp_sender.clone();
                                    tokio::spawn(async move {
                                        while let Some(msg) = read.next().await {
                                            match msg {
                                                Ok(Message::Text(text)) => {
                                                    let _ = sender.send_to(text.as_bytes(), C_CPP_CENTER_ADDR).await;
                                                }
                                                Ok(Message::Binary(bin)) => {
                                                    let _ = sender.send_to(&bin, C_CPP_CENTER_ADDR).await;
                                                }
                                                _ => {}
                                            }
                                        }
                                        tracing::info!("WS Reader finished");
                                    });
                                }
                                Err(e) => tracing::error!("WS Connect Error: {}", e),
                            }
                        }
                    }
                }
                ManagerMsg::SendText(text) => {
                    if let Some(writer) = &mut ws_writer {
                        let _ = writer.send(Message::Text(text)).await;
                    }
                }
                ManagerMsg::SendBinary(bin) => {
                    if let Some(writer) = &mut ws_writer {
                        let _ = writer.send(Message::Binary(bin)).await;
                    }
                }
            }
        }
    });

    // UDP Receiver Loop
    let mut buf = [0u8; 65536];
    let udp_sender_http = udp_socket.clone();
    
    loop {
        match udp_socket.recv_from(&mut buf).await {
            Ok((size, _src)) => {
                let data = &buf[..size];
                
                // Try parse as command
                let mut is_cmd = false;
                if data.len() > 0 && data[0] == b'{' {
                    if let Ok(v) = serde_json::from_slice::<Value>(data) {
                        if v.get("cmd").is_some() {
                            is_cmd = true;
                            if let Ok(cmd) = serde_json::from_slice::<BridgeCommand>(data) {
                                match cmd.cmd.as_str() {
                                    "http_post" => {
                                        let client = http_client.clone();
                                        let sender = udp_sender_http.clone();
                                        tokio::spawn(async move {
                                            if let (Some(url), Some(body)) = (cmd.url, cmd.body) {
                                                let mut req = client.post(&url);
                                                if let Some(headers) = cmd.headers {
                                                    for (k, v) in headers {
                                                        req = req.header(&k, &v);
                                                    }
                                                }
                                                req = req.body(body);
                                                match req.send().await {
                                                    Ok(resp) => {
                                                        if let Ok(text) = resp.text().await {
                                                            let reply = serde_json::json!({
                                                                "type": "http_response",
                                                                "body": text
                                                            });
                                                            let _ = sender.send_to(reply.to_string().as_bytes(), C_CPP_CENTER_ADDR).await;
                                                        }
                                                    }
                                                    Err(e) => tracing::error!("HTTP Error: {}", e),
                                                }
                                            }
                                        });
                                    }
                                    "ws_connect" => {
                                        if let Some(url) = cmd.url {
                                            let _ = mgr_tx.send(ManagerMsg::Connect { url, headers: cmd.headers }).await;
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        }
                    }
                }

                if !is_cmd {
                    // Forward to WS
                    if data.len() > 0 && data[0] == b'{' {
                         let _ = mgr_tx.send(ManagerMsg::SendText(String::from_utf8_lossy(data).to_string())).await;
                    } else {
                         let _ = mgr_tx.send(ManagerMsg::SendBinary(data.to_vec())).await;
                    }
                }
            }
            Err(e) => tracing::error!("UDP Recv Error: {}", e),
        }
    }
}
