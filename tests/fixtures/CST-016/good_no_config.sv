// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CST-016
module top(input logic clka, clkb, rst_a_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
endmodule
