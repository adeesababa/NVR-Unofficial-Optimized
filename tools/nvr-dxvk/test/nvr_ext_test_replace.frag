#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler s_samplers[];
layout(set = 1, binding = 0) uniform texture2D t0;
layout(set = 2, binding = 6, std140) uniform Consts { vec4 c[1]; };
layout(set = 4, binding = 0, std140) uniform Extra { vec4 scale; };
layout(set = 4, binding = 1) uniform texture2D extraTex;

layout(push_constant) uniform Push {
  layout(offset = 32) uint d3d9Samplers[8];
  layout(offset = 64) uint extraSamplers[4];
};

layout(location = 1) in vec4 texcoord0;
layout(location = 0) out vec4 color;

void main() {
  uint s0 = d3d9Samplers[0] & 0xFFFFu;
  uint e0 = extraSamplers[0] & 0xFFFFu;
  vec4 tex = texture(sampler2D(t0, s_samplers[nonuniformEXT(s0)]), texcoord0.xy);
  vec4 ex  = texture(sampler2D(extraTex, s_samplers[nonuniformEXT(e0)]), texcoord0.xy);
  color = tex * c[0] * scale + ex;
}
