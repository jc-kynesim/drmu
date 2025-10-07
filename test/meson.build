# Misc test targets

# hello_drmu & 10bittest are built in the base dir
# Extra tests built here to avoid (more) clutter of the base dir

executable(
	'argbtest',
	'argbtest.c', conf_file,
	include_directories : [ drmu_incs, conf_incs ],
	link_with : [ drmu_base ] + drmu_xlease_opt,
	dependencies : [
		libdrm_dep,
	],
)

executable(
	'resethdr',
	'resethdr.c', conf_file,
	include_directories : [ drmu_incs, conf_incs ],
	link_with : [ drmu_base ] + drmu_xlease_opt,
	dependencies : [
		libdrm_dep,
	],
)

executable(
	'rot_unit',
	'rot_unit.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
)

