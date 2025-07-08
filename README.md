# IOCP_Server

**IOCP_Server** is a high-performance, multithreaded server for Windows built on **IOCP (I/O Completion Ports)**. It's designed to handle a large number of concurrent connections with minimal latency and maximum scalability.

## 🚀 Features

- Asynchronous socket handling using IOCP
- Pipeline-based request processing architecture (`pipeline_stages`)
- Chunked data transfer support
- Client structure with accumulation buffer (`recv_buffer_ChankOperation`)
- Operation queue ensuring sequential and consistent execution
- Modular separation: IOCP core, Telegram Bot, WayForPay Webhook (will be added in next update IONet_script)
- Colored logging system: `SUCCESS` — green, `FAILED` — red
- Telegram notifications via `libcurl` (HTTPS)
- PostgreSQL database integration via `libpq`

## 🧱 Architecture

The project implements a clear and scalable processing flow:
