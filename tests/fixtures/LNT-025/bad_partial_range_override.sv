// sverity-args: --top top
// conditional writes to the same slice also override each other
// expect: LNT-025 WARNING
module top(input logic c, d, input logic [3:0] a, b, output logic [3:0] y);
  always_comb begin
    y = 4'b0;
    if (c) y[3:0] = a[3:0];
    if (d) y[3:0] = b[3:0];
  end
endmodule
