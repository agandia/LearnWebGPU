/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput {
	@location(0) position: vec3f,
	@location(1) normal: vec3f,
	@location(2) color: vec3f,
	@location(3) uv: vec2f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput {
	@builtin(position) position: vec4f,
	@location(0) color: vec3f,
	@location(1) normal: vec3f,
	@location(2) uv: vec2f,
};

/**
 * A structure holding the value of our uniforms
 */
struct BasicShaderUniforms {
    projectionMatrix: mat4x4f,
    viewMatrix: mat4x4f,
    modelMatrix: mat4x4f,
    color: vec4f,
    time: f32,
};

/**
 * A structure holding the lighting settings
 */
struct LightingUniforms {
    directions: array<vec4f, 2>,
    colors: array<vec4f, 2>,
}

@group(0) @binding(0) var<uniform> uUniforms: BasicShaderUniforms; // A uniform struct variable that we can set from the CPU
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var textureSampler: sampler;
@group(0) @binding(3) var<uniform> uLighting: LightingUniforms;

const pi = 3.14159265359;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	var out: VertexOutput;
	out.position = uUniforms.projectionMatrix * uUniforms.viewMatrix * uUniforms.modelMatrix * vec4f(in.position, 1.0);
	// Forward the normal
  out.normal = (uUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
	out.color = in.color;
	out.uv = in.uv; // Map from [-1, 1] to [0, 1]

	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	// Compute shading
    let normal = normalize(in.normal);
    var shading = vec3f(0.0);
    for (var i: i32 = 0; i < 2; i++) {
        let direction = normalize(uLighting.directions[i].xyz);
        let color = uLighting.colors[i].rgb;
        shading += max(0.0, dot(direction, normal)) * color;
    }
    
    // Sample texture
    let baseColor = textureSample(baseColorTexture, textureSampler, in.uv).rgb;

    // Combine texture and lighting
    let color = baseColor * shading;

    // Gamma-correction
    let corrected_color = pow(color, vec3f(2.2));
    return vec4f(corrected_color, uUniforms.color.a);
}
