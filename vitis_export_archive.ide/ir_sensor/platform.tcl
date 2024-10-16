# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\dries\ir_sensor\ir_sensor\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\dries\ir_sensor\ir_sensor\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {ir_sensor}\
-hw {C:\Users\dries\ir_sensor\ir_sensor.xsa}\
-out {C:/Users/dries/ir_sensor}

platform write
domain create -name {standalone_ps7_cortexa9_0} -display-name {standalone_ps7_cortexa9_0} -os {standalone} -proc {ps7_cortexa9_0} -runtime {cpp} -arch {32-bit} -support-app {hello_world}
platform generate -domains 
platform active {ir_sensor}
domain active {zynq_fsbl}
domain active {standalone_ps7_cortexa9_0}
platform generate -quick
platform generate
bsp reload
domain active {zynq_fsbl}
bsp reload
domain active {standalone_ps7_cortexa9_0}
bsp reload
platform config -updatehw {C:/Users/dries/ir_sensor/ir_sensor.xsa}
platform generate -domains 
platform generate
platform generate
