// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Multi-stage data synchroniser in clkb where every stage shares the same foreign async reset, polarity, and binary reset value.
// expect-none: RDC-007
module top(input logic clka, clkb, rst_n, req, d, output logic v);
  logic rg, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) rg <= 1'b0; else rg <= req;
  always_ff @(posedge clkb or negedge rg) if (!rg) begin s1 <= 1'b0; s2 <= 1'b0; end else begin s1 <= d; s2 <= s1; end
  assign v = s2;
endmodule
