################################################
# Detection Case Study
################################################

detection_install_dir = case_studies_install_dir + '/detection'

detection_app_src = ['main.cpp']

executable('detection_case_study',
  detection_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: detection_install_dir,
)

qwen2_vl_demo_src = ['qwen2_vl_demo.cpp']

zmq_dep = dependency('libzmq', required: false)

executable('qwen2_vl_demo',
  qwen2_vl_demo_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, zmq_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: detection_install_dir,
)

# install json config files
install_subdir('configs', install_dir : detection_install_dir + '/resources')