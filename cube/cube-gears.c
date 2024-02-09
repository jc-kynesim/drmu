/*
 * Copyright (c) 2023 Scott Moreau <oreaus@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE /* for sincos */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include <GL/gl.h>

#include "common.h"
#include "esUtil.h"


#define STRIPS_PER_TOOTH 7
#define VERTICES_PER_TOOTH 46
#define GEAR_VERTEX_STRIDE 6

/* Each vertex consist of GEAR_VERTEX_STRIDE GLfloat attributes */
typedef GLfloat GearVertex[GEAR_VERTEX_STRIDE];

struct gears_framebuffer {
	GLuint fb;
	GLuint db;
	GLuint cb_tex;
	GLuint db_tex;
};

static struct {
	struct egl egl;
	struct gears_framebuffer gears_fb;

	GLfloat aspect;
	const struct gbm *gbm;

	GLuint program_face, program_gears;

	GLint modelviewmatrix, modelviewprojectionmatrix, normalmatrix;
	GLint in_position, in_normal, in_texcoord;
	GLint texture;
	GLuint vbo;
	GLuint positionsoffset, texcoordsoffset, normalsoffset;
} gl;

/**
 * Struct representing a gear.
 */
struct gear {
	/** The array of vertices comprising the gear */
	GearVertex *vertices;
	/** The number of vertices comprising the gear */
	int nvertices;
	/** The Vertex Buffer Object holding the vertices in the graphics card */
	GLuint vbo;
};

/** The gears */
static struct gear *gear1, *gear2, *gear3;
/** The current gear rotation angle */
static GLfloat angle = 0.0;
/** The location of the gears shader uniforms */
static GLuint
	modelview_projection_matrix_location,
	normal_matrix_location,
	material_color_location,
	position_location,
	normals_location;
/** The gears projection matrix */
static ESMatrix gears_projection_matrix;

/**
 * Fills a gear vertex.
 *
 * @param v the vertex to fill
 * @param x the x coordinate
 * @param y the y coordinate
 * @param z the z coortinate
 * @param n pointer to the normal table
 *
 * @return the operation error code
 */
static GearVertex *
vert(GearVertex *v, GLfloat x, GLfloat y, GLfloat z, GLfloat n[3])
{
	v[0][0] = x;
	v[0][1] = y;
	v[0][2] = z;
	v[0][3] = n[0];
	v[0][4] = n[1];
	v[0][5] = n[2];

	return v + 1;
}

/**
 *  Create a gear wheel.
 *
 *  @param inner_radius radius of hole at center
 *  @param outer_radius radius at center of teeth
 *  @param width width of gear
 *  @param teeth number of teeth
 *  @param tooth_depth depth of tooth
 *
 *  @return pointer to the constructed struct gear
 */
static struct gear *
create_gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
		GLint teeth, GLfloat tooth_depth)
{
	GLfloat r0, r1, r2;
	GLfloat da;
	GearVertex *v;
	struct gear *gear;
	double s[5], c[5];
	GLfloat normal[3];
	int cur_strip_start = 0;
	int i;

	/* Allocate memory for the gear */
	gear = malloc(sizeof *gear);
	if (gear == NULL)
		return NULL;

	/* Calculate the radii used in the gear */
	r0 = inner_radius;
	r1 = outer_radius - tooth_depth / 2.0;
	r2 = outer_radius + tooth_depth / 2.0;

	da = 2.0 * M_PI / teeth / 4.0;

	/* the first tooth doesn't need the first strip-restart sequence */
	assert(teeth > 0);
	gear->nvertices = VERTICES_PER_TOOTH + (VERTICES_PER_TOOTH + 2) * (teeth - 1);

	/* Allocate memory for the vertices */
	gear->vertices = calloc(gear->nvertices, sizeof(*gear->vertices));
	v = gear->vertices;

	for (i = 0; i < teeth; i++) {
		/* Calculate needed sin/cos for varius angles */
		sincos(i * 2.0 * M_PI / teeth, &s[0], &c[0]);
		sincos(i * 2.0 * M_PI / teeth + da, &s[1], &c[1]);
		sincos(i * 2.0 * M_PI / teeth + da * 2, &s[2], &c[2]);
		sincos(i * 2.0 * M_PI / teeth + da * 3, &s[3], &c[3]);
		sincos(i * 2.0 * M_PI / teeth + da * 4, &s[4], &c[4]);

	/* A set of macros for making the creation of the gears easier */
#define  GEAR_POINT(r, da) { (r) * c[(da)], (r) * s[(da)] }
#define  SET_NORMAL(x, y, z) do { \
	normal[0] = (x); normal[1] = (y); normal[2] = (z); \
} while(0)

#define  GEAR_VERT(v, point, sign) vert((v), p[(point)].x, p[(point)].y, (sign) * width * 0.5, normal)

#define START_STRIP do { \
	cur_strip_start = (v - gear->vertices); \
	if (cur_strip_start) \
		v += 2; \
} while(0);

/* emit prev last vertex
	emit first vertex */
#define END_STRIP do { \
	if (cur_strip_start) { \
		memcpy(gear->vertices + cur_strip_start, \
			gear->vertices + (cur_strip_start - 1), sizeof(GearVertex)); \
		memcpy(gear->vertices + cur_strip_start + 1, \
			gear->vertices + (cur_strip_start + 2), sizeof(GearVertex)); \
	} \
} while (0)

#define QUAD_WITH_NORMAL(p1, p2) do { \
	SET_NORMAL((p[(p1)].y - p[(p2)].y), -(p[(p1)].x - p[(p2)].x), 0); \
	v = GEAR_VERT(v, (p1), -1); \
	v = GEAR_VERT(v, (p1), 1); \
	v = GEAR_VERT(v, (p2), -1); \
	v = GEAR_VERT(v, (p2), 1); \
} while(0)

		struct point {
			GLfloat x;
			GLfloat y;
		};

		/* Create the 7 points (only x,y coords) used to draw a tooth */
		struct point p[7] = {
			GEAR_POINT(r2, 1), // 0
			GEAR_POINT(r2, 2), // 1
			GEAR_POINT(r1, 0), // 2
			GEAR_POINT(r1, 3), // 3
			GEAR_POINT(r0, 0), // 4
			GEAR_POINT(r1, 4), // 5
			GEAR_POINT(r0, 4), // 6
		};

		/* Front face */
		START_STRIP;
		SET_NORMAL(0, 0, 1.0);
		v = GEAR_VERT(v, 0, +1);
		v = GEAR_VERT(v, 1, +1);
		v = GEAR_VERT(v, 2, +1);
		v = GEAR_VERT(v, 3, +1);
		v = GEAR_VERT(v, 4, +1);
		v = GEAR_VERT(v, 5, +1);
		v = GEAR_VERT(v, 6, +1);
		END_STRIP;

		/* Back face */
		START_STRIP;
		SET_NORMAL(0, 0, -1.0);
		v = GEAR_VERT(v, 0, -1);
		v = GEAR_VERT(v, 1, -1);
		v = GEAR_VERT(v, 2, -1);
		v = GEAR_VERT(v, 3, -1);
		v = GEAR_VERT(v, 4, -1);
		v = GEAR_VERT(v, 5, -1);
		v = GEAR_VERT(v, 6, -1);
		END_STRIP;

		/* Outer face */
		START_STRIP;
		QUAD_WITH_NORMAL(0, 2);
		END_STRIP;

		START_STRIP;
		QUAD_WITH_NORMAL(1, 0);
		END_STRIP;

		START_STRIP;
		QUAD_WITH_NORMAL(3, 1);
		END_STRIP;

		START_STRIP;
		QUAD_WITH_NORMAL(5, 3);
		END_STRIP;

		/* Inner face */
		START_STRIP;
		SET_NORMAL(-c[0], -s[0], 0);
		v = GEAR_VERT(v, 4, -1);
		v = GEAR_VERT(v, 4, 1);
		SET_NORMAL(-c[4], -s[4], 0);
		v = GEAR_VERT(v, 6, -1);
		v = GEAR_VERT(v, 6, 1);
		END_STRIP;
	}

	assert(gear->nvertices == (v - gear->vertices));

	/* Store the vertices in a vertex buffer object (VBO) */
	glGenBuffers(1, &gear->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);
	glBufferData(GL_ARRAY_BUFFER, gear->nvertices * sizeof(GearVertex),
		gear->vertices, GL_STATIC_DRAW);

	return gear;
}

static const GLfloat vVertices[] = {
	// front
	-1.0f, -1.0f, +1.0f,
	+1.0f, -1.0f, +1.0f,
	-1.0f, +1.0f, +1.0f,
	+1.0f, +1.0f, +1.0f,
	// back
	+1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f, -1.0f,
	+1.0f, +1.0f, -1.0f,
	-1.0f, +1.0f, -1.0f,
	// right
	+1.0f, -1.0f, +1.0f,
	+1.0f, -1.0f, -1.0f,
	+1.0f, +1.0f, +1.0f,
	+1.0f, +1.0f, -1.0f,
	// left
	-1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f, +1.0f,
	-1.0f, +1.0f, -1.0f,
	-1.0f, +1.0f, +1.0f,
	// top
	-1.0f, +1.0f, +1.0f,
	+1.0f, +1.0f, +1.0f,
	-1.0f, +1.0f, -1.0f,
	+1.0f, +1.0f, -1.0f,
	// bottom
	-1.0f, -1.0f, -1.0f,
	+1.0f, -1.0f, -1.0f,
	-1.0f, -1.0f, +1.0f,
	+1.0f, -1.0f, +1.0f,
};

static const GLfloat vTexCoords[] = {
	//front
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	//back
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	//right
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	//left
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	//top
	1.0f, 1.0f,
	0.0f, 1.0f,
	1.0f, 0.0f,
	0.0f, 0.0f,
	//bottom
	1.0f, 0.0f,
	0.0f, 0.0f,
	1.0f, 1.0f,
	0.0f, 1.0f,
};

static const GLfloat vNormals[] = {
	// front
	+0.0f, +0.0f, +1.0f, // forward
	+0.0f, +0.0f, +1.0f, // forward
	+0.0f, +0.0f, +1.0f, // forward
	+0.0f, +0.0f, +1.0f, // forward
	// back
	+0.0f, +0.0f, -1.0f, // backward
	+0.0f, +0.0f, -1.0f, // backward
	+0.0f, +0.0f, -1.0f, // backward
	+0.0f, +0.0f, -1.0f, // backward
	// right
	+1.0f, +0.0f, +0.0f, // right
	+1.0f, +0.0f, +0.0f, // right
	+1.0f, +0.0f, +0.0f, // right
	+1.0f, +0.0f, +0.0f, // right
	// left
	-1.0f, +0.0f, +0.0f, // left
	-1.0f, +0.0f, +0.0f, // left
	-1.0f, +0.0f, +0.0f, // left
	-1.0f, +0.0f, +0.0f, // left
	// top
	+0.0f, +1.0f, +0.0f, // up
	+0.0f, +1.0f, +0.0f, // up
	+0.0f, +1.0f, +0.0f, // up
	+0.0f, +1.0f, +0.0f, // up
	// bottom
	+0.0f, -1.0f, +0.0f, // down
	+0.0f, -1.0f, +0.0f, // down
	+0.0f, -1.0f, +0.0f, // down
	+0.0f, -1.0f, +0.0f  // down
};

static const char gears_vertex_shader[] =
"attribute vec3 position;                                                            \n"
"attribute vec3 normal;                                                              \n"
"                                                                                    \n"
"uniform mat4 ModelViewProjectionMatrix;                                             \n"
"uniform mat4 NormalMatrix;                                                          \n"
"vec4 LightSourcePosition = vec4(2.0, 2.0, 20.0, 0.0);                               \n"
"uniform vec4 MaterialColor;                                                         \n"
"                                                                                    \n"
"varying vec4 Color;                                                                 \n"
"                                                                                    \n"
"void main(void)                                                                     \n"
"{                                                                                   \n"
"    // Transform the normal to eye coordinates                                      \n"
"    vec3 N = normalize(vec3(NormalMatrix * vec4(normal, 1.0)));                     \n"
"                                                                                    \n"
"    // The LightSourcePosition is actually its direction for directional light      \n"
"    vec3 L = normalize(LightSourcePosition.xyz);                                    \n"
"                                                                                    \n"
"    float diffuse = max(dot(N, L), 0.0);                                            \n"
"    float ambient = 0.2;                                                            \n"
"                                                                                    \n"
"    // Multiply the diffuse value by the vertex color (which is fixed in this case) \n"
"    // to get the actual color that we will use to draw this vertex with            \n"
"    Color = vec4((ambient + diffuse) * MaterialColor.xyz, 1.0);                     \n"
"                                                                                    \n"
"    // Transform the position to clip coordinates                                   \n"
"    gl_Position = ModelViewProjectionMatrix * vec4(position, 1.0);                  \n"
"}";

static const char gears_fragment_shader[] =
"precision mediump float;  \n"
"varying vec4 Color;       \n"
"                          \n"
"void main(void)           \n"
"{                         \n"
"    gl_FragColor = Color; \n"
"}";

static const char *cube_vertex_shader =
"uniform mat4 modelviewMatrix;                                 \n"
"uniform mat4 modelviewprojectionMatrix;                       \n"
"uniform mat3 normalMatrix;                                    \n"
"                                                              \n"
"attribute vec4 in_position;                                   \n"
"attribute vec3 in_normal;                                     \n"
"attribute vec2 in_texcoord;                                   \n"
"                                                              \n"
"vec4 lightSource = vec4(2.0, 2.0, 20.0, 0.0);                 \n"
"                                                              \n"
"varying vec4 vVaryingColor;                                   \n"
"varying vec2 vTexCoord;                                       \n"
"                                                              \n"
"void main()                                                   \n"
"{                                                             \n"
"    gl_Position = modelviewprojectionMatrix * in_position;    \n"
"    vec3 vEyeNormal = normalMatrix * in_normal;               \n"
"    vec4 vPosition4 = modelviewMatrix * in_position;          \n"
"    vec3 vPosition3 = vPosition4.xyz / vPosition4.w;          \n"
"    vec3 vLightDir = normalize(lightSource.xyz - vPosition3); \n"
"    float diff = max(0.0, dot(vEyeNormal, vLightDir));        \n"
"    vVaryingColor = vec4(diff * vec3(1.0, 1.0, 1.0), 1.0);    \n"
"    vTexCoord = in_texcoord;                                  \n"
"}                                                             \n";

static const char *cube_fragment_shader =
"precision mediump float;                                           \n"
"                                                                   \n"
"uniform sampler2D uTex;                                            \n"
"                                                                   \n"
"varying vec4 vVaryingColor;                                        \n"
"varying vec2 vTexCoord;                                            \n"
"                                                                   \n"
"void main()                                                        \n"
"{                                                                  \n"
"    if (vTexCoord.x < 0.01 || vTexCoord.x > 0.99 ||                \n"
"        vTexCoord.y < 0.01 || vTexCoord.y > 0.99)                  \n"
"        gl_FragColor = vec4(0.7, 0.7, 0.7, 1.0);                   \n"
"    else                                                           \n"
"        gl_FragColor = vVaryingColor * texture2D(uTex, vTexCoord); \n"
"}                                                                  \n";

static const uint32_t texw = 512, texh = 512;

static void
gears_framebuffer_destroy()
{
	glDeleteTextures(1, &gl.gears_fb.cb_tex);
	glDeleteTextures(1, &gl.gears_fb.db_tex);
	glDeleteRenderbuffers(1, &gl.gears_fb.db);
	glDeleteFramebuffers(1, &gl.gears_fb.fb);
}

static bool
gears_framebuffer_create()
{
	glGenTextures(1, &gl.gears_fb.cb_tex);
	glBindTexture(GL_TEXTURE_2D, gl.gears_fb.cb_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texw, texh, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);

	glGenTextures(1, &gl.gears_fb.db_tex);
	glBindTexture(GL_TEXTURE_2D, gl.gears_fb.db_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, texw, texh, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0);

	glGenFramebuffers(1, &gl.gears_fb.fb);
	glBindFramebuffer(GL_FRAMEBUFFER, gl.gears_fb.fb);

	glGenRenderbuffers(1, &gl.gears_fb.db);
	glBindRenderbuffer(GL_RENDERBUFFER, gl.gears_fb.db);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gl.gears_fb.db_tex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl.gears_fb.cb_tex, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("failed framebuffer check for created target buffer\n");
		gears_framebuffer_destroy();
		return false;
	}

	return true;
}

/**
 * Draws a gear.
 *
 * @param gear the gear to draw
 * @param transform the current transformation matrix
 * @param x the x position to draw the gear at
 * @param y the y position to draw the gear at
 * @param angle the rotation angle of the gear
 * @param color the color of the gear
 */
static void
draw_gear(struct gear *gear, ESMatrix *transform,
		GLfloat x, GLfloat y, GLfloat angle, const GLfloat color[4])
{
	ESMatrix model_view;
	ESMatrix normal_matrix;
	ESMatrix model_view_projection;

	/* Translate and rotate the gear */
	memcpy(&model_view, transform, sizeof(model_view));
	esTranslate(&model_view, x, y, 0);
	esRotate(&model_view, angle, 0, 0, 1);

	/* Create and set the ModelViewProjectionMatrix */
	memcpy(&model_view_projection, &gears_projection_matrix.m[0][0], sizeof(model_view_projection));
	esMatrixMultiply(&model_view_projection, &model_view, &model_view_projection);

	glUniformMatrix4fv(modelview_projection_matrix_location, 1, GL_FALSE,
							 &model_view_projection.m[0][0]);

	/*
	 * Create and set the NormalMatrix. It's the inverse transpose of the
	 * ModelView matrix.
	 */
	memcpy(&normal_matrix, &model_view, sizeof (normal_matrix));
	esInvert(&normal_matrix);
	esTranspose(&normal_matrix);
	glUniformMatrix4fv(normal_matrix_location, 1, GL_FALSE, &normal_matrix.m[0][0]);

	/* Set the gear color */
	glUniform4fv(material_color_location, 1, color);

	/* Set the vertex buffer object to use */
	glBindBuffer(GL_ARRAY_BUFFER, gear->vbo);

	/* Set up the position of the attributes in the vertex buffer object */
	glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE,
			6 * sizeof(GLfloat), 0);
	glVertexAttribPointer(normals_location, 3, GL_FLOAT, GL_FALSE,
			6 * sizeof(GLfloat), (GLfloat *) 0 + 3);

	/* Enable the attributes */
	glEnableVertexAttribArray(position_location);
	glEnableVertexAttribArray(normals_location);

	/* Draw the triangle strips that comprise the gear */
	glDrawArrays(GL_TRIANGLE_STRIP, 0, gear->nvertices);

	/* Disable the attributes */
	glDisableVertexAttribArray(position_location);
	glDisableVertexAttribArray(normals_location);
}

static void
draw_gears(unsigned i)
{
	int current_fb;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &current_fb);

	static const GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
	static const GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
	static const GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };
	ESMatrix transform;
	esMatrixLoadIdentity(&transform);

	static double tRot0 = -1.0;
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	double ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	double dt, t = ms / 1000.0;

	if (tRot0 < 0.0)
		tRot0 = t;
	dt = t - tRot0;
	tRot0 = t;

	/* advance rotation for next frame */
	angle += 70.0 * dt;  /* 70 degrees per second */
	if (angle > 3600.0)
		angle -= 3600.0;

	/* Translate the view */
	esTranslate(&transform, 0, 0, -40);

	assert(gears_framebuffer_create());

	glViewport(0, 0, texw, texh);

	glClearColor(0.5, 0.5, 0.5, 1.0);

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(gl.program_gears);

	draw_gear(gear1, &transform, -3.0, -2.0, angle, red);
	draw_gear(gear2, &transform, 3.1, -2.0, -2 * angle - 9.0, green);
	draw_gear(gear3, &transform, -3.1, 4.2, -2 * angle - 25.0, blue);

	glBindFramebuffer(GL_FRAMEBUFFER, current_fb);

	glBindTexture(GL_TEXTURE_2D, gl.gears_fb.cb_tex);

	glUseProgram(gl.program_face);

	glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);

	glEnableVertexAttribArray(gl.in_position);
	glEnableVertexAttribArray(gl.in_normal);
	glEnableVertexAttribArray(gl.in_texcoord);

	glViewport(0, 0, gl.gbm->width, gl.gbm->height);

	ESMatrix modelview;

	/* clear the color buffer */
	glClearColor(0.5, 0.5, 0.5, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	esMatrixLoadIdentity(&modelview);
	esTranslate(&modelview, 0.0f, 0.0f, -8.0f);
	esRotate(&modelview, 45.0f + (0.25f * i), 1.0f, 0.0f, 0.0f);
	esRotate(&modelview, 45.0f - (0.5f * i), 0.0f, 1.0f, 0.0f);
	esRotate(&modelview, 10.0f + (0.15f * i), 0.0f, 0.0f, 1.0f);

	ESMatrix projection;
	esFrustum(&projection, -2.8f, +2.8f, -2.8f * gl.aspect, +2.8f * gl.aspect, 6.0f, 10.0f);

	ESMatrix modelviewprojection;
	esMatrixLoadIdentity(&modelviewprojection);
	esMatrixMultiply(&modelviewprojection, &modelview, &projection);

	float normal[9];
	normal[0] = modelview.m[0][0];
	normal[1] = modelview.m[0][1];
	normal[2] = modelview.m[0][2];
	normal[3] = modelview.m[1][0];
	normal[4] = modelview.m[1][1];
	normal[5] = modelview.m[1][2];
	normal[6] = modelview.m[2][0];
	normal[7] = modelview.m[2][1];
	normal[8] = modelview.m[2][2];

	glUniformMatrix4fv(gl.modelviewmatrix, 1, GL_FALSE, &modelview.m[0][0]);
	glUniformMatrix4fv(gl.modelviewprojectionmatrix, 1, GL_FALSE, &modelviewprojection.m[0][0]);
	glUniformMatrix3fv(gl.normalmatrix, 1, GL_FALSE, normal);
	glUniform1i(gl.texture, 0); /* '0' refers to texture unit 0. */

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 4, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 8, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 12, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 16, 4);
	glDrawArrays(GL_TRIANGLE_STRIP, 20, 4);
	glUseProgram(0);

	glDisableVertexAttribArray(gl.in_position);
	glDisableVertexAttribArray(gl.in_normal);
	glDisableVertexAttribArray(gl.in_texcoord);

	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	gears_framebuffer_destroy();
}

const struct egl *
init_cube_gears(const struct gbm *gbm, int samples)
{
	int ret;

	ret = init_egl(&gl.egl, gbm, samples);
	if (ret)
		return NULL;

	if (egl_check(&gl.egl, eglCreateImageKHR) ||
	    egl_check(&gl.egl, glEGLImageTargetTexture2DOES) ||
	    egl_check(&gl.egl, eglDestroyImageKHR))
		return NULL;

	gl.aspect = (GLfloat)(gbm->height) / (GLfloat)(gbm->width);
	gl.gbm = gbm;

	ret = create_program(cube_vertex_shader, cube_fragment_shader);
	if (ret < 0)
		return NULL;

	gl.program_face = ret;

	gl.in_position = 0;
	gl.in_normal = 1;
	gl.in_texcoord = 2;

	glBindAttribLocation(gl.program_face, gl.in_position, "in_position");
	glBindAttribLocation(gl.program_face, gl.in_normal, "in_normal");
	glBindAttribLocation(gl.program_face, gl.in_texcoord, "in_texcoord");

	ret = link_program(gl.program_face);
	if (ret)
		return NULL;

	glUseProgram(gl.program_face);

	gl.modelviewmatrix = glGetUniformLocation(gl.program_face, "modelviewMatrix");
	gl.modelviewprojectionmatrix = glGetUniformLocation(gl.program_face, "modelviewprojectionMatrix");
	gl.normalmatrix = glGetUniformLocation(gl.program_face, "normalMatrix");

	gl.texture   = glGetUniformLocation(gl.program_face, "uTex");

	glViewport(0, 0, gbm->width, gbm->height);
	glEnable(GL_CULL_FACE);

	gl.positionsoffset = 0;
	gl.texcoordsoffset = sizeof(vVertices);
	gl.normalsoffset = sizeof(vVertices) + sizeof(vTexCoords);

	glGenBuffers(1, &gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vVertices) + sizeof(vTexCoords) + sizeof(vNormals), 0, GL_STATIC_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, gl.positionsoffset, sizeof(vVertices), &vVertices[0]);
	glBufferSubData(GL_ARRAY_BUFFER, gl.texcoordsoffset, sizeof(vTexCoords), &vTexCoords[0]);
	glBufferSubData(GL_ARRAY_BUFFER, gl.normalsoffset, sizeof(vNormals), &vNormals[0]);
	glVertexAttribPointer(gl.in_position, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(intptr_t)gl.positionsoffset);
	glEnableVertexAttribArray(gl.in_position);
	glVertexAttribPointer(gl.in_normal, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(intptr_t)gl.normalsoffset);
	glEnableVertexAttribArray(gl.in_normal);
	glVertexAttribPointer(gl.in_texcoord, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(intptr_t)gl.texcoordsoffset);
	glEnableVertexAttribArray(gl.in_texcoord);

	ret = create_program(gears_vertex_shader, gears_fragment_shader);
	if (ret < 0)
		return NULL;
	gl.program_gears = ret;

	position_location = 3;
	normals_location = 4;

	glBindAttribLocation(gl.program_gears, position_location, "position");
	glBindAttribLocation(gl.program_gears, normals_location, "normal");

	ret = link_program(gl.program_gears);
	if (ret)
		return NULL;
	glUseProgram(gl.program_gears);

	/* Get the locations of the uniforms so we can access them */
	modelview_projection_matrix_location = glGetUniformLocation(gl.program_gears, "ModelViewProjectionMatrix");
	normal_matrix_location = glGetUniformLocation(gl.program_gears, "NormalMatrix");
	material_color_location = glGetUniformLocation(gl.program_gears, "MaterialColor");

	/* create the gears */
	gear1 = create_gear(1.0, 4.0, 1.0, 20, 0.7);
	gear2 = create_gear(0.5, 2.0, 2.0, 10, 0.7);
	gear3 = create_gear(1.3, 2.0, 0.5, 10, 0.7);

	esFrustum(&gears_projection_matrix, -1.0, 1.0, -1.0, 1.0, 5.0, 60.0);

	gl.egl.draw = draw_gears;

	return &gl.egl;
}
