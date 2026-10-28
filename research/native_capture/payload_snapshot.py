"""Fail-closed, deterministic payload snapshot bridge for one selected draw.

The runtime adapter supplies immutable byte snapshots and their identities. This
module validates representation/generation metadata and hands payloads to the
existing content-addressed bundle writer; it does not claim to synchronize GPU
memory or discover Vulkan resources itself.
"""
from __future__ import annotations

from dataclasses import dataclass
import threading
from typing import Iterable

from bundle import CaptureBundleError, build_bundle


@dataclass(frozen=True)
class Payload:
    resource_id: str
    generation: int
    kind: str
    data: bytes
    representation: str
    endian: str
    source_registers: tuple[int, ...] = ()
    guest_address: str | None = None
    metadata: tuple[tuple[str, str | int], ...] = ()

    def as_bundle_resource(self) -> dict:
        if self.representation not in ("guest_raw", "host_shader_visible"):
            raise CaptureBundleError(f"unknown byte representation: {self.resource_id}")
        if self.endian not in ("none", "8in16", "8in32", "16in32", "host_native", "not_applicable"):
            raise CaptureBundleError(f"unknown endian mode: {self.resource_id}")
        if not self.data:
            raise CaptureBundleError(f"empty resource snapshot: {self.resource_id}")
        if any(not isinstance(register, int) or register < 0 for register in self.source_registers):
            raise CaptureBundleError(f"invalid source register list: {self.resource_id}")
        return {
            "resource_id": self.resource_id,
            "generation": self.generation,
            "kind": self.kind,
            "guest_address": self.guest_address,
            "data": self.data,
            "metadata": {
                "representation": self.representation,
                "endian": self.endian,
                "source_registers": list(self.source_registers),
                **dict(self.metadata),
            },
        }


class SelectedDrawSnapshot:
    """Thread-safe immutable handoff; copies inputs before source lifetimes end."""

    def __init__(self, max_bytes: int = 256 * 1024 * 1024):
        if max_bytes <= 0:
            raise ValueError("max_bytes must be positive")
        self._max_bytes = max_bytes
        self._lock = threading.Lock()
        self._payloads: dict[str, Payload] = {}
        self._total = 0
        self._sealed = False

    def add(self, payload: Payload) -> None:
        # Materialize bytes while caller still owns/locks the source resource.
        copied = Payload(payload.resource_id, payload.generation, payload.kind,
                         bytes(payload.data), payload.representation, payload.endian,
                         tuple(payload.source_registers), payload.guest_address,
                         tuple(payload.metadata))
        checked = copied.as_bundle_resource()
        with self._lock:
            if self._sealed:
                raise CaptureBundleError("snapshot is sealed")
            if copied.resource_id in self._payloads:
                raise CaptureBundleError(f"duplicate resource identity: {copied.resource_id}")
            if self._total + len(copied.data) > self._max_bytes:
                raise CaptureBundleError("selected draw exceeds payload byte limit")
            # Validate generations and shape before accepting the resource.
            if not isinstance(copied.generation, int) or copied.generation < 0:
                raise CaptureBundleError(f"invalid generation: {copied.resource_id}")
            self._payloads[copied.resource_id] = copied
            self._total += len(copied.data)

    def seal(self, required_kinds: Iterable[str]) -> list[dict]:
        with self._lock:
            kinds = {payload.kind for payload in self._payloads.values()}
            missing = set(required_kinds) - kinds
            if missing:
                raise CaptureBundleError(f"missing required payload kinds: {sorted(missing)}")
            self._sealed = True
            # Stable sort removes scheduler/insertion-order variance.
            return [self._payloads[key].as_bundle_resource()
                    for key in sorted(self._payloads)]

    def write_bundle(self, destination, metadata: dict, required_kinds: Iterable[str]) -> dict:
        resources = self.seal(required_kinds)
        metadata = dict(metadata)
        metadata["required_resource_kinds"] = sorted(set(required_kinds))
        return build_bundle(destination, metadata, resources, max_total_bytes=self._max_bytes)
