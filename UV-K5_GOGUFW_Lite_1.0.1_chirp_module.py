"""CHIRP driver for Quansheng UV-K5 GOGUFW Lite 1.0.1.

The upload range deliberately ends before GOGUFW Messenger drafts at 0x1C00.
"""

import logging

from chirp import chirp_common, directory, errors
from chirp.drivers import uvk5, uvk5_egzumer
from chirp.settings import RadioSettings


LOG = logging.getLogger(__name__)
DRIVER_VERSION = "1.0.1"
GOGUFW_PROGRAM_END = 0x1C00
GOGUFW_BLOCK_SIZE = 0x80


def _do_safe_upload(radio):
    """Upload channels/settings without touching Messenger or calibration."""
    serial = radio.pipe
    serial.timeout = 0.5

    status = chirp_common.Status()
    status.cur = 0
    status.max = GOGUFW_PROGRAM_END
    status.msg = "Uploading to UV-K5 GOGUFW Lite 1.0.1"
    radio.status_fn(status)

    firmware = uvk5._sayhello(serial)
    if not firmware:
        raise errors.RadioError("Unable to determine firmware version")
    if not radio.k5_approve_firmware(firmware):
        raise errors.RadioError(
            "This module only supports UV-K5 GOGUFW Lite 1.0.1")

    LOG.info("Uploading GOGUFW image to firmware %r", firmware)
    mmap = radio.get_mmap()
    for address in range(0, GOGUFW_PROGRAM_END, GOGUFW_BLOCK_SIZE):
        block = mmap[address:address + GOGUFW_BLOCK_SIZE]
        if len(block) != GOGUFW_BLOCK_SIZE:
            raise errors.RadioError("Memory image is incomplete")
        uvk5._writemem(serial, block, address)
        status.cur = address + GOGUFW_BLOCK_SIZE
        radio.status_fn(status)

    status.msg = "Upload complete; Messenger drafts preserved"
    radio.status_fn(status)
    uvk5._resetradio(serial)


@directory.register
class GOGUFWLite101Radio(uvk5_egzumer.UVK5RadioEgzumer):
    """Selectable Quansheng UV-K5 GOGUFW Lite driver."""

    VENDOR = "Quansheng"
    MODEL = "UV-K5 GOGUFW Lite"
    VARIANT = DRIVER_VERSION
    BAUD_RATE = 38400

    @classmethod
    def k5_approve_firmware(cls, firmware):
        return firmware.startswith("GOGUFW 1.0.1")

    def sync_out(self):
        _do_safe_upload(self)

    def get_settings(self):
        settings = super().get_settings()
        safe = RadioSettings()
        hidden_groups = {"dtmf", "dtmfc", "calibration"}
        for group in settings:
            if group.get_name() not in hidden_groups:
                safe.append(group)
        return safe
