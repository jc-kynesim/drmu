project(
	'drmu', 'c',
	default_options: ['warning_level=3',
		'werror=true',
		'buildtype=debugoptimized',
		'default_library=static']
)

libavutil_dep = dependency('libavutil')
threads_dep = dependency('threads')
libdrm_dep = dependency('libdrm')

drmu_base = library('drmu_base',
	'drmu.c', 'drmu_atomic.c', 'pollqueue.c',
	dependencies : [
		threads_dep,
		libdrm_dep
	],
)

drmu_xlease = library('drmu_xlease',
	'drmu_xlease.c',
	link_with : drmu_base,
	dependencies : [
		threads_dep,
		dependency('xcb'),
		dependency('xcb-randr'),
	],
)

drmu_xdri3 = library('drmu_xdri3',
	'drmu_xdri3.c',
	link_with : drmu_base,
	dependencies : [
		threads_dep,
		dependency('x11'),
		dependency('x11-xcb'),
		dependency('xcb'),
		dependency('xcb-dri3'),
	],
)

drmu_av = library('drmu_av',
	'drmu_av.c',
	link_with : drmu_base,
	dependencies : [
		threads_dep,
		libavutil_dep,
	],
)

executable(
	'hello_drmu',
	'hello_drmu.c', 'drmprime_out.c',
	link_with : [ drmu_base, drmu_xlease, drmu_av ],
	dependencies : [
		threads_dep,
		libdrm_dep,
		dependency('libavcodec'),
		dependency('libavfilter'),
		dependency('libavformat'),
		libavutil_dep,
	],
)

executable(
	'argbtest',
	'argbtest.c',
	link_with : [ drmu_base, drmu_xlease ],
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)

executable(
	'10bittest',
	'10bittest.c',
	link_with : [ drmu_base, drmu_xlease ],
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)

executable(
	'resethdr',
	'resethdr.c',
	link_with : [ drmu_base, drmu_xlease ],
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)
