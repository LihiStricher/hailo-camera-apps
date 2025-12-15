################################################
# HAILO 15 AI EXAMPLE APP
################################################

ai_example_install_dir = apps_install_dir + '/ai_example_app'


ai_example_app_src = ['main.cpp']

executable('ai_example_app',
  ai_example_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/ai_example_app',
)

# install json config files
install_subdir('configs', install_dir : apps_install_dir + '/ai_example_app/resources')

# ZMQ dep
zmq_dep = dependency('libzmq', method : 'pkg-config')

# DSP dep for hailo15
dsp_dep = meson.get_compiler('c').find_library('libhailodsp', required: true, dirs: sysroot + '/usr/lib/')

clip_example_app_src = ['clip_main.cpp']

executable('clip_example',
  clip_example_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, zmq_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: apps_install_dir + '/ai_example_app',
)

################################################
# S1 DEMO 
################################################

s1_demo_install_dir = apps_install_dir + '/s1_demo'


s1_pipeline_src = ['s1_pipeline.cpp']

executable('s1_pipeline',
  s1_pipeline_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep, zmq_dep, dsp_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: s1_demo_install_dir,
)
