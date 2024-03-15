#
#  Copyright (c) 2018 Lyude Paul <thatslyude@gmail.com>
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  the rights to use, copy, modify, merge, publish, distribute, sublicense,
#  and/or sell copies of the Software, and to permit persons to whom the
#  Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
#  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.
#

sources = files(
  'common.c',
  'cube-smooth.c',
  'cube-gears.c',
  'cube-tex.c',
  'drm-atomic.c',
  'drm-common.c',
  'drm-drmu.c',
  'drm-legacy.c',
  'drm-offscreen.c',
  'esTransform.c',
  'frame-512x512-NV12.c',
  'frame-512x512-RGBA.c',
  'kmscube.c',
  'perfcntrs.c',
)

cc = meson.get_compiler('c')
dep_m = cc.find_library('m', required : false)
dep_threads = dependency('threads')
dep_libdrm = dependency('libdrm', version : '>=2.4.71')
dep_gbm = dependency('gbm', version : '>=13.0', required : false)
dep_egl = dependency('egl', required : false)
dep_gles2 = dependency('glesv2', required : false)
cube_c_args = []

with_gles3 = cc.check_header('GLES3/gl3.h', dependencies: dep_gles2)

if with_gles3
  sources += files('cube-shadertoy.c')
  cube_c_args += ['-DHAVE_GLES3']
  message('GLES3 supported; shadertoy & texturator are included in this build')
else
  warning('GLES3 not supported; shadertoy & texturator are NOT included in this build')
endif

dep_libpng = dependency('libpng', required : false)

if dep_libpng.found()
  cube_c_args += ['-DHAVE_LIBPNG']
endif

dep_common = [dep_m, dep_threads, dep_libdrm, dep_gbm, dep_egl, dep_gles2, dep_libpng]

dep_gst = []
foreach _dep : [
  'gstreamer-1.0',
  'gstreamer-plugins-base-1.0',
  'gstreamer-app-1.0',
  'gstreamer-allocators-1.0',
  'gstreamer-video-1.0',
]
  dep_gst += dependency(_dep, version : '>= 1.6.0', required : false)
endforeach
dep_gst += dependency('glib-2.0', required : false)

# See if we found everything we needed
with_gst = true
foreach _dep : dep_gst
  if not _dep.found()
    with_gst = false
  endif
endforeach

with_all_common = true
foreach _dep : dep_common
  if not _dep.found()
    with_all_common = false
  endif
endforeach


if with_gst
  dep_common += dep_gst
  sources += files('cube-video.c', 'gst-decoder.c')
  cube_c_args += ['-DHAVE_GST']
  message('Building with gstreamer support')
else
  message('Building without gstreamer support')
endif

if with_all_common
	executable('kmscube', sources,
		dependencies : dep_common,
		c_args : cube_c_args,
		include_directories : drmu_incs,
		link_with : [ drmu_base, drmu_gbm ],
		install : true)

	if with_gles3
	  executable('texturator',
		files(
			'common.c',
			'drm-legacy.c',
			'drm-common.c',
			'perfcntrs.c',  # not used, but required to link
			'texturator.c',
		 ),
		 dependencies : dep_common,
		 c_args : cube_c_args,
		 include_directories : drmu_incs,
		 link_with : [ drmu_base ],
		 install : true
	  )
	endif
else
    message('Not building kmscube - not all required libs found')
endif

# Maybe shoudln't need gbm here but it is built into the headers

#mesondefine HAS_RUNCUBE
if dep_m.found() and dep_egl.found() and dep_gles2.found() and dep_gbm.found()
	runcube_drmu = library('runcube_drmu',
		'runcube.c',
		'common.c',
		'cube-smooth.c',
		'drm-common.c',
		'drm-drmu.c',
		'esTransform.c',
		'perfcntrs.c',
		 c_args : cube_c_args,
		 include_directories : drmu_incs,
		 link_with : [ drmu_base ],
		dependencies : [
			dep_m,
			threads_dep,
			libdrm_dep,
			dep_gbm,
			dep_egl,
			dep_gles2
		],
	)
	runcube_opt = [runcube_drmu]
	conf_data.set10('HAS_RUNCUBE', true)
else
	message('Runcube library not built - not all required libs found')
	runcube_opt = []
	conf_data.set10('HAS_RUNCUBE', false)
endif

