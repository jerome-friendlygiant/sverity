// sverity-args: --top top
// The `continue` makes the path analysis approximate, but `acc = '0` runs before every read of acc in the procedure, so no read
// can see the value from before it.
// expect-none: LNT-012
module top(input logic [7:0] a, input logic [7:0] b, output logic [7:0] y);
  logic [7:0] acc;
  always_comb begin
    acc = '0;
    for (int i = 0; i < 8; i++) begin
      if (a[i]) continue;
      acc = acc ^ b;
    end
    y = acc;
  end
endmodule
