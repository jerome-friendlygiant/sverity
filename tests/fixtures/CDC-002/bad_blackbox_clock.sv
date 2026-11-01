// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// the clock of the register is the output of an unknown module (black box): not a primary input, relationship unresolved
// expect: CDC-002 WARNING
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, d, output logic q);
  logic gclk;
  unknown_clock_cell u_cg(.clk_in(clka), .clk_out(gclk));
  always_ff @(posedge gclk) q <= d;
endmodule
