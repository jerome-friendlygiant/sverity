// sverity-args: --top top
// `posedge ~clk` is one inverter on a port: INFO, not an expression hazard.
// expect: LNT-047 INFO 5
module top(input logic clk, d, output logic q);
  always_ff @(posedge ~clk) q <= d;
endmodule
