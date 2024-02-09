//
// Book:      OpenGL(R) ES 2.0 Programming Guide
// Authors:   Aaftab Munshi, Dan Ginsburg, Dave Shreiner
// ISBN-10:   0321502795
// ISBN-13:   9780321502797
// Publisher: Addison-Wesley Professional
// URLs:      http://safari.informit.com/9780321563835
//            http://www.opengles-book.com
//

/*
 * (c) 2009 Aaftab Munshi, Dan Ginsburg, Dave Shreiner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// ESUtil.c
//
//    A utility library for OpenGL ES.  This library provides a
//    basic common framework for the example applications in the
//    OpenGL ES 2.0 Programming Guide.
//

///
//  Includes
//
#include "esUtil.h"
#include <math.h>
#include <string.h>

#define PI 3.1415926535897932384626433832795f

void ESUTIL_API
esScale(ESMatrix *result, GLfloat sx, GLfloat sy, GLfloat sz)
{
    result->m[0][0] *= sx;
    result->m[0][1] *= sx;
    result->m[0][2] *= sx;
    result->m[0][3] *= sx;

    result->m[1][0] *= sy;
    result->m[1][1] *= sy;
    result->m[1][2] *= sy;
    result->m[1][3] *= sy;

    result->m[2][0] *= sz;
    result->m[2][1] *= sz;
    result->m[2][2] *= sz;
    result->m[2][3] *= sz;
}

void ESUTIL_API
esTranslate(ESMatrix *result, GLfloat tx, GLfloat ty, GLfloat tz)
{
    result->m[3][0] += (result->m[0][0] * tx + result->m[1][0] * ty + result->m[2][0] * tz);
    result->m[3][1] += (result->m[0][1] * tx + result->m[1][1] * ty + result->m[2][1] * tz);
    result->m[3][2] += (result->m[0][2] * tx + result->m[1][2] * ty + result->m[2][2] * tz);
    result->m[3][3] += (result->m[0][3] * tx + result->m[1][3] * ty + result->m[2][3] * tz);
}

void ESUTIL_API
esRotate(ESMatrix *result, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat s, c;
    ESMatrix r;

    s = sinf(angle * PI / 180.0f);
    c = cosf(angle * PI / 180.0f);
    r.m[0][0] = x * x * (1 - c) + c;
    r.m[0][1] = y * x * (1 - c) + z * s;
    r.m[0][2] = x * z * (1 - c) - y * s;
    r.m[0][3] = 0;
    r.m[1][0] = x * y * (1 - c) - z * s;
    r.m[1][1] = y * y * (1 - c) + c;
    r.m[1][2] = y * z * (1 - c) + x * s;
    r.m[1][3] = 0;
    r.m[2][0] = x * z * (1 - c) + y * s;
    r.m[2][1] = y * z * (1 - c) - x * s;
    r.m[2][2] = z * z * (1 - c) + c;
    r.m[2][3] = 0;
    r.m[3][0] = 0;
    r.m[3][1] = 0;
    r.m[3][2] = 0;
    r.m[3][3] = 1;

    esMatrixMultiply(result, &r, result);
}

void ESUTIL_API
esFrustum(ESMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ)
{
    float       deltaX = right - left;
    float       deltaY = top - bottom;
    float       deltaZ = farZ - nearZ;
    ESMatrix    frust;

    esMatrixLoadIdentity(result);

    if ( (nearZ <= 0.0f) || (farZ <= 0.0f) ||
         (deltaX <= 0.0f) || (deltaY <= 0.0f) || (deltaZ <= 0.0f) )
         return;

    frust.m[0][0] = 2.0f * nearZ / deltaX;
    frust.m[0][1] = frust.m[0][2] = frust.m[0][3] = 0.0f;

    frust.m[1][1] = 2.0f * nearZ / deltaY;
    frust.m[1][0] = frust.m[1][2] = frust.m[1][3] = 0.0f;

    frust.m[2][0] = (right + left) / deltaX;
    frust.m[2][1] = (top + bottom) / deltaY;
    frust.m[2][2] = -(nearZ + farZ) / deltaZ;
    frust.m[2][3] = -1.0f;

    frust.m[3][2] = -2.0f * nearZ * farZ / deltaZ;
    frust.m[3][0] = frust.m[3][1] = frust.m[3][3] = 0.0f;

    esMatrixMultiply(result, &frust, result);
}


void ESUTIL_API 
esPerspective(ESMatrix *result, float fovy, float aspect, float nearZ, float farZ)
{
   GLfloat frustumW, frustumH;
   
   frustumH = tanf( fovy / 360.0f * PI ) * nearZ;
   frustumW = frustumH * aspect;

   esFrustum( result, -frustumW, frustumW, -frustumH, frustumH, nearZ, farZ );
}

void ESUTIL_API
esOrtho(ESMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ)
{
    float       deltaX = right - left;
    float       deltaY = top - bottom;
    float       deltaZ = farZ - nearZ;
    ESMatrix    ortho;

    if ( (deltaX == 0.0f) || (deltaY == 0.0f) || (deltaZ == 0.0f) )
        return;

    esMatrixLoadIdentity(&ortho);
    ortho.m[0][0] = 2.0f / deltaX;
    ortho.m[3][0] = -(right + left) / deltaX;
    ortho.m[1][1] = 2.0f / deltaY;
    ortho.m[3][1] = -(top + bottom) / deltaY;
    ortho.m[2][2] = -2.0f / deltaZ;
    ortho.m[3][2] = -(nearZ + farZ) / deltaZ;

    esMatrixMultiply(result, &ortho, result);
}


void ESUTIL_API
esMatrixMultiply(ESMatrix *result, ESMatrix *srcA, ESMatrix *srcB)
{
    ESMatrix    tmp;
    int         i;

	for (i=0; i<4; i++)
	{
		tmp.m[i][0] =	(srcA->m[i][0] * srcB->m[0][0]) +
						(srcA->m[i][1] * srcB->m[1][0]) +
						(srcA->m[i][2] * srcB->m[2][0]) +
						(srcA->m[i][3] * srcB->m[3][0]) ;

		tmp.m[i][1] =	(srcA->m[i][0] * srcB->m[0][1]) + 
						(srcA->m[i][1] * srcB->m[1][1]) +
						(srcA->m[i][2] * srcB->m[2][1]) +
						(srcA->m[i][3] * srcB->m[3][1]) ;

		tmp.m[i][2] =	(srcA->m[i][0] * srcB->m[0][2]) + 
						(srcA->m[i][1] * srcB->m[1][2]) +
						(srcA->m[i][2] * srcB->m[2][2]) +
						(srcA->m[i][3] * srcB->m[3][2]) ;

		tmp.m[i][3] =	(srcA->m[i][0] * srcB->m[0][3]) + 
						(srcA->m[i][1] * srcB->m[1][3]) +
						(srcA->m[i][2] * srcB->m[2][3]) +
						(srcA->m[i][3] * srcB->m[3][3]) ;
	}
    memcpy(result, &tmp, sizeof(ESMatrix));
}


void ESUTIL_API
esMatrixLoadIdentity(ESMatrix *result)
{
    memset(result, 0x0, sizeof(ESMatrix));
    result->m[0][0] = 1.0f;
    result->m[1][1] = 1.0f;
    result->m[2][2] = 1.0f;
    result->m[3][3] = 1.0f;
}

void ESUTIL_API
esTranspose(ESMatrix *result)
{
    ESMatrix tmp;
    tmp.m[0][0] = result->m[0][0];
    tmp.m[0][1] = result->m[1][0];
    tmp.m[0][2] = result->m[2][0];
    tmp.m[0][3] = result->m[3][0];
    tmp.m[1][0] = result->m[0][1];
    tmp.m[1][1] = result->m[1][1];
    tmp.m[1][2] = result->m[2][1];
    tmp.m[1][3] = result->m[3][1];
    tmp.m[2][0] = result->m[0][2];
    tmp.m[2][1] = result->m[1][2];
    tmp.m[2][2] = result->m[2][2];
    tmp.m[2][3] = result->m[3][2];
    tmp.m[3][0] = result->m[0][3];
    tmp.m[3][1] = result->m[1][3];
    tmp.m[3][2] = result->m[2][3];
    tmp.m[3][3] = result->m[3][3];

    memcpy(result, &tmp, sizeof(tmp));
}

void ESUTIL_API
esInvert(ESMatrix *result)
{
    ESMatrix tmp;
    esMatrixLoadIdentity(&tmp);

    // Extract and invert the translation part 't'. The inverse of a
    // translation matrix can be calculated by negating the translation
    // coordinates.
    tmp.m[3][0] = -result->m[3][0];
    tmp.m[3][1] = -result->m[3][1];
    tmp.m[3][2] = -result->m[3][2];

    // Invert the rotation part 'r'. The inverse of a rotation matrix is
    // equal to its transpose.
    result->m[3][0] = result->m[3][1] = result->m[3][2] = 0;
    esTranspose(result);

    // inv(m) = inv(r) * inv(t)
    esMatrixMultiply(result, &tmp, result);
}

