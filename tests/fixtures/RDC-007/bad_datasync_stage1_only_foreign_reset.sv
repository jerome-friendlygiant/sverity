// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Only stage 1 is reset by the foreign reset rg (stage 2 is unreset and stage 1 escapes): unsafe, must remain RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, req, d, output logic q, v);
  logic rg, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) rg <= 1'b0; else rg <= req;
  always_ff @(posedge clkb or negedge rg) if (!rg) s1 <= 1'b1; else s1 <= d;
  always_ff @(posedge clkb) s2 <= s1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s1;
  assign v = s2;
endmodule
