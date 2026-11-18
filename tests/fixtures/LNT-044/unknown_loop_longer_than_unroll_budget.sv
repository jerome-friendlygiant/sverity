// sverity-args: --top top
// The loop is too long to unroll, so the order of the read and the write inside it is not known: Unresolved, not a proven read.
// expect: LNT-044 WARNING 9
module top(input logic [7:0] a, output logic [7:0] y);
  logic [7:0] x;
  always_comb begin
    y = 0;
    for (int i = 0; i < 9000; i++) begin
      if (i > 0) y = x;
      x = a;
    end
  end
endmodule
