varying vec3 vertex_light_position;
varying vec3 vertex_normal;

void main(void) {
	gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;
	gl_TexCoord[0] = gl_MultiTexCoord0;

	vertex_normal = normalize(gl_NormalMatrix * gl_Normal);
	vertex_light_position = normalize(gl_LightSource[0].position.xyz);

	vec3 u = normalize(vec3(gl_ModelViewMatrix * gl_Vertex));
	vec3 n = normalize(gl_NormalMatrix * gl_Normal);
	vec3 r = reflect(u,n);
	float m = 2.0 * sqrt((r.x)*(r.x) + (r.y)*(r.y) + (r.z+1.0)*(r.z+1.));
	gl_TexCoord[1].s = r.x/m + 0.5;
	gl_TexCoord[1].t = r.y/m + 0.5;
}
