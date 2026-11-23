// sverity-args: --top top --sdc @DIR@/rtl_input_delay_index.sdc
// expect: CDC-029 ERROR
// expect-none: CST-018
// revert: rtl_input_delay_index.sdc => internal_input_delay_offset.sdc
// revert-expect-none: CDC-029 ERROR
module top(input logic clk, input logic [7:6] din, output logic q);
  always_ff @(posedge clk) q <= din[7];
endmodule
