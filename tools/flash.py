"""Flash this application's UF2 through its boot command (Windows)."""
import argparse
import ctypes
import shutil
import struct
import time
from pathlib import Path


def flash(port_name, uf2, new_pid=None):
    import serial
    from serial.tools import list_ports

    original = next((p for p in list_ports.comports() if p.device == port_name), None)

    uf2 = Path(uf2)
    with uf2.open("rb") as src:
        if struct.unpack("<II", src.read(8)) != (0x0A324655, 0x9E5D5157):
            raise ValueError("Not a UF2 file")
    with serial.Serial(port_name, 115200, timeout=0.2) as port:
        port.write(b"\x03\r")
        time.sleep(0.2)
        port.write(b"boot\r")
        port.flush()
        time.sleep(0.5)
    target = None
    for _ in range(40):
        candidates = []
        for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
            root = Path(letter + ":\\")
            if ctypes.windll.kernel32.GetDriveTypeW(str(root)) != 2:
                continue
            info = root / "INFO_UF2.TXT"
            if info.exists() and "Board-ID: RP2350" in info.read_text(errors="replace"):
                candidates.append(root)
        if len(candidates) > 1:
            raise RuntimeError("Multiple RP2350 bootloader drives; cannot choose safely")
        if candidates:
            target = candidates[0]
            break
        time.sleep(0.25)
    if target is None:
        raise RuntimeError("RP2350 USB bootloader drive did not appear")
    print(f"Flashing {uf2} to {target}", flush=True)
    shutil.copyfile(uf2, target / "zephyr.uf2")
    for _ in range(80):
        returned = [p for p in list_ports.comports() if p.device and
                    ((original and original.serial_number and
                        p.serial_number == original.serial_number and p.vid == original.vid)
                    or ((not original or not original.serial_number) and p.device == port_name))]
        if new_pid is not None and original:
            returned = [p for p in list_ports.comports() if p.device and p.vid == original.vid and p.pid == new_pid]
        # Composite firmware can expose shell plus telemetry CDC ports.
        # The shell is USB communication interface zero (MI_00 on Windows).
        shells = [p for p in returned if 'MI_00' in p.hwid.upper() or (p.location and p.location.endswith(':x.0'))]
        if len(shells) == 1:
            returned = shells
        if len(returned) == 1:
            time.sleep(1)
            print(f"{returned[0].device} returned", flush=True)
            return returned[0].device
        time.sleep(0.25)
    raise RuntimeError(f"{port_name} did not return after flashing")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--uf2", type=Path, default=Path(__file__).parents[1] / "build/zephyr/zephyr.uf2")
    parser.add_argument("--new-pid", type=lambda value:int(value,0), help="Expected prototype PID when changing firmware USB identity")
    args = parser.parse_args()
    flash(args.port, args.uf2, args.new_pid)
