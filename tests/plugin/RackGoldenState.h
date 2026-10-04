#pragma once

/*
    A BMO Mix Rack session as the build at 0e4bdb0 wrote it (2026-10-03), before
    chain edits kept their engines. Eight modules, BMO DEQ expanded and past
    the grid, every parameter at a fixed pseudo-random draw.
    RackTests restores it and requires the same chain back and a re-save
    byte-identical to this text: what a saved session plays must not move.

    Split one slot per literal because a single string literal is capped at
    16 KB on this compiler.
*/

namespace golden
{

inline constexpr const char* kRackState0e4bdb0 =
R"BMO(<RACK stateVersion="1">
)BMO"
R"BMO(<SLOT index="0" module="eq" schema="1">
<PARAMS stateVersion="1">
<PARAM id="hf_freq" value="2.0"/>
<PARAM id="hf_gain" value="-3.770000457763672"/>
<PARAM id="mid_freq" value="5.0"/>
<PARAM id="mid_gain" value="-4.820000648498535"/>
<PARAM id="mid_hiq" value="0.0"/>
<PARAM id="lf_freq" value="1.0"/>
<PARAM id="lf_gain" value="4.600000381469727"/>
<PARAM id="hpf_freq" value="0.0"/>
<PARAM id="lpf_freq" value="1.0"/>
<PARAM id="input_gain" value="3.819999694824219"/>
<PARAM id="output_level" value="17.82999801635742"/>
<PARAM id="eq_in" value="1.0"/>
<PARAM id="phase" value="0.0"/>
<PARAM id="mix" value="78.20000457763672"/>
<PARAM id="auto_gain" value="0.0"/>
<PARAM id="oversampling" value="1.0"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="1" module="sat" schema="1">
<PARAMS stateVersion="1">
<PARAM id="input_gain" value="1.85999870300293"/>
<PARAM id="drive" value="62.0"/>
<PARAM id="mix" value="82.59999847412109"/>
<PARAM id="output_level" value="23.22999954223633"/>
<PARAM id="sat_in" value="1.0"/>
<PARAM id="phase" value="1.0"/>
<PARAM id="auto_gain" value="1.0"/>
<PARAM id="oversampling" value="3.0"/>
<PARAM id="tone" value="80.80000305175781"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="2" module="deq" schema="1">
<PARAMS stateVersion="1" view="expanded">
<PARAM id="out" value="-1.19999885559082"/>
<PARAM id="b1_freq" value="94.5"/>
<PARAM id="b1_gain" value="13.60000228881836"/>
<PARAM id="b1_q" value="2.909999847412109"/>
<PARAM id="b1_thr" value="-17.79999923706055"/>
<PARAM id="b1_range" value="-3.799999237060547"/>
<PARAM id="b2_freq" value="34.5"/>
<PARAM id="b2_gain" value="-7.399999618530273"/>
<PARAM id="b2_q" value="0.4799999892711639"/>
<PARAM id="b2_thr" value="-42.19999694824219"/>
<PARAM id="b2_range" value="22.0"/>
<PARAM id="b3_freq" value="447.1000061035156"/>
<PARAM id="b3_gain" value="-5.0"/>
<PARAM id="b3_q" value="0.1899999976158142"/>
<PARAM id="b3_thr" value="-2.099998474121094"/>
<PARAM id="b3_range" value="12.90000152587891"/>
<PARAM id="b4_freq" value="3145.5"/>
<PARAM id="b4_gain" value="4.80000114440918"/>
<PARAM id="b4_q" value="2.0"/>
<PARAM id="b4_thr" value="-6.799999237060547"/>
<PARAM id="b4_range" value="-9.699999809265137"/>
<PARAM id="b5_freq" value="85.5"/>
<PARAM id="b5_gain" value="-1.5"/>
<PARAM id="b5_q" value="1.079999923706055"/>
<PARAM id="b5_thr" value="-13.09999847412109"/>
<PARAM id="b5_range" value="1.0"/>
<PARAM id="b6_freq" value="63.90000152587891"/>
<PARAM id="b6_gain" value="-0.7999992370605469"/>
<PARAM id="b6_q" value="0.1400000005960464"/>
<PARAM id="b6_thr" value="-2.299999237060547"/>
<PARAM id="b6_range" value="-3.69999885559082"/>
<PARAM id="active" value="1.0"/>
<PARAM id="b1_on" value="1.0"/>
<PARAM id="b1_shape" value="3.0"/>
<PARAM id="b1_place" value="2.0"/>
<PARAM id="b1_dyn" value="0.0"/>
<PARAM id="b1_dir" value="0.0"/>
<PARAM id="b1_ratio" value="1.710000038146973"/>
<PARAM id="b1_atk" value="0.3599999845027924"/>
<PARAM id="b1_rel" value="6.900000095367432"/>
<PARAM id="b2_on" value="1.0"/>
<PARAM id="b2_shape" value="4.0"/>
<PARAM id="b2_place" value="1.0"/>
<PARAM id="b2_dyn" value="0.0"/>
<PARAM id="b2_dir" value="1.0"/>
<PARAM id="b2_ratio" value="9.609999656677246"/>
<PARAM id="b2_atk" value="0.1700000017881393"/>
<PARAM id="b2_rel" value="412.7000122070312"/>
<PARAM id="b3_on" value="1.0"/>
<PARAM id="b3_shape" value="3.0"/>
<PARAM id="b3_place" value="1.0"/>
<PARAM id="b3_dyn" value="0.0"/>
<PARAM id="b3_dir" value="0.0"/>
<PARAM id="b3_ratio" value="1.620000004768372"/>
<PARAM id="b3_atk" value="190.5800018310547"/>
<PARAM id="b3_rel" value="1314.800048828125"/>
<PARAM id="b4_on" value="1.0"/>
<PARAM id="b4_shape" value="0.0"/>
<PARAM id="b4_place" value="0.0"/>
<PARAM id="b4_dyn" value="1.0"/>
<PARAM id="b4_dir" value="0.0"/>
<PARAM id="b4_ratio" value="16.95999908447266"/>
<PARAM id="b4_atk" value="58.72999572753906"/>
<PARAM id="b4_rel" value="1654.099975585938"/>
<PARAM id="b5_on" value="1.0"/>
<PARAM id="b5_shape" value="3.0"/>
<PARAM id="b5_place" value="1.0"/>
<PARAM id="b5_dyn" value="1.0"/>
<PARAM id="b5_dir" value="1.0"/>
<PARAM id="b5_ratio" value="2.029999971389771"/>
<PARAM id="b5_atk" value="0.1099999994039536"/>
<PARAM id="b5_rel" value="1029.5"/>
<PARAM id="b6_on" value="0.0"/>
<PARAM id="b6_shape" value="4.0"/>
<PARAM id="b6_place" value="1.0"/>
<PARAM id="b6_dyn" value="1.0"/>
<PARAM id="b6_dir" value="0.0"/>
<PARAM id="b6_ratio" value="2.039999961853027"/>
<PARAM id="b6_atk" value="116.1699981689453"/>
<PARAM id="b6_rel" value="1340.700073242188"/>
<PARAM id="b7_on" value="0.0"/>
<PARAM id="b7_shape" value="2.0"/>
<PARAM id="b7_freq" value="6708.80029296875"/>
<PARAM id="b7_gain" value="-9.5"/>
<PARAM id="b7_q" value="0.8600000143051147"/>
<PARAM id="b7_place" value="1.0"/>
<PARAM id="b7_dyn" value="1.0"/>
<PARAM id="b7_dir" value="0.0"/>
<PARAM id="b7_thr" value="-9.700000762939453"/>
<PARAM id="b7_range" value="3.100000381469727"/>
<PARAM id="b7_ratio" value="7.889999866485596"/>
<PARAM id="b7_atk" value="2.489999771118164"/>
<PARAM id="b7_rel" value="50.0"/>
<PARAM id="b8_on" value="1.0"/>
<PARAM id="b8_shape" value="3.0"/>
<PARAM id="b8_freq" value="257.1000061035156"/>
<PARAM id="b8_gain" value="-20.10000038146973"/>
<PARAM id="b8_q" value="2.079999923706055"/>
<PARAM id="b8_place" value="1.0"/>
<PARAM id="b8_dyn" value="1.0"/>
<PARAM id="b8_dir" value="0.0"/>
<PARAM id="b8_thr" value="-45.20000076293945"/>
<PARAM id="b8_range" value="-17.70000076293945"/>
<PARAM id="b8_ratio" value="9.720000267028809"/>
<PARAM id="b8_atk" value="8.590000152587891"/>
<PARAM id="b8_rel" value="80.0"/>
<PARAM id="b9_on" value="0.0"/>
<PARAM id="b9_shape" value="1.0"/>
<PARAM id="b9_freq" value="12800.0"/>
<PARAM id="b9_gain" value="-13.69999980926514"/>
<PARAM id="b9_q" value="0.1599999964237213"/>
<PARAM id="b9_place" value="1.0"/>
<PARAM id="b9_dyn" value="1.0"/>
<PARAM id="b9_dir" value="1.0"/>
<PARAM id="b9_thr" value="-25.39999771118164"/>
<PARAM id="b9_range" value="19.90000152587891"/>
<PARAM id="b9_ratio" value="4.099999904632568"/>
<PARAM id="b9_atk" value="9.270000457763672"/>
<PARAM id="b9_rel" value="205.8000030517578"/>
<PARAM id="b10_on" value="1.0"/>
<PARAM id="b10_shape" value="1.0"/>
<PARAM id="b10_freq" value="96.90000152587891"/>
<PARAM id="b10_gain" value="-19.70000076293945"/>
<PARAM id="b10_q" value="0.4799999892711639"/>
<PARAM id="b10_place" value="0.0"/>
<PARAM id="b10_dyn" value="1.0"/>
<PARAM id="b10_dir" value="1.0"/>
<PARAM id="b10_thr" value="-8.399997711181641"/>
<PARAM id="b10_range" value="-9.300000190734863"/>
<PARAM id="b10_ratio" value="1.029999971389771"/>
<PARAM id="b10_atk" value="31.39999961853027"/>
<PARAM id="b10_rel" value="1141.0"/>
<PARAM id="b11_on" value="0.0"/>
<PARAM id="b11_shape" value="3.0"/>
<PARAM id="b11_freq" value="31.60000038146973"/>
<PARAM id="b11_gain" value="3.30000114440918"/>
<PARAM id="b11_q" value="0.3199999928474426"/>
<PARAM id="b11_place" value="2.0"/>
<PARAM id="b11_dyn" value="1.0"/>
<PARAM id="b11_dir" value="0.0"/>
<PARAM id="b11_thr" value="-37.69999694824219"/>
<PARAM id="b11_range" value="-7.600000381469727"/>
<PARAM id="b11_ratio" value="1.159999966621399"/>
<PARAM id="b11_atk" value="13.02999973297119"/>
<PARAM id="b11_rel" value="1559.5"/>
<PARAM id="b12_on" value="0.0"/>
<PARAM id="b12_shape" value="3.0"/>
<PARAM id="b12_freq" value="208.1000061035156"/>
<PARAM id="b12_gain" value="-23.5"/>
<PARAM id="b12_q" value="1.059999942779541"/>
<PARAM id="b12_place" value="2.0"/>
<PARAM id="b12_dyn" value="1.0"/>
<PARAM id="b12_dir" value="1.0"/>
<PARAM id="b12_thr" value="-48.40000152587891"/>
<PARAM id="b12_range" value="-10.09999942779541"/>
<PARAM id="b12_ratio" value="14.42999935150146"/>
<PARAM id="b12_atk" value="0.1800000071525574"/>
<PARAM id="b12_rel" value="106.0999984741211"/>
<PARAM id="auto_gain" value="0.0"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="3" module="opto" schema="1">
<PARAMS stateVersion="1">
<PARAM id="crush" value="34.60000228881836"/>
<PARAM id="level" value="-7.270000457763672"/>
<PARAM id="mode" value="0.0"/>
<PARAM id="link" value="1.0"/>
<PARAM id="color" value="0.0"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="4" module="dwell" schema="1">
<PARAMS stateVersion="1">
<PARAM id="time" value="16.80999946594238"/>
<PARAM id="sync" value="1.0"/>
<PARAM id="note" value="5.0"/>
<PARAM id="feedback" value="22.70000076293945"/>
<PARAM id="character" value="1.0"/>
<PARAM id="stereo" value="2.0"/>
<PARAM id="low_cut" value="103.0999984741211"/>
<PARAM id="high_cut" value="8685.599609375"/>
<PARAM id="mod_rate" value="0.1400000005960464"/>
<PARAM id="mod_depth" value="27.39999961853027"/>
<PARAM id="drive" value="37.29999923706055"/>
<PARAM id="duck" value="23.10000038146973"/>
<PARAM id="mix" value="32.5"/>
<PARAM id="send" value="1.0"/>
<PARAM id="lane_gain" value="-33.09999847412109"/>
<PARAM id="hold" value="1.0"/>
<PARAM id="chop" value="0.0"/>
<PARAM id="fx" value="1.0"/>
<PARAM id="fx_type" value="1.0"/>
<PARAM id="fx_amount" value="93.80000305175781"/>
<PARAM id="lane_level" value="22.84999847412109"/>
<PARAM id="lane_time" value="108.5999984741211"/>
<PARAM id="lane_note" value="5.0"/>
<PARAM id="lane_fx" value="1.0"/>
<PARAM id="lane_fx_type" value="1.0"/>
<PARAM id="lane_fx_amount" value="9.100000381469727"/>
<PARAM id="fx_link" value="0.0"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="5" module="fetcomp" schema="1">
<PARAMS stateVersion="1">
<PARAM id="input" value="38.66999816894531"/>
<PARAM id="output" value="34.98999786376953"/>
<PARAM id="attack" value="1.460000038146973"/>
<PARAM id="release" value="5.319999694824219"/>
<PARAM id="ratio" value="0.0"/>
<PARAM id="mix" value="82.90000152587891"/>
<PARAM id="voicing" value="1.0"/>
<PARAM id="oversampling" value="1.0"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="6" module="reverb" schema="1">
<PARAMS stateVersion="1">
<PARAM id="type" value="3.0"/>
<PARAM id="size" value="36.20000076293945"/>
<PARAM id="predelay" value="35.70000076293945"/>
<PARAM id="decay" value="2.819999933242798"/>
<PARAM id="feed" value="4.400000095367432"/>
<PARAM id="damplo" value="0.7100000381469727"/>
<PARAM id="damphi" value="1.370000004768372"/>
<PARAM id="eqfilter" value="2.0"/>
<PARAM id="eqlofreq" value="30.79999923706055"/>
<PARAM id="eqlo" value="0.3999996185302734"/>
<PARAM id="eqloq" value="0.2100000083446503"/>
<PARAM id="eqmidfreq" value="360.3000183105469"/>
<PARAM id="eqmid" value="-15.09999942779541"/>
<PARAM id="eqmidq" value="1.960000038146973"/>
<PARAM id="eqhifreq" value="3626.800048828125"/>
<PARAM id="eqhi" value="-6.799999237060547"/>
<PARAM id="eqhiq" value="0.3699999749660492"/>
<PARAM id="ermode" value="1.0"/>
<PARAM id="erdensity" value="65.5"/>
<PARAM id="erspread" value="11.20000076293945"/>
<PARAM id="erhicut" value="1510.300048828125"/>
<PARAM id="ervariation" value="2.0"/>
<PARAM id="moddepth" value="0.7699999809265137"/>
<PARAM id="modrate" value="0.2700000107288361"/>
<PARAM id="width" value="186.0"/>
<PARAM id="inhicut" value="2002.5"/>
<PARAM id="erlevel" value="-2.099998474121094"/>
<PARAM id="verblevel" value="-22.69999885559082"/>
<PARAM id="mix" value="64.90000152587891"/>
<PARAM id="output" value="-1.899999618530273"/>
</PARAMS>
</SLOT>
)BMO"
R"BMO(<SLOT index="7" module="util" schema="1">
<PARAMS stateVersion="1">
<PARAM id="gain" value="21.95999908447266"/>
<PARAM id="pan" value="-88.0"/>
<PARAM id="width" value="142.0"/>
<PARAM id="phase_l" value="0.0"/>
<PARAM id="phase_r" value="1.0"/>
<PARAM id="mono" value="0.0"/>
</PARAMS>
</SLOT>
</RACK>
)BMO"
;

} // namespace golden
