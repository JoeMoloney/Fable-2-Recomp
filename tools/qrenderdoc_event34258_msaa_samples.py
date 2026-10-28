"""Export per-sample color/depth boundaries for the existing Event 34258 RDC.

With the town-square capture open in QRenderDoc's Python console, run:

    exec(compile(open("/mnt/HDD/git_stuff/my_git/Fable-2-native-vulkan-renderer/tools/qrenderdoc_event34258_msaa_samples.py", "rb").read(), "qrenderdoc_event34258_msaa_samples.py", "exec"))

Only the already-captured event is replayed. The script exports samples 0/1 of
the color/depth targets immediately before and after the draw to a fresh
/tmp/opencode/town-square-event34258-msaa-* directory.
"""
import hashlib
import json
from pathlib import Path
import time
import traceback

CAPTURE = Path(
    "/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/renderdoc_captures/"
    "town-square-target-20261028.rdc"
)
EXPECTED_SHA256 = "ee6cbd4f329f867492a1fc573a93188c40929428c93ad65b14078ac0c023afc8"
EVENT = 34258
COLOR_ID = "ResourceId::445070"
DEPTH_ID = "ResourceId::445064"
WIDTH, HEIGHT, SAMPLES = 1120, 1176, 2


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sha_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(controller):
    import renderdoc as rd

    if not CAPTURE.is_file():
        raise RuntimeError("Capture not found: " + str(CAPTURE))
    capture_hash = sha_file(CAPTURE)
    if capture_hash != EXPECTED_SHA256:
        raise RuntimeError("Unexpected RDC SHA-256: " + capture_hash)

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    actions = list(walk(controller.GetRootActions()))
    target_rows = [a for a in actions if int(a.eventId) == EVENT]
    if len(target_rows) != 1:
        raise RuntimeError("Expected exactly one Event 34258 action")
    target = target_rows[0]
    if not (target.flags & rd.ActionFlags.Drawcall and target.flags & rd.ActionFlags.Indexed):
        raise RuntimeError("Event 34258 is not an indexed draw")
    if int(target.numIndices) != 9576:
        raise RuntimeError("Event 34258 index count changed: %s" % target.numIndices)

    ordered = sorted(actions, key=lambda a: int(a.eventId))
    before_rows = [a for a in ordered if int(a.eventId) < EVENT]
    after_rows = [a for a in ordered if int(a.eventId) > EVENT]
    if not before_rows or not after_rows:
        raise RuntimeError("Missing adjacent replay action boundary")
    before_event = int(before_rows[-1].eventId)
    after_event = int(target.eventId)

    # Confirm the action's final recorded API event is the indexed draw itself.
    sf = controller.GetStructuredFile()
    api_events = [e for e in target.events if int(e.chunkIndex) != int(rd.APIEvent.NoChunk)]
    if not api_events:
        raise RuntimeError("Event 34258 has no structured API events")
    final_api = api_events[-1]
    final_chunk = sf.chunks[int(final_api.chunkIndex)]
    if int(final_api.eventId) != EVENT or "DrawIndexed" not in str(final_chunk.name):
        raise RuntimeError("Event boundary mismatch: final API event is %s at %s" %
                           (final_api.eventId, final_chunk.name))

    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    if COLOR_ID not in textures or DEPTH_ID not in textures:
        raise RuntimeError("Captured color/depth resources are missing")
    color_desc, depth_desc = textures[COLOR_ID], textures[DEPTH_ID]
    for label, desc in (("color", color_desc), ("depth", depth_desc)):
        if int(desc.width) != WIDTH or int(desc.height) != HEIGHT or int(desc.msSamp) != SAMPLES:
            raise RuntimeError("Unexpected %s target dimensions/sample count: %s x %s x %s" %
                               (label, desc.width, desc.height, desc.msSamp))

    out = Path("/tmp/opencode") / ("town-square-event34258-msaa-" + time.strftime("%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=False)
    payloads = []
    for label, event in (("before", before_event), ("after", after_event)):
        controller.SetFrameEvent(event, True)
        for kind, rid in (("color", color_desc.resourceId), ("depth", depth_desc.resourceId)):
            for sample in range(SAMPLES):
                data = bytes(controller.GetTextureData(rid, rd.Subresource(0, 0, sample)))
                expected_bytes = WIDTH * HEIGHT * 8
                if len(data) != expected_bytes:
                    raise RuntimeError("Unexpected %s sample payload size at event %d sample %d: %d" %
                                       (kind, event, sample, len(data)))
                name = "%s-%s-sample%d.raw" % (kind, label, sample)
                (out / name).write_bytes(data)
                payloads.append({"kind": kind, "event": event, "sample": sample,
                                 "resource": COLOR_ID if kind == "color" else DEPTH_ID,
                                 "format": color_desc.format.Name() if kind == "color" else depth_desc.format.Name(),
                                 "width": WIDTH, "height": HEIGHT, "bytes": len(data),
                                 "sha256": sha(data), "file": name})

    manifest = {"capture": str(CAPTURE), "capture_sha256": capture_hash,
                "renderdoc": rd.GetVersionString(), "target_event": EVENT,
                "target_name": target.GetName(sf), "target_num_indices": int(target.numIndices),
                "target_indexed": True, "api_events": [
                    {"event": int(e.eventId), "chunk": int(e.chunkIndex),
                     "name": str(sf.chunks[int(e.chunkIndex)].name)} for e in api_events],
                "pre_event": before_event, "post_event": after_event,
                "sample_count": SAMPLES,
                "boundary_note": "Pre is the immediately preceding action (34256); post is the completed draw (34258).",
                "payloads": payloads}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print("Event 34258 MSAA samples exported:", out)
    print("Pre event:", before_event, "post event:", after_event,
          "sample payloads:", len(payloads))


try:
    pyrenderdoc.Replay().BlockInvoke(run)
except Exception:
    error_path = Path("/tmp/opencode/town-square-event34258-msaa-error.txt")
    error_path.write_text(traceback.format_exc())
    raise
