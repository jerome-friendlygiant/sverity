// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Constant-D capture flop async-reset by foreign rg whose output also escapes to functional logic: remains RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, req, d, output logic q, v);
  logic rg, cap, y1, y2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) rg <= 1'b0; else rg <= req;
  always_ff @(posedge clkb or negedge rg) if (!rg) cap <= 1'b1; else cap <= 1'b0;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin y1 <= 1'b1; y2 <= 1'b1; end else begin y1 <= cap; y2 <= y1; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= cap & d;
  assign v = y2;
endmodule
