# Driver Repository Instructions

- This is an independent MS-PL driver repository. Preserve Microsoft notices.
- Do not import GPL-only SAR code or Steinberg ASIO SDK files.
- Communicate with SAR only through public Windows audio device interfaces.
- Do not present the unmodified SysVAD sample as an SAR product driver.
- Build and install only in dedicated Windows driver CI or the driver lab.
- Never commit signing keys, test certificates, binaries, or credentials.
- Keep kernel code free of user-mode assumptions, pageable hot-path work, and
  unbounded allocations. Test endpoint behavior on real Windows before release.
