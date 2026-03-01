/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput {
	@location(0) position: vec2f,
	@location(1) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput {
	@builtin(position) position: vec4f,
	// The location here does not refer to a vertex attribute, it just means
	// that this field must be handled by the rasterizer.
	// (It can also refer to another field of another struct that would be used
	// as input to the fragment shader.)
	@location(0) color: vec3f,
};

/**
 * A structure holding the value of our uniforms
 */
struct MyUniforms {
    color: vec4f,
		time: f32,
};

@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms; // A uniform struct variable that we can set from the CPU

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
	//                         ^^^^^^^^^^^^ We return a custom struct
	var out: VertexOutput; // create the output struct
	let ratio = 640.0 / 480.0; // The width and height of the target surface

	// Simple update based on time and sine/cosine functions to create a nice animation
	var offset = vec2f(-0.6875, -0.463); // The offset that we want to apply to the position

	let uTime = uMyUniforms.time; // Get the time value from the uniform
	offset += vec2f(cos(uTime), sin(uTime)); // Update the offset based on time

	out.position = vec4f(in.position.x + offset.x, (in.position.y + offset.y) * ratio, 0.0, 1.0);
	out.color = in.color; // forward the color attribute to the fragment shader
	return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
	// We multiply the scene's color with our global uniform (this is one
  // possible use of the color uniform, among many others).
  let color = in.color * uMyUniforms.color.rgb;

	// Gamma-correction
	let linear_color = pow(color, vec3f(2.2));
	return vec4f(linear_color, 1.0); // use the interpolated color coming from the vertex shader
}
