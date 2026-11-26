// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A clka clock-divider register directly ANDed into a clkb reset (bypassing any separate clock net) is a real cross-domain reset hazard: RDC-007 ERROR.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q, qd);
  logic clk_div, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) clk_div <= 1'b0; else clk_div <= ~clk_div;
  always_ff @(posedge clk_div or negedge rst_n) if (!rst_n) qd <= 1'b0; else qd <= d;
  assign r = rst_n & clk_div;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
