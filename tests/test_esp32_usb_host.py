#!/usr/bin/env python3
"""Exercise the actual profile generators in an isolated output directory."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostProfileTest(unittest.TestCase):
    def test_generated_host_profile_has_uart_console_and_exclusive_usb(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'pio-build').symlink_to(ROOT / 'pio-build', target_is_directory=True)
            (tmp / 'sdkconfig.local.defaults').write_text('')
            local = tmp / 'local.ini'
            ini = tmp / 'platformio.ini'
            sdk = tmp / 'sdkconfig.defaults'
            local.write_text('[fujinet]\nbuild_board = fujibus-link-usbhost-s3-n16r8\nconsole_type = consoleuart\n')
            subprocess.run([sys.executable, 'pio-build/scripts/create-platformio-ini.py',
                            '-l', str(local), '-o', str(ini)], cwd=tmp, check=True, capture_output=True)
            subprocess.run([sys.executable, 'pio-build/scripts/create-sdkconfig.py', '-i', str(ini),
                            '-m', 'pio-build/sdkconfig/00_platform_sdkconfig_map.txt', '-o', str(sdk)],
                           cwd=tmp, check=True, capture_output=True)
            config = {}
            for line in sdk.read_text().splitlines():
                if line.startswith('CONFIG_') and '=' in line:
                    key, value = line.split('=', 1)
                    config[key] = value
            for key, value in {
                'CONFIG_TINYUSB_CDC_ENABLED': 'n',
                'CONFIG_ESP_CONSOLE_USB_CDC': 'n',
                'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG': 'n',
                'CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG': 'n',
                'CONFIG_ESP_CONSOLE_UART_DEFAULT': 'y',
                'CONFIG_ESPTOOLPY_FLASHMODE_QIO': 'y',
                'CONFIG_FN_CONSOLE_ENABLE': 'y',
                'CONFIG_FN_CONSOLE_TRANSPORT_UART0': 'y',
                'CONFIG_SPIRAM': 'y',
            }.items():
                self.assertEqual(config.get(key), value, key)
            generated = ini.read_text()
            self.assertIn('-DFN_BUILD_ESP32_USB_HOST_LINK', generated)
            self.assertIn('/dev/ttyUSB0', generated)
            self.assertNotIn('-DFN_BUILD_ESP32_USB_CDC_LINK', generated)


if __name__ == '__main__':
    unittest.main()
