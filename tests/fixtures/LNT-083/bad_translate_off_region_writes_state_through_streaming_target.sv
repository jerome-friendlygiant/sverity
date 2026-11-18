// sverity-args: --top top
// `{<<{r}} = d` writes r through a streaming concatenation, and r is read outside the region.
// expect: LNT-083 WARNING 8
// expect-none: LNT-083 INFO
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] r;
  always_ff @(posedge clk) q <= d + r;
  // synopsys translate_off
  logic seen;
  always @(posedge clk) begin seen <= d[0]; {<<{r}} = d; end
  // synopsys translate_on
endmodule
