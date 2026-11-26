// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An output reset muxed between rst_n and a data input, used by no register of this design.
// expect: RDC-009 WARNING
// expect-unresolved: RDC-009
module top(input logic clka, rst_n, sel, x, d, output logic q, rst_o);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  assign rst_o = sel ? x : rst_n;
endmodule
