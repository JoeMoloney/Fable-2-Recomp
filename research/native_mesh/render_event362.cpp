// Isolated single-draw reproduction using the captured event-362 shaders.
#include "context.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>

struct SampledImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  uint32_t width = 0, height = 0, mips = 1;
  VkFormat format = VK_FORMAT_UNDEFINED;
};

static SampledImage make_sampled(Context& c, uint32_t width, uint32_t height, uint32_t mips,
                                 VkFormat format, const std::vector<std::vector<uint8_t>>& levels,
                                 VkComponentMapping mapping) {
  if (!width || !height || !mips || levels.size() != mips)
    throw std::runtime_error("Invalid sampled image dimensions/mips");
  SampledImage im{}; im.width = width; im.height = height; im.mips = mips; im.format = format;
  VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(c.physical, format, &fp);
  if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
      !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
    throw std::runtime_error("Captured sampled format unsupported for optimal upload");
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = {width, height, 1};
  ci.mipLevels = mips; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  check(vkCreateImage(c.device, &ci, nullptr, &im.image));
  VkMemoryRequirements req{}; vkGetImageMemoryRequirements(c.device, im.image, &req);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
  ai.memoryTypeIndex = c.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(c.device, &ai, nullptr, &im.memory));
  check(vkBindImageMemory(c.device, im.image, im.memory, 0));
  auto image = im.image; auto memory = im.memory;
  c.retire.push_back([d=c.device, image]{ vkDestroyImage(d, image, nullptr); });
  c.retire.push_back([d=c.device, memory]{ vkFreeMemory(d, memory, nullptr); });

  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  // Captured Xenos SPIR-V declares sampled images as arrayed 2D images,
  // including single-layer texture resources.
  vi.image = im.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.format = format;
  vi.components = mapping; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
  check(vkCreateImageView(c.device, &vi, nullptr, &im.view));
  auto view = im.view; c.retire.push_back([d=c.device, view]{ vkDestroyImageView(d, view, nullptr); });

  VkDeviceSize total = 0;
  std::vector<VkBufferImageCopy> regions;
  for (uint32_t mip=0; mip<mips; ++mip) {
    total = (total + 3) & ~VkDeviceSize(3);
    VkBufferImageCopy r{}; r.bufferOffset = total;
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    r.imageExtent = {std::max(1u,width >> mip), std::max(1u,height >> mip), 1};
    regions.push_back(r); total += levels[mip].size();
  }
  auto staging = c.buffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  void* mapped = nullptr; check(vkMapMemory(c.device, staging.memory, 0, total, 0, &mapped));
  for (uint32_t mip=0; mip<mips; ++mip)
    std::memcpy(static_cast<uint8_t*>(mapped) + regions[mip].bufferOffset,
                levels[mip].data(), levels[mip].size());
  vkUnmapMemory(c.device, staging.memory);
  c.begin();
  VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  toCopy.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; toCopy.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toCopy.srcQueueFamilyIndex=toCopy.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  toCopy.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; toCopy.image=im.image;
  toCopy.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,mips,0,1};
  vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0,0,nullptr,0,nullptr,1,&toCopy);
  vkCmdCopyBufferToImage(c.command,staging.buffer,im.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         static_cast<uint32_t>(regions.size()),regions.data());
  VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  toRead.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; toRead.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  toRead.srcQueueFamilyIndex=toRead.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
  toRead.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; toRead.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
  toRead.image=im.image; toRead.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,mips,0,1};
  vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       0,0,nullptr,0,nullptr,1,&toRead);
  c.submit();
  return im;
}

static VkSampler make_sampler(Context& c, VkFilter minFilter, VkFilter magFilter,
                              VkSamplerMipmapMode mipMode, VkSamplerAddressMode addressU,
                              VkSamplerAddressMode addressV, VkSamplerAddressMode addressW,
                              float minLod, float maxLod, float maxAnisotropy) {
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter=magFilter; si.minFilter=minFilter; si.mipmapMode=mipMode;
  si.addressModeU=addressU; si.addressModeV=addressV; si.addressModeW=addressW;
  si.minLod=minLod; si.maxLod=maxLod;
  si.anisotropyEnable=maxAnisotropy>1.0f; si.maxAnisotropy=std::max(1.0f,maxAnisotropy);
  VkSampler sampler; check(vkCreateSampler(c.device,&si,nullptr,&sampler));
  c.retire.push_back([d=c.device,sampler]{vkDestroySampler(d,sampler,nullptr);});
  return sampler;
}

static VkDescriptorSetLayout make_set_layout(Context& c,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
  VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  ci.bindingCount=static_cast<uint32_t>(bindings.size()); ci.pBindings=bindings.data();
  VkDescriptorSetLayout layout; check(vkCreateDescriptorSetLayout(c.device,&ci,nullptr,&layout));
  c.retire.push_back([d=c.device,layout]{vkDestroyDescriptorSetLayout(d,layout,nullptr);});
  return layout;
}

using DrawConfig=std::unordered_map<std::string,std::string>;
static DrawConfig read_config(const fs::path& fixture) {
  DrawConfig c;std::ifstream f(fixture/"event.cfg");
  if(!f)throw std::runtime_error("Missing event.cfg in "+fixture.string());
  std::string line;while(std::getline(f,line)){auto p=line.find('=');if(p!=std::string::npos)c[line.substr(0,p)]=line.substr(p+1);}
  return c;
}
static std::string cfg_string(const DrawConfig& c,const std::string& key) {
  auto i=c.find(key);if(i==c.end())throw std::runtime_error("Missing config: "+key);return i->second;
}
static uint32_t cfg_u32(const DrawConfig& c,const std::string& key) {return static_cast<uint32_t>(std::stoul(cfg_string(c,key)));}
static int cfg_int(const DrawConfig& c,const std::string& key) {return std::stoi(cfg_string(c,key));}
static float cfg_float(const DrawConfig& c,const std::string& key) {return std::stof(cfg_string(c,key));}
static bool cfg_bool(const DrawConfig& c,const std::string& key) {
  std::string v=cfg_string(c,key);if(v=="1"||v=="True"||v=="true")return true;
  if(v=="0"||v=="False"||v=="false")return false;
  throw std::runtime_error("Invalid boolean config: "+key);
}

struct PreparedDraw {
  VkPipeline pipeline=VK_NULL_HANDLE;
  VkPipelineLayout layout=VK_NULL_HANDLE;
  std::array<VkDescriptorSet,4> sets{};
  Buffer indices{};
  uint32_t indexCount=0,instances=1,indexStride=2;
  int32_t baseVertex=0;
};

static VkCompareOp compare_op(int value) {
  switch(value){case 0:return VK_COMPARE_OP_NEVER;case 1:return VK_COMPARE_OP_ALWAYS;
    case 2:return VK_COMPARE_OP_LESS;case 3:return VK_COMPARE_OP_LESS_OR_EQUAL;
    case 4:return VK_COMPARE_OP_GREATER;case 5:return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case 6:return VK_COMPARE_OP_EQUAL;case 7:return VK_COMPARE_OP_NOT_EQUAL;
    default:throw std::runtime_error("Unsupported compare operation");}
}
static VkStencilOp stencil_op(int value) {
  switch(value){case 0:return VK_STENCIL_OP_KEEP;case 1:return VK_STENCIL_OP_ZERO;
    case 2:return VK_STENCIL_OP_REPLACE;case 3:return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 4:return VK_STENCIL_OP_DECREMENT_AND_CLAMP;case 5:return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 6:return VK_STENCIL_OP_DECREMENT_AND_WRAP;case 7:return VK_STENCIL_OP_INVERT;
    default:throw std::runtime_error("Unsupported stencil operation");}
}
static VkBlendFactor blend_factor(int value) {
  switch(value){case 0:return VK_BLEND_FACTOR_ZERO;case 1:return VK_BLEND_FACTOR_ONE;
    case 2:return VK_BLEND_FACTOR_SRC_COLOR;case 3:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 4:return VK_BLEND_FACTOR_DST_COLOR;case 5:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 6:return VK_BLEND_FACTOR_SRC_ALPHA;case 7:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 8:return VK_BLEND_FACTOR_DST_ALPHA;case 9:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 10:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;case 11:return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 12:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;case 13:return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 14:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;case 15:return VK_BLEND_FACTOR_SRC1_COLOR;
    case 16:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;case 17:return VK_BLEND_FACTOR_SRC1_ALPHA;
    case 18:return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;default:throw std::runtime_error("Unsupported blend factor");}
}
static VkBlendOp blend_op(int value) {
  switch(value){case 0:return VK_BLEND_OP_ADD;case 1:return VK_BLEND_OP_SUBTRACT;
    case 2:return VK_BLEND_OP_REVERSE_SUBTRACT;case 3:return VK_BLEND_OP_MIN;
    case 4:return VK_BLEND_OP_MAX;default:throw std::runtime_error("Unsupported blend operation");}
}

static PreparedDraw prepare_draw(Context& c,const fs::path& fixture,VkRenderPass pass) {
  const DrawConfig cfg=read_config(fixture);
  auto load=[&](const char* name){return read(fixture/name);};
  const bool fullShared=cfg.count("shared_memory_full")&&cfg_bool(cfg,"shared_memory_full");
  const uint32_t indexCount=cfg_u32(cfg,"index_count"), indexStride=cfg_u32(cfg,"index_stride");
  if(!indexCount||indexCount>1000000||(indexStride!=2&&indexStride!=4))
    throw std::runtime_error("Invalid indexed draw contract");
  auto indices=load("indices.raw");
  auto fetch=fullShared?read(fixture/"shared-memory.raw",512ull*1024*1024):load("vertex-fetch95.raw");
  auto fetchConstants=load("vertex-fetch-constants.bin");
  auto vsSystem=load("vertex-constant-0.bin"),vsFloat=load("vertex-constant-1.bin");
  auto vsBool=load("vertex-constant-2.bin"),vsFetch=fetchConstants;
  auto psSystem=load("pixel-constant-0.bin"),psFloat=load("pixel-constant-1.bin");
  auto psBool=load("pixel-constant-2.bin"),psFetch=load("pixel-constant-3.bin");
  if(indices.size()!=size_t(indexCount)*indexStride||fetch.empty()||fetchConstants.size()!=768||
     vsSystem.size()!=512||vsFloat.empty()||vsBool.size()!=160||psSystem.size()!=512||
     psFloat.empty()||psBool.size()!=160||psFetch.size()!=768||
     vsSystem!=psSystem||vsBool!=psBool||fetchConstants!=psFetch)
    throw std::runtime_error("Captured draw buffer ranges are incomplete or inconsistent");
   if(!fullShared){uint32_t rebased=3;std::memcpy(vsFetch.data()+47*16+2*4,&rebased,4);}
  Buffer shared=c.buffer(fetch.size(),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);c.upload(shared,fetch.data(),fetch.size());
  Buffer index=c.buffer(indices.size(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);c.upload(index,indices.data(),indices.size());
  Buffer uboSys=c.buffer(vsSystem.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);c.upload(uboSys,vsSystem.data(),vsSystem.size());
  Buffer uboVFloat=c.buffer(vsFloat.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);c.upload(uboVFloat,vsFloat.data(),vsFloat.size());
  Buffer uboPFloat=c.buffer(psFloat.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);c.upload(uboPFloat,psFloat.data(),psFloat.size());
  Buffer uboBool=c.buffer(vsBool.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);c.upload(uboBool,vsBool.data(),vsBool.size());
  Buffer uboFetch=c.buffer(vsFetch.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);c.upload(uboFetch,vsFetch.data(),vsFetch.size());

  auto component=[](int v)->VkComponentSwizzle {
    switch(v){case 0:return VK_COMPONENT_SWIZZLE_R;case 1:return VK_COMPONENT_SWIZZLE_G;
      case 2:return VK_COMPONENT_SWIZZLE_B;case 3:return VK_COMPONENT_SWIZZLE_A;
      case 4:return VK_COMPONENT_SWIZZLE_ZERO;case 5:return VK_COMPONENT_SWIZZLE_ONE;
      default:throw std::runtime_error("Invalid captured component swizzle");}
  };
  auto format=[](const std::string& name)->VkFormat {
    if(name=="R8G8B8A8_UNORM")return VK_FORMAT_R8G8B8A8_UNORM;
    if(name=="R8G8_UNORM")return VK_FORMAT_R8G8_UNORM;
    if(name=="R16G16B16A16_FLOAT")return VK_FORMAT_R16G16B16A16_SFLOAT;
    if(name=="BC1_UNORM")return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    if(name=="BC5_UNORM")return VK_FORMAT_BC5_UNORM_BLOCK;
    throw std::runtime_error("Unsupported captured texture format: "+name);
  };
  auto textureSet=[&](const std::string& stage,uint32_t maxSlots){
    std::vector<SampledImage> result(5);
    for(uint32_t slot=0;slot<maxSlots;slot++){
      std::string base=stage+"_textures."+std::to_string(slot)+".";
      uint32_t tw=cfg_u32(cfg,base+"width"),th=cfg_u32(cfg,base+"height"),n=cfg_u32(cfg,base+"mips");
      std::istringstream stream(cfg_string(cfg,base+"swizzle"));std::array<int,4> s{};char comma;
      if(!(stream>>s[0]>>comma>>s[1]>>comma>>s[2]>>comma>>s[3]))throw std::runtime_error("Bad texture swizzle");
      VkComponentMapping mapping{component(s[0]),component(s[1]),component(s[2]),component(s[3])};
      std::vector<std::vector<uint8_t>> levels;
      for(uint32_t mip=0;mip<n;mip++){
        std::string fn=(stage=="vertex"?"vs-image-":"ps-image-")+std::to_string(slot)+"-mip-"+std::to_string(mip)+".bin";
        levels.push_back(load(fn.c_str()));
      }
      result[slot]=make_sampled(c,tw,th,n,format(cfg_string(cfg,base+"format")),levels,mapping);
    }
    return result;
  };
  uint32_t nvt=cfg_u32(cfg,"vertex_textures.count"),npt=cfg_u32(cfg,"pixel_textures.count");
  uint32_t nps=cfg_u32(cfg,"pixel_samplers.count");
  if(nvt>2||npt>5||nps!=npt)throw std::runtime_error("Captured image/sampler layout unsupported");
  auto vImages=textureSet("vertex",nvt),pImages=textureSet("pixel",npt);
  std::vector<uint8_t> dummyBytes(4,0);
  auto dummy=make_sampled(c,1,1,1,VK_FORMAT_R8G8B8A8_UNORM,{dummyBytes},
       VkComponentMapping{VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY});
  for(auto& t:vImages)if(t.image==VK_NULL_HANDLE)t=dummy;
  for(auto& t:pImages)if(t.image==VK_NULL_HANDLE)t=dummy;
  const auto samplerFor=[&](const std::string& stage,uint32_t slot){
    std::string p=stage+"_samplers."+std::to_string(slot)+".";
    auto filter=[&](const char* key){int v=cfg_int(cfg,p+key);return v==2||v==4?VK_FILTER_LINEAR:VK_FILTER_NEAREST;};
    int mip=cfg_int(cfg,p+"mip");VkSamplerMipmapMode mm=(mip==2||mip==4)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
    auto address=[&](const char* key){switch(cfg_int(cfg,p+key)){
      case 0:return VK_SAMPLER_ADDRESS_MODE_REPEAT;case 1:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      case 2:return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;case 3:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      case 4:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;default:throw std::runtime_error("Bad sampler address mode");}};
    int min=cfg_int(cfg,p+"minify"),mag=cfg_int(cfg,p+"magnify");
    float aniso=std::stof(cfg_string(cfg,p+"max_anisotropy"));if(min==4||mag==4)aniso=std::max(4.0f,aniso);
    return make_sampler(c,filter("minify"),filter("magnify"),mm,address("address_u"),
        address("address_v"),address("address_w"),cfg_float(cfg,p+"min_lod"),cfg_float(cfg,p+"max_lod"),aniso);
  };
  std::array<VkSampler,2> vSamplers={samplerFor("vertex",0),samplerFor("vertex",1)};
  std::array<VkSampler,5> pSamplers{};for(uint32_t i=0;i<5;i++)pSamplers[i]=i<nps?samplerFor("pixel",i):pSamplers[0];

  std::vector<VkDescriptorSetLayoutBinding> l0{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
  std::vector<VkDescriptorSetLayoutBinding> l1;for(uint32_t i=0;i<5;i++)l1.push_back({i,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
  std::vector<VkDescriptorSetLayoutBinding> l2,l3;
  for(uint32_t i=0;i<4;i++)l2.push_back({i,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr});
  for(uint32_t i=4;i<6;i++)l2.push_back({i,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_VERTEX_BIT,nullptr});
  for(uint32_t i=0;i<npt*2;i++)l3.push_back({i,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
  for(uint32_t i=0;i<nps;i++)l3.push_back({npt*2+i,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});
  std::array<VkDescriptorSetLayout,4> layouts={make_set_layout(c,l0),make_set_layout(c,l1),make_set_layout(c,l2),make_set_layout(c,l3)};
  VkDescriptorPoolSize poolSizes[4]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,5},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,14},{VK_DESCRIPTOR_TYPE_SAMPLER,7}};
  VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pci.maxSets=4;pci.poolSizeCount=4;pci.pPoolSizes=poolSizes;
  VkDescriptorPool pool;check(vkCreateDescriptorPool(c.device,&pci,nullptr,&pool));c.retire.push_back([d=c.device,pool]{vkDestroyDescriptorPool(d,pool,nullptr);});
  VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sai.descriptorPool=pool;sai.descriptorSetCount=4;sai.pSetLayouts=layouts.data();
  PreparedDraw draw{};check(vkAllocateDescriptorSets(c.device,&sai,draw.sets.data()));
  std::vector<VkDescriptorBufferInfo> bi;bi.reserve(6);std::vector<VkDescriptorImageInfo> ii;ii.reserve(40);std::vector<VkWriteDescriptorSet> writes;writes.reserve(40);
  auto writeBuffer=[&](uint32_t set,uint32_t binding,Buffer b,VkDeviceSize sz,VkDescriptorType type){bi.push_back({b.buffer,0,sz});VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=draw.sets[set];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=type;w.pBufferInfo=&bi.back();writes.push_back(w);};
  writeBuffer(0,0,shared,fetch.size(),VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writeBuffer(1,0,uboSys,vsSystem.size(),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);writeBuffer(1,1,uboVFloat,vsFloat.size(),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  writeBuffer(1,2,uboPFloat,psFloat.size(),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);writeBuffer(1,3,uboBool,vsBool.size(),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  writeBuffer(1,4,uboFetch,vsFetch.size(),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  auto writeImage=[&](uint32_t set,uint32_t binding,SampledImage t){ii.push_back({VK_NULL_HANDLE,t.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=draw.sets[set];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w.pImageInfo=&ii.back();writes.push_back(w);};
  auto writeSampler=[&](uint32_t set,uint32_t binding,VkSampler s){ii.push_back({s,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED});VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=draw.sets[set];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;w.pImageInfo=&ii.back();writes.push_back(w);};
  writeImage(2,0,vImages[0]);writeImage(2,1,dummy);writeImage(2,2,dummy);writeImage(2,3,vImages[1]);
  writeSampler(2,4,vSamplers[0]);writeSampler(2,5,vSamplers[1]);
  for(uint32_t binding=0;binding<npt*2;binding++)writeImage(3,binding,dummy);
  for(uint32_t slot=0;slot<npt;slot++){
    const std::string bindingKey="pixel_textures."+std::to_string(slot)+".binding";
    const uint32_t imageBinding=cfg.count(bindingKey)?cfg_u32(cfg,bindingKey):slot*2;
    if(imageBinding>=npt*2)throw std::runtime_error("Captured pixel image binding out of range");
    writeImage(3,imageBinding,pImages[slot]);
    writeSampler(3,2*npt+slot,pSamplers[slot]);
  }
  vkUpdateDescriptorSets(c.device,static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pli.setLayoutCount=4;pli.pSetLayouts=layouts.data();
  check(vkCreatePipelineLayout(c.device,&pli,nullptr,&draw.layout));c.retire.push_back([d=c.device,h=draw.layout]{vkDestroyPipelineLayout(d,h,nullptr);});
  VkPipelineShaderStageCreateInfo stages[2]{};stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=c.shader(fixture/"vertex.spv");stages[0].pName="main";
  stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=c.shader(fixture/"pixel.spv");stages[1].pName="main";
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  int topology=cfg_int(cfg,"topology");ia.topology=topology==6?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:(topology==5?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:VK_PRIMITIVE_TOPOLOGY_MAX_ENUM);
  if(ia.topology==VK_PRIMITIVE_TOPOLOGY_MAX_ENUM)throw std::runtime_error("Unsupported topology");
  ia.primitiveRestartEnable=cfg_bool(cfg,"primitive_restart");
  VkViewport viewport{cfg_float(cfg,"viewport.x"),cfg_float(cfg,"viewport.y"),cfg_float(cfg,"viewport.width"),cfg_float(cfg,"viewport.height"),cfg_float(cfg,"viewport.min_depth"),cfg_float(cfg,"viewport.max_depth")};
  VkRect2D scissor{{cfg_int(cfg,"scissor.x"),cfg_int(cfg,"scissor.y")},{cfg_u32(cfg,"scissor.width"),cfg_u32(cfg,"scissor.height")}};
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode=VK_POLYGON_MODE_FILL;rs.rasterizerDiscardEnable=VK_FALSE;rs.depthClampEnable=VK_FALSE;
  int cull=cfg_int(cfg,"rasterizer.cull_mode");rs.cullMode=cull==0?VK_CULL_MODE_NONE:(cull==1?VK_CULL_MODE_FRONT_BIT:(cull==2?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_FRONT_AND_BACK));
  rs.frontFace=cfg_bool(cfg,"rasterizer.front_ccw")?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;
  rs.depthBiasEnable=cfg_bool(cfg,"rasterizer.depth_bias_enable");rs.depthBiasConstantFactor=cfg_float(cfg,"rasterizer.depth_bias");
  rs.depthBiasSlopeFactor=cfg_float(cfg,"rasterizer.slope_scaled_depth_bias");rs.depthBiasClamp=cfg_float(cfg,"rasterizer.depth_bias_clamp");rs.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable=cfg_bool(cfg,"depth_stencil.depth_test_enable");ds.depthWriteEnable=cfg_bool(cfg,"depth_stencil.depth_write_enable");
  ds.depthCompareOp=compare_op(cfg_int(cfg,"depth_stencil.depth_function"));ds.stencilTestEnable=cfg_bool(cfg,"depth_stencil.stencil_test_enable");
  auto makeFace=[&](const char* side){std::string p=std::string("depth_stencil.")+side+"_";VkStencilOpState s{};
    s.failOp=stencil_op(cfg_int(cfg,p+"fail_operation"));s.depthFailOp=stencil_op(cfg_int(cfg,p+"depth_fail_operation"));
    s.passOp=stencil_op(cfg_int(cfg,p+"pass_operation"));s.compareOp=compare_op(cfg_int(cfg,p+"function"));
    s.compareMask=cfg_u32(cfg,p+"compare_mask");s.writeMask=cfg_u32(cfg,p+"write_mask");s.reference=cfg_u32(cfg,p+"reference");return s;};
  ds.front=makeFace("front");ds.back=makeFace("back");
   VkPipelineColorBlendAttachmentState blend{};blend.blendEnable=cfg_bool(cfg,"color_blends.0.enabled");blend.colorWriteMask=cfg_u32(cfg,"color_blends.0.write_mask");
   auto blendInt=[&](const std::string& key,int fallback){return cfg.count(key)?cfg_int(cfg,key):fallback;};
   blend.srcColorBlendFactor=blend_factor(blendInt("color_blends.0.color.source",1));
   blend.dstColorBlendFactor=blend_factor(blendInt("color_blends.0.color.destination",0));
   blend.colorBlendOp=blend_op(blendInt("color_blends.0.color.operation",0));
   blend.srcAlphaBlendFactor=blend_factor(blendInt("color_blends.0.alpha.source",1));
   blend.dstAlphaBlendFactor=blend_factor(blendInt("color_blends.0.alpha.destination",0));
   blend.alphaBlendOp=blend_op(blendInt("color_blends.0.alpha.operation",0));
  VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&blend;
  VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gpi.stageCount=2;gpi.pStages=stages;
  gpi.pVertexInputState=&vi;gpi.pInputAssemblyState=&ia;gpi.pViewportState=&vp;gpi.pRasterizationState=&rs;
  gpi.pMultisampleState=&ms;gpi.pDepthStencilState=&ds;gpi.pColorBlendState=&bs;gpi.layout=draw.layout;gpi.renderPass=pass;
  check(vkCreateGraphicsPipelines(c.device,VK_NULL_HANDLE,1,&gpi,nullptr,&draw.pipeline));
  c.retire.push_back([d=c.device,h=draw.pipeline]{vkDestroyPipeline(d,h,nullptr);});
  draw.indices=index;draw.indexCount=indexCount;draw.instances=cfg_u32(cfg,"instance_count");
  draw.indexStride=indexStride;draw.baseVertex=cfg_int(cfg,"base_vertex");return draw;
}

int main(int argc,char** argv) {
  try {
    const bool sequence=argc==5 && std::string(argv[1])=="--sequence";
    const bool floatDebug=argc==4 && std::string(argv[3])=="--float-debug";
    if(!sequence && argc!=3 && !floatDebug)
      throw std::runtime_error("Usage: native_event362 FIXTURE NEW_OUTPUT [--float-debug] | --sequence FIXTURE_A FIXTURE_B NEW_OUTPUT");
    if(sequence && floatDebug)throw std::runtime_error("Float-debug is single-draw only");
    const fs::path fixtureA=sequence?fs::path(argv[2]):fs::path(argv[1]);
    const fs::path fixtureB=sequence?fs::path(argv[3]):fs::path();
    const fs::path output=sequence?fs::path(argv[4]):fs::path(argv[2]);
    if(fs::exists(output))throw std::runtime_error("Output directory must be new");
    fs::create_directories(output);
    DrawConfig cfgA=read_config(fixtureA);
    uint32_t width=cfg_u32(cfgA,"render_width"),height=cfg_u32(cfgA,"render_height");
    if(!width||!height||width>4096||height>4096)throw std::runtime_error("Unsupported render extent");
    const bool capturedFloat16Color=!floatDebug&&cfgA.count("color_float16")&&cfg_bool(cfgA,"color_float16");
    if(sequence){
      DrawConfig cfgB=read_config(fixtureB);
      if(cfg_u32(cfgB,"render_width")!=width||cfg_u32(cfgB,"render_height")!=height)
        throw std::runtime_error("Sequence target extents differ");
      auto afterA=read(fixtureA/"color-after.rgba"),beforeB=read(fixtureB/"color-before.rgba");
      auto depthAfterA=read(fixtureA/"depth-stencil-after.d32s8"),depthBeforeB=read(fixtureB/"depth-stencil-before.d32s8");
      if(afterA!=beforeB||depthAfterA!=depthBeforeB)
        throw std::runtime_error("Captured color/depth attachment handoff is not byte-identical");
    }

    auto initialDepth=read(fixtureA/"depth-stencil-before.d32s8");
    auto initialColor=floatDebug?std::vector<uint8_t>{}:
        read(fixtureA/(capturedFloat16Color?"color-before.rgba16f":"color-before.rgba"));
    const size_t pixelCount=size_t(width)*height;
    const size_t colorBytesPerPixel=capturedFloat16Color?8u:4u;
    if(initialDepth.size()!=pixelCount*8||
       (!floatDebug&&initialColor.size()!=pixelCount*colorBytesPerPixel))
      throw std::runtime_error("Initial attachment crop size mismatch");

    Context c;c.init();auto d=c.device;
    const VkFormat colorFormat=floatDebug?VK_FORMAT_R32G32B32A32_SFLOAT:
        (capturedFloat16Color?VK_FORMAT_R16G16B16A16_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM);
    VkFormatProperties colorProperties{};vkGetPhysicalDeviceFormatProperties(c.physical,colorFormat,&colorProperties);
    if(!(colorProperties.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)||
       !(colorProperties.optimalTilingFeatures&VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
      throw std::runtime_error("Requested color target format unsupported");
    Image color=c.image(width,height,colorFormat);
    VkImage depthImage=VK_NULL_HANDLE;VkDeviceMemory depthMemory=VK_NULL_HANDLE;VkImageView depthView=VK_NULL_HANDLE;
    VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};di.imageType=VK_IMAGE_TYPE_2D;
    di.format=VK_FORMAT_D32_SFLOAT_S8_UINT;di.extent={width,height,1};di.mipLevels=1;di.arrayLayers=1;
    di.samples=VK_SAMPLE_COUNT_1_BIT;di.tiling=VK_IMAGE_TILING_OPTIMAL;
    di.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    check(vkCreateImage(d,&di,nullptr,&depthImage));
    VkMemoryRequirements dreq{};vkGetImageMemoryRequirements(d,depthImage,&dreq);
    VkMemoryAllocateInfo dai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};dai.allocationSize=dreq.size;
    dai.memoryTypeIndex=c.memory_type(dreq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(d,&dai,nullptr,&depthMemory));check(vkBindImageMemory(d,depthImage,depthMemory,0));
    c.retire.push_back([d,depthImage]{vkDestroyImage(d,depthImage,nullptr);});
    c.retire.push_back([d,depthMemory]{vkFreeMemory(d,depthMemory,nullptr);});
    VkImageViewCreateInfo dvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};dvi.image=depthImage;
    dvi.viewType=VK_IMAGE_VIEW_TYPE_2D;dvi.format=VK_FORMAT_D32_SFLOAT_S8_UINT;
    dvi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};
    check(vkCreateImageView(d,&dvi,nullptr,&depthView));c.retire.push_back([d,depthView]{vkDestroyImageView(d,depthView,nullptr);});

    std::vector<uint8_t> depthPlane(pixelCount*4),stencilPlane(pixelCount);
    for(size_t i=0;i<pixelCount;i++){
      std::memcpy(depthPlane.data()+i*4,initialDepth.data()+i*8,4);
      stencilPlane[i]=initialDepth[i*8+4];
    }
    Buffer depthStage=c.buffer(depthPlane.size()+stencilPlane.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    void* mapped=nullptr;check(vkMapMemory(d,depthStage.memory,0,depthPlane.size()+stencilPlane.size(),0,&mapped));
    std::memcpy(mapped,depthPlane.data(),depthPlane.size());
    std::memcpy(static_cast<uint8_t*>(mapped)+depthPlane.size(),stencilPlane.data(),stencilPlane.size());
    vkUnmapMemory(d,depthStage.memory);
    Buffer colorStage{};
    if(!floatDebug){colorStage=c.buffer(initialColor.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);c.upload(colorStage,initialColor.data(),initialColor.size());}

    VkAttachmentDescription attachments[2]{};
    attachments[0].format=colorFormat;attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp=floatDebug?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout=attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format=VK_FORMAT_D32_SFLOAT_S8_UINT;attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout=attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1;sub.pColorAttachments=&colorRef;sub.pDepthStencilAttachment=&depthRef;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpi.attachmentCount=2;rpi.pAttachments=attachments;
    rpi.subpassCount=1;rpi.pSubpasses=&sub;VkRenderPass pass;check(vkCreateRenderPass(d,&rpi,nullptr,&pass));
    c.retire.push_back([d,pass]{vkDestroyRenderPass(d,pass,nullptr);});
    VkImageView views[2]={color.view,depthView};VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass=pass;fbi.attachmentCount=2;fbi.pAttachments=views;fbi.width=width;fbi.height=height;fbi.layers=1;
    VkFramebuffer fb;check(vkCreateFramebuffer(d,&fbi,nullptr,&fb));c.retire.push_back([d,fb]{vkDestroyFramebuffer(d,fb,nullptr);});

    PreparedDraw first=prepare_draw(c,fixtureA,pass);
    PreparedDraw second{};
    if(sequence)second=prepare_draw(c,fixtureB,pass);
    const size_t outputBytes=pixelCount*(floatDebug?16u:colorBytesPerPixel);
    Buffer readback=c.buffer(outputBytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    c.begin();
    if(!floatDebug){
      VkImageMemoryBarrier toCopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};toCopy.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
      toCopy.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;toCopy.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      toCopy.srcQueueFamilyIndex=toCopy.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;toCopy.image=color.image;
      toCopy.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&toCopy);
      VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
      vkCmdCopyBufferToImage(c.command,colorStage.buffer,color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
      VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};toColor.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      toColor.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;toColor.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
      toColor.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      toColor.srcQueueFamilyIndex=toColor.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;toColor.image=color.image;
      toColor.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&toColor);
    }else{
      VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};toColor.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
      toColor.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;toColor.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      toColor.srcQueueFamilyIndex=toColor.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;toColor.image=color.image;
      toColor.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&toColor);
    }
    VkImageMemoryBarrier dcopy{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};dcopy.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    dcopy.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;dcopy.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    dcopy.srcQueueFamilyIndex=dcopy.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;dcopy.image=depthImage;
    dcopy.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};
    vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&dcopy);
    VkBufferImageCopy depthCopy{};depthCopy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};depthCopy.imageExtent={width,height,1};
    vkCmdCopyBufferToImage(c.command,depthStage.buffer,depthImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&depthCopy);
    VkBufferImageCopy stencilCopy{};stencilCopy.bufferOffset=depthPlane.size();
    stencilCopy.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,0,0,1};stencilCopy.imageExtent={width,height,1};
    vkCmdCopyBufferToImage(c.command,depthStage.buffer,depthImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&stencilCopy);
    VkImageMemoryBarrier dattach{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};dattach.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dattach.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;dattach.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    dattach.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dattach.srcQueueFamilyIndex=dattach.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;dattach.image=depthImage;
    dattach.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,1};
    vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,0,0,nullptr,0,nullptr,1,&dattach);

    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=fb;
    begin.renderArea={{0,0},{width,height}};VkClearValue clear{};clear.color={{0,0,0,0}};
    if(floatDebug){begin.clearValueCount=1;begin.pClearValues=&clear;}
    vkCmdBeginRenderPass(c.command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    auto issue=[&](const PreparedDraw& draw){vkCmdBindPipeline(c.command,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.pipeline);
      vkCmdBindDescriptorSets(c.command,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.layout,0,4,draw.sets.data(),0,nullptr);
      vkCmdBindIndexBuffer(c.command,draw.indices.buffer,0,draw.indexStride==2?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(c.command,draw.indexCount,draw.instances,0,draw.baseVertex,0);};
    issue(first);if(sequence)issue(second);
    vkCmdEndRenderPass(c.command);
    c.barrier(color,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy readCopy{};readCopy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};readCopy.imageExtent={width,height,1};
    vkCmdCopyImageToBuffer(c.command,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&readCopy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.buffer=readback.buffer;host.size=VK_WHOLE_SIZE;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    c.submit();void* outData=nullptr;check(vkMapMemory(d,readback.memory,0,outputBytes,0,&outData));
    const char* outputName=floatDebug?"native.shader.rgba32f":
        (capturedFloat16Color?"native.rgba16f":"native.rgba");
    std::ofstream file(output/outputName,std::ios::binary);
    file.write(static_cast<char*>(outData),outputBytes);bool ok=bool(file);vkUnmapMemory(d,readback.memory);
    if(!ok)throw std::runtime_error("native output write failed");
    std::cout<<"native_draws="<<(sequence?2:1)<<" event="<<cfg_u32(cfgA,"event")
             <<(sequence?"+"+std::to_string(cfg_u32(read_config(fixtureB),"event")):"")
             <<" completed="<<width<<"x"<<height<<"\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
  return 0;
}
