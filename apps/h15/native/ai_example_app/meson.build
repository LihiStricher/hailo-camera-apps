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


person_detection_src = ['person_detection.cpp']

executable('person_detection',
  person_detection_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: s1_demo_install_dir,
)

fire_detection_src = ['fire_detection.cpp']

executable('fire_detection',
  fire_detection_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, spdlog_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: s1_demo_install_dir,
)