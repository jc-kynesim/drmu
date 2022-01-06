project(
	'drmu', 'c'
)

libavutil_dep = dependency('libavutil')
threads_dep = dependency('threads')

drmu_base = library('drmu_base',
	'drmu.c', 'drmu_atomic.c', 'pollqueue.c',
	dependencies : [
		threads_dep,
		dependency('libdrm'),
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
		dependency('libdrm'),
		dependency('libavcodec'),
		dependency('libavfilter'),
		dependency('libavformat'),
		libavutil_dep,
	],
)
