// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An inversion of the reset to an output has no functional leaf.
// expect-none: RDC-009
module top(input logic clka, rst_n, d, output logic q, rst_o);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  assign rst_o = ~rst_n;
endmodule
