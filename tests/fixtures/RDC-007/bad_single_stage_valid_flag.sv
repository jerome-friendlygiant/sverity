// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Single-stage flop async-reset by a cross-domain register feeding a valid flag is below min_stages and remains RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, req, output logic v);
  logic rg, s1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) rg <= 1'b0; else rg <= req;
  always_ff @(posedge clkb or negedge rg) if (!rg) s1 <= 1'b0; else s1 <= 1'b1;
  assign v = s1;
endmodule
