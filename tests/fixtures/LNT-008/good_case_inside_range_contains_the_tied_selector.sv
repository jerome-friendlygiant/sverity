// sverity-args: --top top
// The only instance ties the two-bit enable to 2'd2, which the range [2'd2:2'd3] contains: the writing arm runs.
// expect-none: LNT-008
module child(input logic clk, input logic [1:0] en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) case (en) inside [2'd2:2'd3]: r <= d; default: ; endcase
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(2'd2), .d, .q);
endmodule
