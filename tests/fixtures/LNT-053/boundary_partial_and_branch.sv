// sverity-args: --top top
// the default-then-override idiom (`z = 0; if (c) z = a;`) is not a dead store; a partial write read by nobody else is not covered here.
// expect-none: LNT-053
module top(input logic a, b, c, output logic [1:0] y, output logic z);
  logic [1:0] t;
  always_comb begin
    t = b;
    y = t;
    z = 1'b0;
    if (c) z = a;
  end
endmodule
