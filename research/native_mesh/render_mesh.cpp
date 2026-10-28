#include "context.h"
#include <cmath>
#include <limits>

struct Header {
  uint32_t magic,version,width,height,vertices,indices,topology,cull,front_ccw,restart,vs_bytes,shared_bytes;
  float viewport[6];
};
static_assert(sizeof(Header)==72);

int main(int argc,char** argv) {
  try {
    if(argc!=4)throw std::runtime_error("Usage: native_mesh fixture.bin NEW_output_directory shader_directory");
    auto bytes=read(argv[1]);
    if(bytes.size()<sizeof(Header))throw std::runtime_error("Truncated fixture");
    Header h;std::memcpy(&h,bytes.data(),sizeof(h));
    if(h.magic!=0x4853454d || (h.version!=1 && h.version!=2) || !h.width || !h.height || h.width>4096 || h.height>4096 ||
       !h.vertices || h.vertices>1000000 || !h.indices || h.indices>3000000 || h.topology>1 ||
       h.cull>2 || h.front_ccw>1 || h.restart>1 || h.vs_bytes!=4096 || h.shared_bytes!=512)
      throw std::runtime_error("Unsupported fixture contract");
    for(float v:h.viewport)if(!std::isfinite(v))throw std::runtime_error("Invalid viewport");
    uint32_t sample_count=1;size_t header_bytes=sizeof(h);
    if(h.version==2) {
      if(bytes.size()<header_bytes+4)throw std::runtime_error("Truncated sample count");
      std::memcpy(&sample_count,bytes.data()+header_bytes,4);header_bytes+=4;
    }
    if(sample_count!=1 && sample_count!=2)throw std::runtime_error("Unsupported sample count");
    const size_t vb_size=size_t(h.vertices)*20,ib_size=size_t(h.indices)*4;
    if(bytes.size()!=header_bytes+vb_size+ib_size+h.vs_bytes+h.shared_bytes)
      throw std::runtime_error("Fixture length mismatch");
    const uint8_t* vertices=bytes.data()+header_bytes;
    const uint8_t* indices=vertices+vb_size;
    const uint8_t* constants=indices+ib_size;
    const uint8_t* shared=constants+h.vs_bytes;
    for(size_t i=0;i<ib_size;i+=4) {
      uint32_t index;std::memcpy(&index,indices+i,4);
      if(index>=h.vertices && !(h.restart && index==UINT32_MAX))throw std::runtime_error("Index out of range");
    }
    fs::path output=argv[2],shaders=argv[3];
    if(fs::exists(output))throw std::runtime_error("Output must be new");
    fs::create_directories(output);
    Context c;c.init();auto d=c.device;
    auto vb=c.buffer(vb_size,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);c.upload(vb,vertices,vb_size);
    auto ib=c.buffer(ib_size,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);c.upload(ib,indices,ib_size);
    auto vc=c.buffer(h.vs_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);c.upload(vc,constants,h.vs_bytes);
    auto sc=c.buffer(h.shared_bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);c.upload(sc,shared,h.shared_bytes);
    auto readback=c.buffer(size_t(h.width)*h.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto target=c.image(h.width,h.height);
    const auto samples=static_cast<VkSampleCountFlagBits>(sample_count);
    auto raster_target=sample_count==1?target:c.image(h.width,h.height,target.format,false,samples);
    VkAttachmentDescription attachment{};attachment.format=target.format;attachment.samples=samples;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
    VkAttachmentDescription attachments[]={attachment,attachment};
    attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentReference resolve{1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    if(sample_count>1)sub.pResolveAttachments=&resolve;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=sample_count>1?2:1;ri.pAttachments=attachments;ri.subpassCount=1;ri.pSubpasses=&sub;
    VkRenderPass pass;check(vkCreateRenderPass(d,&ri,nullptr,&pass));c.retire.push_back([d,pass]{vkDestroyRenderPass(d,pass,nullptr);});
    VkPushConstantRange push_range{VK_SHADER_STAGE_VERTEX_BIT,0,24};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.pushConstantRangeCount=1;li.pPushConstantRanges=&push_range;
    VkPipelineLayout layout;check(vkCreatePipelineLayout(d,&li,nullptr,&layout));c.retire.push_back([d,layout]{vkDestroyPipelineLayout(d,layout,nullptr);});
    VkPipelineShaderStageCreateInfo stages[2]{};
    for(auto& s:stages){s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;s.pName="main";}
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=c.shader(shaders/"mesh.vert.spv");
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=c.shader(shaders/"flat.frag.spv");
    VkVertexInputBindingDescription binding{0,20,VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&binding;vi.vertexAttributeDescriptionCount=1;vi.pVertexAttributeDescriptions=&attribute;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology=h.topology?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;ia.primitiveRestartEnable=h.restart;
    VkViewport viewport{h.viewport[0],h.viewport[1],h.viewport[2],h.viewport[3],h.viewport[4],h.viewport[5]};
    VkRect2D scissor{{0,0},{h.width,h.height}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    // Match RenderDoc DebugOverlay::Drawcall, not the production material pass.
    rs.depthClampEnable=VK_TRUE;
    rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=h.cull==1?VK_CULL_MODE_FRONT_BIT:(h.cull==2?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE);
    rs.frontFace=h.front_ccw?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=samples;
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&blend;
    VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gi.stageCount=2;gi.pStages=stages;
    gi.pVertexInputState=&vi;gi.pInputAssemblyState=&ia;gi.pViewportState=&vp;gi.pRasterizationState=&rs;
    gi.pMultisampleState=&ms;gi.pColorBlendState=&bs;gi.layout=layout;gi.renderPass=pass;
    VkPipeline pipeline;check(vkCreateGraphicsPipelines(d,VK_NULL_HANDLE,1,&gi,nullptr,&pipeline));c.retire.push_back([d,pipeline]{vkDestroyPipeline(d,pipeline,nullptr);});
    VkImageView views[]={raster_target.view,target.view};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=sample_count>1?2:1;fi.pAttachments=views;fi.width=h.width;fi.height=h.height;fi.layers=1;
    VkFramebuffer fb;check(vkCreateFramebuffer(d,&fi,nullptr,&fb));c.retire.push_back([d,fb]{vkDestroyFramebuffer(d,fb,nullptr);});
    c.begin();
    c.barrier(target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if(sample_count>1)c.barrier(raster_target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=fb;begin.renderArea.extent={h.width,h.height};begin.clearValueCount=1;begin.pClearValues=&clear;
    vkCmdBeginRenderPass(c.command,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(c.command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    const uint64_t push[]={c.address(vc),c.address(vc),c.address(sc)};
    vkCmdPushConstants(c.command,layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),push);
    VkDeviceSize offset=0;vkCmdBindVertexBuffers(c.command,0,1,&vb.buffer,&offset);vkCmdBindIndexBuffer(c.command,ib.buffer,0,VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(c.command,h.indices,1,0,0,0);vkCmdEndRenderPass(c.command);
    c.barrier(target,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={h.width,h.height,1};
    vkCmdCopyImageToBuffer(c.command,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.buffer=readback.buffer;host.size=VK_WHOLE_SIZE;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    c.submit();void* mapped;size_t length=size_t(h.width)*h.height*4;check(vkMapMemory(d,readback.memory,0,length,0,&mapped));
    std::ofstream file(output/"native.rgba",std::ios::binary);file.write(static_cast<char*>(mapped),length);bool good=bool(file);vkUnmapMemory(d,readback.memory);
    if(!good)throw std::runtime_error("Output write failed");
    std::cout<<"mesh_completed="<<h.vertices<<" vertices "<<h.indices<<" indices "<<h.width<<"x"<<h.height<<"\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
