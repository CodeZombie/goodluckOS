import os
import re
import socket
import subprocess
import time
import uuid
from dataclasses import dataclass
from pathlib import Path

SOCK = "/tmp/cmdchan.sock"


@dataclass
class Result:
    rc: int
    output: str  # stdout and stderr merged

    @property
    def ok(self) -> bool:
        return self.rc == 0

    @property
    def stdout(self) -> str:
        return self.output


class Console:
    """Minimal line-oriented client for the QEMU virtio-console unix socket."""

    def __init__(self, path, logfile):
        self.path = path
        self.sock = None
        self.buf = ""
        self.log = open(logfile, "w")

    def connect(self, timeout=30, abort=lambda: None):
        end = time.monotonic() + timeout
        while True:
            abort()  # raises if QEMU already died
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                s.connect(self.path)
                self.sock = s
                return
            except (FileNotFoundError, ConnectionRefusedError):
                s.close()
                if time.monotonic() > end:
                    raise TimeoutError(f"console socket {self.path} never appeared")
                time.sleep(0.2)

    def write(self, text):
        self.sock.sendall(text.encode())

    def _read(self, timeout):
        self.sock.settimeout(timeout)
        try:
            data = self.sock.recv(4096)
        except socket.timeout:
            return False
        if not data:
            raise ConnectionError("console socket closed")
        text = data.decode(errors="replace").replace("\r", "")
        self.buf += text
        self.log.write(text)
        self.log.flush()
        return True

    def drain(self):
        while self._read(0.05):
            pass
        self.buf = ""

    def expect(self, pattern, timeout, poke=None, poke_every=2.0):
        """Wait for regex; returns (text_before_match, match). Consumes through the match."""
        rx = re.compile(pattern)
        end = time.monotonic() + timeout
        last_poke = 0.0
        while True:
            m = rx.search(self.buf)
            if m:
                before = self.buf[: m.start()]
                self.buf = self.buf[m.end():]
                return before, m
            now = time.monotonic()
            if now > end:
                raise TimeoutError(
                    f"timed out waiting for {pattern!r}; last console output:\n{self.buf[-500:]!r}"
                )
            if poke and now - last_poke > poke_every:
                self.write(poke)
                last_poke = now
            self._read(min(1.0, end - now))


class QemuDevice:
    def __init__(self, kernel, rootfs, results_dir="results"):
        self.kernel = kernel
        self.rootfs = rootfs
        self.results = Path(results_dir)
        self.results.mkdir(parents=True, exist_ok=True)
        self.proc = None
        self.console = None
        self.user = os.environ.get("LOGIN_USER", "root")
        self.password = os.environ.get("LOGIN_PASSWORD", "root")
        self.login_pattern = os.environ.get("LOGIN_PATTERN", "login: ")
        self.prompt = os.environ.get("SHELL_PROMPT", "# $")
        self.boot_timeout = int(os.environ.get("BOOT_TIMEOUT", "300"))

    def start(self):
        if os.path.exists(SOCK):
            os.unlink(SOCK)
        cmd = [
            "qemu-system-arm", "-M", "virt", "-cpu", "cortex-a7",
            "-m", os.environ.get("QEMU_MEM", "512M"), "-nic", "none",
            "-kernel", self.kernel,
            "-append", os.environ.get("QEMU_APPEND", "root=/dev/vda rw console=ttyAMA0"),
            "-drive", f"if=none,file={self.rootfs},id=disk,format=raw",
            "-device", "virtio-blk-device,drive=disk",
            "-display", "none", "-monitor", "none",
            # Noisy main console goes to a log file, not to the test channel.
            "-serial", f"file:{self.results / 'ttyAMA0.log'}",
            "-device", "virtio-serial-device",
            "-chardev", f"socket,id=cmdchan,path={SOCK},server=on,wait=off",
            "-device", "virtconsole,chardev=cmdchan",
        ]
        qlog = open(self.results / "qemu.log", "w")
        self.proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=qlog, stderr=qlog)

        def abort():
            if self.proc.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited with {self.proc.returncode}; see results/qemu.log"
                )

        self.console = Console(SOCK, self.results / "hvc0.log")
        self.console.connect(abort=abort)

    def login(self):
        c = self.console
        # QEMU drops console output while nobody is connected, so the first
        # login prompt is lost. Poke getty with newlines until it answers.
        c.expect(self.login_pattern, self.boot_timeout, poke="\n")
        c.write(self.user + "\n")
        _, m = c.expect(f"Password: |{self.prompt}", 60)
        if m.group(0) == "Password: ":
            c.write(self.password + "\n")

        # Make the session quiet: no echo, no prompt. Output of this first
        # call is garbage (echoed input) and is discarded.
        self.execute("stty -echo; PS1=''; PS2=''", timeout=60)

    def execute(self, cmd, timeout=30):
        c = self.console
        nonce = uuid.uuid4().hex[:8]
        c.drain()
        # The %s keeps the literal marker out of the echoed command line, so
        # the regex only matches the real output.
        c.write(f"{{ {cmd}\n}} 2>&1; printf '\\n__DONE_{nonce}_%s__\\n' \"$?\"\n")
        before, m = c.expect(rf"__DONE_{nonce}_(\d+)__", timeout)
        return Result(int(m.group(1)), before.strip())

    def check(self, cmd, timeout=30):
        r = self.execute(cmd, timeout)
        assert r.ok, f"`{cmd}` exited {r.rc}\n{r.output}"
        return r.output

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
