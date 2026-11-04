// sverity-args: --top top --sdc @DIR@/boundary_mux_other_clock.sdc
// expect: CST-014 WARNING
// expect-none: CDC-019
// expect: CDC-003 ERROR
module top(input logic a, b, mode, output logic q);
  logic source_q;
  wire selected_clk = mode ? b : a;
  always_ff @(posedge b) source_q <= ~source_q;
  always_ff @(posedge selected_clk) q <= source_q;
endmodule
