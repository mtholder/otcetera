project('otcetera', ['cpp'],
	version: '0.0.01-dev',
	default_options: [
	  'buildtype=release',
	  'cpp_std=c++17'
	],
	meson_version: '>=0.49')

cpp = meson.get_compiler('cpp')

project_cpp_args = ['-Wall','-Wextra','-Wno-sign-compare','-Woverloaded-virtual','-Wstrict-aliasing','-Wno-unknown-pragmas','-Wno-maybe-uninitialized','-Wno-expansion-to-defined']

add_project_arguments(cpp.get_supported_arguments(project_cpp_args), language: 'cpp')

# 2. Write a 'config.h'
conf_data = configuration_data()
conf_data.set_quoted('PACKAGE_VERSION', meson.project_version())
conf_data.set_quoted('_ARCH_', host_machine.system()+' ' + host_machine.cpu_family())
conf_data.set_quoted('_COMPILER_', cpp.get_id() + ' ' + cpp.version()+' ' + host_machine.cpu_family())
configure_file(output : 'config.h', configuration : conf_data)

clucene = dependency('libclucene-core', required: false)

# Do we need this?
# AC_PROG_LN_S

boost = dependency('boost', modules : ['program_options','system','filesystem'], version: '>=1.54')

json = declare_dependency(include_directories: include_directories('otc'))

rpath = '$ORIGIN/../'+get_option('libdir')

# build libotcetera
subdir('otc')
# build tools
subdir('tools')
# build web services
if get_option('webservices')
  subdir('ws')
endif

# check if python subprocess module is installed
if true
  subdir('test')  
endif
