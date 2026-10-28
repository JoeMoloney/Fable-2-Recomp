"""Town-square RenderDoc inventory + Event 34258 payload extraction.

Run INSIDE QRenderDoc's built-in Python console with the captured RDC open:

    exec(compile(open("/mnt/HDD/git_stuff/my_git/Fable-2-native-vulkan-renderer/tools/qrenderdoc_town_square_extract.py", "rb").read(), "qrenderdoc_town_square_extract.py", "exec"))

Output is written to a fresh /tmp/opencode/town-square-rdc-extract-* folder.
This reuses the open ReplayController, does not open/modify the capture file,
and never exports the full shared-memory buffer.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import time
import traceback


CAPTURE = Path(
    "/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/renderdoc_captures/"
    "town-square-target-20261028.rdc"
)
EXPECTED_CAPTURE_SHA256 = "ee6cbd4f329f867492a1fc573a93188c40929428c93ad65b14078ac0c023afc8"
TARGET_EVENT = 34258
SHARED_MEMORY_BYTES = 512 * 1024 * 1024
MAX_FETCH_RANGE_BYTES = 16 * 1024 * 1024
MAX_FETCH_TOTAL_BYTES = 128 * 1024 * 1024
MAX_TEXTURE_TOTAL_BYTES = 256 * 1024 * 1024
MAX_CONSTANT_BYTES = 16 * 1024 * 1024
MAX_CANDIDATE_COUNT = 24


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, obj):
    Path(path).write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")


def enum_text(value):
    try:
        return str(value)
    except Exception:
        return repr(value)


def resource_text(value):
    return str(value) if value is not None else "ResourceId::0"


def shader_info(pipe, rd, stage, label, out_dir):
    reflection = pipe.GetShaderReflection(stage)
    shader = pipe.GetShader(stage)
    row = {"resource": resource_text(shader), "entry_point": pipe.GetShaderEntryPoint(stage),
           "reflection_available": reflection is not None}
    if reflection is None:
        return row
    raw = bytes(reflection.rawBytes)
    row.update({"spirv_bytes": len(raw), "spirv_sha256": sha(raw) if raw else None})
    if raw:
        name = label + ".spv"
        (out_dir / name).write_bytes(raw)
        row["spirv_file"] = name
    row["constant_blocks"] = [
        {"index": i, "name": block.name, "byte_size": int(block.byteSize),
         "fixed_set_or_space": int(block.fixedBindSetOrSpace),
         "fixed_binding": int(block.fixedBindNumber),
         "variables": [{"name": v.name, "byte_offset": int(v.byteOffset),
                        "type": v.type.name, "rows": int(v.type.rows),
                        "columns": int(v.type.columns), "elements": int(v.type.elements),
                        "array_byte_stride": int(v.type.arrayByteStride),
                        "matrix_byte_stride": int(v.type.matrixByteStride)}
                       for v in block.variables]}
        for i, block in enumerate(reflection.constantBlocks)
    ]
    row["read_only_resources"] = [
        {"index": i, "name": item.name, "fixed_set_or_space": int(item.fixedBindSetOrSpace),
         "fixed_binding": int(item.fixedBindNumber), "type": enum_text(item.descriptorType),
         "array_size": int(item.bindArraySize), "has_sampler": bool(item.hasSampler)}
        for i, item in enumerate(reflection.readOnlyResources)
    ]
    row["read_write_resources"] = [
        {"index": i, "name": item.name, "fixed_set_or_space": int(item.fixedBindSetOrSpace),
         "fixed_binding": int(item.fixedBindNumber), "type": enum_text(item.descriptorType),
         "array_size": int(item.bindArraySize)}
        for i, item in enumerate(reflection.readWriteResources)
    ]
    return row


def sampler_info(sampler):
    filt = sampler.filter
    return {"object": resource_text(sampler.object), "type": enum_text(sampler.type),
            "address_u": enum_text(sampler.addressU), "address_v": enum_text(sampler.addressV),
            "address_w": enum_text(sampler.addressW), "minify": enum_text(filt.minify),
            "magnify": enum_text(filt.magnify), "mip": enum_text(filt.mip),
            "filter": enum_text(filt.filter), "min_lod": sampler.minLOD,
            "max_lod": sampler.maxLOD, "mip_bias": sampler.mipBias,
            "max_anisotropy": sampler.maxAnisotropy,
            "unnormalized": bool(sampler.unnormalized)}


def descriptor_info(used, buffers, textures):
    d = used.descriptor
    key = resource_text(d.resource)
    info = {"access_index": int(used.access.index),
            "array_element": int(used.access.arrayElement),
            "access_type": enum_text(used.access.type),
            "descriptor_store": resource_text(used.access.descriptorStore),
            "resource": key, "view": resource_text(d.view),
            "secondary": resource_text(d.secondary), "descriptor_type": enum_text(d.type),
            "byte_offset": int(d.byteOffset), "byte_size": int(d.byteSize),
            "element_byte_size": int(d.elementByteSize), "format": d.format.Name(),
            "first_mip": int(d.firstMip), "num_mips": int(d.numMips),
            "first_slice": int(d.firstSlice), "num_slices": int(d.numSlices),
            "swizzle": [int(d.swizzle.red), int(d.swizzle.green),
                        int(d.swizzle.blue), int(d.swizzle.alpha)]}
    if key in buffers:
        info["buffer_info"] = {"length": int(buffers[key].length),
                               "creation_flags": int(buffers[key].creationFlags)}
    if key in textures:
        t = textures[key]
        info["texture_info"] = {"width": int(t.width), "height": int(t.height),
                                "depth": int(t.depth), "format": t.format.Name(),
                                "mips": int(t.mips), "array_size": int(t.arraysize),
                                "samples": int(t.msSamp)}
    if d.resource != rd.ResourceId.Null():
        info["sampler"] = sampler_info(used.sampler)
    return info


def draw_summary(action):
    return {"event": int(action.eventId), "name": action.GetName(_structured_file),
            "num_indices": int(action.numIndices), "num_instances": int(action.numInstances),
            "base_vertex": int(action.baseVertex), "index_offset": int(action.indexOffset),
            "flags": int(action.flags), "indexed": bool(action.flags & rd.ActionFlags.Indexed),
            "drawcall": bool(action.flags & rd.ActionFlags.Drawcall)}


def inspect_draw(controller, action, textures, buffers, write_payloads=False,
                 save_overlay=False):
    event = int(action.eventId)
    controller.SetFrameEvent(event, True)
    pipe = controller.GetPipelineState()
    vk = controller.GetVulkanPipelineState()
    folder = OUT / ("event-%d" % event)
    folder.mkdir(exist_ok=True)

    row = draw_summary(action)
    row["topology"] = enum_text(pipe.GetPrimitiveTopology())
    row["viewport"] = {"x": pipe.GetViewport(0).x, "y": pipe.GetViewport(0).y,
                        "width": pipe.GetViewport(0).width, "height": pipe.GetViewport(0).height,
                        "min_depth": pipe.GetViewport(0).minDepth,
                        "max_depth": pipe.GetViewport(0).maxDepth}
    scissor = pipe.GetScissor(0)
    row["scissor"] = {"x": int(scissor.x), "y": int(scissor.y),
                      "width": int(scissor.width), "height": int(scissor.height)}
    row["restart_index"] = int(pipe.GetRestartIndex())
    row["index_buffer"] = {"resource": resource_text(pipe.GetIBuffer().resourceId),
                           "offset": int(pipe.GetIBuffer().byteOffset),
                           "stride": int(pipe.GetIBuffer().byteStride),
                           "size": int(pipe.GetIBuffer().byteSize)}
    row["vertex_buffers"] = [
        {"resource": resource_text(v.resourceId), "offset": int(v.byteOffset),
         "stride": int(v.byteStride), "size": int(v.byteSize)} for v in pipe.GetVBuffers()
    ]
    row["vertex_inputs"] = [
        {"name": v.name, "used": bool(v.used), "buffer_index": int(v.vertexBuffer),
         "offset": int(v.byteOffset), "format": v.format.Name()} for v in pipe.GetVertexInputs()
    ]
    row["pipeline"] = {"graphics_pipeline": resource_text(vk.graphics.pipelineResourceId),
                       "rasterizer": {"cull_mode": enum_text(vk.rasterizer.cullMode),
                                      "front_ccw": bool(vk.rasterizer.frontCCW),
                                      "fill_mode": enum_text(vk.rasterizer.fillMode),
                                      "depth_bias_enable": bool(vk.rasterizer.depthBiasEnable),
                                      "depth_bias": vk.rasterizer.depthBias,
                                      "slope_scaled_depth_bias": vk.rasterizer.slopeScaledDepthBias,
                                      "depth_bias_clamp": vk.rasterizer.depthBiasClamp},
                       "depth_stencil": {"depth_test": bool(vk.depthStencil.depthTestEnable),
                                         "depth_write": bool(vk.depthStencil.depthWriteEnable),
                                         "depth_function": enum_text(vk.depthStencil.depthFunction),
                                         "stencil_test": bool(vk.depthStencil.stencilTestEnable),
                                         "front_stencil": {"reference": int(vk.depthStencil.frontFace.reference),
                                                           "function": enum_text(vk.depthStencil.frontFace.function),
                                                           "compare_mask": int(vk.depthStencil.frontFace.compareMask),
                                                           "write_mask": int(vk.depthStencil.frontFace.writeMask),
                                                           "fail": enum_text(vk.depthStencil.frontFace.failOperation),
                                                           "depth_fail": enum_text(vk.depthStencil.frontFace.depthFailOperation),
                                                           "pass": enum_text(vk.depthStencil.frontFace.passOperation)},
                                         "back_stencil": {"reference": int(vk.depthStencil.backFace.reference),
                                                          "function": enum_text(vk.depthStencil.backFace.function),
                                                          "compare_mask": int(vk.depthStencil.backFace.compareMask),
                                                          "write_mask": int(vk.depthStencil.backFace.writeMask),
                                                          "fail": enum_text(vk.depthStencil.backFace.failOperation),
                                                          "depth_fail": enum_text(vk.depthStencil.backFace.depthFailOperation),
                                                          "pass": enum_text(vk.depthStencil.backFace.passOperation)}},
                       "blends": [{"enabled": bool(b.enabled), "write_mask": int(b.writeMask),
                                   "logic_operation_enabled": bool(b.logicOperationEnabled),
                                   "logic_operation": enum_text(b.logicOperation),
                                   "color": {"source": enum_text(b.colorBlend.source),
                                             "destination": enum_text(b.colorBlend.destination),
                                             "operation": enum_text(b.colorBlend.operation)},
                                   "alpha": {"source": enum_text(b.alphaBlend.source),
                                             "destination": enum_text(b.alphaBlend.destination),
                                             "operation": enum_text(b.alphaBlend.operation)}}
                                  for b in pipe.GetColorBlends()]}

    row["shaders"] = {
        "vertex": shader_info(pipe, rd, rd.ShaderStage.Vertex, "vertex.spv", folder),
        "pixel": shader_info(pipe, rd, rd.ShaderStage.Pixel, "pixel.spv", folder),
    }

    constants = []
    fetch_table = None
    for stage_name, stage in (("vertex", rd.ShaderStage.Vertex), ("pixel", rd.ShaderStage.Pixel)):
        reflection = pipe.GetShaderReflection(stage)
        for i, used in enumerate(pipe.GetConstantBlocks(stage, True)):
            d = used.descriptor
            name = reflection.constantBlocks[i].name if reflection and i < len(reflection.constantBlocks) else ""
            item = {"stage": stage_name, "index": i, "name": name,
                    "resource": resource_text(d.resource), "offset": int(d.byteOffset),
                    "size": int(d.byteSize), "range": int(d.byteSize),
                    "binding": int(reflection.constantBlocks[i].fixedBindNumber)
                    if reflection and i < len(reflection.constantBlocks) else None,
                    "file": None, "sha256": None}
            if d.resource != rd.ResourceId.Null() and 0 < int(d.byteSize) <= MAX_CONSTANT_BYTES:
                data = bytes(controller.GetBufferData(d.resource, int(d.byteOffset), int(d.byteSize)))
                if len(data) == int(d.byteSize):
                    filename = "%s-constant-%02d.bin" % (stage_name, i)
                    (folder / filename).write_bytes(data)
                    item["file"] = filename
                    item["sha256"] = sha(data)
                    if stage_name == "vertex" and name == "xe_uniform_fetch_constants":
                        fetch_table = data
            constants.append(item)
    row["constant_buffers"] = constants

    row["read_only_resources"] = {}
    row["read_write_resources"] = {}
    row["samplers"] = {}
    for stage_name, stage in (("vertex", rd.ShaderStage.Vertex), ("pixel", rd.ShaderStage.Pixel)):
        read = list(pipe.GetReadOnlyResources(stage, True))
        rw = list(pipe.GetReadWriteResources(stage, True))
        samplers = list(pipe.GetSamplers(stage, True))
        row["read_only_resources"][stage_name] = [descriptor_info(x, buffers, textures) for x in read]
        row["read_write_resources"][stage_name] = [descriptor_info(x, buffers, textures) for x in rw]
        row["samplers"][stage_name] = [
            {"access_index": int(x.access.index), "descriptor_store": resource_text(x.access.descriptorStore),
             "sampler": sampler_info(x.sampler)} for x in samplers
        ]

    row["render_targets"] = []
    for target in pipe.GetOutputTargets():
        item = {"resource": resource_text(target.resource), "view": resource_text(target.view),
                "format": target.format.Name()}
        key = resource_text(target.resource)
        if key in textures:
            t = textures[key]
            item.update({"width": int(t.width), "height": int(t.height), "mips": int(t.mips),
                         "samples": int(t.msSamp)})
        row["render_targets"].append(item)
    depth = pipe.GetDepthTarget()
    row["depth_target"] = {"resource": resource_text(depth.resource),
                           "view": resource_text(depth.view), "format": depth.format.Name()}

    if save_overlay:
        target = next((t.resource for t in pipe.GetOutputTargets()
                       if t.resource != rd.ResourceId.Null()), rd.ResourceId.Null())
        if target == rd.ResourceId.Null() and depth.resource != rd.ResourceId.Null():
            target = depth.resource
        target_desc = textures.get(resource_text(target))
        if target_desc is not None:
            try:
                output = controller.CreateOutput(
                    rd.CreateHeadlessWindowingData(int(target_desc.width), int(target_desc.height)),
                    rd.ReplayOutputType.Texture)
                display = rd.TextureDisplay()
                display.resourceId = target
                display.overlay = rd.DebugOverlay.Drawcall
                display.typeCast = rd.CompType.Typeless
                display.scale = 1.0
                output.SetTextureDisplay(display)
                output.Display()
                overlay = output.GetDebugOverlayTexID()
                if overlay != rd.ResourceId.Null():
                    save = rd.TextureSave()
                    save.resourceId = overlay
                    save.destType = rd.FileType.PNG
                    png_name = "drawcall-overlay.png"
                    result = controller.SaveTexture(save, str(folder / png_name))
                    row["drawcall_overlay"] = {"file": png_name,
                                               "status": enum_text(result),
                                               "target": resource_text(target)}
                else:
                    row["drawcall_overlay"] = {"error": "RenderDoc returned a null overlay"}
                output.Shutdown()
            except Exception as e:
                row["drawcall_overlay"] = {"error": str(e)}

    if write_payloads:
        # Raw index bytes at the draw's actual index range.
        ib = pipe.GetIBuffer()
        stride = int(ib.byteStride)
        if stride in (1, 2, 4) and int(action.numIndices) > 0:
            offset = int(ib.byteOffset) + int(action.indexOffset) * stride
            length = int(action.numIndices) * stride
            if length <= 64 * 1024 * 1024:
                data = bytes(controller.GetBufferData(ib.resourceId, offset, length))
                if len(data) == length:
                    (folder / "indices.raw").write_bytes(data)
                    row["index_payload"] = {"file": "indices.raw", "offset": offset,
                                             "bytes": len(data), "stride": stride,
                                             "sha256": sha(data)}
        # Retain post-VS bytes strictly as a comparison oracle, never as native
        # input geometry.
        try:
            post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            post_size = int(post.numIndices) * int(post.vertexByteStride)
            post_info = {"resource": resource_text(post.vertexResourceId),
                         "offset": int(post.vertexByteOffset), "stride": int(post.vertexByteStride),
                         "num_indices": int(post.numIndices), "allow_restart": bool(post.allowRestart),
                         "restart_index": int(post.restartIndex), "native_input": False}
            if 0 < post_size <= 128 * 1024 * 1024:
                post_data = bytes(controller.GetBufferData(post.vertexResourceId,
                                                          int(post.vertexByteOffset), post_size))
                if len(post_data) == post_size:
                    (folder / "post-vs-oracle.raw").write_bytes(post_data)
                    post_info.update({"file": "post-vs-oracle.raw", "bytes": len(post_data),
                                      "sha256": sha(post_data)})
            row["post_vs_oracle"] = post_info
        except Exception as e:
            row["post_vs_oracle"] = {"error": str(e), "native_input": False}
        # Extract candidate guest vertex fetch ranges from the actual bound
        # 96-entry two-dword fetch table. Never dump the 512 MiB shared buffer.
        shared_ids = sorted({resource_text(x.descriptor.resource)
                             for stage in (rd.ShaderStage.Vertex,)
                             for x in list(pipe.GetReadOnlyResources(stage, True)) +
                                      list(pipe.GetReadWriteResources(stage, True))
                             if resource_text(x.descriptor.resource) in buffers and
                             int(buffers[resource_text(x.descriptor.resource)].length) == SHARED_MEMORY_BYTES})
        ranges = []
        if fetch_table and len(shared_ids) == 1 and len(fetch_table) >= 96 * 8:
            shared_id = next(x.descriptor.resource for x in
                             list(pipe.GetReadOnlyResources(rd.ShaderStage.Vertex, True)) +
                             list(pipe.GetReadWriteResources(rd.ShaderStage.Vertex, True))
                             if resource_text(x.descriptor.resource) == shared_ids[0])
            total = 0
            seen = set()
            for slot in range(96):
                word0 = int.from_bytes(fetch_table[slot * 8:slot * 8 + 4], "little")
                word1 = int.from_bytes(fetch_table[slot * 8 + 4:slot * 8 + 8], "little")
                if (word0 & 3) != 3:
                    continue
                address = word0 & ~3
                size = ((word1 >> 2) & 0xFFFFFF) * 4
                if not size or size > MAX_FETCH_RANGE_BYTES or address + size > SHARED_MEMORY_BYTES:
                    continue
                if (address, size) in seen or total + size > MAX_FETCH_TOTAL_BYTES:
                    continue
                seen.add((address, size))
                data = bytes(controller.GetBufferData(shared_id, address, size))
                if len(data) != size:
                    continue
                name = "vertex-fetch-%02d.raw" % slot
                (folder / name).write_bytes(data)
                ranges.append({"slot": slot, "address": address, "bytes": len(data),
                               "endian": word1 & 3, "words": [word0, word1],
                               "file": name, "sha256": sha(data)})
                total += size
        row["shared_memory"] = {"resource": shared_ids[0] if len(shared_ids) == 1 else None,
                                "full_dump_skipped": True, "range_cap_bytes": MAX_FETCH_TOTAL_BYTES,
                                "ranges": ranges}

        # Export used bound texture mips/layers under a strict aggregate cap.
        texture_payloads = []
        texture_total = 0
        texture_seen = set()
        for stage_name in ("vertex", "pixel"):
            stage = rd.ShaderStage.Vertex if stage_name == "vertex" else rd.ShaderStage.Pixel
            used_resources = (list(pipe.GetReadOnlyResources(stage, True)) +
                              list(pipe.GetReadWriteResources(stage, True)))
            for used in used_resources:
                d = used.descriptor
                key = resource_text(d.resource)
                view_key = (key, int(d.firstMip), int(d.numMips),
                            int(d.firstSlice), int(d.numSlices))
                if key not in textures or view_key in texture_seen:
                    continue
                texture_seen.add(view_key)
                desc = textures[key]
                resource_row = {"stage": stage_name, "resource": key,
                                "format": desc.format.Name(), "width": int(desc.width),
                                "height": int(desc.height), "depth": int(desc.depth),
                                "mips": int(desc.mips), "array_size": int(desc.arraysize),
                                "samples": int(desc.msSamp), "subresources": []}
                mip_end = min(int(desc.mips), int(d.firstMip) + int(d.numMips))
                slice_end = min(int(desc.arraysize), int(d.firstSlice) + max(int(d.numSlices), 1))
                for mip in range(int(d.firstMip), mip_end):
                    for layer in range(int(d.firstSlice), slice_end):
                        try:
                            data = bytes(controller.GetTextureData(d.resource, rd.Subresource(mip, layer, 0)))
                        except Exception as e:
                            resource_row["subresources"].append({"mip": mip, "layer": layer,
                                                                 "error": str(e)})
                            continue
                        if not data or texture_total + len(data) > MAX_TEXTURE_TOTAL_BYTES:
                            resource_row["subresources"].append({"mip": mip, "layer": layer,
                                                                 "skipped": "aggregate texture cap"})
                            continue
                        name = "texture-%s-mip-%d-layer-%d.raw" % (key.replace("::", "-"), mip, layer)
                        (folder / name).write_bytes(data)
                        resource_row["subresources"].append({"mip": mip, "layer": layer,
                                                             "bytes": len(data), "file": name,
                                                             "sha256": sha(data)})
                        texture_total += len(data)
                texture_payloads.append(resource_row)
        row["texture_payloads"] = texture_payloads
        row["texture_payload_bytes"] = texture_total

        # Save color target before and after this draw as PNG where supported.
        color_target = next((t.resource for t in pipe.GetOutputTargets()
                             if t.resource != rd.ResourceId.Null()), rd.ResourceId.Null())
        row["reference_output"] = {"target": resource_text(color_target)}
        previous = max((int(a.eventId) for a in ACTIONS if int(a.eventId) < event), default=None)
        if color_target != rd.ResourceId.Null():
            for label, when in (("before", previous), ("after", event)):
                if when is None:
                    continue
                try:
                    controller.SetFrameEvent(when, True)
                    raw = bytes(controller.GetTextureData(color_target, rd.Subresource(0, 0, 0)))
                    raw_name = "color-%s.raw" % label
                    (folder / raw_name).write_bytes(raw)
                    png_name = "color-%s.png" % label
                    save = rd.TextureSave()
                    save.resourceId = color_target
                    save.destType = rd.FileType.PNG
                    result = controller.SaveTexture(save, str(folder / png_name))
                    row["reference_output"][label] = {"event": when, "raw_file": raw_name,
                                                      "raw_bytes": len(raw), "raw_sha256": sha(raw),
                                                      "png_file": png_name,
                                                      "png_status": enum_text(result)}
                except Exception as e:
                    row["reference_output"][label] = {"event": when, "error": str(e)}
            controller.SetFrameEvent(event, True)
        if depth.resource != rd.ResourceId.Null():
            row["depth_reference"] = {"target": resource_text(depth.resource)}
            for label, when in (("before", previous), ("after", event)):
                if when is None:
                    continue
                try:
                    controller.SetFrameEvent(when, True)
                    raw = bytes(controller.GetTextureData(depth.resource, rd.Subresource(0, 0, 0)))
                    filename = "depth-%s.raw" % label
                    (folder / filename).write_bytes(raw)
                    row["depth_reference"][label] = {"event": when, "file": filename,
                                                       "bytes": len(raw), "sha256": sha(raw)}
                except Exception as e:
                    row["depth_reference"][label] = {"event": when, "error": str(e)}
            controller.SetFrameEvent(event, True)
    return row


def run(controller):
    global rd, OUT, ACTIONS, _structured_file
    import renderdoc as rd

    if not CAPTURE.is_file():
        raise RuntimeError("capture not found: " + str(CAPTURE))
    hasher = hashlib.sha256()
    with CAPTURE.open("rb") as capture_file:
        for block in iter(lambda: capture_file.read(1024 * 1024), b""):
            hasher.update(block)
    capture_hash = hasher.hexdigest()
    if capture_hash != EXPECTED_CAPTURE_SHA256:
        raise RuntimeError("capture SHA-256 mismatch: " + capture_hash)

    root = Path("/tmp/opencode")
    root.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    OUT = root / ("town-square-rdc-extract-34258-" + stamp)
    OUT.mkdir()
    _structured_file = controller.GetStructuredFile()
    ACTIONS = []

    def walk(items):
        for action in items:
            ACTIONS.append(action)
            yield action
            yield from walk(action.children)

    all_actions = list(walk(controller.GetRootActions()))
    draw_actions = [a for a in all_actions
                    if a.flags & rd.ActionFlags.Drawcall and a.flags & rd.ActionFlags.Indexed]
    action_rows = [draw_summary(a) for a in draw_actions]
    write_json(OUT / "indexed-draw-actions.json", {
        "capture": str(CAPTURE), "capture_sha256": capture_hash,
        "renderdoc": rd.GetVersionString(), "event_count": len(all_actions),
        "indexed_draw_count": len(draw_actions), "draws": action_rows,
    })

    if not any(int(a.eventId) == TARGET_EVENT for a in draw_actions):
        raise RuntimeError("target event %d is not an indexed draw in this capture" % TARGET_EVENT)

    # Candidate ranking is a bounded survey, not an assertion of staticness.
    # Favor ordinary one-instance triangle draws with modest index ranges; the
    # replay-state inventory lets a human choose a visually useful static mesh.
    ranked = []
    for a in draw_actions:
        n = int(a.numIndices)
        if int(a.numInstances) != 1 or n < 12 or n > 6000:
            continue
        score = 100.0 - abs(math.log2(max(n, 1)) - 8.0) * 8.0
        if int(a.eventId) == TARGET_EVENT:
            score += 5.0
        ranked.append((score, a))
    ranked.sort(key=lambda pair: (-pair[0], int(pair[1].eventId)))
    shortlist = []
    seen = {TARGET_EVENT}
    for score, a in ranked:
        ev = int(a.eventId)
        if ev in seen:
            continue
        shortlist.append((score, a))
        seen.add(ev)
        if len(shortlist) >= MAX_CANDIDATE_COUNT:
            break

    buffers = {str(b.resourceId): b for b in controller.GetBuffers()}
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    inspections = []
    target_row = None
    overlay_events = {int(a.eventId) for _, a in shortlist[:8]}
    for score, action in [(1000.0, next(a for a in draw_actions if int(a.eventId) == TARGET_EVENT))] + shortlist:
        is_target = int(action.eventId) == TARGET_EVENT
        row = inspect_draw(controller, action, textures, buffers, write_payloads=is_target,
                           save_overlay=is_target or int(action.eventId) in overlay_events)
        shader_bytes = sum(int(s.get("spirv_bytes", 0)) for s in row["shaders"].values())
        texture_ids = {d["resource"] for stage in ("vertex", "pixel")
                       for d in row["read_only_resources"].get(stage, [])
                       if d["resource"] in textures}
        read_write_count = sum(len(row["read_write_resources"].get(stage, []))
                               for stage in ("vertex", "pixel"))
        shared_bound = any(x.get("buffer_info", {}).get("length") == SHARED_MEMORY_BYTES
                           for stage in ("vertex", "pixel")
                           for x in row["read_only_resources"].get(stage, []) +
                                    row["read_write_resources"].get(stage, []))
        index_count = max(1, int(row["num_indices"]))
        row["candidate_complexity"] = {"shader_spirv_bytes": shader_bytes,
                                       "used_texture_count": len(texture_ids),
                                       "read_write_binding_count": read_write_count,
                                       "shared_memory_backed": shared_bound}
        row["candidate_score"] = (
            100.0 - abs(math.log2(index_count) - 6.0) * 8.0
            - len(texture_ids) * 5.0 - shader_bytes / 8192.0
            - read_write_count * 10.0 + (8.0 if not shared_bound else 0.0)
        )
        row["full_payload_extracted"] = is_target
        inspections.append(row)
        if is_target:
            target_row = row
        write_json(OUT / "draw-inspection-progress.json", inspections)

    write_json(OUT / "candidate-shortlist.json", {
        "selection_note": "Rank is a triage heuristic: single-instance indexed draws with "
                          "modest index count, fewer/smaller shaders and fewer texture/UAV "
                          "bindings rank higher. It cannot prove staticness; review the saved "
                          "drawcall overlays and event state before choosing a native target.",
        "target_event": TARGET_EVENT,
        "candidate_events": [int(row["event"]) for row in inspections],
        "draws": inspections,
    })
    controller.SetFrameEvent(TARGET_EVENT, True)
    write_json(OUT / "event-34258-manifest.json", {
        "capture": str(CAPTURE), "capture_sha256": capture_hash,
        "renderdoc": rd.GetVersionString(), "event": TARGET_EVENT,
        "extraction": target_row,
        "limits": {"shared_memory_full_dump": False,
                   "max_fetch_range_bytes": MAX_FETCH_RANGE_BYTES,
                   "max_fetch_total_bytes": MAX_FETCH_TOTAL_BYTES,
                   "max_texture_total_bytes": MAX_TEXTURE_TOTAL_BYTES},
    })
    print("Town-square RenderDoc extraction complete:", OUT)
    print("Indexed draw inventory:", len(draw_actions), "shortlist:", len(inspections),
          "target event:", TARGET_EVENT)


try:
    pyrenderdoc.Replay().BlockInvoke(run)
except Exception:
    root = Path("/tmp/opencode")
    root.mkdir(parents=True, exist_ok=True)
    (root / "town-square-rdc-extract-console-error.txt").write_text(traceback.format_exc())
    raise
