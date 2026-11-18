// sverity-args: --top top
// The selector is -1 and the only arm that writes lists 1: the second process never writes, so word 0 has one writer.
// expect-none: LNT-049 WARNING
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);
  logic [7:0] m [2];
  always_ff @(posedge clk) m[0] <= a;
  always_ff @(posedge clk) case (-1) 1: m[0] <= b; default: ; endcase
  assign y = m[0];
endmodule
