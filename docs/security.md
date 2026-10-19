# Security and operating boundaries

The forwarding core is not a security boundary by itself. It receives already admitted events. A real SSH server must validate users, protect credentials, verify its cryptographic configuration, and enforce connection and channel limits before granting UART access.

No host key, password, account list, IP address, or device specific configuration is included. Generate deployment keys locally. Never commit private keys or terminal captures containing credentials. `.gitignore` is a convenience, not a secret detector.

UART access can expose a privileged console, a bootloader, or credentials typed by an operator. Limit network access and do not log terminal payload by default. Diagnostics should report state and counters, not input contents.

The gateway does not interpret or sanitize terminal bytes. This includes terminal escape sequences received from the attached system. Use a trusted target and a terminal client appropriate for the deployment.

The core generates no UART traffic on startup, open, idle, close, EOF, or error. Only explicit input submitted with a valid console token can enter its UART queue. That input may still stop a bootloader. Silence is not proof that an operating system is ready.

Closing the SSH connection does not revoke an existing operating system login on the serial port. Access policy must account for the next operator inheriting that shell. Automatic logout is deliberately absent because it would require sending device specific input.

Local queue cleanup does not erase bytes stored in a hardware adapter, a USB device, a terminal, or the target. A full implementation must document its transport cancellation behavior and protect device access accordingly.
