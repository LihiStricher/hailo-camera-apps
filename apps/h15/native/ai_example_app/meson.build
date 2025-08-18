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
  install_dir: ai_example_install_dir,
)

################################################
# S1 DEMO 
################################################

s1_demo_install_dir = apps_install_dir + '/s1_demo'


s1_pipeline_src = ['s1_pipeline.cpp']

executable('s1_pipeline',
  s1_pipeline_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: s1_demo_install_dir,
)

################################################
# File Source to UDP Case Study
################################################

file_source_to_udp_install_dir = apps_install_dir + '/s1_demo'

s1_personface_detection_from_file_src = ['s1_personface_detection_from_file.cpp']

executable('s1_personface_detection_from_file',
  s1_personface_detection_from_file_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: file_source_to_udp_install_dir,
)

################################################
# File Source to UDP Case Study
################################################

file_source_to_udp_install_dir = apps_install_dir + '/s1_demo'

s1_fire_detection_from_file_src = ['s1_fire_detection_from_file.cpp']

executable('s1_fire_detection_from_file',
  s1_fire_detection_from_file_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: file_source_to_udp_install_dir,
)