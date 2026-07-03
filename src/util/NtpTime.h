#pragma once

namespace NtpTime {
// Sets the system clock via SNTP (pool.ntp.org), blocking up to ~5s.
// Call after WiFi is connected, before anything that validates TLS
// certificates — an unset clock (1970) fails X.509 validity checks.
// Safe to call repeatedly; a timeout leaves the clock as it was.
void syncOnce();
}  // namespace NtpTime
