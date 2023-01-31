project(
	'drmu', 'c',
	default_options: ['warning_level=3',
		'werror=true',
		'buildtype=debugoptimized',
		'default_library=static']
)

conf_data = configuration_data()

libavutil_dep = dependency('libavutil')
threads_dep = dependency('threads')
libdrm_dep = dependency('libdrm')

xdri3_dep = dependency('xcb-dri3', required : get_option('xdri3'))
#mesondefine HAS_XDRI3
conf_data.set10('HAS_XDRI3', xdri3_dep.found())

xlease_dep = dependency('xcb-randr', required : get_option('xlease'))
#mesondefine HAS_XLEASE
conf_data.set10('HAS_XLEASE', xlease_dep.found())

xcb_dep = dependency('xcb', required : xlease_dep.found() or xdri3_dep.found())

drmu_base = library('drmu_base',
	'drmu_output.c', 'drmu.c', 'drmu_atomic.c', 'pollqueue.c', 'drmu_util.c',
	dependencies : [
		threads_dep,
		libdrm_dep
	],
)

drmu_xlease_opt = []
if xlease_dep.found()
	drmu_xlease = library('drmu_xlease',
		'drmu_xlease.c',
		link_with : drmu_base,
		dependencies : [
			threads_dep,
			xcb_dep,
			xlease_dep,
		],
	)
	drmu_xlease_opt += [drmu_xlease]
endif

if xdri3_dep.found()
	drmu_xdri3 = library('drmu_xdri3',
		'drmu_xdri3.c',
		link_with : drmu_base,
		dependencies : [
			threads_dep,
			dependency('x11'),
			dependency('x11-xcb'),
			xcb_dep,
			xdri3_dep,
		],
	)
	drmu_xlease_opt += [drmu_xdri3]
endif

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
	link_with : [ drmu_base, drmu_av ] + drmu_xlease_opt,
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
	link_with : [ drmu_base ] + drmu_xlease_opt,
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)

executable(
	'10bittest',
	'10bittest.c', 'plane16.c',
	link_with : [ drmu_base ] + drmu_xlease_opt,
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)

executable(
	'resethdr',
	'resethdr.c',
	link_with : [ drmu_base ] + drmu_xlease_opt,
	dependencies : [
		threads_dep,
		libdrm_dep,
	],
)

configure_file(
	output : 'config.h',
	configuration : conf_data
)
