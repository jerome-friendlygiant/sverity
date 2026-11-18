// sverity-args: --top top
// The reset test follows the load and overrides it: every loaded bit is reset.
// expect-none: LNT-067
module top(input logic clk, rst, en, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk) begin if (en) q <= d; if (rst) q <= '0; end
endmodule
