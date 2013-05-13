uniform sampler2D texture;
uniform sampler2D spheremap;
varying vec3 vertex_light_position;
varying vec3 vertex_normal;

void main() {
	vec4 color = texture2D(texture, gl_TexCoord[0].st);
	vec4 env   = texture2D(spheremap, gl_TexCoord[1].st);
	float diffuse_value = max(dot(vertex_normal, vertex_light_position), 0.2);
	gl_FragColor = color * diffuse_value + env * 0.5;
	gl_FragColor.a = color.a;
}
