freetype_dep = dependency('freetype2', required : false)

#mesondefine HAS_RUNTICKER
conf_data.set10('HAS_RUNTICKER', freetype_dep.found())
runticker_opt = []

if freetype_dep.found()
	executable('example1', 'example1.c', 'runticker.c', 'ticker.c',
		include_directories : drmu_incs,
		link_with : [ drmu_base ],
		install : true,
		dependencies : [
			threads_dep,
			freetype_dep,
			libdrm_dep
		],
	)

	runticker_drmu = library('runticker_drmu',
		'runticker.c', 'ticker.c',
		include_directories : drmu_incs,
		link_with : [ drmu_base ],
		dependencies : [
			threads_dep,
			freetype_dep,
			libdrm_dep
		],
	)
	runticker_opt = [runticker_drmu]
else
	message('Not building ticker - freetype not found')
endif

