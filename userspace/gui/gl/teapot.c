/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2013-2014 Kevin Lange
 *
 * GL teapot with shaders.
 */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/osmesa.h>
#include <GL/glext.h>

#include "lib/yutani.h"
#include "lib/graphics.h"
#include "lib/pthread.h"

#define PI  3.141592654
#define TAO 6.28318531

/* Despite including the correct header, these are not defined */
GLuint glCreateShader(GLenum shaderType);
void glCompileShader(GLuint shader);
void glAttachShader(GLuint  program,  GLuint  shader);
void glLinkProgram(GLuint program);
void glUseProgram(GLuint program);
GLuint glCreateProgram(void);
GLint glGetUniformLocation(GLuint  program,  const GLchar * name);
void glUniform1i(GLint location, GLint v0);
void glShaderSource(GLuint  shader,  GLsizei  count,  GLchar ** string,  GLint * length);

GLuint texture_a; /* Diffuse texture */
GLuint texture_b; /* Environment spheremap */

int quit = 0;

/* Scene scale */
float scale = 1.0;
/* Object rotation */
float rot = 0.0;
/* Camera height */
float height = 1.0;
/* Where to point the camera */
float cam_offset = 1.0;

char rotation_paused = 0;
int win_width;
int win_height;
float x_light;
float y_light;

/* Normal vector definition */
typedef struct {
	float x;
	float y;
	float z;
} normal_t ;

/* Vertex object */
typedef struct {
	float x; /* Coordinates */
	float y;
	float z;
	float u; /* Texture coordinates */
	float v;
	normal_t normal;
} vertex_t ;

/* Resizable array of vertices */
typedef struct {
	uint32_t len;
	uint32_t capacity;
	vertex_t ** nodes;
} vertices_t;

/* Face definition (3 vertices and a normal vector) */
typedef struct {
	vertex_t * a;
	vertex_t * b;
	vertex_t * c;
	normal_t normal;
} face_t;

/* Resizable array of faces */
typedef struct {
	uint32_t len;
	uint32_t capacity;
	face_t ** nodes;
} faces_t;

/* Model vertices */
vertices_t vertices = {.len = 0, .capacity = 0};
/* Model triangles */
faces_t    faces    = {.len = 0, .capacity = 0};

/* Initialize the model objects */
void init_model() {
	/* We give an initial capacity of 16 for each */
	vertices.capacity = 16;
	vertices.nodes    = (vertex_t **)malloc(sizeof(vertex_t *) * vertices.capacity);
	faces.capacity    = 16;
	faces.nodes       = (face_t **)malloc(sizeof(face_t *) * faces.capacity);
}

/* Add the given coordinates to the vertex list */
void add_vertex(float x, float y, float z) {
	if (vertices.len == vertices.capacity) {
		/* When we run out of space, increase by two */
		vertices.capacity *= 2;
		vertices.nodes = (vertex_t **)realloc(vertices.nodes, sizeof(vertex_t *) * vertices.capacity);
	}
	/* Create a new vertex in the list */
	vertices.nodes[vertices.len] = malloc(sizeof(vertex_t));
	vertices.nodes[vertices.len]->x = x * scale;
	vertices.nodes[vertices.len]->y = y * scale;
	vertices.nodes[vertices.len]->z = z * scale;
	/* Set texture coordinates by cylindrical mapping */
	float theta = atan2(z,x);
	vertices.nodes[vertices.len]->u = (theta + PI) / (TAO);
	vertices.nodes[vertices.len]->v = (y / 2.0);
	/* Initialize normals to 0,0,0 */
	vertices.nodes[vertices.len]->normal.x = 0.0f;
	vertices.nodes[vertices.len]->normal.y = 0.0f;
	vertices.nodes[vertices.len]->normal.z = 0.0f;
	vertices.len++;
}

/* Add a face with the given vertices */
void add_face(int a, int b, int c) {
	if (faces.len == faces.capacity) {
		/* Double size when we run out... */
		faces.capacity *= 2;
		faces.nodes = (face_t **)realloc(faces.nodes, sizeof(face_t *) * faces.capacity);
	}
	if (vertices.len < a || vertices.len < b || vertices.len < c) {
		/* Frick... */
		fprintf(stderr, "ERROR: Haven't yet collected enough vertices for the face %d %d %d (have %d)!\n", a, b, c, vertices.len);
		exit(1);
	}
	/* Create a new triangle */
	faces.nodes[faces.len] = malloc(sizeof(face_t));
	faces.nodes[faces.len]->a = vertices.nodes[a-1];
	faces.nodes[faces.len]->b = vertices.nodes[b-1];
	faces.nodes[faces.len]->c = vertices.nodes[c-1];
	/* Calculate some normals */
	vertex_t u = {.x = faces.nodes[faces.len]->b->x - faces.nodes[faces.len]->a->x,
				  .y = faces.nodes[faces.len]->b->y - faces.nodes[faces.len]->a->y,
				  .z = faces.nodes[faces.len]->b->z - faces.nodes[faces.len]->a->z};
	vertex_t v = {.x = faces.nodes[faces.len]->c->x - faces.nodes[faces.len]->a->x,
				  .y = faces.nodes[faces.len]->c->y - faces.nodes[faces.len]->a->y,
				  .z = faces.nodes[faces.len]->c->z - faces.nodes[faces.len]->a->z};
	/* Set the face normals */
	faces.nodes[faces.len]->normal.x = ((u.y * v.z) - (u.z * v.y));
	faces.nodes[faces.len]->normal.y = -((u.z * v.x) - (u.x * v.z));
	faces.nodes[faces.len]->normal.z = ((u.x * v.y) - (u.y * v.x));
	faces.len++;
}

void finish_normals() {
	/* Loop through vertices and accumulate normals for them */
	for (uint32_t i = 0; i < faces.len; ++i) {
		/* Vertex a */
		faces.nodes[i]->a->normal.x += faces.nodes[i]->normal.x;
		faces.nodes[i]->a->normal.y += faces.nodes[i]->normal.y;
		faces.nodes[i]->a->normal.z += faces.nodes[i]->normal.z;
		/* Vertex b */
		faces.nodes[i]->b->normal.x += faces.nodes[i]->normal.x;
		faces.nodes[i]->b->normal.y += faces.nodes[i]->normal.y;
		faces.nodes[i]->b->normal.z += faces.nodes[i]->normal.z;
		/* Vertex c */
		faces.nodes[i]->c->normal.x += faces.nodes[i]->normal.x;
		faces.nodes[i]->c->normal.y += faces.nodes[i]->normal.y;
		faces.nodes[i]->c->normal.z += faces.nodes[i]->normal.z;
	}
}

/* Discard the rest of this line */
void toss(FILE * f) {
	while (fgetc(f) != '\n');
}

/* Load a Wavefront Obj model */
void load_wavefront(char * filename) {
	/* Open the file */
	FILE * obj = fopen(filename, "r");
	int collected = 0;
	char d = ' ';
	/* Initialize the lists */
	init_model();
	while (!feof(obj)) {
		/* Scan in a line */
		collected = fscanf(obj, "%c ", &d);
		if (collected == 0) continue;
		switch (d) {
			case 'v':
				{
					/* Vertex */
					float x, y, z;
					collected = fscanf(obj, "%f %f %f\n", &x, &y, &z);
					if (collected < 3) fprintf(stderr, "ERROR: Only collected %d points!\n", collected);
					add_vertex(x, y, z);
				}
				break;
			case 'f':
				{
					/* Face */
					int a, b, c;
					collected = fscanf(obj, "%d %d %d\n", &a, &b, &c);
					if (collected < 3) fprintf(stderr, "ERROR: Only collected %d vertices!\n", collected);
					add_face(a,b,c);
				}
				break;
			default:
				/* Something else that we don't care about */
				toss(obj);
				break;
		}
	}
	/* Finalize the vertex normals */
	finish_normals();
	fclose(obj);
}

/* Vertex, fragment, program */
GLuint v, f, p;

/* Read a file into a buffer and return a pointer to the buffer */
char * readFile(char * filename, int32_t * size) {
	FILE * tex;
	char * texture;
	tex = fopen(filename, "r");
	fseek(tex, 0L, SEEK_END);
	*size = ftell(tex);
	texture = malloc(*size);
	fseek(tex, 0L, SEEK_SET);
	fread(texture, *size, 1, tex);
	fclose(tex);
	return texture;
}

/* Initialize the scene */
void init(char * object, char * diffuse, char * sphere) {
	load_wavefront(object);
	/* Check for GLEW compatibility */
#if 0
	glewInit();
	if (glewIsSupported("GL_VERSION_2_0")) {
		printf("Ready.\n");
	} else {
		/* We don't have OpenGL 2.0 support! BAIL! */
		printf("wtf?\n");
		exit(1);
	}
#endif

	/* Some nice defaults */
	glClearColor (0.0, 0.0, 0.0, 0.0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);

	/* Initialize the two textures */
	char * texture;
	int32_t size;
	int dif_size, env_size;
	glGenTextures(1,&texture_a);
	glGenTextures(1,&texture_b);
	/* Diffuse texture { */
	/* The diffuse texture is a wood texture */
	texture = readFile(diffuse, &size); /* We have stored are textures as raw RGBA */
	dif_size = (int)sqrt(size / 4);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture_a);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, dif_size, dif_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture);
	free(texture);
	/* } */
	/* Sphere map texture { */
	texture = readFile(sphere, &size);
	env_size = (int)sqrt(size / 4);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texture_b);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexImage2D(GL_TEXTURE_2D, 0, 3, env_size, env_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture);
	free(texture);
	/* } */

	/* Load in the shader programs */
	char *vs = NULL,
		 *fs = NULL;
	int32_t v_size, f_size;
	v = glCreateShader(GL_VERTEX_SHADER);
	f = glCreateShader(GL_FRAGMENT_SHADER);
	vs = readFile("teapot.vert", &v_size); /* Vertex shader */
	fs = readFile("teapot.frag", &f_size); /* Fragment shader */
	glShaderSource(v, 1, &vs, (GLint *)&v_size); /* Load... */
	glShaderSource(f, 1, &fs, (GLint *)&f_size);
	free(vs); free(fs); /* Free the data blobs */
	glCompileShader(v); /* Compile... */
	glCompileShader(f);
	p = glCreateProgram(); /* Create a program */
	glAttachShader(p, v);  /* Attach the two shaders */
	glAttachShader(p, f);
	glLinkProgram(p);      /* Link it all together */

	/* Use our shaders */
	glUseProgram(p);

	/* Set the texture sources */
	GLint tex0 = glGetUniformLocation(p, "texture");
	GLint tex1 = glGetUniformLocation(p, "spheremap");
	glUniform1i(tex0, 0);
	glUniform1i(tex1, 1);

	/* Check for errors */
	GLenum glErr;
	int    retCode = 0;
	glErr = glGetError();
	while (glErr != GL_NO_ERROR)
	{
		//printf("glError: %s\n", gluErrorString(glErr));
		retCode = 1;
		glErr = glGetError();
	}


}

void lights(void) {
	/* Basic moving lighting */
	GLfloat white[] = {1.0,1.0,1.0,1.0};
	float l_scale = 7.0;
	GLfloat lpos[] = {l_scale * x_light, l_scale * y_light, 3.0};

	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);

	glLightfv(GL_LIGHT0, GL_POSITION, lpos);
	glLightfv(GL_LIGHT0, GL_AMBIENT, white);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
	glLightfv(GL_LIGHT0, GL_SPECULAR, white);
}

void display(void) {
	glLoadIdentity ();
	lights();
	/* Point the camera */
	gluLookAt(4.0 * sin(rot),height,-4.0 * cos(rot),
			  0.0,cam_offset,0.0,
			  0.0,100.0,0.0);

	if (!rotation_paused) {
		rot += 0.002;
	}

	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	/* Draw the teapot */
	glBegin(GL_TRIANGLES);
	for (uint32_t i = 0; i < faces.len; ++i) {
		glNormal3f(faces.nodes[i]->a->normal.x, faces.nodes[i]->a->normal.y, faces.nodes[i]->a->normal.z);
		glTexCoord2f(faces.nodes[i]->a->u,faces.nodes[i]->a->v);
		glVertex3f(faces.nodes[i]->a->x, faces.nodes[i]->a->y, faces.nodes[i]->a->z);
		glNormal3f(faces.nodes[i]->b->normal.x, faces.nodes[i]->b->normal.y, faces.nodes[i]->b->normal.z);
		glTexCoord2f(faces.nodes[i]->b->u,faces.nodes[i]->b->v);
		glVertex3f(faces.nodes[i]->b->x, faces.nodes[i]->b->y, faces.nodes[i]->b->z);
		glNormal3f(faces.nodes[i]->c->normal.x, faces.nodes[i]->c->normal.y, faces.nodes[i]->c->normal.z);
		glTexCoord2f(faces.nodes[i]->c->u,faces.nodes[i]->c->v);
		glVertex3f(faces.nodes[i]->c->x, faces.nodes[i]->c->y, faces.nodes[i]->c->z);
	}
	glEnd();

	glFlush ();

}

void reshape (int w, int h) {
	/* Reshape the viewport properly */
	win_width = w;
	win_height = h;
	glViewport (0, 0, (GLsizei) w, (GLsizei) h); 
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	gluPerspective(90.0,(double)w / (double)h,0.0001,10.0);
	glMatrixMode (GL_MODELVIEW);
}

int resize(gfx_context_t * ctx, OSMesaContext gl_ctx) {

	if (!OSMesaMakeCurrent(gl_ctx, ctx->backbuffer, GL_UNSIGNED_BYTE, ctx->width, ctx->height))
		return 1;

	OSMesaPixelStore(OSMESA_Y_UP, 0);

	reshape(ctx->width, ctx->height);
	return 0;
}

void keyboard(unsigned char key, int x, int y)
{
	switch (key) {
		case 'w':
			/* Raise camera */
			height += 0.07;
			break;
		case 's':
			/* Lower camera */
			height -= 0.07;
			break;
		case 'p':
			/* Pause / unpause object movement */
			rotation_paused = !rotation_paused;
			break;
		case 'q':
			quit = 1;
			break;
	}
}

void mouse(int x, int y) {
	x_light = (x - (float)(win_width / 2)) / ((float)win_height) ;
	y_light = (y - (float)(win_height / 2)) / ((float)win_height);
}

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;

void * draw_thread(void * glctx) {
	while (!quit) {
		display();
		flip(ctx);
		yutani_flip(yctx, wina);
		syscall_yield();
	}

	pthread_exit(0);
}


int main(int argc, char** argv) {
	/* default values */
	char * filename = "teapot.obj";
	char * diffuse  = "wood.rgba";
	char * sphere   = "nvidia.rgba";
	int c, index;

	chdir("/opt/examples");

	/* Parse some command-line arguments */
	while ((c = getopt(argc, argv, "d:e:h:s:")) != -1) {
		switch (c) {
			case 'd':
				diffuse = optarg;
				break;
			case 'e':
				sphere = optarg;
				break;
			case 's':
				/* Set scale */
				scale = atof(optarg);
				break;
			case 'h':
				cam_offset = atof(optarg);
				break;
			default:
				/* Uh, that's it for -args */
				printf("Unrecognized argument!\n");
				break;
		}
	}
	/* Get an optional filename from the last non-- parameter */
	for (index = optind; index < argc; ++index) {
		filename = argv[index];
	}

	printf("Press q to exit.\n");

	yctx = yutani_init();
	wina = yutani_window_create(yctx, 500, 500);
	yutani_window_move(yctx, wina, 100, 100);
	ctx = init_graphics_yutani_double_buffer(wina);
	draw_fill(ctx, rgb(0,0,0));
	yutani_window_update_shape(yctx, wina, YUTANI_SHAPE_THRESHOLD_HALF);

	OSMesaContext gl_ctx = OSMesaCreateContext(OSMESA_BGRA, NULL);
	if (resize(ctx, gl_ctx)) {
		fprintf(stderr, "%s: Something bad happened.\n", argv[0]);
		goto finish;
	}

	/* Load up the file, set everything else up */
	init (filename, diffuse, sphere);

	/* XXX add a method to query if there are available packets in pex */
	pthread_t thread;
	pthread_create(&thread, NULL, draw_thread, NULL);

	while (!quit) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT:
					{
						struct yutani_msg_key_event * ke = (void*)m->data;
						if (ke->event.action == KEY_ACTION_DOWN) {
							keyboard(ke->event.keycode, 0, 0);
						}
					}
					break;
				case YUTANI_MSG_SESSION_END:
					quit = 1;
					break;
				default:
					break;
			}
			free(m);
		}
	}

finish:
	OSMesaDestroyContext(gl_ctx);
	yutani_close(yctx, wina);

	return 0;
}
