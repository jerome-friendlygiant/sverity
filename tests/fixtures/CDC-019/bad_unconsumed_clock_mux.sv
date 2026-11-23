// sverity-args: --top top --sdc @DIR@/unconsumed_clock_mux.sdc
// expect: CDC-019 WARNING
// expect-unresolved: CDC-019
// expect: CDC-031 WARNING
// expect-unresolved: CDC-031
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clk0_a, clk1_b, sel_ctrl, output logic clk_out);
  assign clk_out = (sel_ctrl & clk1_b) | (~sel_ctrl & clk0_a);
endmodule
