// sverity-args: --top top
// The count is two bits, at most 3: nothing reaches past the 8 bits of `a`. A guard that keeps a wider count below 8 is read too.
// expect-none: LNT-035
module top(input logic [7:0] a, input logic [1:0] l, input logic [4:0] n, output logic [31:0] y, z);
  assign y = {24'd0, a << l};
  always_comb begin
    z = '0;
    if (n < 5'd8) z = {24'd0, a << n};
  end
endmodule
