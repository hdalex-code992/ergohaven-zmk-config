if(CONFIG_ZMK_POINTING)
  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_XY_TO_ARROWS app PRIVATE
      src/input_processor_xy_to_arrows.c
    )
    target_sources_ifdef(CONFIG_ZMK_TB_SPEED_CONTROL app PRIVATE
      src/tb_speed_control.c
      src/behavior_tb_speed.c
    )
  endif()
endif()
