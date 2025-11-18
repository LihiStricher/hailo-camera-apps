################################################
# Single Stream Case Study
################################################

single_stream_install_dir = case_studies_install_dir + '/single_stream'

single_stream_app_src = ['main.cpp']

# DSP dependency for DspConvertStage
dsp_dep = meson.get_compiler('c').find_library('hailodsp', required: true)

executable('single_stream_case_study',
  single_stream_app_src,
  cpp_args : hailo_lib_args,
  dependencies : dependencies_apps + [libhailort_dep, reference_camera_dep, tappas_general_dep, image_dep, dsp_dep],
  gnu_symbol_visibility : 'default',
  install: true,
  install_dir: single_stream_install_dir,
)

# install json config files
install_subdir('configs', install_dir : single_stream_install_dir + '/resources')