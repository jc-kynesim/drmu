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

rot_unit = executable(
	'rot_unit',
	'rot_unit.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
)
test('rot_unit', rot_unit)

reduce_unit = executable(
	'reduce_unit',
	'reduce_unit.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
)
test('reduce_unit', reduce_unit)

parse_rect_unit = executable(
	'parse_rect_unit',
	'parse_rect_unit.c',
	include_directories : drmu_incs,
	link_with : [ drmu_base ],
)
test('parse_rect_unit', parse_rect_unit)

plane16_unit = executable(
	'plane16_unit',
	'plane16_unit.c', 'plane16.c',
	include_directories : [ drmu_incs ],
	link_with : [ drmu_base ],
	dependencies : [ libdrm_dep ],
)
test('plane16_unit', plane16_unit)
