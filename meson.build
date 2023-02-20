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

has_sorted_fmts = get_option('sorted_fmts')

xcb_dep = dependency('xcb', required : xlease_dep.found() or xdri3_dep.found())

mk_sorted_fmts = executable(
	'mk_sorted_fmts',
	'drmu_fmts.c',
	build_by_default : false,
	c_args : ['-DBUILD_MK_SORTED_FMTS_H'],
)

h_sorted_fmts = []
args_sorted_fmts = []

if has_sorted_fmts
	h_sorted_fmts = [ custom_target('mk_sorted_fmts_h',
		output : ['sorted_fmts.h'],
		command : [mk_sorted_fmts, '@OUTPUT@'],
	) ]
	args_sorted_fmts = ['-DHAS_SORTED_FMTS=1']
endif

drmu_base = library('drmu_base',
	'drmu_output.c', 'drmu.c', 'drmu_fmts.c', 'drmu_atomic.c', 'pollqueue.c', 'drmu_util.c',
	c_args : args_sorted_fmts,
	sources : h_sorted_fmts,
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

# waylease -------------------------------------------------------------------

wl_client_dep = dependency('wayland-client', required : get_option('waylease'))
#mesondefine HAS_WAYLEASE
conf_data.set10('HAS_WAYLEASE', wl_client_dep.found())

if wl_client_dep.found()
	wl_protocol_dep = dependency('wayland-protocols', version : '>= 1.30')
	wl_scanner = find_program('wayland-scanner')

	protocols_datadir = wl_protocol_dep.get_variable(pkgconfig: 'pkgdatadir', internal: 'pkgdatadir')

	protocol_defs = [
		['/staging/drm-lease/drm-lease-v1.xml', 'drm-lease-v1.c', 'drm-lease-v1-client-protocol.h'],
	]
	protocols_files = []

	foreach protodef: protocol_defs
		xmlfile = protocols_datadir + protodef.get(0)

		protocols_files += [
			custom_target(protodef.get(1),
				output : protodef.get(1),
				input : xmlfile,
				command : [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])]

		protocols_files += [
			custom_target(protodef.get(2),
				output : protodef.get(2),
				input : xmlfile,
				command : [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])]
	endforeach

	drmu_waylease = library('drmu_waylease',
		'drmu_waylease.c',
		sources : protocols_files,
		link_with : drmu_base,
		dependencies : [
			threads_dep,
			wl_client_dep,
			wl_protocol_dep,
		],
	)
	drmu_xlease_opt += [drmu_waylease]
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
