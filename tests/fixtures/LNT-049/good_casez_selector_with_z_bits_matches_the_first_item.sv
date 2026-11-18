// sverity-args: --top top
// The wildcard selector matches the first item, so the default arm never runs and word 0 has one writer.
// expect-none: LNT-049 WARNING
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] y);
  logic [7:0] m [2];
  always_ff @(posedge clk) m[0] <= a;
  always_ff @(posedge clk) casez (2'b1z) 2'b11: ; default: m[0] <= b; endcase
  assign y = m[0];
endmodule
