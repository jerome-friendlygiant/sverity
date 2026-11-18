// Ripple-carry adder built from per-bit full adders.
// Interface:
//   a, b, cin   operands and carry in
//   sum, cout   result and carry out
//   ovf         signed overflow flag
module top #(
  parameter int W = 8
) (
  input  logic [W-1:0] a,
  input  logic [W-1:0] b,
  input  logic         cin,
  output logic [W-1:0] sum,
  output logic         cout,
  output logic         ovf
);
  logic [W:0] c;

  assign c[0] = cin;

  for (genvar i = 0; i < W; i++) begin : g_bit
    assign sum[i]   = a[i] ^ b[i] ^ c[i];
    assign c[i+1]   = (a[i] & b[i]) | (c[i] & (a[i] ^ b[i]));
  end

  assign cout = c[W];
  assign ovf  = c[W] ^ c[W-1];
endmodule
